// DiversitySessionModel unit test -- headless, no widgets, no transport, no
// timers: only the model, DiversitySessionText.cpp, and the payload builders
// in DiversitySessionFixture.h. Adapted from the Phase 3a plan's older
// worked-example test list (RECEIVER's ticks stand in for that list's PAIR,
// BAND for its BEACONS, SITE NOISE for its NOISE, and LISTEN keeps its own
// name) plus R2.3's five additions for the reset rules, manual mode and
// QUICK START.
//
// Every test's comment names the mutation it would catch, matching
// tests/diversity_flow_test.cpp and tests/diversity_band_background_test.cpp's
// own convention.

#include "DiversitySessionFixture.h"
#include "gui/DiversitySessionModel.h"

#include <QCoreApplication>
#include <QSet>
#include <QString>

#include <cstdio>

using namespace AetherSDR;
namespace F = DiversitySessionFixture;

static int g_failed = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++g_failed;                                                                \
        }                                                                              \
    } while (0)

namespace {

const DiversitySessionModel::Step& stepById(const QVector<DiversitySessionModel::Step>& steps,
                                            int id)
{
    for (const DiversitySessionModel::Step& s : steps) {
        if (s.id == id)
            return s;
    }
    return steps.first();
}

// RECEIVER is not done until aligned, mode is on, source is combined/stereo
// and (today, always) headroom is clear.
// MUTATION GUARD: buildReceiver() short-circuiting on the first true tick
// instead of AND-ing all of them.
void receiverNotDoneUntilAllTicks()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), false),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).done == false);

    m.apply(F::makeDiversity(true, QStringLiteral("off"), QStringLiteral("combined"), true),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).done == false);

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("a"), true),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).done == false);

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).done == true);
}

// A held lock (aligned && align_held) is done, quotes the gate's own note
// verbatim, and offers no cure -- the gate is already re-measuring on its
// own, so REALIGN would only restart what is running.
// MUTATION GUARD: reading align_held while !aligned, or still offering
// REALIGN once held is true.
void receiverHeldAlignmentIsDoneWithNoCure()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);

    const QString note = QStringLiteral(
        "held lag 0 through overflow on channel A; re-measuring (peak 2.9, need 10)");
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true,
                             /*realigning=*/true, false, -1, {}, true, {}, true, QString(),
                             QString(), QString(), false, 0.0,
                             /*haveAlignExtras=*/true, /*alignHeld=*/true, note),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    const DiversitySessionModel::Step receiver =
        stepById(m.steps(), DiversitySessionModel::StepReceiver);
    CHECK(receiver.done == true);
    CHECK(receiver.state == note);
    CHECK(receiver.cure.kind.isEmpty());

    // MUTATION: held with an empty note falls back to the fixed sentence,
    // never a blank line.
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, true,
                             false, -1, {}, true, {}, true, QString(), QString(), QString(), false,
                             0.0, true, true, QString()),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).state
          == QStringLiteral("aligned · held, re-measuring"));
}

// !aligned with a numeric align_retry_s keeps the REALIGN cure (an operator
// may want it now) but quotes the gate's own note instead of the bare "not
// aligned".
// MUTATION GUARD: dropping the cure once a retry is pending, or ignoring
// align_retry_s and always printing "not aligned".
void receiverRetryPendingKeepsCureWithNote()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);

    const QString note = QStringLiteral("no lock yet (peak 2.9, need 10); next re-measure in 21 s");
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), false, false,
                             false, -1, {}, true, {}, true, QString(), QString(), QString(), false,
                             0.0, true, false, note, true, 21.0),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    const DiversitySessionModel::Step receiver =
        stepById(m.steps(), DiversitySessionModel::StepReceiver);
    CHECK(receiver.done == false);
    CHECK(receiver.state == note);
    CHECK(receiver.cure.kind == QStringLiteral("align"));

    // MUTATION: no note, still builds the retry sentence rather than the
    // bare "not aligned".
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), false, false,
                             false, -1, {}, true, {}, true, QString(), QString(), QString(), false,
                             0.0, true, false, QString(), true, 21.0),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).state
          == QStringLiteral("no lock yet · re-measuring in 21 s"));
}

