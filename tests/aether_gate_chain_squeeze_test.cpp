// B24 SQUEEZE -- the operator's own null or notch, on one signal or a comb,
// asked for by pointing:
//
//   "gate picks null-vs-notch itself based on inter-loop coherence; the app's
//    job is to draw whatever it is holding, let a Shift+click place or move
//    the target, and let RELEASE or SQUEEZE: COMB do the two things a click
//    on the curve cannot."
//
// The gate's own /filter carries the state under "squeeze" (core/squeeze.py
// Squeeze.status(), adapters/diversity_squeeze.py) -- off is "since" null,
// armed is "since" set but not yet "held" (the gate is listening for enough
// coherence to commit), held is the tool actually in force. Two things about
// its numbers are NOT obvious from the route table and are worth restating
// here because a wrong build of THIS FILE would silently pass with them
// backwards:
//
//   * squeeze.hz and every comb.teeth_in_band[] entry are SIGNED, in the
//     slice's own frame -- core/comb.py's own docstring: "Every tooth's
//     BASEBAND offset (matching Squeeze.hz's own convention)". The gate does
//     NOT abs-ify them the way it abs-ifies low_hz/high_hz/anf.found_hz, so
//     this app does, at paint and hit-test time (DiversityFilterPanelSqueeze
//     .cpp) -- panel Hz is always abs(the signed value).
//   * the SIGN a Shift+click's own write goes out with is read back off
//     "mode": LSB negative, everything else (USB, CW, the digital modes)
//     positive -- the same split adapters/diversity_state.py's own
//     _pass_edges() makes internally.
//
// The "squeeze" objects the tests below poll are built field by field in
// AetherGateChainSqueezeBlocks.h -- none of AetherGateChainFixture.h's own
// bodies carry one (B24 landed after they were written).
#include "AetherGateChainFixture.h"
#include "AetherGateChainSqueezeBlocks.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

constexpr int kTabChain = 0;
constexpr int kTabVisual = 1;

void bringUp(AetherGateChainWindow* w, int tab)
{
    w->resize(1120, 820);
    w->setCurrentTab(tab);
    settle();
    w->grab();
    settle();
}

// --------------------------------------------------------------------------
// The picture
// --------------------------------------------------------------------------

void testTheBracketIsDrawnWhereTheSignedHzAbsIfiesTo()
{
    // hz=1450, width=300: the two edges are 1450-150=1300 and 1450+150=1600,
    // both already positive on USB (visualFilter()'s default mode), so this
    // is the plain case -- the LSB/comb sign flip is covered on its own below.
    const QByteArray body =
        withSqueeze(visualFilter(), squeezeHeldSignalBlock(1450.0, 300.0));
    DiversityFilterPanel p;
    p.resize(1060, 320);
    p.show();
    settle();
    p.applyStatus(QJsonDocument::fromJson(body).object());
    settle();

    CHECK(p.squeezeHeld());
    CHECK(!p.squeezeArmed());
    CHECK(!p.squeezeOff());
    CHECK(p.squeezeTarget() == QStringLiteral("signal"));
    CHECK(p.squeezeTool() == QStringLiteral("null"));
    CHECK((p.squeezeBracketLowHz()) == (1300.0));
    CHECK((p.squeezeBracketHighHz()) == (1600.0));
    // Drawn where it says it is: the bracket's own x-positions are the same
    // xForHz() every other mark on this widget uses.
    CHECK((p.xForHz(p.squeezeBracketLowHz())) == (p.xForHz(1300.0)));
    CHECK((p.xForHz(p.squeezeBracketHighHz())) == (p.xForHz(1600.0)));
    // MUTATION: swap min/max (or drop the abs()) in squeezeBracketLowHz()/
    // squeezeBracketHighHz() and this pair fails -- see this file's header
    // comment for why the LSB/comb case below is the sharper version of it.
    CHECK(p.squeezeBracketLowHz() < p.squeezeBracketHighHz());
}

