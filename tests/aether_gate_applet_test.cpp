// AetherGateApplet — the presence state machine and the controls it builds,
// socket-free.
//
// The applet is handed a QNetworkAccessManager whose createRequest() answers
// every URL from a canned table, so each "the gate said X" below is an injected
// reply rather than a peer: no port is opened, nothing is listened on, and a
// wrong answer fails the assertion instead of hanging on a socket.
//
// Every row here is a defect the #5372 review found (or a mutation that would
// re-open one): presence on any HTTP 200, the button that outlives its radio,
// the probe that stops forever after three misses, and numeric controls that
// clamp a device's value to a guessed range.
//
// Everything the diversity section polls is the other file:
// tests/aether_gate_applet_diversity_test.cpp.

#include "AetherGateAppletFixture.h"

#include "TestSettingsProfile.h"
#include "gui/AetherGateChainWindow.h"
#include "gui/AetherGateDiversityPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QWidget>

#include <cstdio>

using namespace AetherGateAppletFixture;

namespace {

// ---------------------------------------------------------------------------

void testNoAddressNeverAsks()
{
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    tick(a);
    CHECK(net.log.isEmpty());
    CHECK(!pollTimer(a)->isActive());
    CHECK(!a.gatePresent());
}

// An older gate: /status without "res", /device falling through to HTML.
// It is still a gate — present, resolution rows folded away, and a hint
// instead of a silently empty controls box.
void testOldGateIsPresentWithoutResolutionOrControls()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kOldStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kHtml, "text/html"};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(spy.count() == 1 && spy.at(0).at(0).toBool());
    CHECK(net.count(QStringLiteral("/status")) == 1);
    CHECK(net.count(QStringLiteral("/device")) == 1);
    CHECK(a.findChild<QLabel*>(QStringLiteral("gateStatusLabel"))->text().contains(
        QStringLiteral("waiting")));
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateResolutionBox"))->isHidden());
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDeviceBox"))->isHidden());
    CHECK(!a.findChild<QLabel*>(QStringLiteral("gateDeviceHint"))->isHidden());
}

// A port that answers HTTP 200 with something other than a gate's JSON (the
// old code took any success as presence) never confirms one, and the applet
// keeps re-asking slowly instead of giving up.
void testNonJsonAnswerIsNotAGate()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kHtml, "text/html"};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    for (int i = 0; i < 4; ++i)
        tick(a);

    CHECK(!a.gatePresent());
    CHECK(spy.isEmpty());
    CHECK(pollTimer(a)->isActive());
    CHECK(pollTimer(a)->interval() == 15000);
}

// A current gate: resolution rows populated, the running rate picked by its
// RAW value where two labels collide, and every control typed and bounded by
// what the gate reported — with a value beyond the old ±1000 surviving intact.
void testNewGateBuildsTypedBoundedControls()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(!a.findChild<QWidget*>(QStringLiteral("gateResolutionBox"))->isHidden());
    CHECK(a.findChild<QLabel*>(QStringLiteral("gateDeviceHint"))->isHidden());
    CHECK(!a.findChild<QWidget*>(QStringLiteral("gateDeviceBox"))->isHidden());

    auto* span = a.findChild<QComboBox*>(QStringLiteral("gateSpanCombo"));
    CHECK(span->count() == 3);
    CHECK(span->itemText(0) == span->itemText(1));        // the label collision
    CHECK(span->currentIndex() == 1);                     // raw 2000400, not 2000000
    CHECK(span->isEnabled());

    auto* bins = a.findChild<QComboBox*>(QStringLiteral("gateBinsCombo"));
    CHECK(bins->count() == 5);
    CHECK(bins->currentText() == QStringLiteral("1024"));

    auto* antenna = a.findChild<QComboBox*>(QStringLiteral("gateAntennaCombo"));
    CHECK(antenna && antenna->currentText() == QStringLiteral("Antenna B"));

    auto* agc = setting<QSpinBox>(a, "agc_setpoint");
    CHECK(agc && agc->minimum() == -72 && agc->maximum() == -20 && agc->value() == -30);

    auto* corr = setting<QDoubleSpinBox>(a, "corr_ppm");
    CHECK(corr && corr->decimals() == 1 && corr->minimum() == -1000.0
          && corr->maximum() == 1000.0 && corr->value() == 2.5);

    auto* lna = setting<QSpinBox>(a, "lna_state");
    CHECK(lna && lna->value() == 2500);

    auto* bias = setting<QCheckBox>(a, "biasT_ctrl");
    CHECK(bias && !bias->isChecked());

    auto* ifMode = setting<QComboBox>(a, "if_mode");
    CHECK(ifMode && ifMode->currentText() == QStringLiteral("Low-IF"));

    auto* note = setting<QLineEdit>(a, "note");
    CHECK(note && note->text() == QStringLiteral("hello"));

    // Populating from the gate must never look like an operator choice.
    CHECK(net.count(QStringLiteral("/device/set")) == 0);
    CHECK(net.count(QStringLiteral("/resolution")) == 0);
}