// An old gate that has never heard of align_held/align_note/align_retry_s
// (the fixture's default) reads exactly as it did before this fix: the bare
// "not aligned" and "aligning…" sentences, cure unaffected.
// MUTATION GUARD: a new branch firing on absent keys (toBool(false)/
// toString() defaults read as "held" or "retry pending" instead of nothing).
void receiverOldGateWithNoAlignKeysIsUnchanged()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), false, false),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    const DiversitySessionModel::Step notAligned =
        stepById(m.steps(), DiversitySessionModel::StepReceiver);
    CHECK(notAligned.state == QStringLiteral("not aligned"));
    CHECK(notAligned.cure.kind == QStringLiteral("align"));

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, true),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).state
          == QStringLiteral("aligning…"));
}

// RECEIVER's cure is the first unticked thing, in the order the FLOW line
// checks them: not aligned before mode-off before source-not-combined.
// MUTATION GUARD: offering TRACK while still unaligned, or HEAR OUT while
// the mode is still off.
void receiverCureIsTheFirstUnticked()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), false),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).cure.kind
          == QStringLiteral("align"));

    m.apply(F::makeDiversity(true, QStringLiteral("off"), QStringLiteral("combined"), true),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).cure.query
          == QStringLiteral("mode=track"));

    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("a"), true),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).cure.query
          == QStringLiteral("source=combined"));
}

// BAND is done when the tuned band's beacon result updated less than 1800 s
// ago.
// MUTATION GUARD: using <= instead of < 1800, or reading "sampled" instead
// of "heard".
void bandFreshWithin30Min()
{
    DiversitySessionModel m;
    m.setNowSecs(1'700'000'000);
    const double updated = double(1'700'000'000) - 900.0; // 15 min ago
    m.apply(F::makeDiversity(), F::makeFilter(),
            F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 5, 18, 1.0, updated)}),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepBand).done == true);
}

// A band with no beacon frequency in the rota (40 m, 80 m, ...) is always
// done, and never offers a cure.
// MUTATION GUARD: treating "no beacon frequency" the same as "nothing
// measured yet" (done = false).
void bandNotApplicableOffBand()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    m.apply(F::makeDiversity(), F::makeFilter(), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(true), F::makeCompass(), F::kHz40m);
    const QVector<DiversitySessionModel::Step> steps = m.steps();
    const DiversitySessionModel::Step& band = stepById(steps, DiversitySessionModel::StepBand);
    CHECK(band.done == true);
    CHECK(band.cure.kind.isEmpty());
}

// A stale beacon result (>= 1800 s) prints the age in its state line and is
// not done.
// MUTATION GUARD: dropping the age clause when the result is stale, or
// reporting done=true past the 1800 s line.
void bandStalePrintsTheAge()
{
    DiversitySessionModel m;
    m.setNowSecs(1'700'000'000);
    const double updated = double(1'700'000'000) - 3700.0; // just over an hour ago
    m.apply(F::makeDiversity(), F::makeFilter(), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 3, 18, 1.0, updated)}),
            F::makeCompass(), F::kHz20m);
    const QVector<DiversitySessionModel::Step> steps = m.steps();
    const DiversitySessionModel::Step& band = stepById(steps, DiversitySessionModel::StepBand);
    CHECK(band.done == false);
    CHECK(band.state.contains(QStringLiteral("ago")));
    CHECK(!band.cure.kind.isEmpty());
}

// SITE NOISE is done once every kinds[] row with an action is either active
// or has been dismissed by the caller.
// MUTATION GUARD: counting an active-but-not-dismissed row as still
// offered.
void siteNoiseDoneWhenEveryActionTaken()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QVector<QJsonObject> kinds{F::makeKind(QStringLiteral("mains"), true, true),
                              F::makeKind(QStringLiteral("impulse"), false, false)};
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, false,
                             false, -1, {}, true, kinds),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepSiteNoise).done == true);
}