void testCombTeethAreEveryEntryAbsIfiedFromTheSignedSlicesFrame()
{
    // LSB, and every tooth NEGATIVE -- core/comb.py's own convention, the one
    // the task's own prose got backwards (see this file's header comment).
    const QByteArray body =
        withSqueeze(visualFilter(), squeezeHeldCombBlock(), QStringLiteral("LSB"));
    DiversityFilterPanel p;
    p.resize(1060, 320);
    p.show();
    settle();
    p.applyStatus(QJsonDocument::fromJson(body).object());
    settle();

    CHECK(p.squeezeHeld());
    CHECK(p.squeezeTarget() == QStringLiteral("comb"));
    CHECK(p.squeezeTool() == QStringLiteral("notch"));
    CHECK(p.squeezeToothCount() == 4);
    // MUTATION: drop the abs() in squeezeToothHzAt() and every one of these
    // reads negative (or the widget clamps it to the left gutter) instead.
    CHECK((p.squeezeToothHzAt(0)) == (230.0));
    CHECK((p.squeezeToothHzAt(1)) == (430.0));
    CHECK((p.squeezeToothHzAt(2)) == (630.0));
    CHECK((p.squeezeToothHzAt(3)) == (830.0));
    for (int i = 0; i < p.squeezeToothCount(); ++i)
        CHECK(p.squeezeToothHzAt(i) >= 0.0);
}

// --------------------------------------------------------------------------
// The status line
// --------------------------------------------------------------------------

void testStatusLineTextForOffArmedAndHeld()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withSqueeze(visualFilter(), squeezeOffBlock()));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    auto* line = w->findChild<QLabel*>(QStringLiteral("gateChainSqueezeLine"));
    CHECK(line != nullptr);
    if (!line)
        return;

    CHECK(line->accessibleDescription()
          == QStringLiteral("SQUEEZE off — Shift+click a signal, or SQUEEZE: COMB"));

    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withSqueeze(visualFilter(), squeezeArmedSignalBlock(1450.0, 300.0))};
    filterTick(applet);
    CHECK(line->accessibleDescription()
          == QStringLiteral("SQUEEZE arming on 1 450 Hz — listening for 0.50 coherence"));

    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withSqueeze(visualFilter(), squeezeHeldSignalBlock(1450.0, 300.0))};
    filterTick(applet);
    // MUTATION: use squeezeReason() instead of squeezeWhy() here and this
    // line goes blank -- `reason` is not set on any block this file builds.
    CHECK(line->accessibleDescription()
          == QStringLiteral("SQUEEZE NULL on 1 450 Hz — coherence 0.62 — "
                            "nulled in 5 bins"));

    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError, withSqueeze(visualFilter(), squeezeHeldCombBlock())};
    filterTick(applet);
    CHECK(line->accessibleDescription()
          == QStringLiteral("SQUEEZE NOTCH on comb, 4 teeth — coherence 0.40 — "
                            "not one direction; notched 4 teeth"));
}

// --------------------------------------------------------------------------
// The gestures
// --------------------------------------------------------------------------

void testShiftClickWritesSqueezeAndReleasesTheBracketBothWays()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withSqueeze(visualFilter(), squeezeHeldSignalBlock(1450.0, 300.0)));
    // Every write's reply is read back as the next /filter body (see
    // AetherGateApplet::onChainRequestWrite -> DiversityBandPoller::sendFilter
    // -> filterReceived -> applyFilter). Answered with the SAME held bracket
    // for every write in this test on purpose: what is checked here is the
    // OUTGOING query string of each gesture, not the gate's own next state,
    // and the bracket has to still be there for the second and third gesture
    // to have anything to hit.
    net.routes[QStringLiteral("/diversity/set")] = {
        QNetworkReply::NoError,
        withSqueeze(visualFilter(), squeezeHeldSignalBlock(1450.0, 300.0))};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;
    const int y = p->height() / 2;

    // Shift+click AWAY from the bracket: place a NEW target at 1 200 Hz.
    const int before = net.log.size();
    QTest::mouseClick(p, Qt::LeftButton, Qt::ShiftModifier, QPoint(int(p->xForHz(1200)), y));
    settle();
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=1200"));

    // Shift+click ON the bracket: release.
    const int beforeRelease = net.log.size();
    QTest::mouseClick(p, Qt::LeftButton, Qt::ShiftModifier, QPoint(int(p->xForHz(1450)), y));
    settle();
    CHECK(net.log.size() == beforeRelease + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=off"));

    // Right-click ON the bracket: also a release -- the task's other gesture
    // for the same door.
    const int beforeRightClick = net.log.size();
    QTest::mouseClick(p, Qt::RightButton, Qt::NoModifier, QPoint(int(p->xForHz(1450)), y));
    settle();
    CHECK(net.log.size() == beforeRightClick + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=off"));
}

void testShiftClickSignsTheWriteFromTheModeOnLsb()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withSqueeze(visualFilter(), squeezeOffBlock(), QStringLiteral("LSB")));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;
    const int y = p->height() / 2;

    // MUTATION: drop the LSB flip in squeezeHzForClick() and this reads
    // "squeeze=1200" -- the mirror frequency, not the one under the pointer.
    const int before = net.log.size();
    QTest::mouseClick(p, Qt::LeftButton, Qt::ShiftModifier, QPoint(int(p->xForHz(1200)), y));
    settle();
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=-1200"));
}

