// The FLOW strip's derivations. See DiversityFlowStrip.h for why the five
// steps are in this order; what follows is only how each one reads its state
// off a payload the window already had.
//
// Two rules run through all five and are the reason this is a widget of its own
// rather than five more members on DiversityWindow:
//
//   * a step never invents a fact. Every state string is either the gate's own
//     number, the gate's own word, or a dash. "not aligned" is a thing the gate
//     said; "probably fine" is not a thing anything could say.
//   * a step's DONE-ness and its wording are the same derivation. The state
//     that reads "off -> pick TRACK" is the state that is not done, by
//     construction, so the lit button and the sentence beside it can never
//     disagree about whether there is anything left to do.

#include "gui/DiversityFlowStrip.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QSizePolicy>
#include <QStringList>

#include <cmath>

namespace AetherSDR {

namespace {

// One line, one sheet, no new colours. The line's own colour is the caption
// token the FLOW caption beside it already wears, which is what paints the
// " · " between steps; the three per-step colours are resolved from tokens
// already in use in this window (see rebuild()) and inlined into the rich
// text, because a colour that changes per step cannot come from a selector.
const char* kFlowLineStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }";

// The glyph in front of each step. A checklist reads as a checklist before a
// single word of it has been read, which is the whole point of moving off the
// row of pills that read as a second tab bar.
const char* kGlyphDone = "✓";
const char* kGlyphNext = "●";
const char* kGlyphLater = "○";

QString dash()
{
    return QStringLiteral("—");
}

QString arrow()
{
    return QStringLiteral(" → ");
}

// "+4.1" / "−0.6" -- a real minus sign, because this is a number in a sentence
// rather than a cell in a column.
QString signedDb(double v)
{
    if (v < 0.0)
        return QStringLiteral("\u2212%1").arg(-v, 0, 'f', 1);
    return QStringLiteral("+%1").arg(v, 0, 'f', 1);
}

// "1:12" from 72 seconds. Minutes and seconds because the three durations on
// offer are one, three and five minutes and the readout has to be comparable
// with the button that started it.
QString clockText(double seconds)
{
    const qint64 total = qint64(std::llround(std::max(0.0, seconds)));
    return QStringLiteral("%1:%2")
        .arg(total / 60)
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// The order the report names the knobs in. Not alphabetical and not the gate's
// map order (which is unordered by construction): it is the order the chain
// itself runs in, so a report reads the same way twice running whatever the
// gate happened to try first. Anything not on this list is appended after it,
// in the order QJsonObject gives, rather than dropped -- a gate that grows a
// knob must still be able to say it changed it.
const char* const kKnobOrder[] = {"post",  "subband", "mrc",   "width", "nb",
                                  "nb_db", "agc",     "contour", "anf", "apf",
                                  "auto_eq"};

// One "changed" entry as the operator would say it. The values are the gate's
// own wire values and none of them is re-derived here.
QString knobText(const QString& knob, const QJsonValue& value)
{
    if (knob == QLatin1String("width") && value.isArray()) {
        const QJsonArray edges = value.toArray();
        if (edges.size() == 2) {
            return QCoreApplication::translate("DiversityFlowStrip", "width %1-%2")
                .arg(QString::number(qint64(std::llround(edges.at(0).toDouble()))),
                     QString::number(qint64(std::llround(edges.at(1).toDouble()))));
        }
    }
    if (knob == QLatin1String("nb_db") && value.isDouble()) {
        return QCoreApplication::translate("DiversityFlowStrip", "nb %1 dB")
            .arg(value.toDouble(), 0, 'f', 0);
    }
    if (value.isBool()) {
        return value.toBool()
                   ? QCoreApplication::translate("DiversityFlowStrip", "%1 on").arg(knob)
                   : QCoreApplication::translate("DiversityFlowStrip", "%1 off").arg(knob);
    }
    if (value.isString())
        return QStringLiteral("%1 %2").arg(knob, value.toString());
    if (value.isDouble())
        return QStringLiteral("%1 %2").arg(knob, QString::number(value.toDouble()));
    return knob;
}

// "post v2, width 100-2400, nb 11 dB" from one "changed" object.
QString changedText(const QJsonObject& changed)
{
    QStringList parts;
    QStringList seen;
    for (const char* knob : kKnobOrder) {
        const QString key = QString::fromLatin1(knob);
        if (!changed.contains(key))
            continue;
        seen << key;
        parts << knobText(key, changed.value(key));
    }
    for (auto it = changed.begin(); it != changed.end(); ++it) {
        if (seen.contains(it.key()))
            continue;
        parts << knobText(it.key(), it.value());
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

DiversityFlowStrip::DiversityFlowStrip(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFlowStrip"));
    setAccessibleName(tr("Flow"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_caption = DiversityWidgets::makeCaption(tr("FLOW"), this);
    m_caption->setObjectName(QStringLiteral("diversityWindowFlowCaption"));
    m_caption->setAccessibleName(tr("Flow"));
    m_caption->setToolTip(
        tr("The order that gets the best signal: align the tuners, pick a "
           "mode, hear the combined output, act on the noise the gate found, "
           "then set the filter. Beacons (SITE) only matter on 20–10 m; "
           "CAPTURE is for the replay lab, not for listening."));
    m_caption->setAccessibleDescription(m_caption->toolTip());
    layout->addWidget(m_caption);

    struct StepSpec {
        const char* label;
        const char* accessible;
        const char* tip;
    };
    // The word each step is drawn as -- lower case, because this is a sentence
    // about what to do and not a row of buttons -- then one sentence each on
    // what the step is for and what "done" means. The second half is the part
    // an operator cannot guess, and it is why the tooltips are not just the
    // labels again.
    const StepSpec specs[StepCount] = {
        {QT_TR_NOOP("align"), QT_TR_NOOP("Flow step 1, align"),
         QT_TR_NOOP("Line the two tuners' sample streams up. Nothing can be "
                    "combined until they are, so every number on every page is "
                    "meaningless first. Done when the gate reports aligned and "
                    "a lag; click to run REALIGN now.")},
        {QT_TR_NOOP("mode"), QT_TR_NOOP("Flow step 2, mode"),
         QT_TR_NOOP("Choose how the weight on the second loop is arrived at. "
                    "Done when the mode is anything but OFF -- OFF is loop A "
                    "on its own, which is an ordinary single-tuner receiver. "
                    "Click to switch to TRACK.")},
        {QT_TR_NOOP("hear"), QT_TR_NOOP("Flow step 3, hear"),
         QT_TR_NOOP("Send the combiner's output to the audio. Done when you "
                    "are on OUT or STEREO; on A or B the weight is still being "
                    "solved but you are not listening to it. Click to go back "
                    "to the combined output.")},
        {QT_TR_NOOP("noise"), QT_TR_NOOP("Flow step 4, noise"),
         QT_TR_NOOP("Act on what the gate found this address doing. It "
                    "profiles the noise floor by itself once the tuners are "
                    "aligned -- no capture is needed -- and nominates one "
                    "control per finding. Done when no finding is still "
                    "offering an unused button. Click for the SITE page.")},
        {QT_TR_NOOP("filter"), QT_TR_NOOP("Flow step 5, filter"),
         QT_TR_NOOP("Set how the station sounds: the passband in force, its "
                    "shape, and whether the gate is fitting the edges itself. "
                    "This step is never unfinished -- there is always a filter "
                    "-- it is the last stop rather than a chore. Click for the "
                    "FILTER page.")},
        {QT_TR_NOOP("dig"), QT_TR_NOOP("Flow step 6, dig"),
         QT_TR_NOOP("Let the gate spend a minute, three or five trying one "
                    "knob of the chain at a time on whoever is talking, "
                    "keeping only what measurably helped. It is not a step you "
                    "have to do and it is never \"next\" -- it is the offer at "
                    "the end of the list. It says what it changed and what "
                    "that bought, and WORSE puts every one of those changes "
                    "back.")}};

    // The five sentences used to be five tooltips on five buttons. There is one
    // widget to hover now, so they are one tooltip on it -- the same words, in
    // the same order, none of them dropped because the buttons went away.
    QStringList tips;
    tips.reserve(StepCount);
    for (int i = 0; i < StepCount; ++i) {
        tips << tr("%1 — %2").arg(tr(specs[i].accessible), tr(specs[i].tip));
        m_labels << tr(specs[i].label);
    }

    m_line = new QLabel(this);
    m_line->setObjectName(QStringLiteral("diversityWindowFlowLine"));
    m_line->setAccessibleName(tr("Flow steps"));
    m_line->setToolTip(tips.join(QStringLiteral("\n\n")));
    m_line->setTextFormat(Qt::RichText);
    // Only the next step is a link, and it is reachable by keyboard as well as
    // by mouse: this line is the one control in the window that says what to do
    // next, and a control only a mouse can reach is not one.
    m_line->setTextInteractionFlags(Qt::LinksAccessibleByMouse
                                    | Qt::LinksAccessibleByKeyboard);
    // No wrapping, ever: a height-for-width label at the foot of the window
    // would make the whole window height-for-width. Ignored horizontally so a
    // long state string cannot drag the window's minimum width out past the
    // 1120 it opens at -- the constraint the row of pills needed a fixed step
    // width for, met here by letting the line clip instead.
    m_line->setWordWrap(false);
    m_line->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_line->setMinimumWidth(0);
    ThemeManager::instance().applyStyleSheet(m_line, QString::fromLatin1(kFlowLineStyle));
    connect(m_line, &QLabel::linkActivated, this, [this](const QString& href) {
        // "step:N", N being the Step the anchor was drawn for. Anything else
        // is not this line's and is dropped rather than guessed at.
        if (!href.startsWith(QLatin1String("step:")))
            return;
        bool ok = false;
        const int step = href.mid(5).toInt(&ok);
        if (ok && step >= 0 && step < StepCount)
            emit stepActivated(step);
    });
    layout->addWidget(m_line, 1);

    // Re-render on a theme change: the three per-step colours are inlined into
    // the rich text, so unlike a stylesheet they are not re-resolved for us.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this] { rebuild(); });

    m_tones = QVector<QString>(StepCount);
    rebuild();
}

// A finished run that has not been judged. Cancelled and errored runs are NOT
// that: there is nothing to be a verdict about when the chain is already back
// on the operator's own settings.
bool DiversityFlowStrip::digAwaitingVerdict() const
{
    return m_digAvailable && !m_digRunning && m_digPhase == QLatin1String("done")
           && m_digVerdict.isEmpty() && !m_digCancelled && m_digError.isEmpty();
}

// --------------------------------------------------------------------------
// Incoming state
// --------------------------------------------------------------------------

void DiversityFlowStrip::applyDiversity(const QJsonObject& d, bool available)
{
    m_available = available;
    if (!available) {
        m_aligned = false;
        m_realigning = false;
        m_haveLag = false;
        m_mode.clear();
        m_source.clear();
        m_haveProfile = false;
        m_offered = 0;
        m_activeKinds.clear();
        rebuild();
        return;
    }

    m_aligned = d.value(QStringLiteral("aligned")).toBool();
    m_realigning = d.value(QStringLiteral("realigning")).toBool();
    const QJsonValue lag = d.value(QStringLiteral("lag_samples"));
    m_haveLag = lag.isDouble();
    m_lagSamples = m_haveLag ? lag.toDouble() : 0.0;
    m_mode = d.value(QStringLiteral("mode")).toString();
    m_source = d.value(QStringLiteral("source")).toString();

    // Same isObject() guard the rest of the window keeps on optional blocks: a
    // null noise_profile is "not measured yet", which is a different sentence
    // from "measured and clean", and toObject()'s silent {} would blur them.
    const QJsonValue profile = d.value(QStringLiteral("noise_profile"));
    m_haveProfile = profile.isObject();
    m_offered = 0;
    m_activeKinds.clear();
    if (m_haveProfile) {
        const QJsonArray kinds =
            profile.toObject().value(QStringLiteral("kinds")).toArray();
        for (const QJsonValue& v : kinds) {
            const QJsonObject row = v.toObject();
            if (!row.value(QStringLiteral("action")).isObject())
                continue;   // the gate said why there is nothing to do
            if (row.value(QStringLiteral("active")).toBool())
                m_activeKinds << row.value(QStringLiteral("kind")).toString();
            else
                ++m_offered;
        }
    }
    rebuild();
}

void DiversityFlowStrip::applyFilter(const QJsonObject& f)
{
    if (f.isEmpty() || f.contains(QStringLiteral("error")))
        return;
    m_haveFilter = true;
    m_filterAvailable = f.value(QStringLiteral("available")).toBool();
    if (!m_filterAvailable) {
        rebuild();
        return;
    }
    const QJsonValue low = f.value(QStringLiteral("low_hz"));
    const QJsonValue high = f.value(QStringLiteral("high_hz"));
    m_filterEdges =
        (low.isDouble() && high.isDouble())
            ? QStringLiteral("%1–%2").arg(QString::number(qint64(std::llround(low.toDouble()))),
                              QString::number(qint64(std::llround(high.toDouble()))))
            : dash();
    m_filterShape = f.value(QStringLiteral("shape")).toString();
    m_filterAuto = f.value(QStringLiteral("auto"))
                       .toObject()
                       .value(QStringLiteral("enabled"))
                       .toBool();

    const QJsonValue talker = f.value(QStringLiteral("talker"));
    const QJsonObject talkerObj = talker.isObject() ? talker.toObject() : QJsonObject();
    m_talkerOn = talkerObj.value(QStringLiteral("enabled")).toBool();
    const QJsonValue talkerId = talkerObj.value(QStringLiteral("id"));
    m_haveTalkerId = talkerId.isDouble();
    m_talkerId = m_haveTalkerId ? int(std::lround(talkerId.toDouble())) : 0;

    const QJsonObject contour = f.value(QStringLiteral("contour")).toObject();
    m_autoContour = contour.value(QStringLiteral("auto")).toBool();
    const QJsonValue contourHz = contour.value(QStringLiteral("hz"));
    // A fitted bell needs BOTH a centre and a source: auto with hz null is the
    // tracker's honest "I have not heard a print yet", which is a different
    // sentence from a contour at 0 Hz.
    m_haveContour = contourHz.isDouble()
                    && contour.value(QStringLiteral("source")).isString();
    m_contourHz = m_haveContour ? contourHz.toDouble() : 0.0;
    m_contourDb = contour.value(QStringLiteral("db")).toDouble();
    rebuild();
}

// The field names are the same in every phase, so this reads them once and
// lets the empty ones be empty: "phase" is what decides which of them mean
// anything, and digState() is where that decision is made.
void DiversityFlowStrip::applyDig(const QJsonObject& dig)
{
    m_digAvailable = dig.value(QStringLiteral("available")).toBool();
    m_digRunning = m_digAvailable && dig.value(QStringLiteral("running")).toBool();
    m_digPhase = dig.value(QStringLiteral("phase")).toString();
    m_digVerdict = dig.value(QStringLiteral("verdict")).toString();
    m_digError = dig.value(QStringLiteral("error")).toString();
    m_digCancelled = dig.value(QStringLiteral("cancelled")).toBool();
    m_digGainDb = dig.value(QStringLiteral("gain_db")).toDouble();
    m_digElapsedS = dig.value(QStringLiteral("elapsed_s")).toDouble();
    m_digSeconds = dig.value(QStringLiteral("seconds")).toDouble();
    // The knob the gate is on right now is the knob of the step it last
    // appended -- there is no separate "trying" field, and inventing one would
    // be inventing a fact.
    const QJsonArray steps = dig.value(QStringLiteral("steps")).toArray();
    m_digLastKnob = steps.isEmpty()
                        ? QString()
                        : steps.last().toObject().value(QStringLiteral("knob")).toString();
    m_digChanged = changedText(dig.value(QStringLiteral("changed")).toObject());
    // "measured_best" is one step, kept or not; its knob and target read as a
    // one-knob "changed" so the near miss is worded like the report.
    const QJsonObject best = dig.value(QStringLiteral("measured_best")).toObject();
    m_digNearMiss = best.isEmpty()
                        ? QString()
                        : changedText({{best.value(QStringLiteral("knob")).toString(),
                                        best.value(QStringLiteral("to"))}});
    m_digNearMissDb = dig.value(QStringLiteral("measured_best_db")).toDouble();
    m_digMarginDb = dig.value(QStringLiteral("margin_db")).toDouble();
    m_digUnsteady = dig.value(QStringLiteral("unsteady")).toBool();
    m_digSpreadDb = dig.value(QStringLiteral("baseline_spread_db")).toDouble();
    rebuild();
}

void DiversityFlowStrip::setTalkerNames(const QJsonArray& memory)
{
    m_talkerNames.clear();
    for (const QJsonValue& v : memory) {
        const QJsonObject entry = v.toObject();
        const QJsonValue id = entry.value(QStringLiteral("id"));
        const QJsonValue name = entry.value(QStringLiteral("name"));
        if (!id.isDouble() || !name.isString() || name.toString().isEmpty())
            continue;
        m_talkerNames.insert(int(std::lround(id.toDouble())), name.toString());
    }
    rebuild();
}

void DiversityFlowStrip::clear()
{
    m_available = false;
    m_aligned = false;
    m_realigning = false;
    m_haveLag = false;
    m_mode.clear();
    m_source.clear();
    m_haveProfile = false;
    m_offered = 0;
    m_activeKinds.clear();
    m_haveFilter = false;
    m_filterAvailable = false;
    m_filterEdges.clear();
    m_filterShape.clear();
    m_filterAuto = false;
    m_talkerOn = false;
    m_haveTalkerId = false;
    m_talkerId = 0;
    m_talkerNames.clear();
    m_autoContour = false;
    m_haveContour = false;
    // The dig goes away with the gate: a run whose status nothing is answering
    // for is not a run this strip can say anything true about.
    m_digAvailable = false;
    m_digRunning = false;
    m_digPhase.clear();
    m_digVerdict.clear();
    m_digError.clear();
    m_digCancelled = false;
    m_digGainDb = 0.0;
    m_digElapsedS = 0.0;
    m_digSeconds = 0.0;
    m_digLastKnob.clear();
    m_digChanged.clear();
    m_digNearMiss.clear();
    m_digNearMissDb = 0.0;
    m_digMarginDb = 0.0;
    m_digUnsteady = false;
    m_digSpreadDb = 0.0;
    rebuild();
}

// --------------------------------------------------------------------------
// The five derivations
// --------------------------------------------------------------------------

DiversityFlowStrip::State DiversityFlowStrip::alignState() const
{
    if (!m_available)
        return {dash(), false};
    if (m_realigning)
        return {tr("aligning…"), false};
    if (!m_aligned)
        return {tr("not aligned → REALIGN"), false};
    return {m_haveLag
                ? tr("lag %1").arg(QString::number(qint64(std::llround(m_lagSamples))))
                : tr("aligned"),
            true};
}

DiversityFlowStrip::State DiversityFlowStrip::modeState() const
{
    if (!m_available || m_mode.isEmpty())
        return {dash(), false};
    if (m_mode == QLatin1String("off"))
        return {tr("off → pick TRACK"), false};
    return {m_mode, true};
}

DiversityFlowStrip::State DiversityFlowStrip::hearState() const
{
    if (!m_available || m_source.isEmpty())
        return {dash(), false};
    if (m_source == QLatin1String("combined"))
        return {tr("OUT"), true};
    if (m_source == QLatin1String("stereo"))
        return {tr("STEREO"), true};
    // A or B, or a wire value a newer gate grew: whatever it is, it is not the
    // combined output, so the offer is the same one.
    return {tr("%1 only → hear OUT").arg(m_source.toUpper()), false};
}

DiversityFlowStrip::State DiversityFlowStrip::noiseState() const
{
    if (!m_available)
        return {dash(), false};
    // Not measured yet is not a finding and not a chore: the gate does this by
    // itself and the operator has nothing to press.
    if (!m_haveProfile)
        return {tr("profiling…"), true};
    if (m_offered > 0) {
        return {m_offered == 1 ? tr("1 finding → SITE")
                               : tr("%1 findings → SITE").arg(m_offered),
                false};
    }
    if (!m_activeKinds.isEmpty()) {
        // The gate's own kind tokens, joined. Not a re-sentenced summary: the
        // SITE page states each finding in full and this line only has to say
        // which of them are already being handled.
        return {tr("acting on %1").arg(m_activeKinds.join(QStringLiteral(", "))), true};
    }
    return {tr("clean"), true};
}

DiversityFlowStrip::State DiversityFlowStrip::filterState() const
{
    // FILTER is never "not done" (see the header): the bool below is always
    // true, and what changes is only what the line says.
    if (!m_haveFilter)
        return {tr("%1 → FILTER").arg(dash()), true};
    if (!m_filterAvailable)
        return {tr("no filter for this mode"), true};
    QString text = m_filterEdges;
    if (!m_filterShape.isEmpty())
        text += QStringLiteral(" ") + m_filterShape;
    if (m_filterAuto)
        text += QStringLiteral(" · ") + tr("AUTO");
    // WHOSE filter. With the per-talker recall on, "100-2900 soft" is not one
    // fact about the receiver -- it is one fact about one station, and the step
    // that did not say so would be quietly wrong every time somebody else
    // keyed up.
    if (m_talkerOn && m_haveTalkerId) {
        const QString name = m_talkerNames.value(m_talkerId);
        text += QStringLiteral(" · ")
                + (name.isEmpty()
                       ? tr("filter #%1").arg(m_talkerId)
                       : tr("%1's filter (#%2)")
                             .arg(name, QString::number(m_talkerId)));
    }
    if (m_autoContour) {
        text += QStringLiteral(" · ")
                + (m_haveContour
                       ? tr("auto contour %1 dB at %2 Hz")
                             .arg(m_contourDb < 0.0
                                      ? QStringLiteral("−%1").arg(-m_contourDb, 0, 'f', 0)
                                      : QStringLiteral("+%1").arg(m_contourDb, 0, 'f', 0),
                                  QString::number(qint64(std::llround(m_contourHz))))
                       : tr("auto contour: no print yet"));
    }
    return {text, true};
}

// DIG is never "not done" for the same reason FILTER is not: it is an offer,
// not a chore, and a checklist that never stops asking you to press a button
// is a checklist nobody reads. The bool below is always true.
DiversityFlowStrip::State DiversityFlowStrip::digState() const
{
    if (!m_digAvailable)
        return {QString(), true};
    if (m_digRunning) {
        QString text = tr("digging %1 of %2 · %3 dB so far")
                           .arg(clockText(m_digElapsedS), clockText(m_digSeconds),
                                signedDb(m_digGainDb));
        if (m_digPhase == QLatin1String("sampling"))
            text += tr(" · sampling the baseline");
        else if (!m_digLastKnob.isEmpty())
            text += tr(" · trying %1").arg(m_digLastKnob);
        return {text, true};
    }
    // A refusal, and a run put back: both are about a chain that is on the
    // operator's own settings, and neither is a report to be judged.
    if (!m_digError.isEmpty())
        return {m_digError, true};
    if (m_digCancelled)
        return {tr("found %1 dB (put back)").arg(signedDb(m_digGainDb)), true};
    if (m_digPhase != QLatin1String("done"))
        return {QString(), true};
    // A run that kept nothing still measured something: the best trial and
    // the margin it fell short of are the difference between "your settings
    // are right" and "the band was too jumpy to tell", and the operator is
    // the one who can act on that difference.
    QString text;
    if (!m_digChanged.isEmpty())
        text = tr("%1 dB: %2").arg(signedDb(m_digGainDb), m_digChanged);
    else if (!m_digNearMiss.isEmpty() && m_digNearMissDb > 0.0)
        text = tr("nothing cleared the %1 dB margin · %2 measured %3 dB")
                   .arg(m_digMarginDb, 0, 'f', 1)
                   .arg(m_digNearMiss, signedDb(m_digNearMissDb));
    else
        text = tr("nothing beat your settings");
    if (m_digUnsteady)
        text += tr(" · tentative, band swung %1 dB").arg(m_digSpreadDb, 0, 'f', 1);
    // The word the operator gave it, kept on the line until the next run --
    // "what did I decide about that?" is a question the strip should answer
    // without another click.
    if (!m_digVerdict.isEmpty())
        text += QStringLiteral(" · ") + m_digVerdict.toUpper();
    return {text, true};
}

// --------------------------------------------------------------------------
// Which page a step is about
// --------------------------------------------------------------------------

int DiversityFlowStrip::stepPage(int step)
{
    switch (step) {
    case StepAlign:
    case StepMode:
    case StepHear:
        return PageSlice;
    case StepNoise:
        return PageSite;
    case StepFilter:
    // The knobs a dig moves are the chain's, which is the FILTER page's
    // subject -- so on that page its state is quoted in full like the others'.
    case StepDig:
        return PageFilter;
    default:
        break;
    }
    return PageBand;
}

// The tab labels, verbatim: a step that says "→ SITE" has to name the button
// the operator is being sent to, in the letters that are on it.
QString DiversityFlowStrip::pageWord(int page)
{
    switch (page) {
    case PageSlice:
        return tr("SLICE");
    case PageSite:
        return tr("SITE");
    case PageFilter:
        return tr("FILTER");
    default:
        break;
    }
    return QString();
}

QString DiversityFlowStrip::stepTone(int step) const
{
    return (step >= 0 && step < m_tones.size()) ? m_tones.at(step) : QString();
}

void DiversityFlowStrip::setCurrentPage(int page)
{
    if (m_page == page)
        return;
    m_page = page;
    rebuild();
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------
//
// One line, five steps, " · " between them. Three things vary and nothing
// else does:
//
//   * the glyph -- ✓ behind you, ● the one to do, ○ still ahead;
//   * whether the gate's state string is quoted after the word. A step that is
//     done or next says what it is; a step that is still ahead says only its
//     name, unless the operator is standing on its page, where the state is
//     the reason they went there;
//   * the colour, which is the page relevance and nothing more. See stepTone().
//
// Only the next step is an anchor. Everything else is text, because a
// checklist you can click five things on is a menu again -- which is the
// mistake this line exists to undo.

void DiversityFlowStrip::rebuild()
{
    if (!m_line || m_labels.size() != StepCount)
        return;

    const State states[StepCount] = {alignState(), modeState(), hearState(),
                                     noiseState(), filterState(), digState()};

    // The one rule the whole widget is: next is the first step that is not
    // done. FILTER is done by construction, so this always lands somewhere.
    m_next = StepFilter;
    for (int i = 0; i < StepCount; ++i) {
        if (!states[i].done) {
            m_next = i;
            break;
        }
    }

    // No new colours: the primary and disabled text tokens the status strip
    // already keys off, and the accent the window's verdict lines wear.
    const ThemeManager& tm = ThemeManager::instance();
    const QString normal = tm.cssFragment(QStringLiteral("color.text.primary"));
    const QString dim = tm.cssFragment(QStringLiteral("color.text.disabled"));
    const QString accent = tm.cssFragment(QStringLiteral("color.accent.bright"));

    // BAND owns no step, so on BAND the "belongs to this page" rule would dim
    // every one of them including the next -- and the next step is the one
    // thing this line exists to say. There, only it stays lit.
    bool pageOwnsAStep = false;
    for (int i = 0; i < StepCount; ++i) {
        if (stepPage(i) == m_page) {
            pageOwnsAStep = true;
            break;
        }
    }

    QStringList html;
    QStringList plain;
    for (int i = 0; i < StepCount; ++i) {
        // A gate that cannot dig has no dig step. Not greyed and not dashed:
        // there is nothing about it to explain to somebody whose gate will
        // never offer it.
        if (i == StepDig && !m_digAvailable) {
            m_tones[i] = QStringLiteral("hidden");
            continue;
        }
        const bool onPage = stepPage(i) == m_page;
        const bool isNext = i == m_next;
        const bool lit = isNext && (onPage || !pageOwnsAStep);
        // A dash is not a state, it is the absence of one, and "✓ filter —"
        // says less than "✓ filter".
        // DIG is the one step whose state is quoted from every page: a run is
        // happening NOW, and a countdown only the FILTER page could see would
        // be a countdown nobody watches.
        const bool quoteState = (i == StepDig || isNext || i < m_next || onPage)
                                && !states[i].text.isEmpty()
                                && !states[i].text.startsWith(dash());

        QString text;
        if (isNext) {
            text = quoteState
                       ? tr("%1 %2 · %3").arg(QString::fromUtf8(kGlyphNext),
                                              m_labels.at(i), states[i].text)
                       : tr("%1 %2").arg(QString::fromUtf8(kGlyphNext), m_labels.at(i));
            // Where to go to do it, when that is not where the operator is.
            // Several state strings already end in an arrow to their own page
            // ("2 findings → SITE"); those are left alone rather than doubled.
            const QString page = pageWord(stepPage(i));
            if (!onPage && !page.isEmpty() && !text.endsWith(arrow() + page))
                text += arrow() + page;
        } else {
            // ● for a dig in progress: it is not the next step and never will
            // be, but it IS the thing happening, and the glyph says so.
            const char* glyph = (i == StepDig && m_digRunning)
                                    ? kGlyphNext
                                    : (i < m_next ? kGlyphDone : kGlyphLater);
            text = quoteState
                       ? tr("%1 %2 %3").arg(QString::fromUtf8(glyph), m_labels.at(i),
                                            states[i].text)
                       : tr("%1 %2").arg(QString::fromUtf8(glyph), m_labels.at(i));
        }

        const bool accented = lit || (i == StepDig && m_digRunning);
        m_tones[i] = accented ? QStringLiteral("lit")
                              : onPage ? QStringLiteral("normal")
                                       : QStringLiteral("dim");
        const QString colour = accented ? accent : (onPage ? normal : dim);
        // Escaped: talker names and mode words are the gate's, and a station
        // called "<b" must not be able to write markup into this line.
        const QString escaped = text.toHtmlEscaped();
        html << (isNext
                     ? QStringLiteral("<a href=\"step:%1\" style=\"color:%2;"
                                      "text-decoration:none;\">%3</a>")
                           .arg(QString::number(i), colour, escaped)
                     : QStringLiteral("<span style=\"color:%1;\">%2</span>")
                           .arg(colour, escaped));
        plain << text;
    }

    m_line->setText(html.join(QStringLiteral(" · ")));
    // The whole line, unmarked up, is what a screen reader should read: the
    // glyphs are already words there ("✓" reads as "check"), and the order is
    // the point.
    m_line->setAccessibleDescription(plain.join(QStringLiteral(" · ")));
}

} // namespace AetherSDR
