// The Diversity window's BAND page -- the spatial waterfall and the
// conversation FINDER -- driven through a real AetherGateApplet and
// socket-free.
//
// Same harness as tests/diversity_window_test.cpp and for the same reason: the
// page owns no transport. It is reached by opening the sidebar's window and
// pressing BAND, fed by the applet's DiversityBandPoller, and every click on
// it leaves as a request signal the panel forwards. Driving it any other way
// would be testing a wiring diagram we drew rather than the one that ships.
//
// A separate binary from diversity_window_test because that file is at the
// 800-line budget AGENTS.md asks for, and because opening the window writes
// DiversityWindowVisible into the process-wide AppSettings cache -- every case
// here starts from a known closed state for the same reason every case there
// does.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityFinderPanel.h"
#include "gui/DiversitySpatialWaterfall.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QColor>
#include <QJsonObject>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversityFinderPanel;
using AetherSDR::DiversitySpatialWaterfall;
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

// Brings an applet up to "gate present, diversity live" with every route the
// BAND page needs answered.
void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity,
                 const QByteArray& spatial = kDiversitySpatial,
                 const QByteArray& finder = kDiversityFinder)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    if (!spatial.isEmpty())
        net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError, spatial};
    if (!finder.isEmpty())
        net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError, finder};
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

// Opens the window and switches it to BAND, which is also what starts the two
// polls -- there is deliberately no other way in.
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

// One more tick of the band poller, without waiting out 250 ms of real time.
void bandTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

QTableWidget* finderTable(DiversityWindow* w)
{
    return w->findChild<QTableWidget*>(QStringLiteral("diversityWindowFinderTable"));
}

QString cell(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->text() : QString();
}

// (a) The two BAND routes are polled only while the page is on screen: not on
// SLICE, not with the window closed, and never before it has been opened at
// all. This is the whole reason a 4 Hz route is affordable.
void testBandPageStartsAndStopsTheTwoPolls()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    CHECK(a.gatePresent());

    // Nothing opened: nothing asked.
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == 0);
    CHECK(net.count(QStringLiteral("/diversity/finder")) == 0);
    CHECK(!a.diversityPanel()->wantsBandPoll());

    // Open on SLICE: still nothing. The SLICE page has no use for either.
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(!w->bandPageVisible());
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == 0);

    // BAND: both routes, immediately -- not one tick later.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    CHECK(w->bandPageVisible());
    CHECK(a.diversityPanel()->wantsBandPoll());
    CHECK(net.count(QStringLiteral("/diversity/spatial")) >= 1);
    CHECK(net.count(QStringLiteral("/diversity/finder")) >= 1);
    auto* timer = a.findChild<QTimer*>(QStringLiteral("gateDiversityBandTimer"));
    CHECK(timer != nullptr && timer->isActive());

    // Four ticks: spatial every tick, finder every fourth.
    const int spatialBefore = net.count(QStringLiteral("/diversity/spatial"));
    const int finderBefore = net.count(QStringLiteral("/diversity/finder"));
    for (int i = 0; i < 4; ++i)
        bandTick(a);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialBefore + 4);
    CHECK(net.count(QStringLiteral("/diversity/finder")) == finderBefore + 1);

    // Back to SLICE: the timer stops and nothing more is asked.
    pageButton(w, "diversityWindowPageSlice")->click();
    settle();
    CHECK(timer != nullptr && !timer->isActive());
    const int spatialOnSlice = net.count(QStringLiteral("/diversity/spatial"));
    tick(a);
    settle();
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialOnSlice);

    // Back to BAND, then hide the whole window: same again. A hidden window is
    // not looking at anything, whichever page it was left on.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    CHECK(timer->isActive());
    w->hide();
    settle();
    CHECK(!timer->isActive());
    CHECK(!a.diversityPanel()->wantsBandPoll());
    const int spatialHidden = net.count(QStringLiteral("/diversity/spatial"));
    tick(a);
    settle();
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialHidden);
    closedToStart();
}

