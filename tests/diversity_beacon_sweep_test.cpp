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

// (e) A /diversity/beacons body carrying exactly one scored result, band_hz
// either the live one or JSON null (off band) -- the shape the off-band
// fallback tests need, where the payload's OWN band has nothing to do with
// which band's rows the table falls back to showing.
QByteArray oneResultBeacons(bool bandLive, double bandHz, const QString& call,
                            double resultBandHz, qint64 atEpoch)
{
    QByteArray body = R"({"available": true, "band_hz": BANDHZ, "slot": 1,
        "now": null, "station_grid": "EM10", "propagation": [], "pattern": [],
        "last": null,
        "results": [{"call": "CALL", "band_hz": RESULTHZ, "heard": true,
                     "snr_db": 6.0, "lowest_w": 1.0, "at": AT}]})";
    body.replace("BANDHZ",
                bandLive ? QByteArray::number(bandHz, 'f', 1) : QByteArray("null"));
    body.replace("CALL", call.toUtf8());
    body.replace("RESULTHZ", QByteArray::number(resultBandHz, 'f', 1));
    body.replace("AT", QByteArray::number(atEpoch));
    return body;
}

// (f) All five bands sampled, ages spread out -- the fixed height under
// buildPatternColumn() has to hold all five real lines without clipping into
// the feeds line below it, at the window's own opening size (2026-09-03, on
// the air: the propagation block's last line or two overlapped "feeds ->
// pattern dial ..." on the real display).
QByteArray fiveBandPropagationBeacons()
{
    QByteArray body = R"({"available": true, "band_hz": 14100000.0, "slot": 12,
        "station_grid": "EM10",
        "now": {"call": "4X6TU", "location": "Tel Aviv, Israel", "seconds_left": 9.4},
        "results": [], "last": null,
        "propagation": [
          {"band_hz": 14100000.0, "sampled": 18, "heard": 5, "of": 18, "best_w": 0.1,
           "median_snr_db": 7.3, "updated": AT0},
          {"band_hz": 18110000.0, "sampled": 18, "heard": 0, "of": 18, "updated": AT1},
          {"band_hz": 21150000.0, "sampled": 18, "heard": 0, "of": 18, "updated": AT2},
          {"band_hz": 24930000.0, "sampled": 18, "heard": 0, "of": 18, "updated": AT3},
          {"band_hz": 28200000.0, "sampled": 18, "heard": 0, "of": 18, "updated": AT4}
        ],
        "pattern": []})";
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    body.replace("AT0", QByteArray::number(now - 60));
    body.replace("AT1", QByteArray::number(now - 4 * 3600));
    body.replace("AT2", QByteArray::number(now - 5 * 3600));
    body.replace("AT3", QByteArray::number(now - 6 * 3600));
    body.replace("AT4", QByteArray::number(now - 15 * 3600));
    return body;
}

// (a) No beacon frequency in the span, results stored on two bands: the
// table follows whichever one was checked most recently, and says so.
// Mutation: revert renderRows()/applyBeacons() to read m_bandHz instead of
// m_shownBandHz and this table goes back to all dashes with the header
// giving no band name.
void testOffBandShowsTheNewestStoredBandAndNamesItInTheHeader()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithKinds, kDiversityBeaconsNoBand);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* table = child<QTableWidget>(w, "diversityWindowBeaconTable");
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // 20 m, checked (and left) a while ago.
    net.routes[QStringLiteral("/diversity/beacons")] = {
        QNetworkReply::NoError,
        oneResultBeacons(true, 14100000.0, QStringLiteral("4U1UN"), 14100000.0,
                         now - 200)};
    siteTick(a);
    CHECK(cell(table, 0, 2) == QStringLiteral("●")); // 4U1UN heard, 20 m

    // Off band now, but 15 m was checked more recently than 20 m was.
    net.routes[QStringLiteral("/diversity/beacons")] = {
        QNetworkReply::NoError,
        oneResultBeacons(false, 0.0, QStringLiteral("W6WX"), 21150000.0, now)};
    siteTick(a);

    const QString header = labelText(w, "diversityWindowBeaconHeaderLabel");
    CHECK(header.contains(QStringLiteral("showing 15 m")));
    CHECK(header.contains(QStringLiteral("checked")));
    CHECK(header.contains(QStringLiteral("no beacon frequency in the span")));
    CHECK(header.contains(QStringLiteral("14.100")));
    CHECK(header.contains(QStringLiteral("28.200")));
    // The table followed the newer band: 15 m's W6WX is heard, and 4U1UN
    // (still in memory, but only ever heard on 20 m) is a dash on this band.
    CHECK(cell(table, 2, 2) == QStringLiteral("●")); // W6WX, row for it
    CHECK(cell(table, 0, 2) == kDash);
    closedToStart();
}

