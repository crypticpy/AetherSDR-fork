#include "gui/AetherGateChainModes.h"

#include <QCoreApplication>
#include <QTimer>

namespace AetherSDR {

namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("AetherGateChainModes", text);
}

// A step whose answer never came. Two poll intervals (the FILTER poll is at
// 500 ms) plus the poller's own 2 s transfer timeout: past that the reply is
// not late, it is not coming.
constexpr int kStepGuardMs = 3000;

// ---------------------------------------------------------------------------
// Which stage belongs to which mode
// ---------------------------------------------------------------------------
//
// One row per stage the app can name, and the three modes it is FOR. Anything
// not in this table -- a stage a newer gate authored -- is for every mode, on
// the principle that the app must not collapse a stage it cannot reason about.
//
// The judgements, in one sentence each:
//   * the roofs, the blanker, the passband, the shape, the notch and the AGC
//     are ahead of any mode decision: every mode wants them.
//   * CONTOUR, RX EQ, AUTO WIDTH and PER TALKER are all fitted from SPEECH --
//     a voice print, a speech spectrum, an over of somebody talking. On CW
//     they have nothing to fit and on data they would fight the modem.
//   * APF is a resonance at the CW note. On phone it is a formant filter
//     nobody asked for.
struct ModeRow {
    const char* id;
    bool phone;
    bool cw;
    bool data;
};

const ModeRow kModeTable[] = {
    {"roof_rf",      true,  true,  true},
    {"roof_digital", true,  true,  true},
    {"nb",           true,  true,  true},
    {"passband",     true,  true,  true},
    {"shape",        true,  true,  true},
    {"notch",        true,  true,  true},
    {"contour",      true,  false, false},
    {"apf",          false, true,  false},
    {"auto_width",   true,  false, false},
    {"auto_eq",      true,  false, false},
    {"agc",          true,  true,  true},
    {"talker",       true,  false, false},
    {"voice",        true,  true,  true},
};

// ---------------------------------------------------------------------------
// THE TWO SETS
// ---------------------------------------------------------------------------
//
// Every key below is one the gate accepts today: `_FILTER_FLAGS` /
// `_FILTER_WORDS` in aether_gate/core/engine.py and the `set()` branch list in
// aether_gate/core/filter.py. Nothing here writes roof_hz or digital_roof_hz --
// the gate on the bench answers those with an error, and a set whose first
// line fails is not a set.
//
// NB is written as `nb=on` and not `nb=auto`: /diversity/set takes the word
// "auto", /filter/set does not (`_filter_kwargs` rejects any nb value outside
// 1/0/on/off/true/false), and the CHAIN window's blanker row is a /filter row.

// One line of a set as it is written in this file: plain bytes, so the table
// is a table and not a list of constructed QStrings at static-init time. The
// sentences are turned into translatable strings in toList().
struct PresetRow {
    const char* route;
    const char* query;
    const char* why;
};

const PresetRow kVoiceSet[] = {
    // AUTO WIDTH first and off: it re-fits both edges every over, so edges
    // placed while it is on would be moved back before the set finished.
    {"/filter/set", "auto=off",         QT_TR_NOOP("auto width off, so the edges below stay put")},
    // 350-2750 Hz: 2.4 kHz of speech. Below 350 is mains hum and rumble, above
    // 2750 is hiss -- the same passband an FTdx101MP calls its SSB default.
    {"/filter/set", "low=350",          QT_TR_NOOP("bottom edge 350 Hz, above the hum")},
    {"/filter/set", "high=2750",        QT_TR_NOOP("top edge 2750 Hz: 2.4 kHz of speech")},
    // Soft skirts. A 1023-tap Kaiser rings on the consonants.
    {"/filter/set", "shape=soft",       QT_TR_NOOP("soft skirts: sharp ones ring on speech")},
    // The audio peak filter is a CW resonance; on voice it is a formant nobody
    // asked for.
    {"/filter/set", "apf=off",          QT_TR_NOOP("APF off: it is a CW resonance")},
    {"/filter/set", "anf=on",           QT_TR_NOOP("auto notch on, for heterodynes")},
    {"/filter/set", "contour=on",       QT_TR_NOOP("contour on")},
    // The bell fitted from the talker's own voice print rather than a knob.
    {"/filter/set", "auto_contour=on",  QT_TR_NOOP("contour fitted from the talker's print")},
    {"/filter/set", "auto_eq=on",       QT_TR_NOOP("RX EQ on: one automatic tilt")},
    {"/filter/set", "nb=on",            QT_TR_NOOP("blanker on")},
    // Medium: fast enough for SSB syllables, slow enough not to pump.
    {"/filter/set", "agc=med",          QT_TR_NOOP("AGC medium")},
    {"/filter/set", "talker=on",        QT_TR_NOOP("per-talker recall on")},
};