// A fresh finding (action present, not active, not dismissed) flips SITE
// NOISE back to not-done.
// MUTATION GUARD: only checking the first kinds[] row instead of all of
// them.
void siteNoiseNotDoneWhenANewFindingArrives()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QVector<QJsonObject> kinds{F::makeKind(QStringLiteral("mains"), true, true),
                              F::makeKind(QStringLiteral("tone"), true, false)};
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, false,
                             false, -1, {}, true, kinds),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    const QVector<DiversitySessionModel::Step> steps = m.steps();
    const DiversitySessionModel::Step& site = stepById(steps, DiversitySessionModel::StepSiteNoise);
    CHECK(site.done == false);
    CHECK(!site.cure.kind.isEmpty());
}

// A dismissed finding counts as handled even though the gate still reports
// it inactive -- setDismissedKinds() is the caller's memory, not the
// gate's.
// MUTATION GUARD: ignoring the dismissed set entirely.
void dismissedFindingCountsAsHandled()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    m.setDismissedKinds(QSet<QString>{QStringLiteral("tone")});
    QVector<QJsonObject> kinds{F::makeKind(QStringLiteral("mains"), true, true),
                              F::makeKind(QStringLiteral("tone"), true, false)};
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, false,
                             false, -1, {}, true, kinds),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepSiteNoise).done == true);
}

// nextStep() only ever answers one of the five real StepIds (0-4) or -1 for
// gate-gone -- there is no sixth "chain" step in R2.1's model.
// MUTATION GUARD: a future step id creeping outside StepReceiver..StepListen.
void nextStepNeverReturnsOutOfRange()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    const QVector<QJsonObject> variants{
        F::makeDiversity(false), F::makeDiversity(true, QStringLiteral("off")),
        F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, false, true,
                         3, {F::MemoryRow{3, QStringLiteral("Ann")}})};
    for (const QJsonObject& d : variants) {
        m.apply(d, F::makeFilter(true, 3), F::makeDig(false, false, QString(), 0.0),
                F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 5, 18, 1.0, 1000.0)}),
                F::makeCompass(), F::kHz20m);
        const int next = m.nextStep();
        CHECK(next == -1
              || (next >= DiversitySessionModel::StepReceiver && next <= DiversitySessionModel::StepListen));
    }
}

// LISTEN is next once RECEIVER, SITE NOISE, BAND and STATION are all done --
// R2.1's four chores, not the older three-step cut's.
// MUTATION GUARD: forgetting STATION in the walk and calling LISTEN next
// while nobody has a filter yet.
void listenIsNextWhenAllFourChoresAreDone()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QVector<F::MemoryRow> memory{{3, QStringLiteral("Ann")}};
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, false,
                             true, 3, memory, true, {}, true, QString(), QString(), QString(), true,
                             4.0),
            F::makeFilter(true, 3), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 5, 18, 1.0, 1000.0)}),
            F::makeCompass(), F::kHz20m);
    CHECK(m.nextStep() == DiversitySessionModel::StepListen);
    CHECK(m.allChoresDone() == true);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepListen).done == true);
}

// A gone gate dashes every card and offers no cure anywhere.
// MUTATION GUARD: leaving a stale cure or state string from the last
// apply() when the gate drops out.
void gateGoneDashesEverythingAndOffersNoCure()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    m.apply(F::makeDiversity(false), F::makeFilter(), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(false), F::makeCompass(), F::kHz20m);
    CHECK(m.gateAvailable() == false);
    CHECK(m.nextStep() == -1);
    for (const DiversitySessionModel::Step& s : m.steps()) {
        CHECK(s.state == QStringLiteral("—"));
        CHECK(s.cure.kind.isEmpty());
        CHECK(s.tone == QStringLiteral("dim"));
    }
}

