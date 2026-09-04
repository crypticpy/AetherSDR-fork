// B25 AUTO CLEAN -- the app half: the "held by AUTO" note on a stage card,
// and (for every stage but auto_clean itself) the settling/name-hygiene
// coverage AetherGateChainStrip/Stage does not already give it.
//
// The governor's shape and rules are docs/DIVERSITY.md's own "AUTO CLEAN:
// the chain decides" section, verbatim in gui/AetherGateChainAuto.h. This
// file tests gui/AetherGateChainAuto.cpp's parsing and formatting plus the
// "held by AUTO" hook AetherGateChainWindow.cpp calls it through -- it does
// NOT retest anything AetherGateChainStrip/Stage already cover (tile
// selection, no word wrap, the settling window), the way
// aether_gate_chain_frontend_test.cpp and aether_gate_chain_squeeze_test.cpp
// do not either.
//
// AUTO CLEAN's own card and its one write left the diagram entirely (W1,
// design §2.1 item 4): chain[0] is no longer drawn, and what used to be
// this file's write-path test and part of its name-hygiene sweep moved to
// aether_gate_chain_now_test.cpp, which is where AetherGateChainNow.h's own
// widgets and writes now live.
//
// The auto_clean chain row and the "squeeze" row it needs to show a held
// squeeze note are built locally, the same way squeeze's own
// withSqueezeChainRow() is -- B25 landed after AetherGateChainFixture.h was
// written, and the task that adds it may not change a helper an earlier
// test already depends on.
//
// The AUTO CLEAN card's own state+events inspector moved out to
// aether_gate_chain_auto_inspector_test.cpp: this file was at the
// 800-line budget AGENTS.md asks for. The chain-row/governor fixture
// (bringUp/feedDevice/autoCleanRow/squeezeRow/withAutoCleanChain/held/
// event/backoff/ruledOut/governor/localEpoch) is shared by all three
// binaries and lives in AetherGateChainAutoTestSupport.h so it is not
// duplicated.
#include "AetherGateChainAutoTestSupport.h"
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QScrollBar>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

// --------------------------------------------------------------------------
// The "held by AUTO" note
// --------------------------------------------------------------------------

void testNoteForHeldSqueeze()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("carrier"),
                                     QStringLiteral("strongest first"), 100.0, true, 1.8)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(note->isVisible());
    // toolTip()/accessibleDescription(), not text(): chainAutoApplyNotes()
    // runs the note through chainFitToWidth() the same way the card's own
    // WHY line is, so text() may be word-truncated to fit the card's 178 px
    // -- the tooltip and accessible description are set from the untouched
    // string and are the exact-match surface.
    CHECK(note->toolTip() == QStringLiteral("AUTO · carrier · strongest first, +1.8 dB"));
    CHECK(note->accessibleDescription() == note->toolTip());
    CHECK(note->text().startsWith(QStringLiteral("AUTO · carrier")));
    CHECK(!note->wordWrap());
}

// A HELD tool (not pending) with no score yet -- freshly applied, before the
// next tick has measured it. Verified by hand during development: dropping
// the "if (h.hasDelta)" guard in chainAutoNoteForStage() (always append the
// delta) makes this the one test in the file that catches it -- every other
// held case here carries a delta, so an unguarded append would otherwise
// pass by accident with a spurious ", +0.0 dB" nobody's assertion checks
// for. Mutated, reran (this test's toolTip CHECK below failed as expected),
// restored, and confirmed `git diff` on AetherGateChainAuto.cpp was clean.
void testNoteForHeldToolWithNoScoreYetOmitsDelta()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("mode"), QStringLiteral("floor"),
                                     QStringLiteral("coherence steady"), 100.0, false)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("applying"),
                                                 QStringLiteral("nulling floor"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_combiner"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(note->isVisible());
    CHECK(note->toolTip() == QStringLiteral("AUTO · floor · coherence steady"));
}

void testNoteForPendingNbHasNoScoreYet()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonValue pending =
        held(QStringLiteral("nb"), QStringLiteral("impulse"), QStringLiteral("1.4/s"),
            100.0, false);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("measuring"),
                                                 QStringLiteral("trying blanker"), {},
                                                 pending)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_nb"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(note->isVisible());
    CHECK(note->toolTip() == QStringLiteral("AUTO · trying · 1.4/s"));
    CHECK(note->text().startsWith(QStringLiteral("AUTO · trying")));
}

void testNoteRemovedWhenHoldingEmpties()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("mains"),
                                     QStringLiteral("comb found"), 100.0, true, 2.1)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (note)
        CHECK(note->isVisible());

    // The gate releases it: the next /filter carries an empty holding[].
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                          QStringLiteral("nothing to hold")))};
    filterTick(applet);

    note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (note)
        CHECK(!note->isVisible());
    // MUTATION: in chainAutoApplyNotes(), drop the
    // `note->setVisible(!text.isEmpty())` call (leave a once-shown note
    // visible forever) and this pair fails on the second CHECK above -- kept
    // as a comment because it is a one-line removal, not a build worth
    // scripting for CI.
}

// --------------------------------------------------------------------------
// Name hygiene
// --------------------------------------------------------------------------

