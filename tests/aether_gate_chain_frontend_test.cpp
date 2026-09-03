// B23 -- the front-end linearity guard, on the CHAIN window's FRONT END card.
//
// The gate side (already shipped) answers GET /device with a "frontend" key:
// available, guard on/off, the floor and current LNA state, whether the dBm
// scale is still calibrated for the LNA state in force, headroom and clips
// over the last second, the guard's own state machine, and its event log.
// This is the app half: a HEADROOM row (a measured line, no control, warn
// tone under 3 dB or with a clip), a GUARD row (the one control on this
// card: on/off plus a floor), and a calibration caveat that appears only
// when a guard-moved LNA state has broken the gate's own dBm trim.
//
// Nothing here is optimistic, the same as every other control in this
// window: a click greys the control and sends one write; only a body that
// answers through applyDevice() moves it. The real /device poll is
// AetherGateApplet's, which is out of scope for this change -- every test
// here feeds AetherGateChainWindow::applyDevice() directly, the way the real
// poll will once that wiring lands.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

// One /device body with a "frontend" key, applied the way the real poll
// will once AetherGateApplet is wired up to call this.
void feedDevice(AetherGateChainWindow* w, const QJsonObject& fe)
{
    w->applyDevice(QJsonDocument::fromJson(deviceWithFrontend(fe)).object());
    settle();
}

// --------------------------------------------------------------------------
// HEADROOM
// --------------------------------------------------------------------------

void testHeadroomRowTextAndWarnToneUnderThreeDbOrWithClips()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));
    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("frontend_headroom"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    CHECK(tile->primaryText() == QStringLiteral("12.3 dB · clips 0"));
    QLabel* value = w->findChild<QLabel*>(QStringLiteral("gateChainValue_frontend_headroom"));
    CHECK(value != nullptr);
    if (value)
        CHECK(value->property("live").toBool() == false);

    // MUTATION: under 3 dB of headroom wears the warn tone.
    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("1"), true,
                           1.2, 0, QStringLiteral("idle")));
    CHECK(tile->primaryText() == QStringLiteral("1.2 dB · clips 0"));
    if (value)
        CHECK(value->property("live").toBool() == true);

    // MUTATION: plenty of headroom but a clip still wears it.
    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.0, 3, QStringLiteral("idle")));
    CHECK(tile->primaryText() == QStringLiteral("12.0 dB · clips 3"));
    if (value)
        CHECK(value->property("live").toBool() == true);
}

void testHeadroomRowAbsentWhenUnavailable()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    // kDevice carries no "frontend" key at all: absent, not dashed.
    CHECK(strip(w)->tile(QStringLiteral("frontend_headroom")) == nullptr);
    CHECK(strip(w)->tile(QStringLiteral("frontend_guard")) == nullptr);

    // MUTATION: an explicit "available": false must stay absent too, not
    // merely a body with no key at all.
    QJsonObject fe;
    fe.insert(QStringLiteral("available"), false);
    feedDevice(w, fe);
    CHECK(strip(w)->tile(QStringLiteral("frontend_headroom")) == nullptr);
    CHECK(strip(w)->tile(QStringLiteral("frontend_guard")) == nullptr);
}

// --------------------------------------------------------------------------
// GUARD -- the toggle and the floor
// --------------------------------------------------------------------------

void testTogglingGuardSendsGuardOnThenGuardOff()
{
    FakeGate net;
    net.routes[QStringLiteral("/frontend/set")] = {QNetworkReply::NoError,
                                                    QByteArrayLiteral("{}")};
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));

    auto* guardToggle = w->findChild<QPushButton*>(QStringLiteral("gateChainFrontendGuard"));
    CHECK(guardToggle != nullptr);
    if (!guardToggle)
        return;
    CHECK(!guardToggle->isChecked());

    guardToggle->click();
    settle();
    CHECK(sentQueries(net, QStringLiteral("/frontend/set")) == QStringList{QStringLiteral("guard=on")});
    // Nothing optimistic: the switch has not moved on the click alone.
    CHECK(!guardToggle->isChecked());

    // The gate confirms it: guard is on now, so the SAME control's next
    // click carries a different query.
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(guardToggle->isChecked());

    guardToggle->click();
    settle();
    const QStringList sent = sentQueries(net, QStringLiteral("/frontend/set"));
    CHECK(sent.size() == 2);
    CHECK(sent.value(0) == QStringLiteral("guard=on"));
    CHECK(sent.value(1) == QStringLiteral("guard=off"));
}

