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
    // The front end and the converter are ahead of any mode decision.
    {"antenna",      true,  true,  true},
    {"traps",        true,  true,  true},
    {"lna",          true,  true,  true},
    {"ifgr",         true,  true,  true},
    {"rf_agc",       true,  true,  true},
    {"roof_rf",      true,  true,  true},
    {"adc",          true,  true,  true},
    // The pair. Two loops help on every mode.
    {"align",        true,  true,  true},
    {"nb",           true,  true,  true},
    {"combiner",     true,  true,  true},
    {"subband",      true,  true,  true},
    {"post",         true,  true,  true},
    // The slice filter.
    {"roof_digital", true,  true,  true},
    {"slice",        true,  true,  true},
    {"passband",     true,  true,  true},
    {"auto",         true,  false, false},
    {"auto_width",   true,  false, false},   // the app's own fallback spelling
    {"shape",        true,  true,  true},
    {"notch",        true,  true,  true},
    {"anf",          true,  true,  true},
    {"contour",      true,  false, false},
    {"apf",          false, true,  false},
    {"auto_eq",      true,  false, false},
    {"talker",       true,  false, false},
    // Out of the gate.
    {"detect",       true,  true,  true},
    {"agc",          true,  true,  true},
    {"app",          true,  true,  true},
    {"voice",        true,  true,  true},    // the app's own fallback spelling
};

// ---------------------------------------------------------------------------
// Which group a stage is drawn in
// ---------------------------------------------------------------------------
//
// Signal order is the gate's; the GROUP is the app's reading of it, and it is
// the only thing in this window that rearranges anything -- four columns
// instead of one wall of tiles. Within a column the gate's order is kept
// exactly.

struct GroupRow {
    const char* id;
    ChainGroup  group;
};

const GroupRow kGroupTable[] = {
    {"antenna",      ChainGroup::FrontEnd},
    {"traps",        ChainGroup::FrontEnd},
    {"lna",          ChainGroup::FrontEnd},
    {"ifgr",         ChainGroup::FrontEnd},
    {"rf_agc",       ChainGroup::FrontEnd},
    {"roof_rf",      ChainGroup::FrontEnd},
    {"adc",          ChainGroup::FrontEnd},

    {"align",        ChainGroup::Pair},
    {"nb",           ChainGroup::Pair},
    {"combiner",     ChainGroup::Pair},
    {"subband",      ChainGroup::Pair},
    {"post",         ChainGroup::Pair},

    {"roof_digital", ChainGroup::Passband},
    {"slice",        ChainGroup::Passband},
    {"passband",     ChainGroup::Passband},
    {"auto",         ChainGroup::Passband},
    {"auto_width",   ChainGroup::Passband},
    {"shape",        ChainGroup::Passband},
    {"notch",        ChainGroup::Passband},
    {"anf",          ChainGroup::Passband},
    {"contour",      ChainGroup::Passband},
    {"apf",          ChainGroup::Passband},
    {"auto_eq",      ChainGroup::Passband},
    {"talker",       ChainGroup::Passband},

    {"detect",       ChainGroup::Out},
    {"agc",          ChainGroup::Out},
    {"app",          ChainGroup::Out},
    {"voice",        ChainGroup::Out},
};

// ---------------------------------------------------------------------------
// The one line a card shows
// ---------------------------------------------------------------------------
//
// The gate writes `detail` as fields joined by " - ": the whole truth about
// the stage, and far too much of it for a 196 px card. Each row below names
// which of those fields the card keeps, in the order they should be read, and
// how many characters each may spend. `budget` 0 means "as written".
//
// The picks are chosen so the card answers the question the stage raises and
// nothing else: ALIGN is asked "is it locked?" before "by how much", so
// `locked` comes first and the lag is trimmed to its number. COMBINER is
// asked "what is it doing, and is it doing anything?", which is the mode, the
// phase and the ratio -- the four SNR numbers behind them are a whole sentence
// and belong in the inspector.

constexpr int kPicks = 3;

