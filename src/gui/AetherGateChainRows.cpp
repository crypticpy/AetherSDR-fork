#include "gui/AetherGateChainRows.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace AetherSDR {

namespace {

QString emDash()
{
    return QStringLiteral("—");
}

QString tr_(const char* text)
{
    return QCoreApplication::translate("AetherGateChainStrip", text);
}

// "12 kHz", "2.8 kHz", "300 Hz", "1.536 MHz". The unit follows the number
// rather than a fixed choice, because a roofing menu that reads
// "0.3 kHz / 0.6 kHz / 1.2 kHz" is not the menu on anybody's front panel.
QString formatWidthImpl(double hz)
{
    if (hz >= 1.0e6) {
        QString s = QString::number(hz / 1.0e6, 'f', 3);
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
        return s + QStringLiteral(" MHz");
    }
    if (hz >= 1000.0) {
        QString s = QString::number(hz / 1000.0, 'f', 1);
        if (s.endsWith(QStringLiteral(".0")))
            s.chop(2);
        return s + QStringLiteral(" kHz");
    }
    return QString::number(hz, 'f', 0) + QStringLiteral(" Hz");
}

double num(const QJsonObject& obj, const char* key, double fallback = 0.0)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : fallback;
}

bool flag(const QJsonObject& obj, const char* key)
{
    return obj.value(QLatin1String(key)).toBool();
}

QString word(const QJsonObject& obj, const char* key)
{
    return obj.value(QLatin1String(key)).toString();
}

QString onOff(bool enabled)
{
    return enabled ? tr_("on") : tr_("off");
}

// A toggle row whose query is the action it is ABOUT to perform, which is the
// shape the gate uses for the noise profile's kinds[] rows and the shape this
// window's contract inherits (design §0.1).
ChainStage toggleRow(const QString& id, const QString& name, const QString& key,
                     bool enabled, const QString& detail, const QString& tip)
{
    ChainStage row;
    row.id = id;
    row.name = name;
    row.kind = QStringLiteral("toggle");
    row.enabled = enabled;
    row.detail = detail;
    row.tip = tip;
    row.actionRoute = QStringLiteral("/filter/set");
    row.actionQuery = key + QLatin1Char('=')
                      + (enabled ? QStringLiteral("off") : QStringLiteral("on"));
    return row;
}

// `why` is the short line PRINTED ON THE TILE ("set on the setup page", "gate
// does not offer this yet"); `tip` is the paragraph on the hover and in the
// detail pane. They are different lengths for different places, and an empty
// tip falls back to the why.
ChainStage fixedRow(const QString& id, const QString& name, const QString& detail,
                    const QString& why, const QString& tip = QString())
{
    ChainStage row;
    row.id = id;
    row.name = name;
    row.kind = QStringLiteral("fixed");
    row.fixed = true;
    row.enabled = true;
    row.detail = detail;
    row.why = why;
    row.tip = tip.isEmpty() ? why : tip;
    return row;
}

ChainStage wordSelectRow(const QString& id, const QString& name, const QString& key,
                         const QString& value, const QStringList& choices,
                         bool enabled, const QString& detail, const QString& tip)
{
    ChainStage row;
    row.id = id;
    row.name = name;
    row.kind = QStringLiteral("select");
    row.enabled = enabled;
    row.detail = detail;
    row.tip = tip;
    row.value = value;
    for (const QString& choice : choices)
        row.options.append({choice, choice, QString()});
    row.actionRoute = QStringLiteral("/filter/set");
    row.actionQuery = key + QLatin1Char('=');
    return row;
}

// The roofing menus operators already know, in the order the design lists
// them (§0). They are the APP's contribution to the digital roof row: the
// gate has no opinion about what an FTdx101MP's filter set is, and a bare list
// of hertz is not the menu anybody learned.
struct RadioPresets {
    const char* radio;
    const int*  widths;
    int         count;
};

