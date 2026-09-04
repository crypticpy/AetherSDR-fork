// DiversitySessionModel's derivations. See the header for the three-loop
// model this implements (R2.1) and nextStep()'s walk order (R2.2).
//
// Two rules run through all five steps, the same two DiversityFlowStrip.cpp
// kept for the model this one replaces:
//
//   * a step never invents a fact. Every state string is either the gate's
//     own number, the gate's own word, or a dash.
//   * a step's done-ness and its wording are the same derivation, computed
//     fresh from the last apply() every time steps() is called -- nothing is
//     cached, so "done" and the sentence beside it can never disagree.

#include "gui/DiversitySessionModel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

double num(const QJsonValue& v, double fallback = 0.0)
{
    return v.isDouble() ? v.toDouble() : fallback;
}

QString dash()
{
    return QStringLiteral("—");
}

// "+4.1" / "-0.6" -- a real minus sign, matching DiversityFlowStrip.cpp's
// own signedDb(): this is a number in a sentence, not a table cell.
QString signedDb(double v)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', 1);
    return QStringLiteral("+%1").arg(v, 0, 'f', 1);
}

// "4 min ago" under an hour, "3 h ago" at or past it -- beaconsStalePrints
// TheAge only needs the clause to exist, but a "127 min ago" beacon result
// is exactly the kind of number that is easier to read rounded.
QString ageText(double seconds)
{
    const double s = std::max(0.0, seconds);
    if (s < 3600.0)
        return QCoreApplication::translate("DiversitySessionModel", "%1 min ago")
            .arg(qint64(std::llround(s / 60.0)));
    return QCoreApplication::translate("DiversitySessionModel", "%1 h ago")
        .arg(qint64(std::llround(s / 3600.0)));
}

// The amateur bands this model needs to tell apart, 160 m through 10 m.
// beaconHz is 0 for a band the NCDXF/IARU rota does not cover -- BAND reads
// that as "not applicable" per R2.1.
//
// A private table rather than BandPlanManager: that class needs a QObject,
// resource-loaded plans, and a search RANGE you already have to know the
// band to build (contiguousRegionsForBand(lowMhz, highMhz)) -- there is no
// bare "which band is this Hz in" call on it to reuse headlessly, and this
// model links only Qt Core/Gui/Test. DiversityBeaconPanel.cpp's own
// kBandNames table (see its comment) made the same trade for the same five
// bands; this is a fourth copy of that idea, now covering the bands with no
// beacon frequency too, which BAND needs to tell "not applicable" from
// "nothing measured yet".
struct BandRow {
    double loHz;
    double hiHz;
    double beaconHz;
    const char* name;
};

constexpr BandRow kBands[] = {
    {1'800'000.0, 2'000'000.0, 0.0, "160 m"},
    {3'500'000.0, 4'000'000.0, 0.0, "80 m"},
    {5'000'000.0, 5'600'000.0, 0.0, "60 m"},
    {7'000'000.0, 7'300'000.0, 0.0, "40 m"},
    {10'100'000.0, 10'150'000.0, 0.0, "30 m"},
    {14'000'000.0, 14'350'000.0, 14'100'000.0, "20 m"},
    {18'068'000.0, 18'168'000.0, 18'110'000.0, "17 m"},
    {21'000'000.0, 21'450'000.0, 21'150'000.0, "15 m"},
    {24'890'000.0, 24'990'000.0, 24'930'000.0, "12 m"},
    {28'000'000.0, 29'700'000.0, 28'200'000.0, "10 m"},
};
constexpr int kBandCount = int(sizeof(kBands) / sizeof(kBands[0]));

// No filter width reported yet: a generic SSB passband, wide enough that an
// ordinary tuning knob nudge inside one QSO never misfires the reset.
constexpr double kDefaultPassbandHz = 2800.0;

double passbandHzFromFilter(const QJsonObject& filter)
{
    const QJsonValue low = filter.value(QStringLiteral("low_hz"));
    const QJsonValue high = filter.value(QStringLiteral("high_hz"));
    if (low.isDouble() && high.isDouble()) {
        const double width = high.toDouble() - low.toDouble();
        if (width > 0.0)
            return width;
    }
    return kDefaultPassbandHz;
}

