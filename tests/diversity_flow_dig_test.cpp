// The Diversity window FLOW strip's sixth step, DIG -- split out of
// tests/diversity_flow_test.cpp, which was over the 800-line budget
// AGENTS.md asks for.
//
// /diversity/dig is the one thing in this window that MOVES the operator's own
// chain, so every assertion below is either the exact query the fake gate saw
// or the exact sentence the strip drew from the gate's own status. The strip
// composes no number of its own: a run whose gain the window had invented
// would read plausibly and be a lie about what the radio is doing.
//
// Same harness as diversity_flow_test.cpp -- a real AetherGateApplet in
// front of a fake, socket-free QNetworkAccessManager --
// closedToStart()/connectGate()/openButton()/child<T>()/openWindow()/
// fire()/lastRequest() are shared by both binaries and live in
// DiversityFlowTestSupport.h so they are not duplicated.

#include "DiversityFlowTestSupport.h"
#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityNextStrip.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QTest>
#include <QTimer>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversityNextStrip;
using AetherSDR::DiversityWindow;

using namespace DiversityGateFixture;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_EQ(got, want)                                                          \
    do {                                                                             \
        const QString g_ = (got);                                                    \
        const QString w_ = (want);                                                   \
        if (g_ != w_) {                                                              \
            std::printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__,  \
                        qPrintable(g_), qPrintable(w_));                             \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

// The NEXT line at the foot of the window: one step, plus whatever the dig is
// doing, because a run goes on whichever page the operator wandered to.
bool nextHas(DiversityWindow* w, const QString& needle)
{
    auto* strip = w->findChild<DiversityNextStrip*>(QStringLiteral("diversityWindowNextStrip"));
    return strip && strip->lineText().contains(needle);
}

// The START page's own one-line summary of the last run -- the same fact the
// footer carries, said the way an offer says it rather than the way a thing
// happening now does.
bool digLineHas(DiversityWindow* w, const QString& needle)
{
    auto* line = child<QLabel>(w, "diversityWindowSessionDigLine");
    return line && line->text().contains(needle);
}

// The route's status, one phase per fixture. Field names are the same across
// all of them -- that is the route's contract, and it is why the strip can
// read one object and know which of six states it is in.
const QByteArray kDigIdle = R"({"available": true, "running": false,
    "phase": "idle", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 0.0, "steps": [], "best": {}, "changed": {}})";

const QByteArray kDigRunning = R"({"available": true, "running": true,
    "phase": "searching", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 2.1, "elapsed_s": 72.0, "seconds": 180.0, "remaining_s": 108.0,
    "trials_planned": 24, "trials_done": 9,
    "steps": [{"knob": "post", "kept": true}, {"knob": "width", "kept": false}],
    "best": {"post": "v2"}, "changed": {"post": "v2"}})";

const QByteArray kDigDone = R"({"available": true, "running": false,
    "phase": "done", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 4.1, "objective_before": -3.2, "objective_after": 0.9,
    "steps": [{"knob": "nb", "kept": true}],
    "best": {"post": "v2", "width": [100, 2400], "nb_db": 11.0},
    "changed": {"post": "v2", "width": [100, 2400], "nb_db": 11.0}})";

QByteArray digRoute(FakeGate& net, const QByteArray& body)
{
    net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, body};
    return body;
}

// One status poll of /diversity/dig, without waiting out the window's own
// cadence. The window owns that cadence -- the strip keeps no transport -- so
// this is the same door a real second would come through.
void digTick(DiversityWindow* w)
{
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();
}

// (a) The offer. Three durations, each writing the gate's own key and value,
// and a gate that cannot dig has no step and no buttons at all.
void testDigOffersThreeDurationsAndWritesTheGatesOwnQuery()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    digRoute(net, kDigIdle);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    digTick(w);

    auto* stack = child<QStackedWidget>(w, "diversityWindowFlowDigControls");
    CHECK(stack != nullptr);
    if (!stack)
        return;
    CHECK(stack->isVisibleTo(w));
    // Nothing is out, so the footer's dig control is its empty page: the three
    // durations are an OFFER and live on the START page's OFFERS row, not at
    // the foot of every page.
    CHECK(child<QWidget>(w, "diversityWindowFlowDigIdle") == stack->currentWidget());
    CHECK(child<QWidget>(w, "diversityWindowFlowDigOffer") != nullptr);
    CHECK(child<QWidget>(w, "diversityWindowSessionOffers")
              ->isAncestorOf(child<QPushButton>(w, "diversityWindowFlowDig180")));

    child<QPushButton>(w, "diversityWindowFlowDig180")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?seconds=180"));
    child<QPushButton>(w, "diversityWindowFlowDig60")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?seconds=60"));
    child<QPushButton>(w, "diversityWindowFlowDig300")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?seconds=300"));

    // MUTATION: a gate that cannot dig. The step and its buttons go away
    // rather than sitting there greyed -- there is nothing about them to
    // explain to somebody whose gate will never do it.
    digRoute(net, QByteArray(R"({"available": false})"));
    digTick(w);
    CHECK(!stack->isVisibleTo(w));
    CHECK(digLineHas(w, QStringLiteral("no run yet")));
    w->close();
    settle();
    closedToStart();
}