// Every fixed copy line (gives/when, both steps' and sessionPairModeExplain's)
// is <= 92 characters -- the width the Phase 3a plan measured the 1120 px
// window against.
// MUTATION GUARD: a line that word-wraps past the budget going unnoticed.
void everyCopyLineFitsNinetyTwoChars()
{
    for (const QString& line : sessionAllFixedCopyLines())
        CHECK(line.size() <= 92);
}

// digSummary() quotes the gate's own gain_db, never a value recomputed from
// objective_before/objective_after.
// MUTATION GUARD: computing gain as objective_after - objective_before
// (0.286 - 0.538 = -0.252) instead of reading gain_db (0.6) straight.
void digStateQuotesTheGateOnly()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QJsonObject changed;
    changed["post"] = QStringLiteral("v2");
    m.apply(F::makeDiversity(), F::makeFilter(),
            F::makeDig(true, false, QStringLiteral("done"), 0.6, changed), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    const QString summary = m.digSummary();
    CHECK(summary.contains(QStringLiteral("0.6")));
    CHECK(!summary.contains(QStringLiteral("0.25")));
    CHECK(!summary.contains(QStringLiteral("-0.25")));
}

// SITE NOISE's muted AUTO CLEAN line names the governor's own tool, kind and
// why -- never a paraphrase.
// MUTATION GUARD: printing governor.state_label instead of holding[0]'s own
// tool/kind/why fields.
void autoCleanHoldingLineNamesToolKindWhy()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    m.apply(F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"), true, false,
                             false, -1, {}, true, {}, true, QStringLiteral("squeeze"),
                             QStringLiteral("mains"), QStringLiteral("60 Hz hum steady")),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz20m);
    const QString state = stepById(m.steps(), DiversitySessionModel::StepSiteNoise).state;
    CHECK(state.contains(QStringLiteral("squeeze")));
    CHECK(state.contains(QStringLiteral("mains")));
    CHECK(state.contains(QStringLiteral("60 Hz hum steady")));
}

// A band change resets STATION (the gate still names the old band's talker)
// without resetting SESSION's own two steps.
// MUTATION GUARD: clearing RECEIVER/SITE NOISE too, or not noticing the
// band changed at all.
void bandChangeResetsBandAndStationNotSession()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QVector<F::MemoryRow> memory{{3, QStringLiteral("Ann")}};
    QJsonObject diversity = F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"),
                                             true, false, true, 3, memory);
    m.apply(diversity, F::makeFilter(true, 3), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 5, 18, 1.0, 1000.0)}),
            F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepStation).done == true);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).done == true);

    // Retune to 15 m; the gate still reports #3 as the active talker.
    m.apply(diversity, F::makeFilter(true, 3), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 5, 18, 1.0, 1000.0)}),
            F::makeCompass(), F::kHz15m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepStation).done == false);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepReceiver).done == true);
}

// A same-band retune bigger than the current passband also suppresses
// STATION alone; BAND (still fresh on this band) is untouched.
// MUTATION GUARD: only comparing band index and missing an in-band jump.
void retuneWithinBandResetsStationOnly()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QVector<F::MemoryRow> memory{{3, QStringLiteral("Ann")}};
    QJsonObject diversity = F::makeDiversity(true, QStringLiteral("track"), QStringLiteral("combined"),
                                             true, false, true, 3, memory);
    const QJsonObject beacons =
        F::makeBeacons(true, {F::makePropagationRow(F::kHz20m, 5, 18, 1.0, 1000.0)});
    m.apply(diversity, F::makeFilter(true, 3), F::makeDig(false, false, QString(), 0.0), beacons,
            F::makeCompass(), 14100000.0);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepStation).done == true);

    // 50 kHz away, same band, well past the default ~2.8 kHz passband.
    m.apply(diversity, F::makeFilter(true, 3), F::makeDig(false, false, QString(), 0.0), beacons,
            F::makeCompass(), 14150000.0);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepStation).done == false);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepBand).done == true);
}