const int kFtdx101[] = {12000, 3000, 1200, 600, 300};
const int kK3[]      = {13000, 6000, 2800, 2700, 1800, 1000, 500, 400, 250, 200};
const int kPt8000[]  = {12000, 6000, 2400, 500};
const int kIc7851[]  = {12000, 6000, 3000, 1200};
const int kFtdx10[]  = {12000, 3000, 500};

const RadioPresets kRadios[] = {
    {"Yaesu FTdx101MP", kFtdx101, int(sizeof(kFtdx101) / sizeof(int))},
    {"Elecraft K3",     kK3,      int(sizeof(kK3) / sizeof(int))},
    {"Hilberling PT-8000A", kPt8000, int(sizeof(kPt8000) / sizeof(int))},
    {"Icom IC-7851",    kIc7851,  int(sizeof(kIc7851) / sizeof(int))},
    {"Yaesu FTdx10",    kFtdx10,  int(sizeof(kFtdx10) / sizeof(int))},
};

// Group the widths by the radio whose menu they come from. When the gate sent
// its own `options`, a width that is not on that list stays on the menu and
// goes UNPICKABLE rather than disappearing (design §0.3 item 6): an operator
// who knows his K3 has a 250 Hz filter is owed "this receiver cannot make
// one", not a menu that quietly lost it. When the gate sent no options at all
// (every gate shipping today) everything is pickable and the gate refuses what
// it cannot do, which is the same answer one round trip later and never a
// number invented on screen.
QList<ChainOption> roofingPresets(const QList<double>& gateOptions)
{
    QList<ChainOption> out;
    for (const RadioPresets& radio : kRadios) {
        for (int i = 0; i < radio.count; ++i) {
            const double hz = double(radio.widths[i]);
            const bool offered = gateOptions.isEmpty() || gateOptions.contains(hz);
            out.append({formatWidthImpl(hz), QString::number(radio.widths[i]),
                        QString::fromLatin1(radio.radio), offered});
        }
    }
    return out;
}

// The numbers in a gate `options` array, as doubles.
QList<double> numericOptions(const QJsonArray& options)
{
    QList<double> out;
    for (const QJsonValue& v : options) {
        if (v.isDouble())
            out.append(v.toDouble());
    }
    return out;
}

QList<ChainOption> plainOptions(const QJsonArray& options)
{
    QList<ChainOption> out;
    for (const QJsonValue& v : options) {
        if (v.isDouble())
            out.append({formatWidthImpl(v.toDouble()), QString::number(v.toDouble(), 'f', 0),
                        QString()});
        else if (v.isString())
            out.append({v.toString(), v.toString(), QString()});
    }
    return out;
}

// One entry of the gate's own chain[]. Everything is optional except id/name:
// a row the app has never seen renders from its name, its detail and its
// action, which is the entire reason the array is gate-authored.
ChainStage stageFromJson(const QJsonObject& obj)
{
    ChainStage row;
    row.id = word(obj, "id");
    row.name = word(obj, "name");
    row.kind = word(obj, "kind");
    if (row.kind.isEmpty())
        row.kind = QStringLiteral("fixed");
    row.fixed = flag(obj, "fixed") || row.kind == QLatin1String("fixed");
    row.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    row.detail = word(obj, "detail");
    row.why = word(obj, "why");

    const QJsonValue value = obj.value(QStringLiteral("value"));
    if (value.isDouble())
        row.value = QString::number(value.toDouble(), 'f', 0);
    else if (value.isString())
        row.value = value.toString();

    const QJsonArray options = obj.value(QStringLiteral("options")).toArray();
    const QList<double> numeric = numericOptions(options);
    // The roofing rows get the radio menu of §0; everything else gets exactly
    // what the gate listed, in the gate's order.
    if (row.id.startsWith(QStringLiteral("roof")) && !numeric.isEmpty()
        && row.id.contains(QStringLiteral("digital"))) {
        row.options = roofingPresets(numeric);
    } else {
        row.options = plainOptions(options);
    }
    row.freeEntryHz = row.id.contains(QStringLiteral("digital"))
                      && row.kind == QLatin1String("select");

    const QJsonObject measured = obj.value(QStringLiteral("measured")).toObject();
    const QJsonValue inDb = measured.value(QStringLiteral("in_db"));
    const QJsonValue outDb = measured.value(QStringLiteral("out_db"));
    row.hasIn = inDb.isDouble();
    row.inDb = inDb.toDouble();
    row.hasOut = outDb.isDouble();
    row.outDb = outDb.toDouble();

    const QJsonObject action = obj.value(QStringLiteral("action")).toObject();
    row.actionLabel = word(action, "label");
    row.actionRoute = word(action, "route");
    row.actionQuery = word(action, "query");

    row.tip = row.why.isEmpty()
                  ? (row.actionRoute.isEmpty()
                         ? row.detail
                         : QCoreApplication::translate(
                               "AetherGateChainStrip",
                               "Sets this stage on the receiver."))
                  : row.why;
    return row;
}

} // namespace