void testFloorSelectSendsFloorFour()
{
    FakeGate net;
    net.routes[QStringLiteral("/frontend/set")] = {QNetworkReply::NoError,
                                                    QByteArrayLiteral("{}")};
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.3, 0, QStringLiteral("idle")));

    auto* floor = w->findChild<QComboBox*>(QStringLiteral("gateChainFrontendFloor"));
    CHECK(floor != nullptr);
    if (!floor)
        return;
    const int idx = floor->findData(QStringLiteral("4"));
    CHECK(idx >= 0);
    if (idx < 0)
        return;
    QMetaObject::invokeMethod(floor, "activated", Qt::DirectConnection, Q_ARG(int, idx));
    settle();
    CHECK(sentQueries(net, QStringLiteral("/frontend/set")).contains(QStringLiteral("floor=4")));
}

// --------------------------------------------------------------------------
// The calibration caveat
// --------------------------------------------------------------------------

void testCalibrationNoteAppearsOnlyWhenNotCalibrated()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QLabel* note = w->findChild<QLabel*>(QStringLiteral("gateChainFrontendCalNote"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(!note->isVisible());

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(!note->isVisible());

    // MUTATION: calibrated for LNA 0, guard has moved it to 1 -- the note
    // appears, and names both states.
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(note->isVisible());
    CHECK(note->text().contains(QStringLiteral("LNA 0")));
    CHECK(note->text().contains(QStringLiteral("1")));

    // Back to calibrated hides it again.
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(!note->isVisible());
}

// --------------------------------------------------------------------------
// The inspector
// --------------------------------------------------------------------------

void testInspectorTextForTheGuardRow()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QJsonObject ev;
    ev.insert(QStringLiteral("t"), 1756871720.0);
    ev.insert(QStringLiteral("from"), QStringLiteral("0"));
    ev.insert(QStringLiteral("to"), QStringLiteral("1"));
    ev.insert(QStringLiteral("reason"), QStringLiteral("clipping"));
    QJsonArray events{ev};

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           1.2, 0, QStringLiteral("holding"), events));

    strip(w)->selectStage(QStringLiteral("frontend_guard"));
    settle();

    CHECK(labelText(w, "gateChainDetailName") == QStringLiteral("GUARD"));
    // 1. What it does.
    const QString tip = labelText(w, "gateChainDetailTip");
    CHECK(tip.contains(QStringLiteral("LNA")));
    // 2. What it is doing now -- the last EVENT, not the card's own on/off
    // sentence.
    const QString now = labelText(w, "gateChainDetailText");
    CHECK(now.contains(QStringLiteral("stepped 0")));
    CHECK(now.contains(QStringLiteral("1")));
    CHECK(now.contains(QStringLiteral("clipping")));
    // 3. The control, at full size.
    CHECK(w->findChild<QPushButton*>(QStringLiteral("gateChainDetailToggle_frontend_guard"))
          != nullptr);
    // 4. The caveat, because this body is not calibrated.
    const QString aside = labelText(w, "gateChainDetailOff");
    CHECK(aside.contains(QStringLiteral("LNA 0")));
    CHECK(aside.contains(QStringLiteral("now 1")));
}

// The fallback: no events yet, so "now" is the card's own sentence.
void testInspectorNowFallsBackToTheCardSentenceBeforeAnyEvent()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.3, 0, QStringLiteral("idle")));
    strip(w)->selectStage(QStringLiteral("frontend_guard"));
    settle();
    const QString now = labelText(w, "gateChainDetailText");
    CHECK(now.contains(QStringLiteral("on")));
    CHECK(now.contains(QStringLiteral("LNA 1")));
}