void testCombAndReleaseButtonsWriteExactQueriesAndReleaseTracksHeld()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withSqueeze(visualFilter(), squeezeOffBlock()));
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, visualFilter()};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);

    auto* comb = button(w, QStringLiteral("gateChainSqueezeComb"));
    auto* release = button(w, QStringLiteral("gateChainSqueezeRelease"));
    CHECK(comb != nullptr && release != nullptr);
    if (!comb || !release)
        return;

    // Off: RELEASE has nothing to let go of.
    CHECK(!release->isEnabled());

    const int before = net.log.size();
    comb->click();
    settle();
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=comb"));

    // Armed (or held) turns RELEASE on. MUTATION: check squeezeHeld() alone
    // here instead of !squeezeOff() and this stays disabled through the
    // armed state, which is exactly when an operator most wants to bail.
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withSqueeze(visualFilter(), squeezeArmedSignalBlock(1450.0, 300.0))};
    filterTick(applet);
    CHECK(release->isEnabled());

    const int beforeRelease = net.log.size();
    release->click();
    settle();
    CHECK(net.log.size() == beforeRelease + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=off"));

    // Off again: RELEASE goes back to disabled on the gate's own next answer.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             withSqueeze(visualFilter(), squeezeOffBlock())};
    filterTick(applet);
    CHECK(!release->isEnabled());
}

// --------------------------------------------------------------------------
// The door
// --------------------------------------------------------------------------

void testAPlainClickOnTheBracketIsADoorToTheSqueezeStage()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withSqueezeChainRow(withSqueeze(
                   visualFilter(), squeezeHeldSignalBlock(1450.0, 300.0))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;

    const int writes = net.log.size();
    QTest::mouseClick(p, Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(p->xForHz(1450)), p->height() / 2));
    settle();
    CHECK(w->currentTab() == kTabChain);
    CHECK(strip(w)->selectedId() == QStringLiteral("squeeze"));
    CHECK(net.log.size() == writes);          // a click is not a write
}

// --------------------------------------------------------------------------
// Names, and the evidence
// --------------------------------------------------------------------------

void testEveryB24WidgetHasANameNoLabelWrapsAndNothingScrolls()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    // The longest `why` this file writes, on the widest tool word, so the
    // no-scroll claim is checked against the worst case rather than a short
    // one that would pass by accident.
    connectGate(applet, net, withSqueeze(visualFilter(), squeezeHeldCombBlock()));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);

    for (const char* name : {"gateChainSqueezeComb", "gateChainSqueezeRelease",
                             "gateChainSqueezeLine"}) {
        CHECK(w->findChild<QWidget*>(QString::fromLatin1(name)) != nullptr);
        if (!w->findChild<QWidget*>(QString::fromLatin1(name)))
            std::printf("  missing: %s\n", name);
    }
    auto* visual = w->findChild<QWidget*>(QStringLiteral("gateChainVisual"));
    CHECK(visual != nullptr);
    if (!visual)
        return;
    for (QWidget* kid : visual->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        CHECK(!kid->objectName().isEmpty());
    for (QLabel* label : visual->findChildren<QLabel*>())
        CHECK(!label->wordWrap());

    // No horizontal scrollbar anywhere under the VISUAL tab at its initial
    // size: the status line is elided by hand precisely so this stays true
    // even with the longest sentence this file writes sitting under it.
    auto* visualTab = w->findChild<QWidget*>(QStringLiteral("gateChainTabVisual"));
    CHECK(visualTab != nullptr);
    if (visualTab) {
        for (QScrollBar* sb : visualTab->findChildren<QScrollBar*>()) {
            if (sb->orientation() == Qt::Horizontal)
                CHECK(!sb->isVisible());
        }
    }
    auto* line = w->findChild<QLabel*>(QStringLiteral("gateChainSqueezeLine"));
    CHECK(line != nullptr);
    if (line)
        CHECK(line->width() <= visual->width());
}

