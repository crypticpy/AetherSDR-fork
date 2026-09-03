// A1 ROOFING · DIGITAL -- PEAK OFFSET: the digital roof's own centre, out of
// Aether-gate's `roofing` object on /filter (offset_hz/offset_enabled/
// offset_applied_hz/offset_max_hz; docs/DIVERSITY.md's "The CHAIN window" ->
// "ROOFING · DIGITAL -- PEAK OFFSET"). Two things this app draws from it:
//
//   * a check mark on the ROOFING · DIGITAL card, generic off the row's own
//     `checks[]` array -- no per-stage code, no string built here: the box's
//     route and query_on/query_off are the gate's own, sent verbatim.
//   * a draggable handle at the roof's own centre on the VISUAL tab, clamped
//     to +/- offset_max_hz, that writes /filter/set?roof_offset_hz=<n> on
//     release.
//
// None of AetherGateChainFixture.h's existing bodies carry a checks[] array
// or the roofing.offset_* fields -- this feature landed after the fixture
// was written -- so every payload below is built by hand from kChainFilter
// or visualFilter(), the same way aether_gate_chain_squeeze_test.cpp builds
// its own "squeeze" objects for B24.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTest>

#include <cmath>
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

// One "checks[]" entry, shaped exactly the way the gate's own chainstatus.py
// writes ROOFING · DIGITAL's PEAK OFFSET -- see docs/DIVERSITY.md's "For
// scripts" section. `route`/`query_on`/`query_off` are sent back verbatim by
// the app, so a wrong string here would pass a test that builds its own
// write from the label instead of from these fields.
QJsonObject roofOffsetCheck(bool on)
{
    QJsonObject check;
    check.insert(QStringLiteral("key"), QStringLiteral("roof_offset"));
    check.insert(QStringLiteral("label"), QStringLiteral("PEAK OFFSET"));
    check.insert(QStringLiteral("on"), on);
    check.insert(QStringLiteral("route"), QStringLiteral("/filter/set"));
    check.insert(QStringLiteral("query_on"), QStringLiteral("roof_offset=on"));
    check.insert(QStringLiteral("query_off"), QStringLiteral("roof_offset=off"));
    return check;
}

// `body`'s "roofing" object with the offset fields inserted, and (unless
// `withCheck` is false) the roof_digital chain row given a one-entry
// checks[]. `digitalHz`/`appliedHz`/`maxHz` all land on the SAME axis this
// panel already draws low_hz/high_hz on -- see DiversityFilterPanelRoof.cpp's
// own header comment for why, unlike SQUEEZE, there is no sign flip here.
QByteArray withRoofing(const QByteArray& body, bool checkOn, double appliedHz, double maxHz,
                       double digitalHz, bool withCheck = true)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    QJsonObject roofing = root.value(QStringLiteral("roofing")).toObject();
    roofing.insert(QStringLiteral("digital_hz"), digitalHz);
    roofing.insert(QStringLiteral("offset_hz"), appliedHz);
    roofing.insert(QStringLiteral("offset_enabled"), checkOn);
    roofing.insert(QStringLiteral("offset_applied_hz"), appliedHz);
    roofing.insert(QStringLiteral("offset_max_hz"), maxHz);
    root.insert(QStringLiteral("roofing"), roofing);

    QJsonArray chain = root.value(QStringLiteral("chain")).toArray();
    for (int i = 0; i < chain.size(); ++i) {
        QJsonObject row = chain.at(i).toObject();
        if (row.value(QStringLiteral("id")).toString() != QStringLiteral("roof_digital"))
            continue;
        if (withCheck)
            row.insert(QStringLiteral("checks"), QJsonArray{roofOffsetCheck(checkOn)});
        else
            row.remove(QStringLiteral("checks"));
        chain.replace(i, row);
    }
    root.insert(QStringLiteral("chain"), chain);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// --------------------------------------------------------------------------
// The CHAIN card -- checks[] drawn generically
// --------------------------------------------------------------------------