// --------------------------------------------------------------------------
// The fallback: 13 rows out of a chain-less /filter
// --------------------------------------------------------------------------

QList<ChainStage> chainFallback(const QJsonObject& f)
{
    QList<ChainStage> rows;

    const QJsonObject roofing = f.value(QStringLiteral("roofing")).toObject();
    const double analogueHz = num(roofing, "analogue_hz");
    const double digitalHz = num(roofing, "digital_hz");
    const QList<double> analogueOptions =
        numericOptions(roofing.value(QStringLiteral("analogue_options")).toArray());
    const QList<double> digitalOptions =
        numericOptions(roofing.value(QStringLiteral("digital_options")).toArray());

    const QString rfDetail = analogueHz > 0.0
                                 ? (analogueHz <= 200000.0
                                        ? tr_("200 kHz · the narrowest this hardware has")
                                        : formatWidthImpl(analogueHz))
                                 : emDash();
    const QString rfTip =
        tr_("The analogue IF filter inside the receiver, ahead of the ADC. It "
            "protects the converter, not your ear: an FTdx101MP's 300 Hz roofing "
            "filter is two orders of magnitude narrower and there is no analogue "
            "path to it here. It follows the sample rate unless the receiver "
            "lists its own IF bandwidths, and until it does nothing in the product "
            "can move it.");

    // 1 -- the analogue IF filter. Two rows in one, decided by the gate rather
    // than by hope: a gate that lists `analogue_options` gets a real menu on
    // roof_hz, and a gate that does not gets a DIMMED tile whose own face says
    // so (design §0.3 item 6). The alternative -- a live-looking combo that
    // answers every choice with an error -- is the failure the operator
    // reported.
    if (analogueOptions.isEmpty()) {
        rows << fixedRow(QStringLiteral("roof_rf"), tr_("ROOFING · RF"), rfDetail,
                         tr_("this receiver does not offer it yet"), rfTip);
    } else {
        ChainStage rf;
        rf.id = QStringLiteral("roof_rf");
        rf.name = tr_("ROOFING · RF");
        rf.kind = QStringLiteral("select");
        rf.enabled = true;
        rf.detail = rfDetail;
        rf.tip = rfTip;
        rf.value = QString::number(analogueHz, 'f', 0);
        for (double hz : analogueOptions)
            rf.options.append({formatWidthImpl(hz), QString::number(hz, 'f', 0), QString(), true});
        rf.actionRoute = QStringLiteral("/filter/set");
        rf.actionQuery = QStringLiteral("roof_hz=");
        rows << rf;
    }

    // 2 -- the digital roof, which IS the menu operators know.
    ChainStage digital;
    digital.id = QStringLiteral("roof_digital");
    digital.name = tr_("ROOFING · DIGITAL");
    digital.kind = QStringLiteral("select");
    digital.enabled = true;
    digital.detail = digitalHz > 0.0 ? formatWidthImpl(digitalHz) : emDash();
    digital.value = digitalHz > 0.0 ? QString::number(digitalHz, 'f', 0) : QString();
    digital.options = roofingPresets(digitalOptions);
    digital.freeEntryHz = true;
    digital.actionRoute = QStringLiteral("/filter/set");
    digital.actionQuery = QStringLiteral("digital_roof_hz=");
    digital.tip = tr_("The decimation filters ahead of the slice filter - the DSP "
                      "IF bandwidth. This is where a roofing filter of the width "
                      "you are used to would live. A gate that has not built the "
                      "stage yet answers with an error and the row does not move: "
                      "nothing here is optimistic.");
    rows << digital;

    // 3 -- the noise blanker, at the full IQ rate where an IF blanker belongs.
    const QJsonObject nb = f.value(QStringLiteral("nb")).toObject();
    rows << toggleRow(QStringLiteral("nb"), tr_("NB"), QStringLiteral("nb"),
                      flag(nb, "enabled"),
                      QStringLiteral("%1 · %2 dB · %3 %")
                          .arg(onOff(flag(nb, "enabled")),
                               QString::number(num(nb, "threshold_db"), 'f', 1),
                               QString::number(num(nb, "blanked_pct"), 'f', 1)),
                      tr_("Impulse blanking, run at the full sample rate before any "
                          "filter can smear an impulse into a thud. Level only - "
                          "there is no width control, unlike the NB on a radio."));

    // 4 -- the passband. Two edges, both movable, which is twin PBT; they are
    // moved by dragging the curve on the Diversity window's FILTER page, so
    // this row states them rather than duplicating that control.
    rows << fixedRow(QStringLiteral("passband"), tr_("PASSBAND"),
                     QStringLiteral("%1–%2 Hz · asked %3–%4")
                         .arg(QString::number(num(f, "low_hz"), 'f', 0),
                              QString::number(num(f, "high_hz"), 'f', 0),
                              QString::number(num(f, "set_low_hz"), 'f', 0),
                              QString::number(num(f, "set_high_hz"), 'f', 0)),
                     tr_("Both edges of the slice filter, independently placed - "
                         "twin PBT, an IC-7851 would call it. Drag them on the "
                         "FILTER page's curve; the numbers in force can differ from "
                         "the numbers asked for when AUTO WIDTH is fitting them."));

    // 5 -- shape.
    rows << wordSelectRow(QStringLiteral("shape"), tr_("SHAPE"),
                          QStringLiteral("shape"), word(f, "shape"),
                          {QStringLiteral("soft"), QStringLiteral("sharp")}, true,
                          QStringLiteral("%1 · %2 taps · %3 Hz skirt")
                              .arg(word(f, "shape"),
                                   QString::number(num(f, "taps"), 'f', 0),
                                   QString::number(num(f, "transition_hz"), 'f', 0)),
                          tr_("How steep the passband edges are. SOFT is a 255-tap "
                              "Hamming design, SHARP a 1023-tap Kaiser - Icom's own "
                              "words for the same choice."));

    // 6 -- notches, manual and automatic. One row, because they are one filter.
    const QJsonObject anf = f.value(QStringLiteral("anf")).toObject();
    const int manual = f.value(QStringLiteral("notches")).toArray().size();
    const int found = anf.value(QStringLiteral("found_hz")).toArray().size();
    rows << toggleRow(QStringLiteral("notch"), tr_("NOTCH · DNF"),
                      QStringLiteral("anf"), flag(anf, "enabled"),
                      QStringLiteral("%1 manual · ANF %2, %3 %4")
                          .arg(QString::number(manual), onOff(flag(anf, "enabled")),
                               QString::number(found),
                               found == 1 ? tr_("tone") : tr_("tones")),
                      tr_("The manual notches you placed and the automatic one that "
                          "hunts heterodynes - Yaesu's DNF, Icom's AN. Both sit "
                          "inside the slice filter, which is ahead of the AGC, so a "
                          "notched carrier cannot pump the gain."));

    // 7 -- contour.
    const QJsonObject contour = f.value(QStringLiteral("contour")).toObject();
    rows << toggleRow(QStringLiteral("contour"), tr_("CONTOUR"),
                      QStringLiteral("contour"), flag(contour, "enabled"),
                      flag(contour, "enabled")
                          ? QStringLiteral("%1 · %2 Hz · %3 dB")
                                .arg(flag(contour, "auto") ? tr_("auto") : tr_("manual"),
                                     QString::number(num(contour, "hz"), 'f', 0),
                                     QString::number(num(contour, "db"), 'f', 1))
                          : onOff(false),
                      tr_("A broad bell in or out of the passband - Yaesu's own word "
                          "for it. On AUTO it is fitted from the talker's own voice "
                          "print against an average speech spectrum, which no radio "
                          "does."));

    // 8 -- APF.
    const QJsonObject apf = f.value(QStringLiteral("apf")).toObject();
    rows << toggleRow(QStringLiteral("apf"), tr_("APF"), QStringLiteral("apf"),
                      flag(apf, "enabled"),
                      QStringLiteral("%1 · %2 Hz · %3 Hz wide")
                          .arg(onOff(flag(apf, "enabled")),
                               QString::number(num(apf, "hz"), 'f', 0),
                               QString::number(num(apf, "width_hz"), 'f', 0)),
                      tr_("The audio peak filter: a narrow resonance for digging one "
                          "CW note out of noise. Same control, same default region "
                          "(600 Hz), as the APF on an FTdx101 or an IC-7851."));

    // 9 -- automatic width.
    const QJsonObject autoWidth = f.value(QStringLiteral("auto")).toObject();
    rows << toggleRow(QStringLiteral("auto_width"), tr_("AUTO WIDTH"),
                      QStringLiteral("auto"), flag(autoWidth, "enabled"),
                      flag(autoWidth, "enabled")
                          ? QStringLiteral("%1 · %2–%3 Hz")
                                .arg(word(autoWidth, "source").isEmpty()
                                         ? emDash()
                                         : word(autoWidth, "source"),
                                     QString::number(num(autoWidth, "low_hz"), 'f', 0),
                                     QString::number(num(autoWidth, "high_hz"), 'f', 0))
                          : onOff(false),
                      tr_("One fit per over: the receiver measures where this station's "
                          "energy actually is and moves both passband edges to it. "
                          "No radio has a counterpart."));

    // 10 -- the automatic RX EQ tilt.
    const QJsonObject eq = f.value(QStringLiteral("auto_eq")).toObject();
    rows << toggleRow(QStringLiteral("auto_eq"), tr_("RX EQ"),
                      QStringLiteral("auto_eq"), flag(eq, "enabled"),
                      QStringLiteral("%1 · tilt %2 · lean %3 dB")
                          .arg(onOff(flag(eq, "enabled")),
                               QString::number(num(eq, "tilt_db"), 'f', 1),
                               QString::number(num(eq, "lean_db"), 'f', 1)),
                      tr_("One automatic tilt between 550 Hz and 2 kHz, not a "
                          "multi-band equaliser. It flattens a station that is all "
                          "bass or all edge; it does not let you shape the audio to "
                          "taste."));

    // 11 -- the audio AGC.
    const QJsonObject agc = f.value(QStringLiteral("agc")).toObject();
    rows << wordSelectRow(
        QStringLiteral("agc"), tr_("AGC"), QStringLiteral("agc"), word(agc, "mode"),
        {QStringLiteral("fast"), QStringLiteral("med"), QStringLiteral("slow"),
         QStringLiteral("long"), QStringLiteral("off")},
        word(agc, "mode") != QLatin1String("off"),
        QStringLiteral("%1 · %2/%3/%4 ms · AGC-T %5 · %6 dB")
            .arg(word(agc, "mode"), QString::number(num(agc, "attack_ms"), 'f', 0),
                 QString::number(num(agc, "decay_ms"), 'f', 0),
                 QString::number(num(agc, "hang_ms"), 'f', 0),
                 QString::number(num(agc, "threshold_db"), 'f', 0),
                 QString::number(num(agc, "gain_db"), 'f', 1)),
        tr_("Attack, decay and hang in real milliseconds, which is unusually "
            "explicit - most radios give you three words. The threshold is "
            "Elecraft's AGC-T, and the last number is the gain the AGC is "
            "applying right now."));

    // 12 -- per-talker recall.
    const QJsonObject talker = f.value(QStringLiteral("talker")).toObject();
    rows << toggleRow(
        QStringLiteral("talker"), tr_("PER TALKER"), QStringLiteral("talker"),
        flag(talker, "enabled"),
        QStringLiteral("%1 · %2 · id %3 · %4 kept")
            .arg(onOff(flag(talker, "enabled")), word(talker, "snap"),
                 QString::number(int(num(talker, "id"))),
                 QString::number(
                     talker.value(QStringLiteral("remembered")).toArray().size())),
        tr_("The receiver remembers the AUTOMATIC settings - fitted edges, EQ tilt, the "
            "contour bell - per voice it recognises. Your own settings are "
            "deliberately not per talker."));

    // 13 -- the full stop. Where the chain leaves the gate, so "why is there
    // no NR on this page" never becomes a question.
    rows << fixedRow(QStringLiteral("voice"), tr_("→ AETHER VOICE"),
                     tr_("NR and compression run in the app"),
                     tr_("Noise reduction and compression are AetherSDR's own, "
                         "downstream of everything above and after the audio has "
                         "arrived. They are on the Aetherial Voice panel, and "
                         "nothing upstream duplicates them."));

    return rows;
}

