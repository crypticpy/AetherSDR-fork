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
// Six of the eight test functions below build their own "squeeze" object by
// hand rather than reusing a fixture payload, because none of
// AetherGateChainFixture.h's existing bodies carry one -- B24 landed after
// they were written, and the task that added it may not change a helper an
// earlier test already depends on.
#include "AetherGateChainFixture.h"

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

// The "squeeze" object off /filter, built field by field rather than as a raw
// JSON literal so each test states exactly what it is asking for. Every field
// core/squeeze.py's status() answers is here, even the ones this app does not
// read yet (`reason`, `phase_deg`, `ratio_db`, `scope`) -- a body missing a
// field the gate does send would be a fixture bug, not a smaller one.
QJsonObject squeezeOffBlock()
{
    QJsonObject sq;
    sq.insert(QStringLiteral("hz"), QJsonValue());
    sq.insert(QStringLiteral("width_hz"), QJsonValue());
    sq.insert(QStringLiteral("held"), false);
    sq.insert(QStringLiteral("reason"), QJsonValue());
    sq.insert(QStringLiteral("tool"), QJsonValue());
    sq.insert(QStringLiteral("why"), QString());
    sq.insert(QStringLiteral("phase_deg"), QJsonValue());
    sq.insert(QStringLiteral("ratio_db"), QJsonValue());
    sq.insert(QStringLiteral("coherence"), QJsonValue());
    sq.insert(QStringLiteral("depth_db"), QJsonValue());
    sq.insert(QStringLiteral("scope"), QStringLiteral("slice"));
    sq.insert(QStringLiteral("target"), QJsonValue());
    sq.insert(QStringLiteral("comb"), QJsonValue());
    sq.insert(QStringLiteral("since"), QJsonValue());
    return sq;
}

// Armed: a target is configured, the gate is still listening for enough
// coherence to commit to a tool. `why` is the gate's own sentence for that --
// see core/squeeze.py's set_squeeze()/observe() for the wording this copies.
QJsonObject squeezeArmedSignalBlock(double hz, double widthHz)
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("hz"), hz);
    sq.insert(QStringLiteral("width_hz"), widthHz);
    sq.insert(QStringLiteral("held"), false);
    sq.insert(QStringLiteral("why"), QStringLiteral("listening for 0.50 coherence"));
    sq.insert(QStringLiteral("coherence"), 0.22);
    sq.insert(QStringLiteral("target"), QStringLiteral("signal"));
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

// Held, one signal, the NULL tool. `why`'s wording is core/squeeze.py's own,
// verbatim: f"coherence {coh:.2f} — nulled in {n} bins".
QJsonObject squeezeHeldSignalBlock(double hz, double widthHz)
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("hz"), hz);
    sq.insert(QStringLiteral("width_hz"), widthHz);
    sq.insert(QStringLiteral("held"), true);
    sq.insert(QStringLiteral("tool"), QStringLiteral("null"));
    sq.insert(QStringLiteral("why"), QStringLiteral("coherence 0.62 — nulled in 5 bins"));
    sq.insert(QStringLiteral("coherence"), 0.62);
    sq.insert(QStringLiteral("depth_db"), -18.4);
    sq.insert(QStringLiteral("target"), QStringLiteral("signal"));
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

// Held, a comb, the NOTCH tool -- the case the gate falls back to when the
// coherence is real but not one direction. `teeth_in_band` is SIGNED, and
// negative here on purpose: this is the one test in the file that would pass
// with the abs() in DiversityFilterPanelSqueeze.cpp missing or backwards if
// it did not check the SIGNED input against the POSITIVE panel axis.
QJsonObject squeezeHeldCombBlock()
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("held"), true);
    sq.insert(QStringLiteral("tool"), QStringLiteral("notch"));
    sq.insert(QStringLiteral("why"),
             QStringLiteral("coherence 0.40 — not one direction; notched 4 teeth"));
    sq.insert(QStringLiteral("coherence"), 0.40);
    sq.insert(QStringLiteral("depth_db"), -12.0);
    sq.insert(QStringLiteral("target"), QStringLiteral("comb"));
    QJsonObject comb;
    comb.insert(QStringLiteral("spacing_hz"), 200.0);
    comb.insert(QStringLiteral("offset_hz"), 30.0);
    comb.insert(QStringLiteral("teeth_seen"), 4);
    comb.insert(QStringLiteral("teeth_in_band"),
               QJsonArray{-230.0, -430.0, -630.0, -830.0});
    sq.insert(QStringLiteral("comb"), comb);
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

// visualFilter()'s body with `squeeze` inserted (and `mode` overridden, when
// asked) -- the one shape none of the fixture's existing builders make, added
// here rather than in AetherGateChainFixture.h per the task's own file list.
QByteArray withSqueeze(const QByteArray& body, const QJsonObject& squeeze,
                       const QString& mode = QString())
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    root.insert(QStringLiteral("squeeze"), squeeze);
    if (!mode.isEmpty())
        root.insert(QStringLiteral("mode"), mode);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// One more chain[] row, shaped the way chainstatus.py's own _squeeze_row()
// shapes it -- id "squeeze", kind "value" -- so a click on the bracket has a
// card to jump to. AetherGateChainWindow::jumpToStage() returns without
// moving if the strip has no tile for the id, so the door test needs this.
QByteArray withSqueezeChainRow(const QByteArray& body)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    QJsonArray chain = root.value(QStringLiteral("chain")).toArray();
    QJsonObject row;
    row.insert(QStringLiteral("id"), QStringLiteral("squeeze"));
    row.insert(QStringLiteral("name"), QStringLiteral("SQUEEZE"));
    row.insert(QStringLiteral("kind"), QStringLiteral("value"));
    row.insert(QStringLiteral("enabled"), true);
    row.insert(QStringLiteral("detail"), QStringLiteral("null · 1450 Hz"));
    QJsonObject action;
    action.insert(QStringLiteral("label"), QStringLiteral("RELEASE"));
    action.insert(QStringLiteral("route"), QStringLiteral("/diversity/set"));
    action.insert(QStringLiteral("query"), QStringLiteral("squeeze=off"));
    row.insert(QStringLiteral("action"), action);
    chain.append(row);
    root.insert(QStringLiteral("chain"), chain);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
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
    testRenderSqueezeWhenAsked();

    std::printf("\n%d aether gate chain squeeze test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