// AUTO CLEAN off (governor.auto == false) offers no cure anywhere, even on
// a card that would otherwise be next.
// MUTATION GUARD: suppressing the cure only on nextStep()'s own card.
void manualModeNeverOffersAButton()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    m.apply(F::makeDiversity(true, QStringLiteral("off"), QStringLiteral("combined"), true, false,
                             false, -1, {}, true, {}, false),
            F::makeFilter(), F::makeDig(false, false, QString(), 0.0), F::makeBeacons(false),
            F::makeCompass(), F::kHz40m);
    for (const DiversitySessionModel::Step& s : m.steps())
        CHECK(s.cure.kind.isEmpty());
}

// QUICK START is exactly three queries, in order: track, combined, auto on.
// MUTATION GUARD: reordering them or adding/dropping a query.
void quickStartSendsExactlyThreeQueries()
{
    DiversitySessionModel m;
    const QStringList queries = m.quickStartQueries();
    CHECK(queries.size() == 3);
    CHECK(queries.value(0) == QStringLiteral("mode=track"));
    CHECK(queries.value(1) == QStringLiteral("source=combined"));
    CHECK(queries.value(2) == QStringLiteral("auto=on"));
}

// STATION is done once the gate's talker id matches /filter's own
// talker.id, and offers DIG 1 MIN while it does not.
// MUTATION GUARD: treating a talker with no filter as done, or offering
// AUTO CLEAN instead of DIG 1 MIN.
void stationDoneWhenFilterMatchesTalkerElseCureIsDig()
{
    DiversitySessionModel m;
    m.setNowSecs(1000);
    QVector<F::MemoryRow> memory{{7, QStringLiteral("Ted")}};
    const QJsonObject diversity = F::makeDiversity(true, QStringLiteral("track"),
                                                    QStringLiteral("combined"), true, false, true, 7,
                                                    memory);

    m.apply(diversity, F::makeFilter(false), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(false), F::makeCompass(), F::kHz20m);
    const QVector<DiversitySessionModel::Step> noFilterSteps = m.steps();
    const DiversitySessionModel::Step& noFilter =
        stepById(noFilterSteps, DiversitySessionModel::StepStation);
    CHECK(noFilter.done == false);
    CHECK(noFilter.cure.kind == QStringLiteral("dig"));
    CHECK(noFilter.state.contains(QStringLiteral("Ted")));

    m.apply(diversity, F::makeFilter(true, 7), F::makeDig(false, false, QString(), 0.0),
            F::makeBeacons(false), F::makeCompass(), F::kHz20m);
    CHECK(stepById(m.steps(), DiversitySessionModel::StepStation).done == true);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    receiverNotDoneUntilAllTicks();
    receiverHeldAlignmentIsDoneWithNoCure();
    receiverRetryPendingKeepsCureWithNote();
    receiverOldGateWithNoAlignKeysIsUnchanged();
    receiverCureIsTheFirstUnticked();
    bandFreshWithin30Min();
    bandNotApplicableOffBand();
    bandStalePrintsTheAge();
    siteNoiseDoneWhenEveryActionTaken();
    siteNoiseNotDoneWhenANewFindingArrives();
    dismissedFindingCountsAsHandled();
    nextStepNeverReturnsOutOfRange();
    listenIsNextWhenAllFourChoresAreDone();
    gateGoneDashesEverythingAndOffersNoCure();
    everyCopyLineFitsNinetyTwoChars();
    digStateQuotesTheGateOnly();
    autoCleanHoldingLineNamesToolKindWhy();
    bandChangeResetsBandAndStationNotSession();
    retuneWithinBandResetsStationOnly();
    manualModeNeverOffersAButton();
    quickStartSendsExactlyThreeQueries();
    stationDoneWhenFilterMatchesTalkerElseCureIsDig();

    std::printf("\n%d test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