void testCheckBoxAppearsFromChecksWithRightObjectNameAndCheckedState()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withRoofing(kChainFilter, /*checkOn=*/true, 800.0, 900.0, 3000.0));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* box = w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"));
    CHECK(box != nullptr);
    if (!box)
        return;
    CHECK(box->isChecked());
    CHECK(box->text() == QStringLiteral("PEAK OFFSET"));

    // The gate's answer with the check off moves it, and moves it alone --
    // "moves only on read-back" like every other control in this window.
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withRoofing(kChainFilter, /*checkOn=*/false, 800.0, 900.0, 3000.0)};
    filterTick(applet);
    CHECK(!box->isChecked());
}

void testTogglingWritesExactQueryOnAndOff()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withRoofing(kChainFilter, /*checkOn=*/false, 800.0, 900.0, 3000.0));
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        withRoofing(kChainFilter, /*checkOn=*/true, 800.0, 900.0, 3000.0)};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* box = w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"));
    CHECK(box != nullptr);
    if (!box)
        return;
    CHECK(!box->isChecked());

    // MUTATION: build "roof_offset=on" from the label instead of forwarding
    // query_on verbatim and this still passes today -- but see the malformed
    // -check test below for the case that would catch it structurally.
    const int before = countWrites(net);
    box->click();
    settle();
    CHECK(countWrites(net) == before + 1);
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?roof_offset=on"));
    CHECK(box->isChecked());          // the gate's own answer, not the click

    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        withRoofing(kChainFilter, /*checkOn=*/false, 800.0, 900.0, 3000.0)};
    const int beforeOff = countWrites(net);
    box->click();
    settle();
    CHECK(countWrites(net) == beforeOff + 1);
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?roof_offset=off"));
    CHECK(!box->isChecked());
}

void testNoWriteWhileAWriteIsPending()
{
    SlowGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withRoofing(kChainFilter, /*checkOn=*/false, 800.0, 900.0, 3000.0));
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        withRoofing(kChainFilter, /*checkOn=*/true, 800.0, 900.0, 3000.0)};
    net.delays[QStringLiteral("/filter/set")] = 60;
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* box = w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"));
    CHECK(box != nullptr);
    if (!box)
        return;

    const int before = countWrites(net);
    box->click();
    // A second click while the first write is still on the wire must not
    // reach the gate -- the same rule every other control in this window
    // keeps (see testOneClickSwitchesAStageEvenWhenAStalePollLandsAfter
    // TheWriteReply in aether_gate_chain_ux_test.cpp).
    CHECK(!box->isEnabled());
    box->click();
    QTest::qWait(200);

    CHECK(countWrites(net) == before + 1);
    auto* after =
        w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"));
    CHECK(after != nullptr);
    if (after)
        CHECK(after->isEnabled());
}

void testRefusalReachesTheInspector()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withRoofing(kChainFilter, /*checkOn=*/false, 800.0, 900.0, 3000.0));
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        QByteArray(R"({"error": "bad value: roof_offset already applied"})")};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* box = w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"));
    CHECK(box != nullptr);
    if (!box)
        return;
    box->click();
    settle();

    CHECK(!box->isChecked());         // refused: it goes straight back
    QLabel* why = label(w, QStringLiteral("gateChainWhy_roof_digital"));
    CHECK(why != nullptr);
    if (why) {
        CHECK(why->isVisibleTo(w));
        CHECK(why->toolTip().contains(QStringLiteral("roof_offset already applied")));
    }
    QLabel* note = label(w, QStringLiteral("gateChainDetailNote"));
    CHECK(note != nullptr);
    if (note)
        CHECK(note->toolTip().contains(QStringLiteral("roof_offset already applied")));
}

void testARowWithoutChecksDrawsNone()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);       // no roofing.offset_*, no checks[]
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"))
          == nullptr);
}