const PresetRow kCwSet[] = {
    {"/filter/set", "auto=off",         QT_TR_NOOP("auto width off: it fits speech, not CW")},
    // 350-850 Hz: 500 Hz wide, centred on the 600 Hz the APF defaults to, so
    // the note the peak filter resonates at is in the middle of the passband.
    {"/filter/set", "low=350",          QT_TR_NOOP("bottom edge 350 Hz")},
    {"/filter/set", "high=850",         QT_TR_NOOP("top edge 850 Hz: 500 Hz wide, centred on 600")},
    // Steep skirts are the whole point of a CW filter.
    {"/filter/set", "shape=sharp",      QT_TR_NOOP("sharp skirts: a 1023-tap Kaiser")},
    {"/filter/set", "apf=on",           QT_TR_NOOP("APF on")},
    {"/filter/set", "apf_hz=600",       QT_TR_NOOP("peak at 600 Hz")},
    {"/filter/set", "apf_width=150",    QT_TR_NOOP("150 Hz wide")},
    {"/filter/set", "contour=off",      QT_TR_NOOP("contour off: nothing to fit on a tone")},
    {"/filter/set", "auto_contour=off", QT_TR_NOOP("and its auto fit off with it")},
    {"/filter/set", "auto_eq=off",      QT_TR_NOOP("RX EQ off: it tilts speech")},
    // A CW note IS a steady carrier. The auto notch would hunt the signal.
    {"/filter/set", "anf=off",          QT_TR_NOOP("auto notch off: it would hunt the note")},
    {"/filter/set", "nb=on",            QT_TR_NOOP("blanker on")},
    {"/filter/set", "agc=fast",         QT_TR_NOOP("AGC fast, for keying")},
    {"/filter/set", "talker=off",       QT_TR_NOOP("per-talker recall off: no voice to know")},
};

QList<ChainPresetWrite> toList(const PresetRow* rows, int count)
{
    QList<ChainPresetWrite> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        out.append({QString::fromLatin1(rows[i].route),
                    QString::fromLatin1(rows[i].query),
                    tr_(rows[i].why)});
    }
    return out;
}

} // namespace

QString chainModeId(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone: return QStringLiteral("phone");
    case ChainMode::Cw:    return QStringLiteral("cw");
    case ChainMode::Data:  return QStringLiteral("data");
    }
    return QStringLiteral("phone");
}

QString chainModeLabel(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone: return tr_("PHONE");
    case ChainMode::Cw:    return tr_("CW");
    case ChainMode::Data:  return tr_("DATA/OTHER");
    }
    return tr_("PHONE");
}

QString chainModeTip(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone:
        return tr_("Voice. The stages that shape speech stay on the strip; the "
                   "CW-only ones go into the group below it. VOICE SET applies "
                   "a 350-2750 Hz passband, the contour, the automatic EQ and a "
                   "medium AGC, one write at a time.");
    case ChainMode::Cw:
        return tr_("CW. The audio peak filter comes onto the strip and the "
                   "speech-fitted stages -- contour, RX EQ, auto width, per "
                   "talker -- go into the group below it. CW SET applies a "
                   "500 Hz passband centred on 600 Hz, the APF and a fast AGC.");
    case ChainMode::Data:
        return tr_("Data, and anything else. Only the stages that are ahead of "
                   "any mode decision stay on the strip; the speech and CW ones "
                   "go below. There is no data set: the gate has no data stage "
                   "of its own, and a button that wrote nothing would be a lie.");
    }
    return QString();
}

bool chainStageInMode(const QString& id, ChainMode mode)
{
    for (const ModeRow& row : kModeTable) {
        if (id != QLatin1String(row.id))
            continue;
        switch (mode) {
        case ChainMode::Phone: return row.phone;
        case ChainMode::Cw:    return row.cw;
        case ChainMode::Data:  return row.data;
        }
    }
    return true;   // a stage the app has never heard of belongs everywhere
}

QString chainSetLabel(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone: return tr_("VOICE SET");
    case ChainMode::Cw:    return tr_("CW SET");
    case ChainMode::Data:  return tr_("DATA SET");
    }
    return QString();
}

QList<ChainPresetWrite> chainPreset(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone:
        return toList(kVoiceSet, int(sizeof(kVoiceSet) / sizeof(PresetRow)));
    case ChainMode::Cw:
        return toList(kCwSet, int(sizeof(kCwSet) / sizeof(PresetRow)));
    case ChainMode::Data:
        return {};
    }
    return {};
}

// ---------------------------------------------------------------------------
// AetherGateChainPreset
// ---------------------------------------------------------------------------

AetherGateChainPreset::AetherGateChainPreset(QObject* parent)
    : QObject(parent)
{
    setObjectName(QStringLiteral("gateChainPresetRunner"));
    m_guard = new QTimer(this);
    m_guard->setObjectName(QStringLiteral("gateChainPresetGuardTimer"));
    m_guard->setSingleShot(true);
    m_guard->setInterval(kStepGuardMs);
    connect(m_guard, &QTimer::timeout, this, [this] {
        const QString name = m_name;
        m_index = -1;
        emit finished(name, false, tr("the gate stopped answering mid-set"));
    });
}

void AetherGateChainPreset::start(const QList<ChainPresetWrite>& writes,
                                  const QString& name)
{
    m_guard->stop();
    m_writes = writes;
    m_name = name;
    if (m_writes.isEmpty()) {
        m_index = -1;
        return;
    }
    m_index = 0;
    sendStep();
}

void AetherGateChainPreset::sendStep()
{
    if (m_index < 0 || m_index >= m_writes.size()) {
        const QString name = m_name;
        m_index = -1;
        m_guard->stop();
        emit finished(name, true, QString());
        return;
    }
    const ChainPresetWrite& step = m_writes.at(m_index);
    emit progress(m_name, m_index + 1, m_writes.size(), step.why);
    m_guard->start();
    emit requestWrite(step.route, QUrlQuery(step.query));
}

void AetherGateChainPreset::noteFilterBody()
{
    if (m_index < 0)
        return;
    m_guard->stop();
    ++m_index;
    sendStep();
}

void AetherGateChainPreset::noteError(const QString& error)
{
    if (m_index < 0)
        return;
    const QString name = m_name;
    m_index = -1;
    m_guard->stop();
    emit finished(name, false, error);
}

void AetherGateChainPreset::abort()
{
    m_index = -1;
    m_guard->stop();
}

} // namespace AetherSDR