QList<ChainStage> chainFromFilter(const QJsonObject& filter, bool* fromGate)
{
    const QJsonValue chain = filter.value(QStringLiteral("chain"));
    if (chain.isArray()) {
        if (fromGate)
            *fromGate = true;
        QList<ChainStage> rows;
        const QJsonArray array = chain.toArray();
        for (const QJsonValue& v : array) {
            if (v.isObject())
                rows.append(stageFromJson(v.toObject()));
        }
        return rows;
    }
    if (fromGate)
        *fromGate = false;
    return chainFallback(filter);
}


QString chainFormatWidth(double hz)
{
    return formatWidthImpl(hz);
}

// --------------------------------------------------------------------------
// B23 -- the front-end linearity guard, from GET /device's "frontend" key
// --------------------------------------------------------------------------

ChainFrontendStatus chainFrontendFromDevice(const QJsonObject& device)
{
    ChainFrontendStatus fe;
    const QJsonValue v = device.value(QStringLiteral("frontend"));
    if (!v.isObject())
        return fe;
    const QJsonObject obj = v.toObject();
    if (!obj.value(QStringLiteral("available")).toBool())
        return fe;

    fe.available = true;
    fe.guard = flag(obj, "guard");
    fe.floorState = word(obj, "floor_state");
    fe.maxState = word(obj, "max_state");
    fe.lnaState = word(obj, "lna_state");
    fe.dbmCalibrated = obj.value(QStringLiteral("dbm_calibrated")).toBool(true);
    fe.calState = word(obj, "cal_state");
    const QJsonValue headroom = obj.value(QStringLiteral("headroom_db"));
    fe.hasHeadroom = headroom.isDouble();
    fe.headroomDb = headroom.toDouble();
    fe.clips1s = int(num(obj, "clips_1s"));
    fe.state = word(obj, "state");

    const QJsonArray events = obj.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& ev : events) {
        if (!ev.isObject())
            continue;
        const QJsonObject e = ev.toObject();
        ChainFrontendEvent item;
        item.t = qint64(num(e, "t"));
        item.from = word(e, "from");
        item.to = word(e, "to");
        item.reason = word(e, "reason");
        fe.events.append(item);
    }
    return fe;
}