// (b) A run: the progress readout while it goes, the one-line report when it
// lands, the verdict row that only exists between a finished run and a word
// about it -- and WORSE, which is the one verdict that puts the chain back.
void testDigNarratesTheRunTheReportAndTheVerdict()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    digRoute(net, kDigRunning);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    child<QToolButton>(w, "diversityWindowPageSite")->click();
    settle();
    digTick(w);

    // Elapsed of asked-for, what it has bought so far, and the knob it is on
    // -- which is the knob of the last step it appended, because there is no
    // separate "trying" field and inventing one would be inventing a fact.
    // Elapsed of asked-for and what it has bought so far -- the footer says it
    // as a thing happening now, the START page's offer line as a state.
    CHECK(nextHas(w, QStringLiteral("DIG 1:12 of 3:00 · +2.1 dB · started by you")));
    CHECK(digLineHas(w, QStringLiteral("digging · +2.1 dB so far")));
    // remaining_s/trials_planned/trials_done: kDigRunning's own countdown and
    // trial count (108 s, trial 9 of 24), appended after "started by you".
    CHECK(nextHas(w, QStringLiteral("started by you, 108 s left · trial 9/24")));
    auto* stack = child<QStackedWidget>(w, "diversityWindowFlowDigControls");
    CHECK(child<QWidget>(w, "diversityWindowFlowDigRunning") == stack->currentWidget());
    child<QPushButton>(w, "diversityWindowFlowDigStop")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?cancel=1"));

    // MUTATION: the run lands. The report is built from "changed" alone --
    // what it MOVED, not what the chain now is -- in the order the chain runs.
    digRoute(net, kDigDone);
    digTick(w);
    CHECK(nextHas(w, QStringLiteral("DIG done · +4.1 dB — better or worse?")));
    CHECK(digLineHas(w, QStringLiteral("+4.1 dB: nb_db, post, width")));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") == stack->currentWidget());
    child<QPushButton>(w, "diversityWindowFlowDigWorse")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?verdict=worse"));

    // MUTATION: the word is given. The report stays and wears it, and the
    // verdict row is gone -- there is nothing left to decide.
    QByteArray judged = kDigDone;
    judged.replace("\"verdict\": \"\"", "\"verdict\": \"better\"");
    CHECK(judged != kDigDone);
    digRoute(net, judged);
    digTick(w);
    CHECK(digLineHas(w, QStringLiteral("+4.1 dB: nb_db, post, width")));
    // objective_before -> objective_after (kDigDone's own -3.2 and 0.9), quoted
    // once the verdict is in -- never while still awaiting one, which is why
    // this CHECK sits after judged rather than after kDigDone above.
    CHECK(nextHas(w, QStringLiteral("DIG +4.1 dB, you said better (-3.2 → 0.9)")));
    // Judged: the footer stops asking, because there is nothing left to decide.
    CHECK(!nextHas(w, QStringLiteral("better or worse?")));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") != stack->currentWidget());

    // MUTATION: a run that found nothing, and a run put back. Neither is a
    // report to be judged, so neither offers the three words.
    QByteArray nothing = kDigDone;
    nothing.replace("\"changed\": {\"post\": \"v2\", \"width\": [100, 2400], \"nb_db\": 11.0}",
                    "\"changed\": {}");
    nothing.replace("\"gain_db\": 4.1", "\"gain_db\": 0.0");
    CHECK(nothing != kDigDone);
    digRoute(net, nothing);
    digTick(w);
    CHECK(digLineHas(w, QStringLiteral("nothing beat your settings")));

    // MUTATION: a run that kept nothing still leaves the verdict row up -- a
    // report the operator can still judge. (The near-miss and margin wording
    // the FLOW line used to carry belongs to the gate's own report now; the
    // START page's offer line quotes only what the model derives.)
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") == stack->currentWidget());

    QByteArray cancelled = kDigDone;
    cancelled.replace("\"cancelled\": false", "\"cancelled\": true");
    digRoute(net, cancelled);
    digTick(w);
    CHECK(nextHas(w, QStringLiteral("DIG found +4.1 dB (put back)")));
    CHECK(digLineHas(w, QStringLiteral("found +4.1 dB (put back)")));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") != stack->currentWidget());

    // MUTATION: a gate that refused. Its own words, and nothing else.
    QByteArray refused = kDigDone;
    refused.replace("\"error\": \"\"", "\"error\": \"no talker to measure against\"");
    digRoute(net, refused);
    digTick(w);
    CHECK(nextHas(w, QStringLiteral("no talker to measure against")));
    CHECK(digLineHas(w, QStringLiteral("no talker to measure against")));
    w->close();
    settle();
    closedToStart();
}

// A gate too old to send remaining_s/trials_planned/trials_done: the running
// tail reads exactly as it did before those keys existed, with no dangling
// ", s left" or "trial" clause -- the other half of the behaviour
// testDigNarratesTheRunTheReportAndTheVerdict's kDigRunning already checks
// present.
// MUTATION GUARD: printing ", 0 s left · trial 0/0" for absent fields instead
// of omitting the clause entirely.
void testDigRunningOmitsCountdownAndTrialsWhenGateDoesNotSendThem()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    QByteArray old = kDigRunning;
    old.replace(", \"remaining_s\": 108.0", "");
    old.replace("\"trials_planned\": 24, \"trials_done\": 9,\n    \"steps\"",
               "\"steps\"");
    CHECK(old != kDigRunning);
    digRoute(net, old);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    digTick(w);

    CHECK(nextHas(w, QStringLiteral("DIG 1:12 of 3:00 · +2.1 dB · started by you")));
    CHECK(!nextHas(w, QStringLiteral("s left")));
    CHECK(!nextHas(w, QStringLiteral("trial")));
    w->close();
    settle();
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_flow_dig_test"));
    QApplication app(argc, argv);

    testDigOffersThreeDurationsAndWritesTheGatesOwnQuery();
    testDigNarratesTheRunTheReportAndTheVerdict();
    testDigRunningOmitsCountdownAndTrialsWhenGateDoesNotSendThem();

    std::printf("\n%d diversity flow dig test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