// One governor.holding[] row, read directly rather than through
// AetherGateChainAuto.h's ChainAutoGovernor: that header pulls in QLabel/
// QPushButton (AetherGateChainStage.h), which would force this headless
// model's test binary to link Qt Widgets. Only the three fields SITE
// NOISE's AUTO CLEAN line needs are read here.
struct HoldingRow {
    QString tool;
    QString kind;
    QString why;
};

bool governorAutoOn(const QJsonObject& diversity)
{
    const QJsonValue gov = diversity.value(QStringLiteral("governor"));
    if (!gov.isObject())
        return false;
    return gov.toObject().value(QStringLiteral("auto")).toBool(false);
}

QVector<HoldingRow> governorHolding(const QJsonObject& diversity)
{
    QVector<HoldingRow> out;
    const QJsonValue gov = diversity.value(QStringLiteral("governor"));
    if (!gov.isObject())
        return out;
    for (const QJsonValue& v : gov.toObject().value(QStringLiteral("holding")).toArray()) {
        const QJsonObject row = v.toObject();
        out << HoldingRow{row.value(QStringLiteral("tool")).toString(),
                          row.value(QStringLiteral("kind")).toString(),
                          row.value(QStringLiteral("why")).toString()};
    }
    return out;
}

} // namespace

DiversitySessionModel::DiversitySessionModel() = default;

void DiversitySessionModel::setNowSecs(qint64 nowSecs)
{
    m_nowSecs = nowSecs;
    m_haveNow = true;
}

void DiversitySessionModel::setDismissedKinds(const QSet<QString>& kinds)
{
    m_dismissed = kinds;
}

int DiversitySessionModel::bandIndexForHz(double hz)
{
    for (int i = 0; i < kBandCount; ++i) {
        if (hz >= kBands[i].loHz && hz <= kBands[i].hiHz)
            return i;
    }
    return -1;
}

double DiversitySessionModel::beaconHzForBandIndex(int index)
{
    if (index < 0 || index >= kBandCount)
        return 0.0;
    return kBands[index].beaconHz;
}

void DiversitySessionModel::apply(const QJsonObject& diversity, const QJsonObject& filter,
                                  const QJsonObject& dig, const QJsonObject& beacons,
                                  const QJsonObject& compass, double tunedHz)
{
    m_diversity = diversity;
    m_filter = filter;
    m_dig = dig;
    m_beacons = beacons;
    m_compass = compass;
    m_tunedHz = tunedHz;

    m_available = !diversity.isEmpty()
                  && diversity.value(QStringLiteral("available")).toBool(false);

    // --- STATION's reset guard -------------------------------------------
    // See the header's own comment on m_stationSuppressed for why this has
    // to live across calls instead of being re-derived from this payload
    // alone: the gate's own talker match does not know a retune happened.
    const int prevBand = m_haveLastTuned ? bandIndexForHz(m_lastTunedHz) : -2;
    const int curBand = bandIndexForHz(tunedHz);
    const double passbandHz = passbandHzFromFilter(filter);
    const bool retuneEvent =
        m_haveLastTuned
        && (curBand != prevBand || std::abs(tunedHz - m_lastTunedHz) > passbandHz);

    const QJsonObject talkerObj = diversity.value(QStringLiteral("talker")).toObject();
    const QJsonValue idVal = talkerObj.value(QStringLiteral("id"));
    const bool haveId = idVal.isDouble();
    const int curTalkerId = haveId ? int(std::lround(idVal.toDouble())) : -1;

    if (retuneEvent) {
        // Whichever talker this payload names right now becomes suspect --
        // it was solved for the frequency behind us, not the one ahead.
        m_stationSuppressed = true;
        m_suppressedTalkerId = curTalkerId;
    } else if (m_stationSuppressed && (!haveId || curTalkerId != m_suppressedTalkerId)) {
        // A different talker (or nobody) is a real re-confirmation: the
        // suspicion was about ONE id, and this is not it.
        m_stationSuppressed = false;
    }
    m_lastTunedHz = tunedHz;
    m_haveLastTuned = true;
}

// ---------------------------------------------------------------------------
// RECEIVER -- aligned, mode, hear, headroom
// ---------------------------------------------------------------------------