// The VISUAL tab's own audit (2026-09): the axis, the caption, the legend, the
// corner, the gate-away state and the staleness cue. In THIS file rather than
// a new one because tests/tests.cmake lists the panel sources in twenty-seven
// separate targets, each of which a new sibling would have to be added to.

// The gate's `sideband` key, which core/filter.py's status() answers "lsb" or
// "usb" at the TOP level, next to `mode`.
QByteArray withSideband(const QByteArray& body, const QString& sideband,
                        const QString& mode = QString())
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    root.insert(QStringLiteral("sideband"), sideband);
    if (!mode.isEmpty())
        root.insert(QStringLiteral("mode"), mode);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

AetherGateChainVisual* visualTab(AetherGateChainWindow* w)
{
    return w ? w->findChild<AetherGateChainVisual*>() : nullptr;
}

// A 250 Hz CW filter used to be four pixels wide in the middle of three
// kilohertz of empty air. The span is the passband plus a margin of 60% of
// its width, floored at 250 Hz, clamped to what the gate actually sent.
//
// MUTATION: drop updateSpan()'s margin and the first pair reads 900/1150;
// drop the call from applyStatus() and it reads 0/3000.
void testTheAxisSpansThePassbandPlusItsMargin()
{
    DiversityFilterPanel p;
    p.resize(1060, 320);
    p.show();
    settle();
    p.applyStatus(QJsonDocument::fromJson(visualFilter(128, 900, 1150)).object());
    settle();
    CHECK(std::abs(p.viewMinHz() - 650.0) < 1.0);
    CHECK(std::abs(p.viewMaxHz() - 1400.0) < 1.0);

    // A wide SSB passband asks for more margin than the array has, so the span
    // clamps back to the whole of it -- the picture everyone already knows.
    p.applyStatus(QJsonDocument::fromJson(visualFilter()).object());
    settle();
    CHECK(std::abs(p.viewMinHz()) < 1.0);
    CHECK(std::abs(p.viewMaxHz() - 3000.0) < 1.0);
}

// The corner used to read the axis coordinate under the pointer, which is a
// fact about the mouse. It reads a measurement now: how far the arriving band
// stands over the gate's own floor.
//
// MUTATION: return the axis dB and both readings are about -5; drop the floor
// subtraction and both are near -70. The fixture's carrier is a peak at
// 1 200 Hz over a -70 dB floor; 2 800 Hz is empty air on the same floor. The
// carrier is not checked against 70 exactly because the nearest of the 128
// bins the gate sends is 4.7 Hz off the peak of a 40 Hz-wide triangle.
void testTheCursorReadsDbOverTheFloorNotTheAxis()
{
    DiversityFilterPanel p;
    p.resize(1060, 320);
    p.show();
    settle();
    p.applyStatus(QJsonDocument::fromJson(visualFilter()).object());
    settle();
    QTest::mouseMove(&p, QPoint(int(p.xForHz(1200)), p.height() / 2));
    settle();
    CHECK(std::abs(p.cursorHz() - 1200.0) < 30.0);
    CHECK(p.cursorDbOverFloor() > 50.0);

    QTest::mouseMove(&p, QPoint(int(p.xForHz(2800)), p.height() / 2));
    settle();
    CHECK(std::abs(p.cursorDbOverFloor()) < 5.0);
}