struct PrimaryRow {
    const char* id;
    const char* verbatim;        // a row whose detail is prose, not fields
    int         pick[kPicks];    // index into the detail's fields, -1 to stop
    int         budget[kPicks];  // characters, 0 for "as written"
};

const PrimaryRow kPrimaryTable[] = {
    {"antenna",      nullptr, {0, -1, -1}, {0,  0, 0}},
    {"traps",        nullptr, {0,  1, -1}, {0,  0, 0}},
    {"lna",          nullptr, {0, -1, -1}, {0,  0, 0}},
    {"ifgr",         nullptr, {0, -1, -1}, {0,  0, 0}},
    // "off" is the answer; the set-point is a number for the inspector.
    {"rf_agc",       nullptr, {0, -1, -1}, {0,  0, 0}},
    // The menu beside it already shows the width, so the line says the thing
    // the menu cannot: whether this is as narrow as the hardware goes, or
    // whether the sample rate is choosing it.
    {"roof_rf",      nullptr, {1, -1, -1}, {0,  0, 0}},
    {"adc",          nullptr, {0, -1, -1}, {0,  0, 0}},

    // "locked" answers the question; "lag -63 samples" trims to "lag -63".
    {"align",        nullptr, {1,  0, -1}, {0,  8, 0}},
    {"nb",           nullptr, {0,  1, -1}, {0, 14, 0}},
    {"combiner",     nullptr, {0,  1,  2}, {0,  0, 0}},
    {"subband",      nullptr, {0,  1, -1}, {0,  0, 0}},
    {"post",         nullptr, {0,  1, -1}, {0,  0, 0}},

    // Same again: the width is on the menu, so the line is the tap count.
    {"roof_digital", nullptr, {1, -1, -1}, {0,  0, 0}},
    {"slice",        nullptr, {0,  1, -1}, {0,  0, 0}},
    // Both edges. "asked 100-2900" is the AUTO WIDTH story and lives there.
    {"passband",     nullptr, {0, -1, -1}, {0,  0, 0}},
    {"auto",         nullptr, {0,  1, -1}, {0,  0, 0}},
    {"auto_width",   nullptr, {0,  1, -1}, {0,  0, 0}},
    // The word and the skirt; the tap count is a consequence of the word.
    {"shape",        nullptr, {0,  2, -1}, {0,  0, 0}},
    {"notch",        nullptr, {0,  1, -1}, {0,  0, 0}},
    {"anf",          nullptr, {0, -1, -1}, {0,  0, 0}},
    {"contour",      nullptr, {0,  1, -1}, {0, 16, 0}},
    {"apf",          nullptr, {0,  1, -1}, {0,  0, 0}},
    {"auto_eq",      nullptr, {0,  1, -1}, {10, 0, 0}},
    {"talker",       nullptr, {0,  1, -1}, {0,  0, 0}},

    {"detect",       nullptr, {0, -1, -1}, {0,  0, 0}},
    {"agc",          nullptr, {0,  1, -1}, {0,  0, 0}},
    // The only row whose detail is a sentence with no numbers in it at all.
    // Trimming it by words gives "noise reduction and", which is worse than
    // saying the one thing that matters: this stage is not the gate's.
    {"app",          QT_TR_NOOP("runs in the app"), {-1, -1, -1}, {0, 0, 0}},
    {"voice",        QT_TR_NOOP("runs in the app"), {-1, -1, -1}, {0, 0, 0}},
};

// The longest line the table above can produce, measured against the gate's
// own worst case: COMBINER in track with three digits of phase and a signed
// ratio. Every card in the window is built to this width, so a stage that
// gains a digit cannot move the diagram.
const char* kPrimaryWorst = QT_TR_NOOP("floor -16.0 dB \u00b7 mean -18.5 dB");

// One field of a detail, trimmed to `budget` characters by dropping WHOLE
// words off the end -- never a cut through the middle of a number -- and then
// stripped of the punctuation the cut left behind.
QString trimField(const QString& field, int budget)
{
    QString out = field.trimmed();
    if (budget <= 0 || out.size() <= budget)
        return out;
    while (out.size() > budget && out.contains(QLatin1Char(' '))) {
        out.truncate(out.lastIndexOf(QLatin1Char(' ')));
        out = out.trimmed();
        while (out.endsWith(QLatin1Char(',')) || out.endsWith(QLatin1Char(':')))
            out.chop(1);
        out = out.trimmed();
    }
    return out;
}