// (b) A spatial payload becomes one waterfall row, and the colour rule is the
// one the legend claims: phase drives hue, coherence drives saturation.
void testSpatialPayloadPaintsARowAndPhaseDrivesHue()
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

    CHECK(waterfall->available());
    CHECK(waterfall->rowCount() == 1);
    CHECK(waterfall->points() == 8);
    CHECK(qFuzzyCompare(waterfall->startHz(), 14100000.0));
    CHECK(qFuzzyCompare(waterfall->stepHz(), 250.0));
    CHECK(waterfall->hasPassband());
    CHECK(qFuzzyCompare(waterfall->passbandLoHz(), 14100500.0));

    // Two stations 180 degrees apart cannot share a colour -- that is the
    // entire claim the page makes.
    const QColor atZero = waterfall->newestColour(0);
    const QColor atHalfTurn = waterfall->newestColour(1);
    CHECK(atZero.isValid() && atHalfTurn.isValid());
    CHECK(atZero.hue() != atHalfTurn.hue());
    // ... and both are saturated, because both bins are coherent.
    CHECK(atZero.saturation() > 128);
    CHECK(atHalfTurn.saturation() > 128);
    // Bin 3 has zero coherence: no direction, so no colour. Grey, not black.
    CHECK(waterfall->newestColour(3).saturation() == 0);
    // Bin 0 is the row's loudest and bin 6 is 40 dB down: brightness follows.
    CHECK(waterfall->newestColour(0).value() > waterfall->newestColour(6).value());

    // Each poll appends exactly one row.
    bandTick(a);
    CHECK(waterfall->rowCount() == 2);
    bandTick(a);
    CHECK(waterfall->rowCount() == 3);

    // Clicking a column asks to tune to that column's CENTRE.
    QSignalSpy tunes(a.diversityPanel(), &AetherGateDiversityPanel::requestTune);
    const int column = 2;
    const double expected = 14100000.0 + (double(column) + 0.5) * 250.0;
    waterfall->resize(800, 300);
    const int x = int((double(column) + 0.5) / 8.0 * 800.0);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(x, 10), QPointF(x, 10),
                      QPointF(x, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(waterfall, &press);
    settle();
    CHECK(tunes.count() == 1);
    if (tunes.count() == 1)
        CHECK(std::abs(tunes.at(0).at(0).toDouble() - expected) < 1.0);
    closedToStart();
}

// (c) The candidate table is the gate's ranking, rendered verbatim and in the
// order it arrived -- the score column and the row order must agree.
void testFinderPayloadFillsTheTableInTheGatesOrder()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QTableWidget* table = finderTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;
    CHECK(table->rowCount() == 3);
    CHECK(cell(table, 0, 0) == QStringLiteral("14100.60"));
    CHECK(cell(table, 1, 0) == QStringLiteral("14101.45"));
    CHECK(cell(table, 2, 0) == QStringLiteral("14101.90"));
    // Ranked best first, as sent: 0.82, 0.55, 0.31.
    CHECK(cell(table, 0, 1) == QStringLiteral("0.82"));
    CHECK(cell(table, 1, 1) == QStringLiteral("0.55"));
    CHECK(cell(table, 2, 1) == QStringLiteral("0.31"));
    // Signed dB, both directions: the second candidate's pair is COSTING it.
    CHECK(cell(table, 0, 8) == QStringLiteral("+1.4"));
    CHECK(cell(table, 1, 8) == QStringLiteral("-0.3"));
    // 184 s of activity is three minutes four, not "184".
    CHECK(cell(table, 0, 4) == QStringLiteral("3:04"));
    // Somebody talking as we look is "now", not "0 s".
    CHECK(cell(table, 0, 5) == QStringLiteral("now"));
    CHECK(cell(table, 1, 5) == QStringLiteral("12 s"));

    // The activity strip took the gate's array.
    auto* strip = w->findChild<AetherSDR::DiversityActivityStrip*>();
    CHECK(strip != nullptr);
    if (strip)
        CHECK(strip->binCount() == 8);

    // One Tune button per row, and every one of them reachable.
    const QList<QPushButton*> tuneButtons =
        table->findChildren<QPushButton*>(QStringLiteral("diversityWindowFinderTune"));
    CHECK(tuneButtons.size() == 3);
    closedToStart();
}