// The no-news rule, with the NaNs the gate really sends in it. AUTO is off in
// the fixture, so autoLowHz/autoHighHz are NaN -- and NaN != NaN, so a
// fingerprint over the raw bytes of every double would differ from itself on
// every poll and rebuild both cached layers forever.
//
// MUTATION: feed the doubles to the fingerprint without canonicalising the
// NaNs and the rebuild count climbs with the poll count.
void testIdenticalBodiesWithNaNsInThemRebuildNothing()
{
    DiversityFilterPanel p;
    p.resize(1060, 320);
    p.show();
    settle();
    const QJsonObject body = QJsonDocument::fromJson(visualFilter()).object();
    p.applyStatus(body);
    settle();
    p.grab();
    CHECK(std::isnan(p.autoLowHz()));
    const int rebuilds = p.staticRebuildCount();
    CHECK(rebuilds > 0);
    for (int i = 0; i < 3; ++i) {
        p.applyStatus(body);
        settle();
        p.grab();
    }
    CHECK(p.staticRebuildCount() == rebuilds);
}

// What the picture is OF. A response curve on an always-positive audio axis
// looks the same on USB, on LSB and on CW-R, so the caption says which.
//
// MUTATION: read `mode` first and the caption says "PASSBAND · CW-R · USB".
void testTheCaptionSaysTheSidebandAndTheMode()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
                withSideband(visualFilter(), QStringLiteral("usb"),
                             QStringLiteral("CW-R")));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    CHECK(labelText(w, "gateChainVisualCaption")
          == QStringLiteral("PASSBAND · USB · CW-R"));

    // A mode that is only its own sideband spelled again is said once.
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withSideband(visualFilter(), QStringLiteral("usb"), QStringLiteral("USB"))};
    filterTick(applet);
    settle();
    CHECK(labelText(w, "gateChainVisualCaption") == QStringLiteral("PASSBAND · USB"));

    // ...and the key under the picture names the families that are ON it and
    // no others: the fixture has notches, ANF tones and a contour, and no
    // test has an audio engine, so there is nothing to hear.
    const QString legend = labelText(w, "gateChainVisualLegend");
    CHECK(legend.contains(QStringLiteral("auto-notch")));
    CHECK(legend.contains(QStringLiteral("contour/APF")));
    CHECK(!legend.contains(QStringLiteral("hearing")));
}

// The gate is away. clear() used to empty the picture and leave SQUEEZE: COMB
// live under "Shift+click a signal" -- an offer nothing can accept.
//
// MUTATION: drop setPresent()'s refreshSqueezeLine() and the line keeps the
// invitation; drop its m_present term and RELEASE comes back on.
void testGateAwayTakesTheSqueezeOfferWithIt()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withSqueeze(visualFilter(), squeezeOffBlock()));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    AetherGateChainVisual* v = visualTab(w);
    QPushButton* comb = button(w, QStringLiteral("gateChainSqueezeComb"));
    CHECK(v != nullptr);
    CHECK(comb != nullptr);
    if (!v || !comb)
        return;
    CHECK(comb->isEnabled());

    v->setPresent(false);
    settle();
    CHECK(!comb->isEnabled());
    CHECK(labelText(w, "gateChainSqueezeLine").contains(QStringLiteral("gate is away")));
    QPushButton* release = button(w, QStringLiteral("gateChainSqueezeRelease"));
    CHECK(release != nullptr);
    if (release)
        CHECK(!release->isEnabled());

    v->setPresent(true);
    settle();
    CHECK(comb->isEnabled());
    CHECK(labelText(w, "gateChainSqueezeLine").contains(QStringLiteral("Shift+click")));
}

// A poll that did not answer. The curve stays -- it was true a second ago --
// and the caption carries the cue, in the same [live] property the AUTO CLEAN
// banner uses.
//
// MUTATION: drop the setLive() call and the property never turns true; drop
// the m_stale branch in refreshCaption() and the words never appear.
void testAFailedPollIsSaidInTheCaptionAndNotByBlanking()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    AetherGateChainVisual* v = visualTab(w);
    QLabel* caption = label(w, QStringLiteral("gateChainVisualCaption"));
    DiversityFilterPanel* p = panel(w);
    CHECK(v != nullptr);
    CHECK(caption != nullptr);
    CHECK(p != nullptr);
    if (!v || !caption || !p)
        return;
    const int pointsBefore = p->spectrumPointCount();
    CHECK(pointsBefore > 0);

    v->setStale(true);
    settle();
    CHECK(caption->text().contains(QStringLiteral("NOT ANSWERING")));
    CHECK(caption->property("live").toBool());
    // The picture is NOT emptied: staleness is a cue, not a clear().
    CHECK(p->spectrumPointCount() == pointsBefore);

    v->setStale(false);
    settle();
    CHECK(!caption->text().contains(QStringLiteral("NOT ANSWERING")));
    CHECK(!caption->property("live").toBool());
}