DiversitySessionModel::Step DiversitySessionModel::buildReceiver() const
{
    Step s;
    s.id = StepReceiver;
    s.loop = LoopSession;
    const SessionCopy copy = sessionStepCopy(StepReceiver);
    s.title = copy.title;
    s.gives[0] = copy.gives[0];
    s.gives[1] = copy.gives[1];
    s.when = copy.when;
    s.page = PageStart;

    if (!m_available) {
        s.state = dash();
        s.done = false;
        return s;
    }

    const bool realigning = m_diversity.value(QStringLiteral("realigning")).toBool(false);
    const bool aligned = m_diversity.value(QStringLiteral("aligned")).toBool(false);
    const QString mode = m_diversity.value(QStringLiteral("mode")).toString();
    const QString source = m_diversity.value(QStringLiteral("source")).toString();
    // TODO(gate): no headroom/guard key exists on /diversity or /filter yet
    // (AetherGateChainAuto.h's own note: the FRONT END guard is synthesised
    // from GET /device's "frontend" key, which is outside this model's five-
    // payload input contract). Read as always clear until a gate-side key
    // lands and this model is told which one.
    const bool headroomClear = true;

    const bool sourceOk = source == QLatin1String("combined")
                          || source == QLatin1String("stereo");
    s.done = aligned && !realigning && !mode.isEmpty() && mode != QLatin1String("off")
             && sourceOk && headroomClear;

    if (realigning) {
        s.state = QStringLiteral("aligning…");
    } else if (!aligned) {
        s.state = QStringLiteral("not aligned");
        s.cure = Cure{QStringLiteral("REALIGN"), QStringLiteral("align"), QString()};
    } else if (mode.isEmpty() || mode == QLatin1String("off")) {
        s.state = QStringLiteral("mode is off — the second loop is doing nothing · ")
                  + sessionPairModeExplain();
        s.cure = Cure{QStringLiteral("TRACK"), QStringLiteral("set"),
                      QStringLiteral("mode=track")};
    } else if (!sourceOk) {
        s.state = QStringLiteral("hearing %1 only — a diagnostic, not a listening state")
                      .arg(source.toUpper());
        s.cure = Cure{QStringLiteral("HEAR OUT"), QStringLiteral("set"),
                      QStringLiteral("source=combined")};
    } else if (!headroomClear) {
        s.state = QStringLiteral("loops overloading, headroom low");
        s.cure = Cure{QStringLiteral("OPEN FRONT END"), QStringLiteral("page"), QString()};
    } else {
        s.state = QStringLiteral("aligned, %1 · hearing %2")
                      .arg(mode, source.toUpper());
    }
    return s;
}

// ---------------------------------------------------------------------------
// SITE NOISE -- the noise_profile.kinds[] table
// ---------------------------------------------------------------------------

