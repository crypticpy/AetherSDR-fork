// The FILTER page no longer fights a poll against a hand: PRESETS, shape, AGC,
// notch, contour, APF and the width drag -- everything the write-hold
// (DiversityFilterControls::kWriteHoldMs) existed to protect -- are the gate's
// own CHAIN window now (AetherGateChainWindow). POST-FILTER and MRC, the two
// stages that stayed, follow DiversityWindow's own MODE/HEAR/PAN convention
// instead: a plain click, and a check-back guarded by a QSignalBlocker rather
// than a timed hold.
//
// What is worth its own binary is the other half of that convention actually
// holding: a status update that CHECKS a button back must not, by doing so,
// ask the gate for anything -- or a poll and the click that produced it would
// loop each other forever the moment they ever disagreed for one tick. That is
// the whole subject here, for both PAIR STAGES groups.
//
// A separate binary rather than more cases in diversity_filter_test.cpp for
// the same file-size reason the original hold file was: every diversity
// window binary in this family stays near the 800-line budget AGENTS.md asks
// for, and every case wants the same fresh, process-wide AppSettings start.
//
// Same harness as diversity_filter_test.cpp: a real AetherGateApplet in front
// of a fake, socket-free QNetworkAccessManager, the window opened through the
// sidebar button because there is deliberately no other way in.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QNetworkReply>
#include <QPushButton>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
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
void connectGate(AetherGateApplet& a, FakeGate& net)
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
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
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

// /diversity is fetched once at connect, before there is a window to feed it
// to, so one applet tick follows -- the same reason
// tests/diversity_talker_test.cpp's openWindow() ticks before returning.
DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    tick(a);
    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    settle();
    return w;
}

// (a) POST-FILTER: the gate reporting V2 in force checks the V2 button without
// asking the gate for anything -- clicked(), not toggled(), is what
// DiversityFilterControls::checkValue() relies on, and this is the case that
// would fail if that ever changed to toggled() by mistake.
void testPostGroupCheckedBackFromStatusDoesNotReemit()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* off = child<QPushButton>(w, "diversityWindowFilterPostOff");
    auto* v2 = child<QPushButton>(w, "diversityWindowFilterPostV2");
    CHECK(off != nullptr && v2 != nullptr);
    if (!off || !v2)
        return;
    CHECK(off->isChecked());
    CHECK(!v2->isChecked());

    const int before = net.count(QStringLiteral("/diversity/set"));
    QByteArray withV2 = kDiversityFull;
    withV2.replace("\"capture\"",
                   "\"post\": {\"enabled\": true, \"version\": 2}, \"capture\"");
    CHECK(withV2 != kDiversityFull);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, withV2};
    tick(a);
    CHECK(v2->isChecked());
    CHECK(!off->isChecked());
    CHECK(net.count(QStringLiteral("/diversity/set")) == before);

    // MUTATION: the same check-back the other way is still silent.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    tick(a);
    CHECK(off->isChecked());
    CHECK(!v2->isChecked());
    CHECK(net.count(QStringLiteral("/diversity/set")) == before);
    closedToStart();
}

// (b) MRC: the same proof for a single checkable button rather than an
// exclusive group -- applyMrc() sets checked state under a QSignalBlocker of
// its own, by hand, because there is no group to lean on.
void testMrcButtonCheckedBackFromStatusDoesNotReemit()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* mrc = child<QPushButton>(w, "diversityWindowFilterMrc");
    CHECK(mrc != nullptr);
    if (!mrc)
        return;
    CHECK(!mrc->isChecked());

    const int before = net.count(QStringLiteral("/diversity/set"));
    QByteArray withMrc = kDiversityFull;
    withMrc.replace("\"capture\"", "\"mrc\": {\"enabled\": true}, \"capture\"");
    CHECK(withMrc != kDiversityFull);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, withMrc};
    tick(a);
    CHECK(mrc->isChecked());
    CHECK(net.count(QStringLiteral("/diversity/set")) == before);

    // MUTATION: back off is still silent.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    tick(a);
    CHECK(!mrc->isChecked());
    CHECK(net.count(QStringLiteral("/diversity/set")) == before);
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_filter_hold_test"));
    QApplication app(argc, argv);

    testPostGroupCheckedBackFromStatusDoesNotReemit();
    testMrcButtonCheckedBackFromStatusDoesNotReemit();

    std::printf("\n%d diversity filter hold test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