// (d) A Tune goes out as a real tune request AND puts the combiner into track,
// because arriving on a conversation with a weight solved somewhere else is
// not arriving.
void testTuneButtonTunesTheRowAndAsksForTrack()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    // mode "manual": the tune has to ask for track.
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    QTableWidget* table = finderTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;

    QSignalSpy tunes(a.diversityPanel(), &AetherGateDiversityPanel::requestTune);
    const QList<QPushButton*> tuneButtons =
        table->findChildren<QPushButton*>(QStringLiteral("diversityWindowFinderTune"));
    CHECK(tuneButtons.size() == 3);
    if (tuneButtons.isEmpty())
        return;
    tuneButtons.at(0)->click();
    settle();

    CHECK(tunes.count() == 1);
    if (tunes.count() == 1)
        CHECK(std::abs(tunes.at(0).at(0).toDouble() - 14100600.0) < 1.0);
    CHECK(net.log.contains(QStringLiteral("/diversity/set?mode=track")));

    // A double-click on the row is the same offer by another route.
    const int setsBefore = net.count(QStringLiteral("/diversity/set"));
    emit table->cellDoubleClicked(1, 0);
    settle();
    CHECK(tunes.count() == 2);
    if (tunes.count() == 2)
        CHECK(std::abs(tunes.at(1).at(0).toDouble() - 14101450.0) < 1.0);
    CHECK(net.count(QStringLiteral("/diversity/set")) > setsBefore);

    // Already tracking: the tune must not write a mode it is already in.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityTalkers};
    tick(a);
    settle();
    const int setsWhileTracking = net.count(QStringLiteral("/diversity/set"));
    tuneButtons.at(0)->click();
    settle();
    CHECK(tunes.count() == 3);
    CHECK(net.count(QStringLiteral("/diversity/set")) == setsWhileTracking);
    closedToStart();
}

// (e) A gate that has no map yet, and an older one whose candidates carry half
// the fields, must both render as what they are. Nothing invented, no zeros
// standing in for measurements nobody made.
void testOldGatePayloadsRenderWithoutInventingAnything()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull, kDiversitySpatialUnavailable, kDiversityFinder);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* waterfall = w->findChild<DiversitySpatialWaterfall*>();
    CHECK(waterfall != nullptr);
    if (!waterfall)
        return;
    // "available": false is not a row of zeros and not a black row: it is no
    // row at all, and the widget says "waiting for the gate" instead.
    CHECK(!waterfall->available());
    CHECK(waterfall->rowCount() == 0);
    CHECK(waterfall->points() == 0);
    CHECK(!waterfall->hasPassband());
    // A column that is not there has no frequency to offer, so a click on it
    // cannot become a tune.
    QSignalSpy tunes(a.diversityPanel(), &AetherGateDiversityPanel::requestTune);
    waterfall->resize(800, 300);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(400, 10), QPointF(400, 10),
                      QPointF(400, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(waterfall, &press);
    settle();
    CHECK(tunes.count() == 0);

    // The third candidate carries hz and score and nothing else.
    QTableWidget* table = finderTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;
    CHECK(table->rowCount() == 3);
    const QString dash = QStringLiteral("—");
    CHECK(cell(table, 2, 0) == QStringLiteral("14101.90"));
    CHECK(cell(table, 2, 1) == QStringLiteral("0.31"));
    for (int col : {2, 3, 4, 5, 6, 7, 8})
        CHECK(cell(table, 2, col) == dash);

    // A finder that has nothing to report empties the table rather than
    // leaving last minute's candidates up looking current.
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError,
                                                       kDiversityFinderUnavailable};
    for (int i = 0; i < 4; ++i)
        bandTick(a);
    CHECK(table->rowCount() == 0);

    // A gate that never had these routes at all (404 on both) is the same
    // story, not a crash and not a stale picture.
    net.routes.remove(QStringLiteral("/diversity/spatial"));
    net.routes.remove(QStringLiteral("/diversity/finder"));
    for (int i = 0; i < 4; ++i)
        bandTick(a);
    CHECK(!waterfall->available());
    CHECK(table->rowCount() == 0);
    closedToStart();
}

// (f) The same promise the SLICE page makes: at the size the window opens at,
// nothing on the BAND page is behind a scrollbar.
void testNothingScrollsOnTheBandPageAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("diversityWindowBandScroll"));
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());

    // A full table of twelve candidates is the worst case the gate can send.
    CHECK(finderTable(w) != nullptr);
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_band_test"));
    QApplication app(argc, argv);

    testBandPageStartsAndStopsTheTwoPolls();
    testSpatialPayloadPaintsARowAndPhaseDrivesHue();
    testFinderPayloadFillsTheTableInTheGatesOrder();
    testTuneButtonTunesTheRowAndAsksForTrack();
    testOldGatePayloadsRenderWithoutInventingAnything();
    testNothingScrollsOnTheBandPageAtTheInitialSize();

    std::printf("\n%d diversity band test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