// --------------------------------------------------------------------------
// Hygiene: names, wrap, and the card's own arithmetic
// --------------------------------------------------------------------------

void testEveryNewWidgetHasANameAndNothingWraps()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           1.2, 2, QStringLiteral("stepping_up")));

    // Direct children only, and never inside a QComboBox: the bridge
    // addresses the widgets THIS window builds, and a combo's private popup
    // is Qt's. Same convention aether_gate_chain_test.cpp's
    // testEveryWidgetTheWindowBuildsHasAName() already uses.
    const auto sweep = [](QWidget* parent) {
        int missing = 0;
        for (QWidget* kid : parent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (kid->objectName().isEmpty())
                ++missing;
        return missing;
    };

    for (const QString& id :
        {QStringLiteral("frontend_headroom"), QStringLiteral("frontend_guard")}) {
        AetherGateChainTile* tile = strip(w)->tile(id);
        CHECK(tile != nullptr);
        if (!tile)
            continue;
        CHECK(sweep(tile) == 0);
        auto* control = tile->findChild<AetherGateChainControl*>(
            QString(), Qt::FindDirectChildrenOnly);
        if (control)
            CHECK(sweep(control) == 0);
        for (QLabel* l : tile->findChildren<QLabel*>())
            CHECK(!l->wordWrap());
    }

    QLabel* note = w->findChild<QLabel*>(QStringLiteral("gateChainFrontendCalNote"));
    CHECK(note != nullptr);
    if (note)
        CHECK(!note->wordWrap());

    for (const char* name : {"gateChainFrontendGuard", "gateChainFrontendFloor"}) {
        QWidget* widget = w->findChild<QWidget*>(QString::fromLatin1(name));
        CHECK(widget != nullptr);
        if (!widget)
            std::printf("  missing: %s\n", name);
    }
}

// The same check tests/aether_gate_chain_test.cpp runs at the initial size,
// with the two new rows on the card: the viewport must still be big enough
// for its own content, and neither scrollbar shows.
void testNothingScrollsWithTheTwoNewRowsOnTheCard()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           1.2, 2, QStringLiteral("stepping_up")));
    w->resize(1120, 820);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("gateChainScroll"));
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
}

// With CHAIN_FRONTEND_RENDER_PNG set, a window with the guard on, 1.2 dB of
// headroom, one event and an uncalibrated scale is written out so B23 can be
// looked at.
void testRenderFrontendWhenAsked()
{
    const QByteArray path = qgetenv("CHAIN_FRONTEND_RENDER_PNG");
    if (path.isEmpty())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);

    QJsonObject ev;
    ev.insert(QStringLiteral("t"), 1756871720.0);
    ev.insert(QStringLiteral("from"), QStringLiteral("0"));
    ev.insert(QStringLiteral("to"), QStringLiteral("1"));
    ev.insert(QStringLiteral("reason"), QStringLiteral("clipping"));
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                          1.2, 1, QStringLiteral("stepping_up"), QJsonArray{ev}));
    strip(w)->selectStage(QStringLiteral("frontend_guard"));
    settle();
    w->grab();
    settle();
    CHECK(w->grab().save(QString::fromLocal8Bit(path)));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_frontend_test"));
    QApplication app(argc, argv);

    testHeadroomRowTextAndWarnToneUnderThreeDbOrWithClips();
    testHeadroomRowAbsentWhenUnavailable();
    testTogglingGuardSendsGuardOnThenGuardOff();
    testFloorSelectSendsFloorFour();
    testCalibrationNoteAppearsOnlyWhenNotCalibrated();
    testInspectorTextForTheGuardRow();
    testInspectorNowFallsBackToTheCardSentenceBeforeAnyEvent();
    testEveryNewWidgetHasANameAndNothingWraps();
    testNothingScrollsWithTheTwoNewRowsOnTheCard();
    testRenderFrontendWhenAsked();

    std::printf("\n%d aether gate chain frontend test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
