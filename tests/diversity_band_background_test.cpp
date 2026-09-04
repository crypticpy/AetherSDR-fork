// The Diversity window's BAND page, the BACKGROUND half of its contract --
// driven through a real AetherGateApplet and socket-free, same as
// tests/diversity_band_test.cpp.
//
// A separate binary from diversity_band_test for the reason every window
// binary here is its own: that file is already at the 800-line budget
// AGENTS.md asks for (see its own header comment), and this half needs the
// same fresh, process-wide AppSettings start every window case does.
//
// What is under test here is the operator's actual complaint: the spatial
// waterfall and the FINDER table used to start from nothing every time the
// window was opened onto BAND, because /diversity/spatial and
// /diversity/finder were only ever polled while that one page was actually on
// screen. DiversityBandPoller::setBandAvailable() now keeps both warm in the
// BACKGROUND -- at 1 Hz, on its own QTimer, whenever the window exists and
// the gate reports a dual-tuner pair -- regardless of which page (if any) is
// showing, so BAND already has history by the time the operator opens onto
// it. Four things are under test:
//
//   * the background poll keeps going with the window hidden;
//   * the waterfall's history survives a hide/show, because it was never
//     asked to forget anything -- only DiversitySpatialWaterfall's own
//     span-change rule does that;
//   * a span change (a different "points" count) still clears it, exactly
//     as it always did;
//   * two different kinds still get two different colours in the FINDER
//     table -- the other half of the operator's complaint, that everything
//     used to render blue (see DiversityFinderPanel.cpp's kindToken()).
//
// A fifth thing lives here too: B-SITE-1, the beacon table's own version of
// the same background-priming complaint (a relaunch left it exactly as blank
// as the last session's until an operator opened SITE and forced a check).
// diversity_site_test.cpp's testSitePageStartsAndStopsTheBeaconPoll() covers
// this in full; the one test below is the same claim in miniature, kept here
// because this is the file that already exists to hold background-cadence
// coverage for the poller the fix lives in.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityFinderPanel.h"
#include "gui/DiversitySpatialWaterfall.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QColor>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversitySpatialWaterfall;
using AetherSDR::DiversityWindow;
using AetherSDR::ThemeManager;

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

// Reduced to the one block SITE reads -- same shape as diversity_site_test.cpp's
// own kCompassBearing, not shared from the fixture because that file is the
// only other one that needs it.
const QByteArray kCompassBearing = R"({"available": true,
    "noise": {"available": true, "kind": "hum", "phase_deg": 106.5,
              "coherence": 0.415, "bearing_deg": 212.0, "mirror_deg": 32.0,
              "bins": 147, "since": 1756867200, "reason": ""}})";

// Brings an applet up to "gate present, diversity live" with every route the
// BAND page needs answered -- same shape as diversity_band_test.cpp's own,
// plus beacons/compass (defaulted, not required by most of this file's own
// tests) for the one background-priming test that does need them.
void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity,
                 const QByteArray& spatial = kDiversitySpatial,
                 const QByteArray& finder = kDiversityFinder,
                 const QByteArray& beacons = kDiversityBeacons,
                 const QByteArray& compass = kCompassBearing)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    if (!spatial.isEmpty())
        net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError, spatial};
    if (!finder.isEmpty())
        net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError, finder};
    if (!beacons.isEmpty())
        net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError, beacons};
    if (!compass.isEmpty())
        net.routes[QStringLiteral("/diversity/compass")] = {QNetworkReply::NoError, compass};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

QToolButton* pageButton(DiversityWindow* w, const char* name)
{
    return w->findChild<QToolButton*>(QString::fromLatin1(name));
}

// Opens the window and switches it to BAND -- the window's own construction
// already starts the background timer (the gate is dual-tuner and it now
// exists); BAND on top of that is only so the waterfall/FINDER are the ones
// actually reachable through findChild() below.
DiversityWindow* openOnBand(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    return w;
}

// One more tick of the FOREGROUND (page-driven) poll, without waiting out
// real milliseconds.
void bandTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// One more tick of the BACKGROUND poll -- the one driven by its own
// gateDiversityBackgroundTimer, independent of whichever page (if any) is on
// screen. Same reason bandTick() steps the foreground one directly: a real
// second of wall-clock time is not something a test should have to wait out.
void backgroundTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "backgroundPoll", Qt::DirectConnection);
    settle();
}