// (b) A live beacon frequency in the span wins over any other band's more
// recent history: the table shows the band you are tuned to, not merely the
// newest one on file. Mutation: drop the `m_bandHz <= 0.0` guard around the
// newestStoredBandHz() fallback and this starts showing 15 m instead of 20 m
// even while 20 m is live.
void testALiveBandBeatsNewerHistoryOnAnotherBand()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithKinds, kDiversityBeaconsNoBand);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* table = child<QTableWidget>(w, "diversityWindowBeaconTable");
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // 15 m stored off band, most recently of anything on file.
    net.routes[QStringLiteral("/diversity/beacons")] = {
        QNetworkReply::NoError,
        oneResultBeacons(false, 0.0, QStringLiteral("W6WX"), 21150000.0, now)};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
              .contains(QStringLiteral("showing 15 m")));

    // The span is now live on 20 m, with a fresh 20 m result -- the table
    // must show THAT, not 15 m, even though 15 m is still the newer check.
    net.routes[QStringLiteral("/diversity/beacons")] = {
        QNetworkReply::NoError,
        oneResultBeacons(true, 14100000.0, QStringLiteral("4U1UN"), 14100000.0,
                         now)};
    siteTick(a);
    const QString header = labelText(w, "diversityWindowBeaconHeaderLabel");
    CHECK(!header.contains(QStringLiteral("no beacon frequency in the span")));
    CHECK(cell(table, 0, 2) == QStringLiteral("●")); // 4U1UN, 20 m, live
    CHECK(cell(table, 2, 2) == kDash);                    // W6WX not on 20 m
    closedToStart();
}

// (c) A newer check on a different band moves the table off the one it was
// showing -- the fallback tracks the latest check, not just whatever it
// first landed on. Mutation: cache m_shownBandHz once and never refresh it
// from a later newestStoredBandHz() call, and the table stays on 20 m.
void testANewerCheckOnAnotherBandMovesTheTable()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithKinds, kDiversityBeaconsNoBand);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* table = child<QTableWidget>(w, "diversityWindowBeaconTable");
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // 20 m checked first, off band throughout.
    net.routes[QStringLiteral("/diversity/beacons")] = {
        QNetworkReply::NoError,
        oneResultBeacons(false, 0.0, QStringLiteral("4U1UN"), 14100000.0,
                         now - 200)};
    siteTick(a);
    CHECK(cell(table, 0, 2) == QStringLiteral("●"));
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
              .contains(QStringLiteral("showing 20 m")));

    // 12 m checked more recently -- the table follows it.
    net.routes[QStringLiteral("/diversity/beacons")] = {
        QNetworkReply::NoError,
        oneResultBeacons(false, 0.0, QStringLiteral("ZL6B"), 24930000.0, now)};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
              .contains(QStringLiteral("showing 12 m")));
    CHECK(cell(table, 4, 2) == QStringLiteral("●")); // ZL6B, 12 m
    CHECK(cell(table, 0, 2) == kDash);                    // 4U1UN not on 12 m
    closedToStart();
}

// (d) The propagation label's five real lines fit inside its own fixed
// height at the window's opening size, with a real margin rather than
// exactly the bare minimum -- and the SITE page's no-scroll contract still
// holds. Mutation: revert buildPatternColumn() to size the label off a
// placeholder's sizeHint() (the pre-fix approach) and prop->height() comes
// back down to exactly fm.lineSpacing() * 5, failing the margin check even
// though it still equals the bare minimum needed on this platform's fonts --
// which is exactly how the real clipping shipped unnoticed under offscreen
// rendering in the first place.
void testThePropagationLabelsFiveLinesFitItsHeightAtTheOpeningSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithKinds, fiveBandPropagationBeacons());
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    settle();
    w->grab(); // offscreen platforms only finish layout on a real paint pass

    auto* prop = child<QLabel>(w, "diversityWindowBeaconPropagationLabel");
    auto* feeds = child<QLabel>(w, "diversityWindowBeaconFeedsLine");
    CHECK(prop != nullptr && feeds != nullptr);
    if (!prop || !feeds)
        return;

    // Five real lines, one per band -- not the placeholder "00 m" sizeHint()
    // used to be fixed against.
    CHECK(prop->text().count(QChar('\n')) == 4);

    const QFontMetrics fm(prop->fontMetrics());
    CHECK(prop->height() > fm.lineSpacing() * 5);
    CHECK(prop->geometry().bottom() <= feeds->geometry().top());

    // The no-scroll contract this page runs under (tests/diversity_site_test.cpp
    // owns the full version): the extra headroom still has to fit inside the
    // SITE page's own budget.
    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("diversityWindowSiteScroll"));
    CHECK(scroll != nullptr);
    if (scroll) {
        CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        CHECK(!scroll->horizontalScrollBar()->isVisible());
        CHECK(!scroll->verticalScrollBar()->isVisible());
    }
    closedToStart();
}