void testAMalformedCheckWithNoKeyDrawsNothingAndDoesNotCrash()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    QJsonObject root = QJsonDocument::fromJson(kChainFilter).object();
    QJsonArray chain = root.value(QStringLiteral("chain")).toArray();
    for (int i = 0; i < chain.size(); ++i) {
        QJsonObject row = chain.at(i).toObject();
        if (row.value(QStringLiteral("id")).toString() != QStringLiteral("roof_digital"))
            continue;
        QJsonObject broken;
        broken.insert(QStringLiteral("label"), QStringLiteral("PEAK OFFSET"));
        broken.insert(QStringLiteral("on"), true);
        broken.insert(QStringLiteral("route"), QStringLiteral("/filter/set"));
        broken.insert(QStringLiteral("query_on"), QStringLiteral("roof_offset=on"));
        broken.insert(QStringLiteral("query_off"), QStringLiteral("roof_offset=off"));
        // No "key". MUTATION: drop the key.isEmpty() guard in stageFromJson()
        // and this either crashes findCheck()'s lookup-by-key or draws a box
        // that writes nothing sensible when clicked.
        row.insert(QStringLiteral("checks"), QJsonArray{broken});
        chain.replace(i, row);
    }
    root.insert(QStringLiteral("chain"), chain);
    connectGate(applet, net, QJsonDocument(root).toJson(QJsonDocument::Compact));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"))
          == nullptr);
    // The rest of the row still built: a malformed check must not take the
    // whole card down with it.
    CHECK(w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_roof_digital")) != nullptr);
}

void testTheInspectorShowsTheCheckToo()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withRoofing(kChainFilter, /*checkOn=*/true, 800.0, 900.0, 3000.0));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("roof_digital"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    QTest::mouseClick(tile, Qt::LeftButton);
    settle();
    auto* box = w->findChild<QCheckBox*>(
        QStringLiteral("gateChainDetailCheck_roof_digital_roof_offset"));
    CHECK(box != nullptr);
    if (box)
        CHECK(box->isChecked());
}

// --------------------------------------------------------------------------
// VISUAL -- the handle
// --------------------------------------------------------------------------

void testHandlePositionFromOffsetAppliedHz()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withRoofing(visualFilter(), /*checkOn=*/true, 1450.0, 900.0, 1200.0));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;

    CHECK(p->roofAvailable());
    CHECK(p->roofDraggable());
    CHECK(p->roofChecked());
    CHECK((p->roofOffsetHz()) == (1450.0));
    CHECK((p->roofDigitalHz()) == (1200.0));
    CHECK((p->roofMaxHz()) == (900.0));
}

void testADragBeyondTheClampWritesTheClampedValue()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    // Checked OFF on purpose: "the drag still writes the value (the gate
    // remembers it)" -- the check mark does not gate the gesture.
    connectGate(applet, net,
               withRoofing(visualFilter(), /*checkOn=*/false, 1450.0, 500.0, 1200.0));
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        withRoofing(visualFilter(), /*checkOn=*/false, 1950.0, 500.0, 1200.0)};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr && p->roofDraggable());
    if (!p || !p->roofDraggable())
        return;

    const int y = p->height() / 2;
    const int before = countWrites(net);
    QTest::mousePress(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(1450)), y));
    CHECK(p->dragging());
    // Dragged all the way to the right of the plot -- hzForX() clamps to
    // m_maxHz (~3000 on this fixture), well past offset_max_hz (500 either
    // side of 0, so the ceiling this drag must land on is 500, not 3000).
    QTest::mouseMove(p, QPoint(int(p->xForHz(2900)), y));
    QTest::mouseMove(p, QPoint(int(p->xForHz(3000)), y));
    QTest::mouseRelease(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(3000)), y));
    settle();

    // MUTATION: drop the clamp in roofClampedHz() and this reads
    // "roof_offset_hz=3000" (or whatever hzForX() saw) instead of 500.
    CHECK(countWrites(net) == before + 1);
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?roof_offset_hz=500"));
}

void testZeroMaxHzDrawsNoHandleAndTakesNoDrag()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withRoofing(visualFilter(), /*checkOn=*/false, 0.0, 0.0, 1200.0));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;

    // The band itself is still available (a roof too narrow for the passband
    // it is meant to carry keeps its width, docs/DIVERSITY.md's own words) --
    // only the handle and the drag are gone.
    CHECK(p->roofAvailable());
    CHECK(!p->roofDraggable());

    const int y = p->height() / 2;
    const int before = countWrites(net);
    QTest::mousePress(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(0)), y));
    // MUTATION: drop the m_roofMaxHz <= 0.0 guard in roofHandleHit() and this
    // starts a drag on a handle the picture never drew.
    CHECK(!p->dragging());
    QTest::mouseRelease(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(0)), y));
    settle();
    CHECK(countWrites(net) == before);
}