QTableWidget* finderTable(DiversityWindow* w)
{
    return w->findChild<QTableWidget*>(QStringLiteral("diversityWindowFinderTable"));
}

// (a) MUTATION COVERAGE: catches a revert of setBandAvailable()/
// restartBackground() to a no-op, or of backgroundPoll() being wired to fire
// only while BAND is visible -- either would leave both counts flat here,
// since the window is hidden throughout the ticks this checks.
void testBackgroundPollingContinuesWithWindowHidden()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    w->hide();
    settle();
    CHECK(!a.diversityPanel()->wantsBandPoll());
    auto* backgroundTimer =
        a.findChild<QTimer*>(QStringLiteral("gateDiversityBackgroundTimer"));
    CHECK(backgroundTimer != nullptr && backgroundTimer->isActive());

    const int spatialBefore = net.count(QStringLiteral("/diversity/spatial"));
    const int finderBefore = net.count(QStringLiteral("/diversity/finder"));
    backgroundTick(a);
    backgroundTick(a);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialBefore + 2);
    CHECK(net.count(QStringLiteral("/diversity/finder")) == finderBefore + 2);
    closedToStart();
}

// (b) MUTATION COVERAGE: catches DiversitySpatialWaterfall::clear() (or an
// equivalent reset) being called from hide()/show(), and catches
// applySpatial()/DiversityWindowBand's forwarding being made conditional on
// bandPageVisible() -- either would either drop the rows fetched while hidden
// or throw away what was there before hiding.
void testHistorySurvivesHideAndShow()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* waterfall = w->findChild<DiversitySpatialWaterfall*>();
    CHECK(waterfall != nullptr);
    if (!waterfall)
        return;
    const int rowsBeforeHide = waterfall->rowCount();
    CHECK(rowsBeforeHide >= 1);

    w->hide();
    settle();
    backgroundTick(a);
    backgroundTick(a);
    // The rows fetched in the background while hidden are not lost -- they
    // reach the SAME waterfall applySpatial() always fed, hidden or not.
    const int rowsBeforeShow = waterfall->rowCount();
    CHECK(rowsBeforeShow == rowsBeforeHide + 2);

    w->show();
    settle();
    // Showing the window puts BAND back on screen (it was the page left
    // active before hiding), and the FOREGROUND poll's own "a page just
    // became visible" rule -- unrelated to this fix, and already covered by
    // diversity_band_test.cpp -- fetches one more row immediately. What this
    // is actually checking is that the two rows accumulated while hidden are
    // still under it rather than having been thrown away: the count keeps
    // climbing from where it was, not resetting back down to 0 or 1.
    CHECK(waterfall->rowCount() == rowsBeforeShow + 1);
    closedToStart();
}

// A second span, deliberately a different POINT COUNT from kDiversitySpatial's
// eight: DiversitySpatialWaterfall::setSpatial() only calls resetHistory()
// when the point count itself changes (see its own comment), which is the
// rule under test below.
const QByteArray kDiversitySpatialNarrowerSpan = R"({"available": true,
    "start_hz": 14150000.0, "step_hz": 500.0, "points": 4,
    "phase_deg": [0.0, 90.0, -90.0, 45.0],
    "coherence": [0.9, 0.85, 0.8, 0.75],
    "level_db": [-38.0, -42.0, -50.0, -46.0]})";

// (c) MUTATION COVERAGE: catches the `m_points != n` guard in
// DiversitySpatialWaterfall::setSpatial() being weakened to always call
// resetHistory() (history would never accumulate past one row, breaking (a)
// and (b) above too) or removed entirely (a span change would leave old rows,
// at the wrong point count, drawn under the new ones).
void testHistoryClearsOnSpanChange()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* waterfall = w->findChild<DiversitySpatialWaterfall*>();
    CHECK(waterfall != nullptr);
    if (!waterfall)
        return;
    CHECK(waterfall->points() == 8);
    bandTick(a);
    bandTick(a);
    CHECK(waterfall->rowCount() >= 3);

    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                         kDiversitySpatialNarrowerSpan};
    bandTick(a);
    CHECK(waterfall->points() == 4);
    // The span changed, so the history the OLD span drew is gone: one row,
    // the one that just landed, not the old count plus one.
    CHECK(waterfall->rowCount() == 1);
    CHECK(qFuzzyCompare(waterfall->startHz(), 14150000.0));
    closedToStart();
}