// (g) A SWEEP report that does not fit the countdown row's real width elides
// there rather than clipping into whatever is beside it, and more of it goes
// on beside the pattern dial rather than only on hover. Mutation: revert
// renderReport() to set m_checkLine's text unconditionally and leave
// m_reportOverflow hidden, and the first two checks below fail -- the row
// would hold the whole unelided string (this test's very reason for using
// 18-of-18 on every band: it is the widest line renderReport() ever builds,
// wider than the row has left of 1120 px once the caption, five band
// buttons, SWEEP ALL and CANCEL have theirs). Mutation: give m_reportOverflow
// the checkLine ROW's own width instead of the (narrower) pattern column's --
// both labels would then elide identically and the last check (10 m absent
// from even the wider overflow line) would stop being a fact about the
// column's real, narrower budget.
void testASweepReportThatDoesNotFitTheRowShowsInFullBesideThePatternDial()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    settle();
    w->grab(); // offscreen platforms only finish layout on a real paint pass,
               // and m_checkLine's width() (what elidedText() below measures
               // against) is only real once that pass has happened.

    auto* checkLine = child<QLabel>(w, "diversityWindowBeaconCheckLine");
    auto* overflow = child<QLabel>(w, "diversityWindowBeaconReportOverflow");
    CHECK(checkLine != nullptr && overflow != nullptr);
    if (!checkLine || !overflow)
        return;
    CHECK(!overflow->isVisible());

    const double bandsHz[5] = {14100000.0, 18110000.0, 21150000.0, 24930000.0,
                               28200000.0};
    a.diversityPanel()->setActiveSliceHz(3860000.0);
    child<QPushButton>(w, "diversityWindowBeaconSweep")->click();
    for (int leg = 0; leg < 5; ++leg) {
        QByteArray body = R"({"available": true, "band_hz": HZ, "slot": 1,
            "now": null, "station_grid": "EM10", "propagation": [], "pattern": [],
            "last": null, "results": [)";
        for (int c = 0; c < 18; ++c) {
            if (c)
                body += ",";
            body += QByteArray("{\"call\": \"C") + QByteArray::number(c)
                  + "\", \"band_hz\": HZ, \"heard\": true, \"snr_db\": 5.0, "
                    "\"lowest_w\": 0.1, \"at\": AT}";
        }
        body += "]}";
        body.replace("HZ", QByteArray::number(bandsHz[leg], 'f', 1));
        body.replace("AT", QByteArray::number(QDateTime::currentSecsSinceEpoch()));
        net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                             body};
        for (int i = 0; i < 190; ++i) {
            checkTick(w);
            if (i % 20 == 0)
                siteTick(a);
        }
        siteTick(a);
    }
    settle();
    w->grab();

    CHECK(checkLine->text().endsWith(QChar(0x2026))); // ellipsis: elided
    CHECK(overflow->isVisible());
    CHECK(overflow->text().startsWith(QStringLiteral("home at ")));
    // The pattern column is its own, narrower budget than the checkLine row
    // (measured ~366 px against the row's 579): this report reaches further
    // into the five bands there than the row alone ever could, but a five-
    // band 18-of-18 report still runs past even that, so it elides too --
    // 20 m and 17 m fit, 10 m does not, and the full five-band detail is
    // still in checkLine's tooltip regardless.
    CHECK(overflow->text().contains(QStringLiteral("20 m: 18 of 18 heard")));
    CHECK(overflow->text().contains(QStringLiteral("17 m: 18 of 18 heard")));
    // The column's real ~366 px, not the wider ~579 px checkLine's row has:
    // 15 m does not fit either. Distinguishes this from a mutation that gives
    // m_reportOverflow the ROW's width instead of the column's -- at 579 px
    // both labels would elide identically (15 m and 12 m both fit checkLine's
    // own row) and this line, and the != check below, would stop being a
    // fact about the column's own, narrower budget.
    CHECK(!overflow->text().contains(QStringLiteral("15 m: 18 of 18 heard")));
    CHECK(!overflow->text().contains(QStringLiteral("10 m: 18 of 18 heard")));
    CHECK(overflow->text().endsWith(QChar(0x2026)));
    CHECK(overflow->text() != checkLine->text());
    CHECK(checkLine->toolTip().contains(QStringLiteral("10 m: 18 of 18 heard")));
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
    testOffBandShowsTheNewestStoredBandAndNamesItInTheHeader();
    testALiveBandBeatsNewerHistoryOnAnotherBand();
    testANewerCheckOnAnotherBandMovesTheTable();
    testThePropagationLabelsFiveLinesFitItsHeightAtTheOpeningSize();
    testASweepReportThatDoesNotFitTheRowShowsInFullBesideThePatternDial();

    std::printf("\n%d diversity beacon sweep test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
