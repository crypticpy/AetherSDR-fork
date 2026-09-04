// NOW -- the CHAIN window's own "one thing worth changing right now" strip
// (AetherGateChainNow.h). The eight-case ladder design §2.3 defines, the
// lit row that comes with each case, and the two name-hygiene checks the
// auto_clean chain row used to answer before it left the diagram: the
// toggle write (testToggleWritesExactlyAutoOffWhileOnAndAutoOnWhileOff) and
// the "every B25 widget has a name" sweep's own auto_clean-toggle line, both
// moved here from aether_gate_chain_auto_test.cpp where they no longer
// apply -- auto_clean no longer has a card on the diagram at all.
//
// The chain-row/governor fixture (bringUp/feedDevice/autoCleanRow/
// squeezeRow/withAutoCleanChain/held/event/backoff/ruledOut/governor/
// localEpoch) is shared with aether_gate_chain_auto_test.cpp and
// aether_gate_chain_auto_inspector_test.cpp; it lives in
// AetherGateChainAutoTestSupport.h so it is not duplicated a third time.
#include "AetherGateChainAutoTestSupport.h"
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

QPushButton* nowAction(AetherGateChainWindow* w)
{
    return w->findChild<QPushButton*>(QStringLiteral("gateChainNowAction"));
}

QLabel* nowLine(AetherGateChainWindow* w)
{
    return w->findChild<QLabel*>(QStringLiteral("gateChainNowLine"));
}

QWidget* nowStrip(AetherGateChainWindow* w)
{
    return w->findChild<QWidget*>(QStringLiteral("gateChainNowStrip"));
}

bool tileIsLit(AetherGateChainWindow* w, const QString& id)
{
    AetherGateChainTile* t = strip(w)->tile(id);
    return t && t->property("lit").toBool();
}

// --------------------------------------------------------------------------
// The ladder -- design §2.3, case by case
// --------------------------------------------------------------------------

// Case 1 -- FRONT END clipping outranks the governor entirely, even a
// governor that is happily holding something: nobody can hear what AUTO
// CLEAN found while the audio is breaking up.
void testCase1ClippingOutranksGovernorAndLightsGuard()
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
    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("0"), true, 1.5, 0,
                          QStringLiteral("idle")));
    bringUp(w);

    CHECK(nowStrip(w) != nullptr);
    CHECK(nowStrip(w)->isVisible());
    auto* line = nowLine(w);
    CHECK(line != nullptr);
    if (line)
        CHECK(line->text().contains(QStringLiteral("clipping")));
    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action) {
        CHECK(action->isVisible());
        CHECK(action->text() == QStringLiteral("TURN GUARD ON"));
    }
    CHECK(tileIsLit(w, QStringLiteral("frontend_guard")));

    int writes = net.log.size();
    if (action)
        action->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/frontend/set?guard=on"));
}

// Case 2 -- the governor stopped on a refusal; TRY AGAIN sends auto=on and
// the row named by the newest error event is lit.
void testCase2ErrorOffersTryAgainAndLightsTheErroringRow()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(localEpoch(12, 40, 1), QStringLiteral("nb"), QStringLiteral("impulse"),
             QStringLiteral("1.4/s"), QStringLiteral("error"), false)};
    connectGate(applet, net,
               withAutoCleanChain(true,
                                  governor(true, QStringLiteral("idle"), QStringLiteral("stopped"),
                                          {}, QJsonValue(), events, {}, {}, QString(),
                                          QStringLiteral("receiver refused nb"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* line = nowLine(w);
    CHECK(line != nullptr);
    if (line)
        CHECK(line->text().contains(QStringLiteral("receiver refused nb")));
    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action)
        CHECK(action->text() == QStringLiteral("TRY AGAIN"));
    CHECK(tileIsLit(w, QStringLiteral("nb")));

    int writes = net.log.size();
    if (action)
        action->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?auto=on"));
}

// Case 3 -- pending: "trying X on ROW", HAND IT BACK writes auto=off, the
// tool's own row is lit.
void testCase3PendingOffersHandItBack()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonValue pending =
        held(QStringLiteral("nb"), QStringLiteral("impulse"), QStringLiteral("1.4/s"), 100.0,
            false);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("measuring"),
                                                 QStringLiteral("trying blanker"), {}, pending)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* line = nowLine(w);
    CHECK(line != nullptr);
    if (line)
        CHECK(line->text().contains(QStringLiteral("trying")));
    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action)
        CHECK(action->text() == QStringLiteral("HAND IT BACK"));
    CHECK(tileIsLit(w, QStringLiteral("nb")));

    int writes = net.log.size();
    if (action)
        action->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?auto=off"));
}