QString chainFrontendGuardValueText(const ChainFrontendStatus& fe)
{
    if (!fe.available)
        return QString();
    if (!fe.guard)
        return tr_("off · LNA %1").arg(fe.lnaState);
    QString text = tr_("on · LNA %1 (floor %2)").arg(fe.lnaState, fe.floorState);
    QString stateWord;
    if (fe.state == QLatin1String("stepping_up"))
        stateWord = tr_("stepping up");
    else if (fe.state == QLatin1String("stepping_down"))
        stateWord = tr_("stepping down");
    else if (fe.state == QLatin1String("holding"))
        stateWord = tr_("holding");
    if (!stateWord.isEmpty())
        text += QStringLiteral(" · ") + stateWord;
    return text;
}

QString chainFrontendEventSentence(const ChainFrontendStatus& fe)
{
    if (fe.events.isEmpty())
        return QString();
    const ChainFrontendEvent& e = fe.events.last();
    const QString time =
        QDateTime::fromSecsSinceEpoch(e.t).time().toString(QStringLiteral("HH:mm"));
    return tr_("stepped %1 → %2 at %3, %4").arg(e.from, e.to, time, e.reason);
}

QString chainFrontendCalNoteText(const ChainFrontendStatus& fe)
{
    if (!fe.available || fe.dbmCalibrated)
        return QString();
    return tr_("dBm scale calibrated for LNA %1, now %2 — levels are relative "
               "until re-trimmed")
        .arg(fe.calState, fe.lnaState);
}

