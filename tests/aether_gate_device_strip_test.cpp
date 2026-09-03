// The gate applet's device line and its two diversity controls (B13) --
// driven through a real AetherGateApplet and socket-free.
//
// The operator could see that a gate was answering and never what it had
// plugged in, and once diversity was running the only way back to one tuner
// was the gate's own web page. So: a line naming the device, a DIVERSITY
// switch, and an A/B selector for which tuner is left feeding the receiver.
//
// One assertion each: a duo reports its label and both controls with the
// states the gate reported; a single-tuner device gets the line and no
// controls; a gate that predates the "device" field gets a dash; leaving
// diversity sends mode=off on the selected tuner and picking a tuner sends the
// same write with that tuner; entering it sends the combined trio; and a click
// moves nothing on screen until the gate says so.
//
// Same harness as tests/aether_gate_chain_test.cpp: the applet takes the
// injected QNetworkAccessManager, so nothing here opens a port.
#include "AetherGateChainFixture.h"

#include "gui/AetherGateDeviceStrip.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>

#include <cstdio>

using AetherSDR::AetherGateDeviceStrip;
using namespace AetherGateChainFixture;

namespace {

// The RSPduo with both tuners in the sum -- the state B13 is about, because it
// is the one the app had no way out of.
const QByteArray kDuoDiversity = R"JSON({"driver": "sdrplay", "model": "RSPduo",
    "serial": "2405055D34", "hardware_key": "rspduo-2405055D34", "tuners": 2,
    "diversity": {"capable": true, "running": true, "mode": "track",
                  "tuner": "both"},
    "label": "RSPduo 2405055D34 - diversity (track)"})JSON";

// The same duo stopped, on tuner B.
const QByteArray kDuoTunerB = R"JSON({"driver": "sdrplay", "model": "RSPduo",
    "serial": "2405055D34", "hardware_key": "rspduo-2405055D34", "tuners": 2,
    "diversity": {"capable": true, "running": false, "mode": "off",
                  "tuner": "b"},
    "label": "RSPduo 2405055D34 - tuner B"})JSON";

// A device that cannot combine anything.
const QByteArray kSingleTuner = R"JSON({"driver": "sdrplay", "model": "RSPdx",
    "serial": "2405xxxx", "hardware_key": "rspdx-2405xxxx", "tuners": 1,
    "diversity": {"capable": false, "running": false, "mode": "off",
                  "tuner": "a"},
    "label": "RSPdx 2405xxxx - single tuner"})JSON";

// /status with a "device" object spliced in ahead of the fields the applet
// already reads, so the rest of the applet behaves exactly as it does today.
QByteArray statusWith(const QByteArray& device)
{
    return QByteArray("{\"device\": ") + device + QByteArray(", ") + kStatus.mid(1);
}

void connectWith(AetherGateApplet& a, FakeGate& net, const QByteArray& status)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QLabel* deviceLabel(AetherGateApplet& a)
{
    return a.findChild<QLabel*>(QStringLiteral("gateDeviceLabel"));
}

QPushButton* control(AetherGateApplet& a, const char* name)
{
    return a.findChild<QPushButton*>(QString::fromLatin1(name));
}

// ---------------------------------------------------------------------------

void testADuoReportsItsLabelAndBothControls()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, statusWith(kDuoDiversity));

    QLabel* label = deviceLabel(applet);
    CHECK(label != nullptr);
    CHECK(label && label->text().contains(
                       QStringLiteral("RSPduo 2405055D34 - diversity (track)")));
    // Never wrapped: the gate's label is a sentence, and a wrapped one would
    // push every row below it down a line each time the mode changed.
    CHECK(label && !label->wordWrap());

    QPushButton* toggle = control(applet, "gateDiversityToggle");
    QPushButton* a = control(applet, "gateTunerA");
    QPushButton* b = control(applet, "gateTunerB");
    CHECK(toggle != nullptr && a != nullptr && b != nullptr);
    if (!toggle || !a || !b)
        return;
    CHECK(!toggle->isHidden() && !a->isHidden() && !b->isHidden());
    // Straight off device.diversity.running / .tuner: with both tuners in the
    // sum there is no single tuner selected, and no choice to make yet.
    CHECK(toggle->isChecked());
    CHECK(!a->isChecked() && !b->isChecked());
    CHECK(!a->isEnabled() && !b->isEnabled());
}