const PrimaryRow* primaryRowFor(const QString& id)
{
    for (const PrimaryRow& row : kPrimaryTable) {
        if (id == QLatin1String(row.id))
            return &row;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// What the inspector says
// ---------------------------------------------------------------------------
//
// Two sentences per stage, both about the SOUND. Neither repeats the card:
// the card says what the stage is DOING, these say what it is FOR and what
// its absence would cost. A stage nothing can switch has no "off" line --
// there is no off.

struct WordsRow {
    const char* id;
    const char* sound;
    const char* off;     // nullptr where the stage cannot be switched
};

const WordsRow kWordsTable[] = {
    {"antenna",
     QT_TR_NOOP("The port the receiver is listening on."), nullptr},
    {"traps",
     QT_TR_NOOP("Notches for the broadcast bands, so a local transmitter "
                "cannot flatten everything else."), nullptr},
    {"lna",
     QT_TR_NOOP("The front-end amplifier and attenuator. Too much and strong "
                "signals distort; too little and you hear the receiver."),
     nullptr},
    {"ifgr",
     QT_TR_NOOP("How much gain the IF is running. Take it down when the band "
                "is loud and the noise floor will follow it down."), nullptr},
    {"rf_agc",
     QT_TR_NOOP("The receiver's own gain control, ahead of anything the "
                "filters do."), nullptr},
    {"roof_rf",
     QT_TR_NOOP("The analogue filter inside the receiver. It protects the "
                "converter, not your ear."), nullptr},
    {"adc",
     QT_TR_NOOP("How fast the receiver samples. Everything after it is "
                "bounded by this number."), nullptr},

    {"align",
     QT_TR_NOOP("Slides one loop against the other until the same wavefront "
                "arrives at both at the same instant."),
     QT_TR_NOOP("adrift: the two loops add noise instead of signal.")},
    {"nb",
     QT_TR_NOOP("Cuts impulse noise -- ignition, a fence charger -- before "
                "any filter can smear the click into a thud."),
     QT_TR_NOOP("off: every impulse arrives as a thud across the passband.")},
    {"combiner",
     QT_TR_NOOP("Adds the two loops with the phase and gain that make the "
                "wanted signal loudest and the local noise quietest."),
     QT_TR_NOOP("off: you are listening to one loop.")},
    {"subband",
     QT_TR_NOOP("Steers a separate null onto each narrow slice of the "
                "passband, so one carrier can be nulled without moving the "
                "rest."),
     QT_TR_NOOP("off: an interfering carrier inside the voice stays at full "
                "strength.")},
    {"post",
     QT_TR_NOOP("A gentle floor after the pair, so the combiner's own hiss "
                "does not breathe behind the voice."),
     QT_TR_NOOP("off: a little more hiss between syllables.")},

    {"roof_digital",
     QT_TR_NOOP("The DSP IF bandwidth, ahead of everything you tune. Narrow "
                "it and a strong neighbour stops reaching the gain control."),
     QT_TR_NOOP("wide open: a loud station a few kilohertz away can pump the "
                "gain.")},
    {"slice",
     QT_TR_NOOP("The receive filter itself. Every stage below it happens "
                "inside this filter."),
     QT_TR_NOOP("bypassed: the whole IF reaches your ears unfiltered.")},
    {"passband",
     QT_TR_NOOP("The two edges of the filter, placed independently. This is "
                "what decides how wide the voice sounds."), nullptr},
    {"auto",
     QT_TR_NOOP("Measures where this station's energy actually is and moves "
                "both edges onto it, once per over."),
     QT_TR_NOOP("off: the edges stay exactly where you put them.")},
    {"auto_width",
     QT_TR_NOOP("Measures where this station's energy actually is and moves "
                "both edges onto it, once per over."),
     QT_TR_NOOP("off: the edges stay exactly where you put them.")},
    {"shape",
     QT_TR_NOOP("How steep the edges are. Soft is kinder on speech; sharp "
                "cuts a close neighbour harder and rings a little."),
     nullptr},
    {"notch",
     QT_TR_NOOP("The notches you placed by hand, cut out of the same filter "
                "rather than added after it."),
     QT_TR_NOOP("off: every carrier you notched comes back.")},
    {"anf",
     QT_TR_NOOP("Hunts steady heterodynes on its own and notches them as "
                "they appear."),
     QT_TR_NOOP("off: a drifting whistle stays with you.")},
    {"contour",
     QT_TR_NOOP("A broad lift or dip inside the passband, fitted to this "
                "talker's own voice."),
     QT_TR_NOOP("off: the station keeps whatever bass or edge it arrived "
                "with.")},
    {"apf",
     QT_TR_NOOP("A narrow resonance at the CW note, for digging one signal "
                "out of the noise around it."),
     QT_TR_NOOP("off: the note sits at the level of the noise beside it.")},
    {"auto_eq",
     QT_TR_NOOP("One automatic tilt between 550 Hz and 2 kHz, to flatten a "
                "station that is all bass or all edge."),
     QT_TR_NOOP("off: a bassy station stays bassy.")},
    {"talker",
     QT_TR_NOOP("Remembers the automatic settings for each voice the receiver "
                "recognises, so a station you worked last night comes back "
                "sounding the same."),
     QT_TR_NOOP("off: every station starts from the same settings.")},

    {"detect",
     QT_TR_NOOP("Turns the tuned slice into audio. The mode decides which "
                "detector."), nullptr},
    {"agc",
     QT_TR_NOOP("How fast the gain follows the signal. Medium suits speech; "
                "fast suits keying; slow rides out a flutter."),
     QT_TR_NOOP("off: a loud signal arrives at full strength.")},
    {"app",
     QT_TR_NOOP("Noise reduction and compression happen here in the app, "
                "after everything above."), nullptr},
    {"voice",
     QT_TR_NOOP("Noise reduction and compression happen here in the app, "
                "after everything above."), nullptr},
};

const WordsRow* wordsRowFor(const QString& id)
{
    for (const WordsRow& row : kWordsTable) {
        if (id == QLatin1String(row.id))
            return &row;
    }
    return nullptr;
}

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
    case ChainMode::Data:  return tr_("DATA");
    }
    return tr_("PHONE");
}