// HEADROOM (a measured line, no control) then GUARD (the one control this
// card carries) -- in that order, because a card reads the number before the
// switch that reacts to it.
QList<ChainStage> chainFrontendRows(const ChainFrontendStatus& fe)
{
    QList<ChainStage> rows;
    if (!fe.available)
        return rows;

    ChainStage headroom;
    headroom.id = QStringLiteral("frontend_headroom");
    headroom.name = tr_("HEADROOM");
    headroom.kind = QStringLiteral("value");
    headroom.enabled = true;
    const QString headroomNum =
        fe.hasHeadroom ? QString::number(fe.headroomDb, 'f', 1) : emDash();
    headroom.detail = tr_("%1 dB · clips %2").arg(headroomNum).arg(fe.clips1s);
    headroom.warn = fe.hasHeadroom && (fe.headroomDb < 3.0 || fe.clips1s > 0);
    headroom.tip =
        tr_("How far the strongest sample in the last second sits below full "
            "scale, and how many of them clipped. Under 3 dB, or any clip at "
            "all, is what the guard -- when it is on -- steps the LNA down "
            "for.");
    rows.append(headroom);

    ChainStage guard;
    guard.id = QStringLiteral("frontend_guard");
    guard.name = tr_("GUARD");
    guard.kind = QStringLiteral("toggle");
    guard.enabled = fe.guard;
    guard.detail = chainFrontendGuardValueText(fe);
    guard.tip =
        tr_("Steps the LNA state down when the ADC is within 3 dB of full "
            "scale or has clipped, and back up 30 s after it is clear. It "
            "never goes below the floor beside it.");
    guard.actionRoute = QStringLiteral("/frontend/set");
    guard.actionQuery = QStringLiteral("guard=")
                         + (fe.guard ? QStringLiteral("off") : QStringLiteral("on"));
    guard.toggleObjectName = QStringLiteral("gateChainFrontendGuard");
    guard.hasFloorControl = true;
    guard.floorObjectName = QStringLiteral("gateChainFrontendFloor");
    guard.floorValue = fe.floorState;
    guard.floorActionRoute = QStringLiteral("/frontend/set");
    guard.floorActionQuery = QStringLiteral("floor=");
    bool maxOk = false;
    const int top = fe.maxState.toInt(&maxOk);
    for (int i = 0; i <= (maxOk ? top : 9); ++i) {
        const QString s = QString::number(i);
        guard.floorOptions.append({s, s, QString()});
    }
    rows.append(guard);

    return rows;
}

} // namespace AetherSDR
