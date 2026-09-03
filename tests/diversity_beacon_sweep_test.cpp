// The Diversity window's SITE page, third part: what a BEACON CHECK does
// beyond tuning away and back (2026-09-03, on the air: "it doesn't look like
// it actually tuned the radio ... maybe there should be an auto sweep that
// runs through everything and then reports at the end", and "we don't see in
// the interface what we're doing with that information").
//
// Same harness as tests/diversity_site_actions_test.cpp -- a real
// AetherGateApplet in front of a fake, socket-free QNetworkAccessManager --
// and its own binary because that file is at the 800-line budget.
//
// Four things: SWEEP ALL runs the five bands nose to tail and comes home once;
// the report a run comes home with lists what was heard, from results the gate
// reported SINCE the run left; CANCEL mid-sweep comes home and reports only
// the bands done; and the /diversity/beacons poll stays wanted while a check
// is out whatever page is showing, or the report would be written from stale
// results.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityBeaconPanel.h"
#include "gui/DiversityBeaconPattern.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversityBeaconPanel;
using AetherSDR::DiversityBeaconPattern;
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

const QString kDash = QStringLiteral("—");

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Gate present, diversity live, every route the SITE page reads or writes
// answered. /diversity/set and /filter/notch reply with a status object, which
// is what the real gate does: a write and the read-back after it are one
// request.
void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& status = kDiversityStatusWithKinds,
                 const QByteArray& beacons = kDiversityBeaconsWithPattern)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        beacons};
    net.routes[QStringLiteral("/filter/notch")] = {QNetworkReply::NoError,
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

// Opens the window and switches it to SITE, which is also what starts the
// /diversity/beacons poll -- there is deliberately no other way in.
DiversityWindow* openOnSite(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    auto* page = w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageSite"));
    if (page)
        page->click();
    settle();
    // One applet poll after the window exists: /diversity is fetched once at
    // connect, before there is a window to feed, and the noise profile lives
    // on that status object.
    tick(a);
    return w;
}

// One more tick of the band poller, without waiting out a second of real time.
void siteTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// One second of a running BEACON CHECK, likewise: waiting out the real 190
// would make this file the slowest test in the tree by two orders of
// magnitude.
void checkTick(DiversityWindow* w)
{
    auto* panel = w->findChild<DiversityBeaconPanel*>();
    if (!panel)
        return;
    QMetaObject::invokeMethod(panel, "checkTick", Qt::DirectConnection);
}

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

QString cell(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->text() : QString();
}

QString cellTip(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->toolTip() : QString();
}

QTableWidget* kinds(DiversityWindow* w)
{
    return child<QTableWidget>(w, "diversityWindowNoiseKindsTable");
}

QPushButton* kindAction(DiversityWindow* w, int row)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowNoiseKindAction%1").arg(row));
}

QString labelText(DiversityWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

// A station that HAS told the gate where it is but has not yet heard anything
// on both loops: the third of the pattern plot's three states, and the only one
// no fixture above can produce.
const QByteArray kBeaconsGridNoPattern = R"({"available": true,
    "band_hz": 14100000.0, "slot": 12, "station_grid": "EM10",
    "now": null, "results": [], "last": null,
    "propagation": [], "pattern": []})";

// A /diversity/beacons body whose results were scored NOW: three on 20 m, two
// of them heard. The default fixture's results are from another day and must
// not appear in a report about this run.
QByteArray scoredNow()
{
    QByteArray body = R"({"available": true, "band_hz": 14100000.0, "slot": 4,
        "now": {"call": "4U1UN", "location": "United Nations NY", "seconds_left": 6.0},
        "station_grid": "", "propagation": [], "pattern": [], "last": null,
        "results": [
          {"call": "4U1UN", "band_hz": 14100000.0, "heard": true,  "snr_db": 8.7,  "lowest_w": 10.0,  "at": AT},
          {"call": "KH6RS", "band_hz": 14100000.0, "heard": true,  "snr_db": -1.4, "lowest_w": 100.0, "at": AT},
          {"call": "ZL6B",  "band_hz": 14100000.0, "heard": false, "snr_db": -9.5, "lowest_w": null,  "at": AT}
        ]})";
    body.replace("AT", QByteArray::number(QDateTime::currentSecsSinceEpoch()));
    return body;
}

QToolButton* pageButton(DiversityWindow* w, const char* name)
{
    return w->findChild<QToolButton*>(QString::fromLatin1(name));
}

// (a) SWEEP ALL: 20 m, then 17, 15, 12, 10 m straight on -- no trip home
// between bands -- and home once after the fifth.
void testSweepAllRunsTheFiveBandsThenComesHomeOnce()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QList<double> tuned;
    QObject::connect(a.diversityPanel(), &AetherGateDiversityPanel::requestTune,
                     [&tuned](double hz) { tuned << hz; });

    child<QPushButton>(w, "diversityWindowBeaconSweep")->click();
    settle();
    CHECK(tuned.isEmpty());                    // no slice: refused, like CHECK

    a.diversityPanel()->setActiveSliceHz(3860000.0);
    child<QPushButton>(w, "diversityWindowBeaconSweep")->click();
    CHECK(tuned.size() == 1);
    if (tuned.isEmpty())
        return;
    CHECK(qFuzzyCompare(tuned.at(0), 14100000.0));
    CHECK(labelText(w, "diversityWindowBeaconCheckLine")
          == QStringLiteral("SWEEP 1/5 · 20 m · 3:10 left"));

    const double expected[] = {18110000.0, 21150000.0, 24930000.0, 28200000.0, 3860000.0};
    const char* lines[] = {"SWEEP 2/5 · 17 m · 3:10 left", "SWEEP 3/5 · 15 m · 3:10 left",
                           "SWEEP 4/5 · 12 m · 3:10 left", "SWEEP 5/5 · 10 m · 3:10 left",
                           "home at "};   // home, and the line is the report
    for (int leg = 0; leg < 5; ++leg) {
        for (int i = 0; i < 190; ++i)
            checkTick(w);
        CHECK(tuned.size() == leg + 2);
        if (tuned.size() != leg + 2)
            return;
        CHECK(qFuzzyCompare(tuned.at(leg + 1), expected[leg]));
        CHECK(labelText(w, "diversityWindowBeaconCheckLine")
                  .startsWith(QString::fromUtf8(lines[leg])));
    }
    CHECK(!child<QPushButton>(w, "diversityWindowBeaconCheckCancel")->isEnabled());
    // Home, and the report names every band it went to, in order.
    const QString report = labelText(w, "diversityWindowBeaconCheckLine");
    CHECK(report.startsWith(QStringLiteral("home at ")));
    CHECK(report.indexOf(QStringLiteral("20 m:")) < report.indexOf(QStringLiteral("17 m:")));
    CHECK(report.contains(QStringLiteral("10 m:")));
    closedToStart();
}