// Case 4 -- holding: the delta and the age both appear, HAND IT BACK still
// writes auto=off. This is also the on->off half of the old toggle test.
void testCase4HoldingShowsAgeAndDeltaAndWritesAutoOff()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const double since = QDateTime::currentDateTime().toSecsSinceEpoch() - 240.0;   // 4 min
    const QJsonArray holding = {held(QStringLiteral("squeeze"), QStringLiteral("carrier"),
                                     QStringLiteral("strongest first"), since, true, 1.8)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("settling"),
                                                 QStringLiteral("holding squeeze"), holding)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* line = nowLine(w);
    CHECK(line != nullptr);
    if (line) {
        CHECK(line->text().contains(QStringLiteral("holding")));
        CHECK(line->text().contains(QStringLiteral("+1.8 dB")));
        CHECK(line->text().contains(QStringLiteral("min")));
    }
    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action)
        CHECK(action->text() == QStringLiteral("HAND IT BACK"));
    CHECK(tileIsLit(w, QStringLiteral("squeeze")));

    int writes = net.log.size();
    if (action)
        action->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?auto=off"));
}

// Case 5 -- off: AUTO CLEAN ON writes auto=on. This is the off->on half of
// the old toggle test.
void testCase5OffOffersAutoCleanOnAndWritesAutoOn()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(false,
                                  governor(false, QStringLiteral("idle"),
                                          QStringLiteral("AUTO CLEAN is off"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action)
        CHECK(action->text() == QStringLiteral("AUTO CLEAN ON"));
    CHECK(!tileIsLit(w, QStringLiteral("squeeze")));

    int writes = net.log.size();
    if (action)
        action->click();
    settle();
    CHECK(net.log.size() == writes + 1);
    CHECK(net.log.last() == QStringLiteral("/diversity/set?auto=on"));
}

// Case 6 -- idle with something ruled out: no button, the line names why.
void testCase6RuledOutNamesWhyAndHasNoButton()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray ruled = {ruledOut(QStringLiteral("nb"), QStringLiteral("nothing impulsive")),
                             ruledOut(QStringLiteral("squeeze"), QStringLiteral("no comb found"))};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), {}, {}, ruled)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* line = nowLine(w);
    CHECK(line != nullptr);
    if (line) {
        CHECK(line->text().contains(QStringLiteral("nothing impulsive")));
        CHECK(line->text().contains(QStringLiteral("no comb found")));
    }
    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action)
        CHECK(!action->isVisible());
}

// Case 7 -- plain listening: the bare sentence, no button.
void testCase7ListeningIsBareAndHasNoButton()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* line = nowLine(w);
    CHECK(line != nullptr);
    if (line)
        CHECK(line->text() == QStringLiteral("AUTO CLEAN ON · listening"));
    auto* action = nowAction(w);
    CHECK(action != nullptr);
    if (action)
        CHECK(!action->isVisible());
}

// Case 8 -- no governor at all: the whole strip hides.
void testCase8NoGovernorHidesTheWholeStrip()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, withAutoCleanChain(true));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    CHECK(nowStrip(w) != nullptr);
    if (nowStrip(w))
        CHECK(!nowStrip(w)->isVisible());
}

// --------------------------------------------------------------------------
// AUTO CLEAN no longer draws on the diagram at all
// --------------------------------------------------------------------------

void testAutoCleanHasNoTileAndNoLineInTheFrontEndCard()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"))));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    CHECK(strip(w)->tile(QStringLiteral("auto_clean")) == nullptr);
    for (const AetherSDR::ChainStage& s : strip(w)->stages())
        CHECK(s.id != QStringLiteral("auto_clean"));
}

// --------------------------------------------------------------------------
// HISTORY
// --------------------------------------------------------------------------

void testHistoryExpandsAndCollapsesInPlace()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    const QJsonArray events = {
        event(localEpoch(12, 41, 7), QStringLiteral("squeeze"), QStringLiteral("carrier"),
             QStringLiteral("strongest first"), QStringLiteral("kept"), true, 1.8)};
    connectGate(applet, net,
               withAutoCleanChain(true, governor(true, QStringLiteral("idle"),
                                                 QStringLiteral("nothing to hold"), {},
                                                 QJsonValue(), events)));
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w);

    auto* history = w->findChild<QPushButton*>(QStringLiteral("gateChainNowHistory"));
    auto* panel = w->findChild<QLabel*>(QStringLiteral("gateChainNowHistoryPanel"));
    CHECK(history != nullptr);
    CHECK(panel != nullptr);
    if (!history || !panel)
        return;

    CHECK(!panel->isVisible());
    history->click();
    settle();
    CHECK(panel->isVisible());
    CHECK(panel->text().contains(QStringLiteral("squeeze")));
    CHECK(!panel->wordWrap());

    history->click();
    settle();
    CHECK(!panel->isVisible());
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_now_test"));
    QApplication app(argc, argv);

    testCase1ClippingOutranksGovernorAndLightsGuard();
    testCase2ErrorOffersTryAgainAndLightsTheErroringRow();
    testCase3PendingOffersHandItBack();
    testCase4HoldingShowsAgeAndDeltaAndWritesAutoOff();
    testCase5OffOffersAutoCleanOnAndWritesAutoOn();
    testCase6RuledOutNamesWhyAndHasNoButton();
    testCase7ListeningIsBareAndHasNoButton();
    testCase8NoGovernorHidesTheWholeStrip();
    testAutoCleanHasNoTileAndNoLineInTheFrontEndCard();
    testHistoryExpandsAndCollapsesInPlace();

    std::printf("\n%d aether gate chain now test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
