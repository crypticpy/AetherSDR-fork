// The FILTER page's write hold, and the APF block's CW-only visibility --
// both from the same live-round bug report: "when I select contour and auto
// and tone it selects and deselects sometimes; I have to click a couple times
// to get it to turn on," and "APF is still in the interface without the CW
// settings."
//
// The first is a race, not a rendering bug: /filter is polled once a second,
// so a poll already in flight at the moment of a click can land AFTER the
// click carrying the value from BEFORE it, painting the control back to where
// it used to be for one tick. DiversityFilterControls::kWriteHoldMs is the
// fix -- see the header comment beside it -- and what is checked here is the
// hold's three edges: a stale echo is ignored, a matching echo clears it, and
// an expired one stops blocking at all.
//
// An eighth binary rather than more cases in diversity_filter_test.cpp (764
// lines) or diversity_filter_layout_test.cpp for the reason every one of the
// seven before it is separate: each is at or near the 800-line budget AGENTS
// .md asks for, and every window case wants the same fresh, process-wide
// AppSettings start.
//
// Same harness as diversity_filter_test.cpp: a real AetherGateApplet in front
// of a fake, socket-free QNetworkAccessManager, the window opened through the
// sidebar button because there is deliberately no other way in, and the band
// poller driven by hand rather than by waiting out real seconds.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QNetworkReply>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversityWindow;

using namespace DiversityGateFixture;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Gate present, diversity live, every route the FILTER page needs answered.
// /filter/set replies with the same status object a poll returns, which is
// what the real gate does -- and which is also exactly the situation the hold
// exists for: the write's own reply is a poll landing with the OLD value,
// because this fake gate (like the real one) has not moved yet when it answers
// the request that is about to move it.
void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& filter = kDiversityFilterStatus)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                        kDiversitySpatial};
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError,
                                                       kDiversityFinder};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeacons};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filter};
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, filter};
    net.routes[QStringLiteral("/filter/notch")] = {QNetworkReply::NoError, filter};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

// One more tick of the band poller, without waiting out half a second of real
// time. The FILTER page's /filter read rides on the same timer.
void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageFilter"))->click();
    settle();
    settle();
    return w;
}

// (a) A checkbox clicked on survives a stale echo of its own write, stays put
// once the matching one arrives and clears the hold, and then follows the
// very next poll -- which is the flicker turning back into an ordinary read.
void testCheckboxHoldSurvivesAStaleEchoThenFollowsOnceCleared()
{
    closedToStart();
    QByteArray anfOff = kDiversityFilterStatus;
    anfOff.replace("\"anf\": {\"enabled\": true", "\"anf\": {\"enabled\": false");
    CHECK(anfOff != kDiversityFilterStatus);

    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, anfOff);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* anf = child<QCheckBox>(w, "diversityWindowFilterAnfCheck");
    CHECK(anf != nullptr);
    if (!anf)
        return;
    CHECK(!anf->isChecked());

    // Click on. The write's own reply is this same fake gate's "anf off"
    // route -- the stale echo, landing on the very next settle() -- and the
    // hold is what keeps it from being believed.
    anf->click();
    CHECK(anf->isChecked());   // clicking a checkable button checks it at once
    settle();
    CHECK(anf->isChecked());
    CHECK(anf->property("pendingUntil").isValid());

    // MUTATION: a poll that actually echoes the click. The hold clears -- and
    // the box does not move, because the echo agrees with where it already is.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    filterTick(a);
    CHECK(anf->isChecked());
    CHECK(!anf->property("pendingUntil").isValid());

    // MUTATION: the hold is gone, so an ordinary poll is believed again --
    // including one that disagrees with the click, which is the box actually
    // following the gate rather than being stuck on the operator's last word.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, anfOff};
    filterTick(a);
    CHECK(!anf->isChecked());
    closedToStart();
}

// (b) The same fight for a spin box committed on editingFinished, and for a
// button in an exclusive group -- both go through the same three-beat proof
// as the checkbox above: stale echo held, matching echo clears the hold, next
// poll is believed.
void testSpinAndShapeGroupHoldsSurviveAStaleEchoThenFollow()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // --- the low-edge spin, 100 -> 450 -------------------------------------
    auto* low = child<QSpinBox>(w, "diversityWindowFilterLowSpin");
    auto* high = child<QSpinBox>(w, "diversityWindowFilterHighSpin");
    CHECK(low != nullptr && high != nullptr);
    if (!low || !high)
        return;
    CHECK(low->value() == 100);

    low->setFocus(Qt::OtherFocusReason);
    settle();
    low->setValue(450);
    emit low->editingFinished();
    // Focus moves off the spin before the reply lands: writeSpin()'s focus
    // rule would otherwise mask the hold rule this case exists to prove.
    high->setFocus(Qt::OtherFocusReason);
    settle();
    CHECK(low->value() == 450);   // the stale echo (set_low_hz: 100) is held

    QByteArray low450 = kDiversityFilterStatus;
    low450.replace("\"set_low_hz\": 100", "\"set_low_hz\": 450");
    CHECK(low450 != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, low450};
    filterTick(a);
    CHECK(low->value() == 450);
    CHECK(!low->property("pendingUntil").isValid());

    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, kDiversityFilterStatus};
    filterTick(a);
    CHECK(low->value() == 100);

    // --- the shape group, sharp -> soft -------------------------------------
    auto* soft = child<QPushButton>(w, "diversityWindowFilterShapesoft");
    auto* sharp = child<QPushButton>(w, "diversityWindowFilterShapesharp");
    CHECK(soft != nullptr && sharp != nullptr);
    if (!soft || !sharp)
        return;
    CHECK(sharp->isChecked());

    soft->click();
    CHECK(soft->isChecked());
    settle();   // the write's own reply is the default fixture: shape "sharp"
    CHECK(soft->isChecked());

    // MUTATION: a poll that echoes "soft" -- the auto-spectrum fixture -- both
    // clears the hold and, unlike the spin case above, changes several other
    // readouts on the page at once. None of that is asserted here: only the
    // one control this case is about.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK(soft->isChecked());

    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, kDiversityFilterStatus};
    filterTick(a);
    CHECK(sharp->isChecked());
    closedToStart();
}