// Which way a Shift+click's own write is signed. `sideband` is the gate's own
// answer and `mode` is the adapter's slice mode -- on CW-R, RTTY-R and the
// digital reverse modes the two disagree, and reading only `mode` put every
// squeeze on the mirror frequency.
//
// MUTATION: read `mode` first in parseSqueeze() and this writes squeeze=1200.
void testTheSqueezeSignComesFromTheSidebandKeyNotTheMode()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
                withSideband(withSqueeze(visualFilter(), squeezeOffBlock()),
                             QStringLiteral("lsb"), QStringLiteral("CW-R")));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;
    const int before = net.log.size();
    QTest::mouseClick(p, Qt::LeftButton, Qt::ShiftModifier,
                      QPoint(int(p->xForHz(1200)), p->height() / 2));
    settle();
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?squeeze=-1200"));
}

// With CHAIN_SQUEEZE_RENDER_PNG set (to anything), a held signal and a held
// comb are each rendered and written to fixed paths, so B24 can be looked at.
void testRenderSqueezeWhenAsked()
{
    if (qgetenv("CHAIN_SQUEEZE_RENDER_PNG").isEmpty())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withSqueeze(visualFilter(), squeezeHeldSignalBlock(1450.0, 300.0)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    CHECK(w->grab().save(QStringLiteral("/tmp/chain-b24-squeeze-signal.png")));

    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError, withSqueeze(visualFilter(), squeezeHeldCombBlock(),
                                            QStringLiteral("LSB"))};
    filterTick(applet);
    settle();
    CHECK(w->grab().save(QStringLiteral("/tmp/chain-b24-squeeze-comb.png")));
}

} // namespace

// LIVE, 2026-09-04: squeeze=comb asked for, the gate still hunting -- every
// comb number null, `reason` "no comb found" -- and the caption read "SQUEEZE
// arming on a comb, spacing 0 Hz": a spacing nobody measured, and nothing to
// say the gate was still looking. Now it says so, and the gate's own reason
// rides behind the dash while `why` is still empty.
void testACombNotFoundYetSaysSoInsteadOfASpacingOfZero()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withSqueeze(visualFilter(), squeezeArmedCombBlock()));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    auto* line = w->findChild<QLabel*>(QStringLiteral("gateChainSqueezeLine"));
    CHECK(line != nullptr);
    if (!line)
        return;
    CHECK(line->accessibleDescription()
          == QStringLiteral("SQUEEZE looking for a comb to notch — no comb found"));
    CHECK(!line->accessibleDescription().contains(QStringLiteral("0 Hz")));
}

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_squeeze_test"));
    QApplication app(argc, argv);

    testTheBracketIsDrawnWhereTheSignedHzAbsIfiesTo();
    testCombTeethAreEveryEntryAbsIfiedFromTheSignedSlicesFrame();
    testStatusLineTextForOffArmedAndHeld();
    testShiftClickWritesSqueezeAndReleasesTheBracketBothWays();
    testShiftClickSignsTheWriteFromTheModeOnLsb();
    testCombAndReleaseButtonsWriteExactQueriesAndReleaseTracksHeld();
    testAPlainClickOnTheBracketIsADoorToTheSqueezeStage();
    testEveryB24WidgetHasANameNoLabelWrapsAndNothingScrolls();
    testTheAxisSpansThePassbandPlusItsMargin();
    testTheCaptionSaysTheSidebandAndTheMode();
    testGateAwayTakesTheSqueezeOfferWithIt();
    testAFailedPollIsSaidInTheCaptionAndNotByBlanking();
    testTheCursorReadsDbOverTheFloorNotTheAxis();
    testTheSqueezeSignComesFromTheSidebandKeyNotTheMode();
    testIdenticalBodiesWithNaNsInThemRebuildNothing();
    testRenderSqueezeWhenAsked();
    testACombNotFoundYetSaysSoInsteadOfASpacingOfZero();

    std::printf("\n%d aether gate chain squeeze test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