// (b) The report is what the gate scored during the run, by name and by the
// weakest power step heard. Results from another day are not in it.
void testTheReportListsWhatWasHeardThisRun()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // Before any run: the default fixture's 20 m results are on screen in the
    // table, but the line says idle -- no run, no report.
    CHECK(labelText(w, "diversityWindowBeaconCheckLine")
              .startsWith(QStringLiteral("idle")));
    CHECK(labelText(w, "diversityWindowBeaconFeedsLine")
              .startsWith(QStringLiteral("feeds → pattern dial (")));

    a.diversityPanel()->setActiveSliceHz(3860000.0);
    child<QPushButton>(w, "diversityWindowBeaconCheck0")->click();
    for (int i = 0; i < 189; ++i)
        checkTick(w);
    // The scores land on a poll near the end of the run.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError, scoredNow()};
    siteTick(a);
    checkTick(w);                              // home
    QString report = labelText(w, "diversityWindowBeaconCheckLine");
    CHECK(report.startsWith(QStringLiteral("home at ")));
    CHECK(report.contains(QStringLiteral("20 m: 2 of 3 heard — 4U1UN 10 W, KH6RS 100 W")));

    // MUTATION: the same poll a minute AFTER the run would carry the last
    // slot, scored at its boundary -- so the report follows the poll.
    QByteArray more = scoredNow();
    more.replace("\"heard\": false", "\"heard\": true").replace("\"lowest_w\": null", "\"lowest_w\": 1.0");
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError, more};
    siteTick(a);
    report = labelText(w, "diversityWindowBeaconCheckLine");
    CHECK(report.contains(QStringLiteral("20 m: 3 of 3 heard")));
    CHECK(report.contains(QStringLiteral("ZL6B 1 W")));
    closedToStart();
}

// (c) CANCEL in the middle of a sweep: straight home, no resuming, and the
// report is the bands that finished -- not the one it was cut off on.
void testCancelMidSweepComesHomeAndReportsTheBandsDone()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QList<double> tuned;
    QObject::connect(a.diversityPanel(), &AetherGateDiversityPanel::requestTune,
                     [&tuned](double hz) { tuned << hz; });
    a.diversityPanel()->setActiveSliceHz(3860000.0);
    child<QPushButton>(w, "diversityWindowBeaconSweep")->click();
    for (int i = 0; i < 190; ++i)
        checkTick(w);
    CHECK(tuned.size() == 2);                  // on 17 m now
    child<QPushButton>(w, "diversityWindowBeaconCheckCancel")->click();
    CHECK(tuned.size() == 3);
    if (tuned.size() != 3)
        return;
    CHECK(qFuzzyCompare(tuned.at(2), 3860000.0));
    for (int i = 0; i < 190; ++i)
        checkTick(w);
    CHECK(tuned.size() == 3);                  // it did not carry on to 15 m
    const QString report = labelText(w, "diversityWindowBeaconCheckLine");
    CHECK(report.contains(QStringLiteral("20 m:")));
    CHECK(!report.contains(QStringLiteral("17 m:")));
    closedToStart();
}

// (d) The site poll follows the check off the page: on BAND with nothing
// running it is not wanted; with a check out it is, whatever page is up.
void testThePollStaysWantedOffTheSitePageWhileACheckIsOut()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(a.diversityPanel()->wantsSitePoll());
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    CHECK(!a.diversityPanel()->wantsSitePoll());

    a.diversityPanel()->setActiveSliceHz(3860000.0);
    child<QPushButton>(w, "diversityWindowBeaconCheck1")->click();
    settle();
    CHECK(a.diversityPanel()->wantsSitePoll());
    // And a poll that lands while BAND is showing still reaches the report.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError, scoredNow()};
    siteTick(a);
    for (int i = 0; i < 190; ++i)
        checkTick(w);                          // home from 17 m
    CHECK(labelText(w, "diversityWindowBeaconCheckLine")
              .startsWith(QStringLiteral("home at ")));
    CHECK(a.diversityPanel()->wantsSitePoll()); // still: the last slot's poll
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_beacon_sweep_test"));
    QApplication app(argc, argv);

    testSweepAllRunsTheFiveBandsThenComesHomeOnce();
    testTheReportListsWhatWasHeardThisRun();
    testCancelMidSweepComesHomeAndReportsTheBandsDone();
    testThePollStaysWantedOffTheSitePageWhileACheckIsOut();

    std::printf("\n%d diversity beacon sweep test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
