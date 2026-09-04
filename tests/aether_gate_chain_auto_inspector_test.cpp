// B25 AUTO CLEAN -- the governor's two lines: the state+why line and the
// recent-moves list (newest first, wall-clock preferred over the gate's
// monotonic `t`, the backoff line last). Split out of
// aether_gate_chain_auto_test.cpp, which was over the 800-line budget.
//
// The governor's shape and rules are docs/DIVERSITY.md's own "AUTO CLEAN:
// the chain decides" section, verbatim in gui/AetherGateChainAuto.h. This
// file tests gui/AetherGateChainAuto.cpp's parsing and formatting of
// governor.events[]/backoff[]/ruled_out/objective_source/error through the
// two pure helpers themselves -- chainAutoStateLine() and
// chainAutoEventLines() -- which is all the CHAIN window's NOW strip
// (AetherGateChainNow.cpp, its HISTORY disclosure) reads. The AUTO CLEAN
// card and its inspector are gone: auto_clean never draws on the diagram
// any more (design §2.1), so there is no tile to select here.
//
// The chain-row/governor fixture (bringUp/feedDevice/autoCleanRow/
// squeezeRow/withAutoCleanChain/held/event/backoff/ruledOut/governor/
// localEpoch) is shared with aether_gate_chain_auto_test.cpp and lives in
// AetherGateChainAutoTestSupport.h so it is not duplicated.
#include "AetherGateChainAutoTestSupport.h"
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;
using AetherSDR::ChainAutoGovernor;
using AetherSDR::chainAutoEventLines;
using AetherSDR::chainAutoParseGovernor;
using AetherSDR::chainAutoStateLine;

namespace {

// One /filter body, parsed the way AetherGateChainWindow::applyFilter()
// parses it, down to the governor block the two helpers read.
ChainAutoGovernor governorOf(const QByteArray& body)
{
    return chainAutoParseGovernor(QJsonDocument::fromJson(body).object());
}

// --------------------------------------------------------------------------
// The governor's two lines
// --------------------------------------------------------------------------

void testInspectorEventLinesNewestFirstWithResultAndDelta()
{
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));

    const QString state = chainAutoStateLine(gov);
    CHECK(state == QStringLiteral("idle · nothing to hold"));

    const QString eventsText = chainAutoEventLines(gov).join(QLatin1Char('\n'));
    const QStringList lines = eventsText.split(QLatin1Char('\n'));
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
    const QJsonArray events = {
        event(localEpoch(9, 1, 0), QStringLiteral("mode"), QStringLiteral("floor"),
             QStringLiteral("bad value: mode='x'"), QStringLiteral("error"), false),
    };
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), localEpoch(12, 46, 0)),
    };
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("backoff"),
                                                 QStringLiteral("mains/squeeze backing off"),
                                                 {}, QJsonValue(), events, backoffs)));

    const QString eventsText = chainAutoEventLines(gov).join(QLatin1Char('\n'));
    const QStringList lines = eventsText.split(QLatin1Char('\n'));
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
    const QJsonArray events = {
        event(12.5, QStringLiteral("nb"), QStringLiteral("impulse"), QStringLiteral("1.4/s"),
             QStringLiteral("released"), false, 0.0, true, localEpoch(9, 15, 3)),
    };
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));

    const QString eventsText = chainAutoEventLines(gov).join(QLatin1Char('\n'));
    CHECK(eventsText == QStringLiteral("09:15:03 · nb · impulse · released · 1.4/s"));
}

void testBackoffLinePrefersUntilWallAndAddsSeconds()
{
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), 999.0, true,
               localEpoch(12, 46, 30)),
    };
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("backoff"),
                                                 QStringLiteral("mains/squeeze backing off"),
                                                 {}, QJsonValue(), {}, backoffs)));

    const QString eventsText = chainAutoEventLines(gov).join(QLatin1Char('\n'));
    CHECK(eventsText == QStringLiteral("backing off: mains/squeeze until 12:46:30"));
}

void testStateLineAppendsHeldBackFromRuledOut()
{
    const QJsonArray ruled = {
        ruledOut(QStringLiteral("guard"), QStringLiteral("the guard is already on")),
        ruledOut(QStringLiteral("dig"), QStringLiteral("talker 3.2 dB: waiting for steady")),
    };
    // `why` here is deliberately NOT the ruled_out join (the governor is
    // "applying" something else this tick), so the held-back clause has
    // something to say that `why` alone does not.
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("applying"),
                                                 QStringLiteral("nulling floor"), {},
                                                 QJsonValue(), {}, {}, ruled)));

    const QString state = chainAutoStateLine(gov);
    CHECK(state
              == QStringLiteral("applying · nulling floor · held back: the guard is "
                                "already on · talker 3.2 dB: waiting for steady"));
}

// When the governor is idle with nothing held, `why` IS already ruled_out's
// own join (core/governor.py's _why_idle()) -- the held-back clause must not
// repeat it a second time on the same line.
void testStateLineOmitsHeldBackWhenSameAsWhy()
{
    const QJsonArray ruled = {
        ruledOut(QStringLiteral("guard"), QStringLiteral("2 dB of ADC headroom, no clipping")),
    };
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("2 dB of ADC headroom, no "
                                                               "clipping"),
                                                 {}, QJsonValue(), {}, {}, ruled)));

    const QString state = chainAutoStateLine(gov);
    CHECK(state
              == QStringLiteral("idle · 2 dB of ADC headroom, no clipping"));
}

void testStateLineAppendsObjectiveSource()
{
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, {},
                                                 QStringLiteral("none"))));

    const QString state = chainAutoStateLine(gov);
    CHECK(state == QStringLiteral("idle · nothing to hold · objective: none"));
}

void testStateLinePrefixesNonEmptyErrorAsWarning()
{
    const ChainAutoGovernor gov = governorOf(
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, {}, QString(),
                                                 QStringLiteral("RuntimeError: adapter has "
                                                               "no device controls"))));

    const QString state = chainAutoStateLine(gov);
    CHECK(state
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