void testASingleTunerDeviceGetsTheLineAndNoControls()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, statusWith(kSingleTuner));

    QLabel* label = deviceLabel(applet);
    CHECK(label && label->text().contains(
                       QStringLiteral("RSPdx 2405xxxx - single tuner")));
    // A switch that would write to a device with nothing to combine is worse
    // than no switch: it goes away rather than sitting there dead.
    CHECK(control(applet, "gateDiversityToggle")->isHidden());
    CHECK(control(applet, "gateTunerA")->isHidden());
    CHECK(control(applet, "gateTunerB")->isHidden());
}

void testAGateThatNeverSaysWhatItHasGetsADash()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, kStatus);          // no "device" at all

    QLabel* label = deviceLabel(applet);
    CHECK(label && label->text() == QStringLiteral("device: -"));
    CHECK(control(applet, "gateDiversityToggle")->isHidden());
    CHECK(control(applet, "gateTunerA")->isHidden());
}

void testLeavingDiversitySendsOffOnATunerAndMovesNothingYet()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, statusWith(kDuoDiversity));

    QPushButton* toggle = control(applet, "gateDiversityToggle");
    CHECK(toggle != nullptr);
    if (!toggle)
        return;
    toggle->click();
    settle();
    // The gate's own three parameters, in one write, so the mode and the tuner
    // can never disagree about what "off" meant. "both" is not a tuner, so the
    // write falls back to A.
    CHECK(lastRequest(net)
          == QStringLiteral("/diversity/set?mode=off&source=a&pan=a"));
    // ...and the switch still shows what the receiver IS doing. The next poll
    // is what moves it (nothing here is optimistic).
    CHECK(toggle->isChecked());
}

void testPickingATunerSendsOffAndThatTuner()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, statusWith(kDuoTunerB));

    QPushButton* a = control(applet, "gateTunerA");
    QPushButton* b = control(applet, "gateTunerB");
    CHECK(a != nullptr && b != nullptr);
    if (!a || !b)
        return;
    // Stopped: the choice exists now, and it reads off device.diversity.tuner.
    CHECK(a->isEnabled() && b->isEnabled());
    CHECK(!a->isChecked() && b->isChecked());

    a->click();
    settle();
    CHECK(lastRequest(net)
          == QStringLiteral("/diversity/set?mode=off&source=a&pan=a"));
    // Still B until the gate answers otherwise.
    CHECK(!a->isChecked() && b->isChecked());
}

void testEnteringDiversitySendsTheCombinedTrio()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, statusWith(kDuoTunerB));

    QPushButton* toggle = control(applet, "gateDiversityToggle");
    CHECK(toggle != nullptr && !toggle->isChecked());
    if (!toggle)
        return;
    toggle->click();
    settle();
    CHECK(lastRequest(net)
          == QStringLiteral("/diversity/set?mode=track&source=combined&pan=combined"));
    CHECK(!toggle->isChecked());
}

void testEveryWidgetTheStripBuildsHasAName()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectWith(applet, net, statusWith(kDuoDiversity));

    auto* strip = applet.findChild<AetherGateDeviceStrip*>(
        QStringLiteral("gateDeviceStrip"));
    CHECK(strip != nullptr);
    if (!strip)
        return;
    const QList<QWidget*> children = strip->findChildren<QWidget*>();
    for (QWidget* w : children)
        CHECK(!w->objectName().isEmpty());
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_device_strip_test"));
    QApplication app(argc, argv);

    testADuoReportsItsLabelAndBothControls();
    testASingleTunerDeviceGetsTheLineAndNoControls();
    testAGateThatNeverSaysWhatItHasGetsADash();
    testLeavingDiversitySendsOffOnATunerAndMovesNothingYet();
    testPickingATunerSendsOffAndThatTuner();
    testEnteringDiversitySendsTheCombinedTrio();
    testEveryWidgetTheStripBuildsHasAName();

    std::printf("\n%d aether gate device strip test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