void testReleaseWritesExactlyRoofOffsetHz()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withRoofing(visualFilter(), /*checkOn=*/true, 800.0, 900.0, 1200.0));
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        withRoofing(visualFilter(), /*checkOn=*/true, 350.0, 900.0, 1200.0)};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr && p->roofDraggable());
    if (!p || !p->roofDraggable())
        return;

    const int y = p->height() / 2;
    const int before = countWrites(net);
    // The written value is whatever hzForX() sees at the release pixel, not a
    // hand-picked Hz -- the same int()/hzForX() round trip a whole pixel of
    // slop can leave one Hz short of the value xForHz() was asked for, the
    // same slack testHandleDragBeyondClampWritesClampedValue()'s own comment
    // names for the clamp case.
    const int releaseX = int(p->xForHz(350));
    const int target = int(std::lround(p->hzForX(double(releaseX))));
    QTest::mousePress(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(800)), y));
    QTest::mouseMove(p, QPoint(int(p->xForHz(600)), y));
    QTest::mouseMove(p, QPoint(releaseX, y));
    QTest::mouseRelease(p, Qt::LeftButton, Qt::NoModifier, QPoint(releaseX, y));
    settle();

    CHECK(countWrites(net) == before + 1);
    CHECK(lastRequest(net)
          == QStringLiteral("/filter/set?roof_offset_hz=%1").arg(target));
    CHECK(!p->dragging());
}

// --------------------------------------------------------------------------
// Names, and the evidence
// --------------------------------------------------------------------------

void testEveryA1WidgetHasANameNoLabelWrapsAndNothingScrolls()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withRoofing(visualFilter(), /*checkOn=*/true, 800.0, 900.0, 1200.0));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabChain);

    auto* box = w->findChild<QCheckBox*>(QStringLiteral("gateChainCheck_roof_digital_roof_offset"));
    CHECK(box != nullptr);
    if (box)
        CHECK(!box->objectName().isEmpty());

    bringUp(w, kTabVisual);
    auto* visual = w->findChild<QWidget*>(QStringLiteral("gateChainVisual"));
    CHECK(visual != nullptr);
    if (!visual)
        return;
    for (QWidget* kid : visual->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        CHECK(!kid->objectName().isEmpty());
    for (QLabel* lbl : visual->findChildren<QLabel*>())
        CHECK(!lbl->wordWrap());

    auto* visualTab = w->findChild<QWidget*>(QStringLiteral("gateChainTabVisual"));
    CHECK(visualTab != nullptr);
    if (visualTab) {
        for (QScrollBar* sb : visualTab->findChildren<QScrollBar*>()) {
            if (sb->orientation() == Qt::Horizontal)
                CHECK(!sb->isVisible());
        }
    }
}

// With CHAIN_ROOF_RENDER_PREFIX set (to anything), the CHAIN card's check box
// and the VISUAL tab's band+handle are each rendered to a fixed path, so A1
// can be looked at.
void testRenderRoofWhenAsked()
{
    if (qgetenv("CHAIN_ROOF_RENDER_PREFIX").isEmpty())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withRoofing(visualFilter(), /*checkOn=*/true, 800.0, 900.0, 1200.0));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabChain);
    CHECK(w->grab().save(QStringLiteral("/tmp/chain-roof-card.png")));

    bringUp(w, kTabVisual);
    CHECK(w->grab().save(QStringLiteral("/tmp/chain-roof-visual.png")));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_roof_test"));
    QApplication app(argc, argv);

    testCheckBoxAppearsFromChecksWithRightObjectNameAndCheckedState();
    testTogglingWritesExactQueryOnAndOff();
    testNoWriteWhileAWriteIsPending();
    testRefusalReachesTheInspector();
    testARowWithoutChecksDrawsNone();
    testAMalformedCheckWithNoKeyDrawsNothingAndDoesNotCrash();
    testTheInspectorShowsTheCheckToo();
    testHandlePositionFromOffsetAppliedHz();
    testADragBeyondTheClampWritesTheClampedValue();
    testZeroMaxHzDrawsNoHandleAndTakesNoDrag();
    testReleaseWritesExactlyRoofOffsetHz();
    testEveryA1WidgetHasANameNoLabelWrapsAndNothingScrolls();
    testRenderRoofWhenAsked();

    std::printf("\n%d aether gate chain roof test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