DiversitySessionModel::Step DiversitySessionModel::buildSiteNoise() const
{
    Step s;
    s.id = StepSiteNoise;
    s.loop = LoopSession;
    const SessionCopy copy = sessionStepCopy(StepSiteNoise);
    s.title = copy.title;
    s.gives[0] = copy.gives[0];
    s.gives[1] = copy.gives[1];
    s.when = copy.when;
    s.page = PageSite;

    if (!m_available) {
        s.state = dash();
        s.done = false;
        return s;
    }

    const QJsonValue profileVal = m_diversity.value(QStringLiteral("noise_profile"));
    const bool haveProfile = profileVal.isObject();

    int offered = 0;
    QStringList activeKinds;
    if (haveProfile) {
        const QJsonArray kinds = profileVal.toObject().value(QStringLiteral("kinds")).toArray();
        for (const QJsonValue& v : kinds) {
            const QJsonObject row = v.toObject();
            if (!row.value(QStringLiteral("action")).isObject())
                continue; // the gate said why there is nothing to do
            const QString kind = row.value(QStringLiteral("kind")).toString();
            const bool active = row.value(QStringLiteral("active")).toBool(false);
            const bool dismissed = m_dismissed.contains(kind);
            if (active) {
                activeKinds << kind;
            } else if (dismissed) {
                // Handled, but not something to narrate as "acting on" --
                // the operator chose to hear no more about it.
                continue;
            } else {
                ++offered;
            }
        }
    }

    s.done = !haveProfile || offered == 0;

    if (!haveProfile) {
        s.state = QStringLiteral("profiling…");
    } else if (offered > 0) {
        s.state = offered == 1 ? QStringLiteral("1 finding with a button")
                                : QStringLiteral("%1 findings with a button").arg(offered);
        s.cure = Cure{QStringLiteral("GO"), QStringLiteral("page"), QString()};
    } else if (!activeKinds.isEmpty()) {
        s.state = QStringLiteral("acting on %1").arg(activeKinds.join(QStringLiteral(", ")));
    } else {
        s.state = QStringLiteral("clean");
    }

    // The muted AUTO CLEAN note (R2.1): named tool, kind and why, verbatim --
    // never the governor's own paraphrased state_label, which is what
    // autoCleanHoldingLineNamesToolKindWhy guards against.
    if (governorAutoOn(m_diversity)) {
        const QVector<HoldingRow> holding = governorHolding(m_diversity);
        if (!holding.isEmpty()) {
            const HoldingRow& h = holding.first();
            s.state += QStringLiteral(" · AUTO CLEAN is holding: %1 · %2 · %3")
                           .arg(h.tool, h.kind, h.why);
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// BAND -- the beacon watch, per amateur band
// ---------------------------------------------------------------------------

DiversitySessionModel::Step DiversitySessionModel::buildBand() const
{
    Step s;
    s.id = StepBand;
    s.loop = LoopBand;
    const SessionCopy copy = sessionStepCopy(StepBand);
    s.title = copy.title;
    s.gives[0] = copy.gives[0];
    s.gives[1] = copy.gives[1];
    s.when = copy.when;
    s.page = PageSite; // the beacon check controls live on SITE, not BAND

    if (!m_available) {
        s.state = dash();
        s.done = false;
        return s;
    }

    const int bandIdx = bandIndexForHz(m_tunedHz);
    const double beaconHz = beaconHzForBandIndex(bandIdx);
    const QString bandName = (bandIdx >= 0) ? QString::fromLatin1(kBands[bandIdx].name)
                                             : QString();

    if (beaconHz <= 0.0) {
        // 40 m, 80 m, and everything else the rota does not cover: never a
        // chore, per R2.1's "not-applicable-off-band counts as done".
        s.done = true;
        s.state = QStringLiteral("no beacon frequency in this span — 40 m and 80 m "
                                 "have none");
        return s;
    }

    const bool beaconsAvailable = m_beacons.value(QStringLiteral("available")).toBool(false);
    if (!beaconsAvailable) {
        // A gate too old to serve the beacon route -- docs/DIVERSITY.md's
        // own words. Nothing to block on; there is no cure that fixes a
        // gate version.
        s.done = true;
        s.state = QStringLiteral("beacon watch: not available from this gate");
        return s;
    }

    const QJsonArray propagation = m_beacons.value(QStringLiteral("propagation")).toArray();
    bool foundRow = false;
    int heard = 0;
    int of = 0;
    double bestW = 0.0;
    double updated = 0.0;
    for (const QJsonValue& v : propagation) {
        const QJsonObject row = v.toObject();
        if (std::abs(num(row.value(QStringLiteral("band_hz"))) - beaconHz) >= 1000.0)
            continue;
        foundRow = true;
        heard = int(num(row.value(QStringLiteral("heard"))));
        of = int(num(row.value(QStringLiteral("of"))));
        bestW = num(row.value(QStringLiteral("best_w"))); // MUTATION GUARD: heard, not sampled
        updated = num(row.value(QStringLiteral("updated"))); // 0 if absent -> reads maximally stale
        break;
    }

    if (!foundRow || heard < 1) {
        s.done = false;
        s.state = QStringLiteral("nothing measured on %1 yet").arg(bandName);
        s.cure = Cure{QStringLiteral("GO"), QStringLiteral("page"), QString()};
        return s;
    }

    const double nowSecs = m_haveNow ? double(m_nowSecs) : updated;
    const double age = nowSecs - updated;
    const bool fresh = age < 1800.0; // MUTATION GUARD: strictly less than, not <=/>

    s.done = fresh;
    s.state = QStringLiteral("%1 · %2 of %3 heard · weakest %4 W · %5")
                  .arg(bandName)
                  .arg(heard)
                  .arg(of)
                  .arg(bestW, 0, 'g', 2)
                  .arg(ageText(age));
    if (!fresh)
        s.cure = Cure{QStringLiteral("GO"), QStringLiteral("page"), QString()};
    return s;
}

// ---------------------------------------------------------------------------
// STATION -- the tuned voice
// ---------------------------------------------------------------------------

DiversitySessionModel::Step DiversitySessionModel::buildStation() const
{
    Step s;
    s.id = StepStation;
    s.loop = LoopStation;
    const SessionCopy copy = sessionStepCopy(StepStation);
    s.title = copy.title;
    s.gives[0] = copy.gives[0];
    s.gives[1] = copy.gives[1];
    s.when = copy.when;
    s.page = PageStart;

    if (!m_available) {
        s.state = dash();
        s.done = false;
        return s;
    }

    const QJsonObject talkerObj = m_diversity.value(QStringLiteral("talker")).toObject();
    const QJsonValue idVal = talkerObj.value(QStringLiteral("id"));
    const bool haveTalkerId = idVal.isDouble();
    const int talkerId = haveTalkerId ? int(std::lround(idVal.toDouble())) : -1;

    const QJsonObject filterTalker = m_filter.value(QStringLiteral("talker")).toObject();
    const bool filterEnabled = filterTalker.value(QStringLiteral("enabled")).toBool(false);
    const QJsonValue filterIdVal = filterTalker.value(QStringLiteral("id"));
    const bool haveFilterId = filterIdVal.isDouble();
    const int filterId = haveFilterId ? int(std::lround(filterIdVal.toDouble())) : -1;

    const bool payloadDone = haveTalkerId && filterEnabled && haveFilterId
                             && filterId == talkerId;
    const bool suppressed = m_stationSuppressed && haveTalkerId
                            && talkerId == m_suppressedTalkerId;
    s.done = payloadDone && !suppressed;

    QString name;
    for (const QJsonValue& v : m_diversity.value(QStringLiteral("memory")).toArray()) {
        const QJsonObject entry = v.toObject();
        const QJsonValue entryId = entry.value(QStringLiteral("id"));
        if (entryId.isDouble() && int(std::lround(entryId.toDouble())) == talkerId
            && entry.value(QStringLiteral("name")).isString()) {
            name = entry.value(QStringLiteral("name")).toString();
            break;
        }
    }
    const QString who = !name.isEmpty() ? name
                        : haveTalkerId    ? QStringLiteral("#%1").arg(talkerId)
                                          : QString();

    if (!haveTalkerId) {
        s.state = QStringLiteral("nobody talking yet");
        s.cure = Cure{QStringLiteral("GO"), QStringLiteral("page"), QString()};
        s.page = PageBand;
    } else if (suppressed) {
        s.state = QStringLiteral("retuned off %1 — waiting for a fresh lock").arg(who);
        s.cure = Cure{QStringLiteral("GO"), QStringLiteral("page"), QString()};
        s.page = PageBand;
    } else if (!s.done) {
        s.state = QStringLiteral("%1 has no filter yet").arg(who);
        s.cure = Cure{QStringLiteral("DIG 1 MIN"), QStringLiteral("dig"),
                      QStringLiteral("seconds=60")};
    } else {
        s.state = QStringLiteral("%1 · filter in force").arg(who);
    }
    return s;
}

// ---------------------------------------------------------------------------
// LISTEN -- the destination; never a chore, never resets
// ---------------------------------------------------------------------------

DiversitySessionModel::Step DiversitySessionModel::buildListen() const
{
    Step s;
    s.id = StepListen;
    s.loop = LoopDestination;
    const SessionCopy copy = sessionStepCopy(StepListen);
    s.title = copy.title;
    s.gives[0] = copy.gives[0];
    s.gives[1] = copy.gives[1];
    s.when = copy.when;
    s.page = PageBand;
    s.done = true; // never a chore -- see nextStep()

    if (!m_available) {
        s.state = dash();
        s.cure = Cure{};
        return s;
    }

    const bool talking = m_diversity.value(QStringLiteral("talking")).toBool(false);
    QString who;
    const QJsonObject talkerObj = m_diversity.value(QStringLiteral("talker")).toObject();
    const QJsonValue idVal = talkerObj.value(QStringLiteral("id"));
    if (idVal.isDouble()) {
        const int id = int(std::lround(idVal.toDouble()));
        for (const QJsonValue& v : m_diversity.value(QStringLiteral("memory")).toArray()) {
            const QJsonObject entry = v.toObject();
            const QJsonValue entryId = entry.value(QStringLiteral("id"));
            if (entryId.isDouble() && int(std::lround(entryId.toDouble())) == id
                && entry.value(QStringLiteral("name")).isString()) {
                who = entry.value(QStringLiteral("name")).toString();
                break;
            }
        }
        if (who.isEmpty())
            who = QStringLiteral("#%1").arg(id);
    }

    const int remembered = m_diversity.value(QStringLiteral("memory")).toArray().size();
    const double outDb = num(m_diversity.value(QStringLiteral("snr_db"))
                                  .toObject()
                                  .value(QStringLiteral("out")));

    if (talking && !who.isEmpty()) {
        s.state = QStringLiteral("%1 talking · OUT %2 dB · %3 remembered")
                      .arg(who, signedDb(outDb)).arg(remembered);
    } else {
        s.state = QStringLiteral("nobody talking · %1 remembered").arg(remembered);
    }
    s.cure = Cure{QStringLiteral("FIND ON BAND"), QStringLiteral("page"), QString()};
    return s;
}

// ---------------------------------------------------------------------------
// Assembly
// ---------------------------------------------------------------------------

void DiversitySessionModel::applyToneAndCure(Step& step, int next) const
{
    if (!m_available) {
        step.tone = QStringLiteral("dim");
        step.cure = Cure{};
        return;
    }
    const bool autoOn = governorAutoOn(m_diversity);
    if (step.done) {
        step.tone = QStringLiteral("plain");
    } else if (step.id == next) {
        step.tone = autoOn ? QStringLiteral("lit") : QStringLiteral("state");
    } else {
        step.tone = QStringLiteral("dim");
    }
    // R2.1: "Manual operators (AUTO CLEAN off): NEXT becomes a status line,
    // never a nudge -- no button, no lit card." Applied model-wide: no card
    // offers a cure while the operator has chosen manual control, not only
    // the one the footer would have nudged.
    if (!autoOn)
        step.cure = Cure{};
}

QVector<DiversitySessionModel::Step> DiversitySessionModel::steps() const
{
    QVector<Step> out{buildReceiver(), buildSiteNoise(), buildBand(), buildStation(),
                      buildListen()};
    const int next = nextStep();
    for (Step& s : out)
        applyToneAndCure(s, next);
    return out;
}

int DiversitySessionModel::nextStep() const
{
    if (!m_available)
        return -1;
    const Step receiver = buildReceiver();
    if (!receiver.done)
        return StepReceiver;
    const Step site = buildSiteNoise();
    if (!site.done)
        return StepSiteNoise;
    const Step band = buildBand();
    if (!band.done)
        return StepBand;
    const Step station = buildStation();
    if (!station.done)
        return StepStation;
    return StepListen;
}

bool DiversitySessionModel::allChoresDone() const
{
    return m_available && nextStep() == StepListen;
}

QStringList DiversitySessionModel::quickStartQueries() const
{
    return {QStringLiteral("mode=track"), QStringLiteral("source=combined"),
            QStringLiteral("auto=on")};
}

QString DiversitySessionModel::digSummary() const
{
    if (m_dig.isEmpty() || !m_dig.value(QStringLiteral("available")).toBool(false))
        return QString();

    const QString error = m_dig.value(QStringLiteral("error")).toString();
    if (!error.isEmpty())
        return error;

    const bool running = m_dig.value(QStringLiteral("running")).toBool(false);
    const double gainDb = num(m_dig.value(QStringLiteral("gain_db"))); // gate's own number
    if (running) {
        return QStringLiteral("digging · %1 dB so far").arg(signedDb(gainDb));
    }
    if (m_dig.value(QStringLiteral("cancelled")).toBool(false))
        return QStringLiteral("found %1 dB (put back)").arg(signedDb(gainDb));
    if (m_dig.value(QStringLiteral("phase")).toString() != QLatin1String("done"))
        return QString();

    const QJsonObject changed = m_dig.value(QStringLiteral("changed")).toObject();
    if (!changed.isEmpty()) {
        QStringList knobs;
        for (auto it = changed.begin(); it != changed.end(); ++it)
            knobs << it.key();
        return QStringLiteral("%1 dB: %2").arg(signedDb(gainDb), knobs.join(QStringLiteral(", ")));
    }
    return QStringLiteral("nothing beat your settings");
}

} // namespace AetherSDR
