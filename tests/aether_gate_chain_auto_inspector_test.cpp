// B25 AUTO CLEAN -- the AUTO CLEAN card's own inspector: the state+why
// line, the recent-moves event list (newest first, wall-clock preferred
// over the gate's monotonic `t`), and the backoff line -- split out of
// aether_gate_chain_auto_test.cpp, which was over the 800-line budget
// AGENTS.md asks for.
//
// The governor's shape and rules are docs/DIVERSITY.md's own "AUTO CLEAN:
// the chain decides" section, verbatim in gui/AetherGateChainAuto.h. This
// file tests gui/AetherGateChainAuto.cpp's parsing and formatting of
// governor.events[]/backoff[]/ruled_out/objective_source/error into the
// inspector's two labels.
//
// The chain-row/governor fixture (bringUp/feedDevice/autoCleanRow/
// squeezeRow/withAutoCleanChain/held/event/backoff/ruledOut/governor/
// localEpoch) is shared with aether_gate_chain_auto_test.cpp and lives in
// AetherGateChainAutoTestSupport.h so it is not duplicated.
#include "AetherGateChainAutoTestSupport.h"
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

// --------------------------------------------------------------------------
// The AUTO CLEAN card's own inspector
// --------------------------------------------------------------------------

void testInspectorEventLinesNewestFirstWithResultAndDelta()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    // selectStage(), not a click on the toggle: AetherGateChainTile's own
    // mousePressEvent is what a real click on the CARD reaches, and the
    // switch is a child QPushButton that consumes the press before it gets
    // there -- clicking the switch actually writes, which is its own test
    // below and must not be entangled with this one.
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text() == QStringLiteral("idle · nothing to hold"));

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (!events_)
        return;
    CHECK(events_->isVisible());
    CHECK(!events_->wordWrap());
    const QStringList lines = events_->text().split(QLatin1Char('\n'));
    CHECK(lines.size() == 3);
    if (lines.size() != 3)
        return;
    // Newest first: the 12:42:30 undone row, then 12:41:07 kept, then
    // 12:40:01 released.
    CHECK(lines.at(0) == QStringLiteral("12:42:30 · guard · neighbour · undone -0.9 dB · "
                                        "headroom low"));
    CHECK(lines.at(1) == QStringLiteral("12:41:07 · squeeze · carrier · kept +1.8 dB · "
                                        "strongest first"));
    CHECK(lines.at(2) == QStringLiteral("12:40:01 · nb · impulse · released · 1.4/s"));
}

void testInspectorErrorEventAndBackoffLine()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(localEpoch(9, 1, 0), QStringLiteral("mode"), QStringLiteral("floor"),
             QStringLiteral("bad value: mode='x'"), QStringLiteral("error"), false),
    };
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), localEpoch(12, 46, 0)),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("backoff"),
                                                 QStringLiteral("mains/squeeze backing off"),
                                                 {}, QJsonValue(), events, backoffs)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (!events_)
        return;
    const QStringList lines = events_->text().split(QLatin1Char('\n'));
    CHECK(lines.size() == 2);
    if (lines.size() != 2)
        return;
    CHECK(lines.at(0) == QStringLiteral("09:01:00 · mode · floor · error: bad value: "
                                        "mode='x'"));
    CHECK(lines.at(1) == QStringLiteral("backing off: mains/squeeze until 12:46"));
}

// H2: events[].wall, governor.ruled_out/objective_source/error, and
// backoff[].until_wall -- all previously parsed-and-ignored or never parsed
// at all. Each test below turns exactly one of them on and checks the one
// line it is supposed to change; the "absent" half of every one of these is
// already covered above (testInspectorEventLinesNewestFirstWithResultAndDelta
// and testInspectorErrorEventAndBackoffLine both omit wall/until_wall/
// ruled_out/objective_source and still pass unchanged).
// --------------------------------------------------------------------------

// A `t` chosen to format as a wildly wrong time of day (it is the gate's
// monotonic clock, not epoch seconds) so the assertion only passes if the
// line is actually reading `wall`, not silently still reading `t`.
void testEventLinePrefersWallOverT()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(12.5, QStringLiteral("nb"), QStringLiteral("impulse"), QStringLiteral("1.4/s"),
             QStringLiteral("released"), false, 0.0, true, localEpoch(9, 15, 3)),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (!events_)
        return;
    CHECK(events_->text() == QStringLiteral("09:15:03 · nb · impulse · released · 1.4/s"));
}

void testBackoffLinePrefersUntilWallAndAddsSeconds()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), 999.0, true,
               localEpoch(12, 46, 30)),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("backoff"),
                                                 QStringLiteral("mains/squeeze backing off"),
                                                 {}, QJsonValue(), {}, backoffs)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(events_ != nullptr);
    if (events_)
        CHECK(events_->text() == QStringLiteral("backing off: mains/squeeze until 12:46:30"));
}

void testStateLineAppendsHeldBackFromRuledOut()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray ruled = {
        ruledOut(QStringLiteral("guard"), QStringLiteral("the guard is already on")),
        ruledOut(QStringLiteral("dig"), QStringLiteral("talker 3.2 dB: waiting for steady")),
    };
    // `why` here is deliberately NOT the ruled_out join (the governor is
    // "applying" something else this tick), so the held-back clause has
    // something to say that `why` alone does not.
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("applying"),
                                                 QStringLiteral("nulling floor"), {},
                                                 QJsonValue(), {}, {}, ruled)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text()
              == QStringLiteral("applying · nulling floor · held back: the guard is "
                                "already on · talker 3.2 dB: waiting for steady"));
}

// When the governor is idle with nothing held, `why` IS already ruled_out's
// own join (core/governor.py's _why_idle()) -- the held-back clause must not
// repeat it a second time on the same line.
void testStateLineOmitsHeldBackWhenSameAsWhy()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray ruled = {
        ruledOut(QStringLiteral("guard"), QStringLiteral("2 dB of ADC headroom, no clipping")),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("2 dB of ADC headroom, no "
                                                               "clipping"),
                                                 {}, QJsonValue(), {}, {}, ruled)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text()
              == QStringLiteral("idle · 2 dB of ADC headroom, no clipping"));
}

void testStateLineAppendsObjectiveSource()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, {},
                                                 QStringLiteral("none"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text() == QStringLiteral("idle · nothing to hold · objective: none"));
}

void testStateLinePrefixesNonEmptyErrorAsWarning()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, {}, QString(),
                                                 QStringLiteral("RuntimeError: adapter has "
                                                               "no device controls"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);
    strip(w)->selectStage(QStringLiteral("auto_clean"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    CHECK(state != nullptr);
    if (state)
        CHECK(state->text()
              == QStringLiteral("AUTO CLEAN ERROR: RuntimeError: adapter has no device "
                                "controls · idle · nothing to hold"));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_auto_inspector_test"));
    QApplication app(argc, argv);

    testInspectorEventLinesNewestFirstWithResultAndDelta();
    testInspectorErrorEventAndBackoffLine();
    testEventLinePrefersWallOverT();
    testBackoffLinePrefersUntilWallAndAddsSeconds();
    testStateLineAppendsHeldBackFromRuledOut();
    testStateLineOmitsHeldBackWhenSameAsWhy();
    testStateLineAppendsObjectiveSource();
    testStateLinePrefixesNonEmptyErrorAsWarning();

    std::printf("\n%d aether gate chain auto inspector test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