void testNoteIsADirectChildOfItsOwnTileOnly()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("carrier"),
                                     QStringLiteral("strongest first"), 100.0, true, 1.8)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("squeeze"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    auto direct = tile->findChildren<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"),
                                              Qt::FindDirectChildrenOnly);
    CHECK(direct.size() == 1);
    // Not found two levels down as some other tile's grandchild.
    AetherGateChainTile* nb = strip(w)->tile(QStringLiteral("nb"));
    CHECK(nb != nullptr);
    if (nb)
        CHECK(nb->findChildren<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"),
                                        Qt::FindDirectChildrenOnly)
                  .isEmpty());
}

// --------------------------------------------------------------------------
// Names, and nothing scrolls
//
// The write path itself -- the toggle that used to live on the auto_clean
// card -- moved to aether_gate_chain_now_test.cpp along with the card: NOW
// (AetherGateChainNow.h) is the only thing that still writes auto=on/off,
// and its own ladder tests (cases 4 and 5) cover the on->off and off->on
// halves the old testToggleWritesExactlyAutoOffWhileOnAndAutoOnWhileOff()
// used to.
// --------------------------------------------------------------------------

void testEveryB25WidgetHasANameAndNothingScrollsAtInitialSize()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    // The longest why/kind pair this file writes, and three events plus a
    // backoff, so the no-scroll claim is checked against a full inspector
    // rather than an empty one that would pass by accident.
    const QJsonArray holding = {
        held(QStringLiteral("squeeze"), QStringLiteral("mains"),
            QStringLiteral("mains comb found and coherence at least 0.4"), 100.0, true, 3.4)};
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    const QJsonArray backoffs = {
        backoff(QStringLiteral("mains"), QStringLiteral("squeeze"), localEpoch(12, 46, 0))};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding,
                                                 QJsonValue(), events, backoffs)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* note = w->findChild<QLabel*>(QStringLiteral("gateChainAutoNote_squeeze"));
    CHECK(note != nullptr);
    if (note) {
        CHECK(!note->objectName().isEmpty());
        CHECK(!note->wordWrap());
    }

    // auto_clean itself has no tile any more -- see AetherGateChainNow.h --
    // so this is any other real stage, only to exercise the inspector's own
    // no-word-wrap widgets below.
    strip(w)->selectStage(QStringLiteral("squeeze"));
    settle();

    auto* state = w->findChild<QLabel*>(QStringLiteral("gateChainAutoState"));
    auto* events_ = w->findChild<QLabel*>(QStringLiteral("gateChainAutoEvents"));
    CHECK(state != nullptr);
    CHECK(events_ != nullptr);
    if (state)
        CHECK(!state->wordWrap());
    if (events_)
        CHECK(!events_->wordWrap());

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("gateChainScroll"));
    CHECK(scroll != nullptr);
    if (scroll) {
        for (QScrollBar* sb : scroll->findChildren<QScrollBar*>()) {
            if (sb->orientation() == Qt::Horizontal)
                CHECK(!sb->isVisible());
        }
    }
}

// With CHAIN_AUTO_RENDER_PNG set (to anything), the AUTO CLEAN card holding
// two tools with a third event queued is rendered to a fixed path, so B25
// can be looked at.
void testRenderAutoWhenAsked()
{
    if (qgetenv("CHAIN_AUTO_RENDER_PNG").isEmpty())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray holding = {
        held(QStringLiteral("squeeze"), QStringLiteral("mains"),
            QStringLiteral("mains comb found"), 100.0, true, 3.4),
        held(QStringLiteral("guard"), QStringLiteral("neighbour"),
            QStringLiteral("headroom low"), 90.0, true, 1.1),
    };
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("released"), false),
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8),
        event(localEpoch(12, 42, 30), QStringLiteral("guard"), QStringLiteral("neighbour"),
             QStringLiteral("headroom low"), QStringLiteral("undone"), true, -0.9),
    };
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding 2"), holding,
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    // The FRONT END card's synthetic GUARD row so the held "guard" tool has
    // a row to hold, matching the squeeze row withAutoCleanChain() already
    // carries -- otherwise only one of the two held tools would have a card
    // to put its note on, and the PNG would show one note for two holds.
    feedDevice(w, frontend(true, QStringLiteral("4"), QStringLiteral("6"), true, 1.1, 0,
                          QStringLiteral("holding")));
    bringUp(w);
    // auto_clean itself has no tile any more -- see AetherGateChainNow.h,
    // which is what now draws on this same PNG above the tabs -- so the
    // selection is the squeeze card its own held note decorates.
    strip(w)->selectStage(QStringLiteral("squeeze"));
    settle();
    CHECK(w->grab().save(QStringLiteral("/tmp/chain-b25-auto.png")));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_auto_test"));
    QApplication app(argc, argv);

    testNoteForHeldSqueeze();
    testNoteForHeldToolWithNoScoreYetOmitsDelta();
    testNoteForPendingNbHasNoScoreYet();
    testNoteRemovedWhenHoldingEmpties();
    testNoteIsADirectChildOfItsOwnTileOnly();
    testEveryB25WidgetHasANameAndNothingScrollsAtInitialSize();
    testRenderAutoWhenAsked();

    std::printf("\n%d aether gate chain auto test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