// Two candidates picked deliberately from the pair that used to collide: on
// the pre-fix kindToken(), neither "rtty" nor "signal" was a named kind, so
// both fell through to the SAME empty token -- which the strip's own fallback
// (DiversityActivityStrip::paintEvent()) and, before this fix, the table's
// blank-on-empty guard (DiversityFinderPanel::applyKindColours()) both turned
// into "renders like nothing was said", i.e. indistinguishable from each
// other and, on the strip, indistinguishable from "voice". This is the
// operator's second complaint: rows the gate told apart all painting alike.
const QByteArray kDiversityFinderTwoKinds = R"({"available": true,
    "span_hz": [14100000.0, 14102000.0], "history_s": 600,
    "activity": [0.0, 0.4, 0.0, 0.5],
    "candidates": [
      {"hz": 14100500.0, "width_hz": 200.0, "mode": "USB", "score": 0.70,
       "kind": "rtty", "kind_conf": 0.80},
      {"hz": 14101500.0, "width_hz": 0.0, "mode": "USB", "score": 0.40,
       "kind": "signal", "kind_conf": 0.35}
    ]})";

// (d) MUTATION COVERAGE: catches kindToken() reverting to an empty fallback
// for an unrecognized kind (both rows would then keep the table's own default
// foreground and compare EQUAL, failing the != check), and catches the
// "rtty"/"data"/"ft8"/"ft4"/"psk31" branch being deleted specifically (rtty
// would then fall into the SAME fallback "signal" already uses, again
// comparing equal).
void testTwoKindsGetTwoDifferentTokens()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull, kDiversitySpatial, kDiversityFinderTwoKinds);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QTableWidget* table = finderTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;
    CHECK(table->rowCount() == 2);
    QTableWidgetItem* rtty = table->item(0, 1);
    QTableWidgetItem* signal = table->item(1, 1);
    CHECK(rtty != nullptr && signal != nullptr);
    if (!rtty || !signal)
        return;

    const QColor rttyColour = rtty->foreground().color();
    const QColor signalColour = signal->foreground().color();
    CHECK(rttyColour != signalColour);
    CHECK(rttyColour == ThemeManager::instance().color(table, QStringLiteral("color.accent.success")));
    CHECK(signalColour == ThemeManager::instance().color(table, QStringLiteral("color.accent.warning")));
    closedToStart();
}

// (e) MUTATION COVERAGE, B-SITE-1: catches poll()/backgroundPoll() only ever
// reaching beacons/compass while SITE itself is the visible page (the bug --
// a relaunch left the table exactly as blank as the last session's until an
// operator opened SITE and forced a check would count 0 requests before any
// page switch), and catches SITE's own foreground 1 Hz cadence being weakened
// by the background addition (three ticks on SITE would then not land three
// fresh answers).
void testBackgroundPollPrimesBeaconsOffTheSitePage()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);

    // Open onto SLICE, not SITE or BAND -- opening onto BAND (openOnBand())
    // would answer this test's own claim by construction.
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(!w->sitePageVisible());

    // restartBackground() fires its first poll immediately on construction --
    // beacons and compass are already in flight before anyone opens SITE, or
    // even ticks a clock by hand.
    CHECK(net.count(QStringLiteral("/diversity/beacons")) >= 1);
    CHECK(net.count(QStringLiteral("/diversity/compass")) >= 1);

    // SITE's own foreground poll is still 1 Hz, untouched by the background
    // addition: three ticks land three fresh beacon answers.
    pageButton(w, "diversityWindowPageSite")->click();
    settle();
    CHECK(w->sitePageVisible());
    auto* timer = a.findChild<QTimer*>(QStringLiteral("gateDiversityBandTimer"));
    CHECK(timer != nullptr && timer->interval() == 1000);
    const int beaconsBefore = net.count(QStringLiteral("/diversity/beacons"));
    for (int i = 0; i < 3; ++i)
        bandTick(a);
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsBefore + 3);
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_band_background_test"));
    QApplication app(argc, argv);

    testBackgroundPollingContinuesWithWindowHidden();
    testHistorySurvivesHideAndShow();
    testHistoryClearsOnSpanChange();
    testTwoKindsGetTwoDifferentTokens();
    testBackgroundPollPrimesBeaconsOffTheSitePage();

    std::printf("\n%d diversity band background test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