// (c) A hold that has passed does not block at all -- the operator's click is
// forgotten and the next poll's answer is simply believed, exactly as it
// would be with no hold in the way.
void testExpiredHoldStopsBlocking()
{
    closedToStart();
    QByteArray anfOff = kDiversityFilterStatus;
    anfOff.replace("\"anf\": {\"enabled\": true", "\"anf\": {\"enabled\": false");
    CHECK(anfOff != kDiversityFilterStatus);

    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, anfOff);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* anf = child<QCheckBox>(w, "diversityWindowFilterAnfCheck");
    CHECK(anf != nullptr);
    if (!anf)
        return;

    anf->click();
    settle();
    CHECK(anf->isChecked());
    CHECK(anf->property("pendingUntil").isValid());

    // Back-date the hold instead of waiting out 1.5 real seconds: the property
    // is the whole mechanism, so this is the same thing a clock would do.
    anf->setProperty("pendingUntil", QDateTime::currentMSecsSinceEpoch() - 1);
    filterTick(a);   // still the "anf off" route -- the old value, unheld now
    CHECK(!anf->isChecked());
    CHECK(!anf->property("pendingUntil").isValid());
    closedToStart();
}

// (d) The APF block -- checkbox and Hz/W row together -- is hidden for every
// mode except CW, matched case-insensitively, and comes back the moment the
// mode does.
void testApfBlockIsVisibleOnlyInCwMode()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* block = w->findChild<QWidget*>(QStringLiteral("diversityWindowFilterApfBlock"));
    CHECK(block != nullptr);
    if (!block)
        return;
    // The default fixture is LSB: APF has nothing to say and stays out of the
    // way.
    CHECK(!block->isVisible());

    QByteArray cw = kDiversityFilterStatus;
    cw.replace("\"mode\": \"lsb\"", "\"mode\": \"cw\"");
    CHECK(cw != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, cw};
    filterTick(a);
    CHECK(block->isVisible());

    // MUTATION: the gate's own case does not matter -- only the word does.
    QByteArray cwUpper = kDiversityFilterStatus;
    cwUpper.replace("\"mode\": \"lsb\"", "\"mode\": \"CW\"");
    CHECK(cwUpper != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, cwUpper};
    filterTick(a);
    CHECK(block->isVisible());

    // MUTATION: back to a voice mode and the block is gone again, without the
    // window being reopened.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, kDiversityFilterStatus};
    filterTick(a);
    CHECK(!block->isVisible());
    closedToStart();
}

} // namespace

// A width preset lights when the asked-for width is its span, and none
// lights for a custom width. A click lights at once and holds through a
// stale echo; the gate's own width decides once the hold is off.
void testWidthPresetLightsFromTheStatusAndHoldsAfterAClick()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* p2400 = child<QPushButton>(w, "diversityWindowFilterPreset2400");
    auto* p2700 = child<QPushButton>(w, "diversityWindowFilterPreset2700");
    CHECK(p2400 != nullptr && p2700 != nullptr);
    if (!p2400 || !p2700)
        return;
    CHECK(!p2400->isChecked() && !p2700->isChecked());   // 100..2900 is custom

    p2400->click();                                       // lit at once...
    settle();
    CHECK(p2400->isChecked());
    filterTick(a);                                        // ...through the stale echo
    CHECK(p2400->isChecked());

    QByteArray high2500 = kDiversityFilterStatus;         // the gate took it
    high2500.replace("\"set_high_hz\": 2900", "\"set_high_hz\": 2500");
    CHECK(high2500 != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, high2500};
    filterTick(a);
    CHECK(p2400->isChecked() && !p2700->isChecked());

    QByteArray high2800 = kDiversityFilterStatus;         // someone else moved it
    high2800.replace("\"set_high_hz\": 2900", "\"set_high_hz\": 2800");
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, high2800};
    filterTick(a);
    CHECK(!p2400->isChecked() && p2700->isChecked());

    // MUTATION: a click on the lit preset keeps it lit (a plain checkable
    // button would toggle it off).
    p2700->click();
    settle();
    CHECK(p2700->isChecked() && !p2400->isChecked());
    closedToStart();
}

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_filter_hold_test"));
    QApplication app(argc, argv);

    testCheckboxHoldSurvivesAStaleEchoThenFollowsOnceCleared();
    testSpinAndShapeGroupHoldsSurviveAStaleEchoThenFollow();
    testExpiredHoldStopsBlocking();
    testApfBlockIsVisibleOnlyInCwMode();
    testWidthPresetLightsFromTheStatusAndHoldsAfterAClick();

    std::printf("\n%d diversity filter hold test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