QString chainModeTip(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone:
        return tr_("Voice. The stages that shape speech stay on the diagram; "
                   "the CW-only ones fold away under it.");
    case ChainMode::Cw:
        return tr_("CW. The audio peak filter comes onto the diagram and the "
                   "stages that fit themselves to speech fold away under it.");
    case ChainMode::Data:
        return tr_("Data, and anything else. Only the stages that are ahead "
                   "of any mode decision stay on the diagram.");
    }
    return QString();
}

// The line under the set button. It is about the SOUND and about nothing
// else: an operator who presses this wants to know what his receiver will
// sound like afterwards, not how many parameters moved.
QString chainModeSound(ChainMode mode)
{
    switch (mode) {
    case ChainMode::Phone:
        return tr_("Opens the passband to 350-2750 Hz with soft edges, puts "
                   "the auto notch, the contour and the blanker on, and sets "
                   "a medium AGC.");
    case ChainMode::Cw:
        return tr_("Narrows the passband to 500 Hz around 600, sharpens the "
                   "edges, puts the peak filter on the note and sets a fast "
                   "AGC.");
    case ChainMode::Data:
        return tr_("No set for data yet: nothing in the chain is specific to "
                   "a modem, so there is nothing honest to apply.");
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
    case ChainMode::Phone: return tr_("SET UP FOR PHONE");
    case ChainMode::Cw:    return tr_("SET UP FOR CW");
    case ChainMode::Data:  return tr_("SET UP FOR DATA");
    }
    return QString();
}

QString chainSetBusyLabel()
{
    return tr_("SETTING UP...");
}