// One operator change → one write; the read-back that arrives with the reply
// updates the control without echoing a second write.
void testPushWritesOnceAndReadBackDoesNotEcho()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    QByteArray after = kDevice;
    after.replace("\"value\": \"false\"", "\"value\": \"true\"");
    net.routes[QStringLiteral("/device/set")] = {QNetworkReply::NoError, after};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* bias = setting<QCheckBox>(a, "biasT_ctrl");
    CHECK(bias && !bias->isChecked());
    bias->setChecked(true);
    settle();

    CHECK(net.count(QStringLiteral("/device/set")) == 1);
    CHECK(net.log.contains(QStringLiteral("/device/set?key=biasT_ctrl&value=true")));
    CHECK(bias->isChecked());

    // The read-back must not be re-sent, and a further poll's refresh of the
    // same values must not be either.
    tick(a);
    CHECK(net.count(QStringLiteral("/device/set")) == 1);
}

// Three refused polls off screen: absent, button gone — but the probe keeps
// going at the slow cadence, and the gate's return is noticed unaided.
void testBlipRecoversWithoutHelp()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());
    CHECK(!pollTimer(a)->isActive());        // hidden and found: nothing to learn

    net.down = true;
    tick(a);
    tick(a);
    CHECK(a.gatePresent());                  // two misses are a blip
    tick(a);
    CHECK(!a.gatePresent());                 // three are an absence
    CHECK(spy.count() == 2 && !spy.at(1).at(0).toBool());
    CHECK(pollTimer(a)->isActive());         // ...but the door stays open
    CHECK(pollTimer(a)->interval() == 15000);

    net.down = false;
    tick(a);
    CHECK(a.gatePresent());
    CHECK(spy.count() == 3 && spy.at(2).at(0).toBool());
    CHECK(!pollTimer(a)->isActive());
}

// On screen the cadence is one second; hiding a found gate parks the timer.
void testVisibleCadence()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    const int before = net.count(QStringLiteral("/status"));

    a.show();
    settle();
    CHECK(net.count(QStringLiteral("/status")) == before + 1);   // showEvent polls at once
    CHECK(pollTimer(a)->isActive());
    CHECK(pollTimer(a)->interval() == 1000);

    a.hide();
    CHECK(!pollTimer(a)->isActive());
}

// The radio's down edge drops presence at once and stops every write; the
// same address coming back is asked again from scratch.
void testDownEdgeDropsPresenceAndReprobesOnReturn()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/device/set")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());

    a.setRadioAddress(QString());
    CHECK(!a.gatePresent());
    CHECK(spy.count() == 2 && !spy.at(1).at(0).toBool());
    CHECK(!pollTimer(a)->isActive());

    // The widgets are still alive under the cursor for a moment; a change on
    // one must not go anywhere.
    const int writes = net.log.size();
    if (auto* bias = setting<QCheckBox>(a, "biasT_ctrl"))
        bias->setChecked(true);
    settle();
    CHECK(net.log.size() == writes);
    tick(a);
    CHECK(net.log.size() == writes);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());
    CHECK(spy.count() == 3 && spy.at(2).at(0).toBool());
}

// The Diversity window's FILTER page carries an OPEN CHAIN button of its own
// (the neighbouring stages are drawn there, so that is where an operator looks
// for them), and it has to reach the SAME window the sidebar door opens. The
// window is built on first use, so a request that arrived nowhere would leave
// that button doing nothing at all -- and nothing on screen would say so.
void testThePanelsOpenChainRequestOpensTheChainWindow()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    CHECK(a.gatePresent());
    CHECK(a.chainWindow() == nullptr);        // lazy: not built until it is asked for

    emit a.diversityPanel()->requestOpenChain();
    settle();
    CHECK(a.chainWindow() != nullptr);
    if (a.chainWindow())
        CHECK(a.chainWindow()->isVisible());
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_applet_test"));
    QApplication app(argc, argv);

    testNoAddressNeverAsks();
    testOldGateIsPresentWithoutResolutionOrControls();
    testNonJsonAnswerIsNotAGate();
    testNewGateBuildsTypedBoundedControls();
    testPushWritesOnceAndReadBackDoesNotEcho();
    testBlipRecoversWithoutHelp();
    testVisibleCadence();
    testDownEdgeDropsPresenceAndReprobesOnReturn();
    testThePanelsOpenChainRequestOpensTheChainWindow();

    std::printf("\n%d aether gate applet test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