// ---------------------------------------------------------------------------
// The four groups
// ---------------------------------------------------------------------------

QString chainGroupId(ChainGroup group)
{
    switch (group) {
    case ChainGroup::FrontEnd: return QStringLiteral("frontend");
    case ChainGroup::Pair:     return QStringLiteral("pair");
    case ChainGroup::Passband: return QStringLiteral("passband");
    case ChainGroup::Out:      return QStringLiteral("out");
    }
    return QStringLiteral("passband");
}

QString chainGroupLabel(ChainGroup group)
{
    switch (group) {
    case ChainGroup::FrontEnd: return tr_("FRONT END");
    case ChainGroup::Pair:     return tr_("PAIR");
    case ChainGroup::Passband: return tr_("PASSBAND");
    case ChainGroup::Out:      return tr_("OUT");
    }
    return QString();
}

QString chainGroupTip(ChainGroup group)
{
    switch (group) {
    case ChainGroup::FrontEnd:
        return tr_("What the antenna and the receiver do to the signal before "
                   "any of this reaches the filters. None of it is switched "
                   "from here, so it is one card rather than seven.");
    case ChainGroup::Pair:
        return tr_("What the two loops do together: line them up, blank the "
                   "impulses, add them the way that helps, and tidy up after.");
    case ChainGroup::Passband:
        return tr_("The filter you actually tune. Everything here happens "
                   "inside the receive filter, ahead of the gain control.");
    case ChainGroup::Out:
        return tr_("What leaves the receiver for your ears.");
    }
    return QString();
}

ChainGroup chainStageGroup(const QString& id, ChainGroup previous)
{
    for (const GroupRow& row : kGroupTable) {
        if (id == QLatin1String(row.id))
            return row.group;
    }
    return previous;
}

// ---------------------------------------------------------------------------
// The one line a card shows
// ---------------------------------------------------------------------------

QStringList chainPrimaryParts(const ChainStage& stage)
{
    const QStringList fields =
        stage.detail.split(QStringLiteral(" \u00b7 "), Qt::SkipEmptyParts);
    const PrimaryRow* row = primaryRowFor(stage.id);

    QStringList parts;
    if (row && row->verbatim) {
        parts << tr_(row->verbatim);
    } else if (row) {
        for (int i = 0; i < kPicks; ++i) {
            const int at = row->pick[i];
            if (at < 0 || at >= fields.size())
                continue;
            const QString part = trimField(fields.at(at), row->budget[i]);
            if (!part.isEmpty())
                parts << part;
        }
    } else {
        // A stage the app has never heard of: its first two fields, which is
        // the gate's own idea of what matters most about it.
        for (int i = 0; i < fields.size() && i < 2; ++i)
            parts << fields.at(i).trimmed();
    }

    // A control already says what it is set to. A line that opened by saying
    // it again would be spending a third of the card on something the
    // operator can see -- so drop it, as long as something else is left to
    // read. A switch says on or off; a menu says the value in force.
    if (parts.size() > 1 && stage.kind == QLatin1String("toggle")
        && (parts.first() == tr_("on") || parts.first() == tr_("off"))) {
        parts.removeFirst();
    }
    if (parts.size() > 1 && stage.kind == QLatin1String("select")
        && !stage.value.isEmpty()
        && parts.first().compare(stage.value, Qt::CaseInsensitive) == 0) {
        parts.removeFirst();
    }
    if (parts.isEmpty() && !stage.detail.isEmpty())
        parts << stage.detail;
    return parts;
}

QString chainPrimaryWorstCase()
{
    return tr_(kPrimaryWorst);
}

// ---------------------------------------------------------------------------
// What the inspector says
// ---------------------------------------------------------------------------

QString chainSoundSentence(const QString& id)
{
    const WordsRow* row = wordsRowFor(id);
    return row ? tr_(row->sound) : QString();
}

QString chainOffSentence(const QString& id)
{
    const WordsRow* row = wordsRowFor(id);
    return (row && row->off) ? tr_(row->off) : QString();
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
        emit finished(name, false, tr("the receiver stopped replying part way through"));
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
