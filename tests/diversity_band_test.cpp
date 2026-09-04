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
#include "gui/DiversitySpatialLegend.h"
#include "gui/DiversitySpatialWaterfall.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
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

QString cellTip(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->toolTip() : QString();
}

// A gate that says what it found, kept local to this file because the shared
// kDiversityFinder is deliberately an OLDER one: both have to render, and the
// older one may never be quietly upgraded to keep a test passing.
//
// Three candidates: a conversation, a keyed tone the finder's voice score was
// fooled by (which is the whole point of the column), and a bare carrier from
// a gate that sent the verdict but no confidence and no width.
const QByteArray kDiversityFinderKinds = R"({"available": true,
    "span_hz": [14100000.0, 14102000.0], "history_s": 600,
    "activity": [0.0, 0.2, 0.9, 0.4, 0.0, 0.1, 0.6, 0.0],
    "candidates": [
      {"hz": 14100600.0, "width_hz": 2700.0, "mode": "USB", "score": 0.82,
       "kind": "voice", "kind_conf": 0.91, "snr_db": 6.1, "syllabic": 0.61,
       "active_s": 184.0, "last_s": 0.0, "phase_deg": 141.0, "coherence": 0.70,
       "ratio_db": -2.1, "gain_db": 1.4},
      {"hz": 14101450.0, "width_hz": 300.0, "mode": "USB", "score": 0.55,
       "kind": "cw", "kind_conf": 0.64, "snr_db": -1.2, "syllabic": 0.44,
       "active_s": 42.0, "last_s": 12.0, "phase_deg": -30.0, "coherence": 0.21,
       "ratio_db": 0.4, "gain_db": -0.3},
      {"hz": 14101900.0, "score": 0.31, "kind": "carrier"}
    ]})";

// (a) The two BAND routes run at 4 Hz only while the page is on screen, and
// never before the window has been opened at least once (there is nowhere for
// a reply to go: AetherGateDiversityPanel::applySpatial()/applyFinder() are
// no-ops on a null window). Once the window exists AND the gate reports a
// dual-tuner pair, both routes keep going in the BACKGROUND at 1 Hz on every
// OTHER page and while the window is hidden -- see
// tests/diversity_band_background_test.cpp for the cases that are really
// about that half of the contract; this one stays focused on the foreground
// rate stepping up and back down.
void testBandPageStartsAndStopsTheTwoPolls()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    CHECK(a.gatePresent());

    // Nothing opened: nothing asked, background included -- there is no
    // window yet for either route's reply to reach.
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == 0);
    CHECK(net.count(QStringLiteral("/diversity/finder")) == 0);
    CHECK(!a.diversityPanel()->wantsBandPoll());

    // Open on SLICE: the FOREGROUND timer has no use for BAND, SITE or
    // FILTER, so it stays stopped. The window now exists and the gate is
    // dual-tuner, though, so the independent BACKGROUND timer starts at
    // once -- with one immediate fetch of each route, not a wait for its own
    // first second.
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(!w->bandPageVisible());
    auto* bandTimer = a.findChild<QTimer*>(QStringLiteral("gateDiversityBandTimer"));
    auto* backgroundTimer =
        a.findChild<QTimer*>(QStringLiteral("gateDiversityBackgroundTimer"));
    CHECK(bandTimer != nullptr && !bandTimer->isActive());
    CHECK(backgroundTimer != nullptr && backgroundTimer->isActive());
    CHECK(backgroundTimer->interval() == 1000);
    const int spatialOnSlice = net.count(QStringLiteral("/diversity/spatial"));
    CHECK(spatialOnSlice >= 1);
    CHECK(net.count(QStringLiteral("/diversity/finder")) >= 1);
    tick(a);
    settle();
    // The applet's own /status+/diversity poll (tick()) does not itself touch
    // either BAND route, and 20 real ms is not a background tick either.
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialOnSlice);

    // BAND: the foreground timer starts at 4 Hz immediately -- not one tick
    // later, and not waiting on the background timer's own schedule.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    CHECK(w->bandPageVisible());
    CHECK(a.diversityPanel()->wantsBandPoll());
    CHECK(bandTimer->isActive());
    CHECK(bandTimer->interval() == 250);

    // Four ticks: spatial every tick, finder every fourth -- driven by the
    // foreground timer alone. The background timer keeps running underneath
    // it but skips spatial/finder entirely while BAND is the page on screen
    // (see DiversityBandPoller::backgroundPoll()), so it cannot double either
    // count here.
    const int spatialBefore = net.count(QStringLiteral("/diversity/spatial"));
    const int finderBefore = net.count(QStringLiteral("/diversity/finder"));
    for (int i = 0; i < 4; ++i)
        bandTick(a);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialBefore + 4);
    CHECK(net.count(QStringLiteral("/diversity/finder")) == finderBefore + 1);

    // Back to SLICE: the foreground timer stops rather than dropping to a
    // slower rate -- BAND is the only page it ever serves. The background
    // timer is unaffected: the connection that justifies it has not changed,
    // only which page (if any) is on screen.
    pageButton(w, "diversityWindowPageSlice")->click();
    settle();
    CHECK(!bandTimer->isActive());
    CHECK(backgroundTimer->isActive());

    // Back to BAND, then hide the whole window: the foreground timer stops
    // again and the background one keeps running -- hiding did not close the
    // window, and the pair is still dual-tuner.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    CHECK(bandTimer->isActive());
    w->hide();
    settle();
    CHECK(!bandTimer->isActive());
    CHECK(backgroundTimer->isActive());
    CHECK(!a.diversityPanel()->wantsBandPoll());
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
    // openOnBand() opens onto SLICE before switching to BAND, and by then the
    // background timer has already fired its own immediate fetch of the same
    // route (see testBandPageStartsAndStopsTheTwoPolls()) -- so the row count
    // here is a baseline, not a bare 1: one row from that background fetch,
    // one from BAND's own immediate foreground fetch on becoming visible,
    // both of the same canned payload. Everything below reads the NEWEST row,
    // which is unaffected by there being two identical ones under it.
    const int rowsOnOpen = waterfall->rowCount();
    CHECK(rowsOnOpen >= 1);
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
    CHECK(waterfall->rowCount() == rowsOnOpen + 1);
    bandTick(a);
    CHECK(waterfall->rowCount() == rowsOnOpen + 2);

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

// (b2) Colour is GATED on coherence, not proportional to it. The operator's
// verdict on the proportional version was "a big blurry mess": every noise bin
// came out a third of the way to a confident hue, and a span of those is a
// pastel wash with the real signals lost inside it. Below 0.5 there is no
// direction to report and the bin is grey; from there the colour comes up to
// full by 0.9.
void testCoherenceGatesTheColourRatherThanScalingIt()
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

    // Bin 0 is at 0.9 -- the top of the ramp, so fully saturated.
    CHECK(waterfall->newestColour(0).saturation() == 255);
    // Bin 7 is at 0.3. Under the old rule that was a quarter-saturated hue
    // claiming a direction; now it is grey, and grey has no hue at all.
    CHECK(waterfall->newestColour(7).saturation() == 0);
    CHECK(waterfall->newestColour(7).hue() == -1);
    // Bin 4 is at exactly 0.5, the floor: still grey. The gate is a floor, not
    // a rounding.
    CHECK(waterfall->newestColour(4).saturation() == 0);
    // Bin 2 is at 0.7, half way up the ramp -- coloured, and visibly less sure
    // of itself than bin 0.
    CHECK(waterfall->newestColour(2).saturation() > 100);
    CHECK(waterfall->newestColour(2).saturation() < 160);

    // The key beside the picture is drawn rather than described, and it is the
    // thing that makes any of the above readable.
    CHECK(w->findChild<AetherSDR::DiversitySpatialLegend*>() != nullptr);

    w->close();
    settle();
    closedToStart();
}

// A second spatial poll, deliberately unlike the fixture's in every leg: it is
// what makes "the readout quotes the ROW under the pointer" a claim a test can
// tell apart from "the readout quotes the newest row".
const QByteArray kDiversitySpatialSecondPoll = R"({"available": true,
    "start_hz": 14100000.0, "step_hz": 250.0, "points": 8,
    "phase_deg": [10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0],
    "coherence": [0.95, 0.95, 0.95, 0.95, 0.95, 0.95, 0.95, 0.95],
    "level_db": [-30.0, -31.0, -32.0, -33.0, -34.0, -35.0, -36.0, -37.0]})";

// (b3) A colour is only worth painting if it can be turned back into numbers,
// and the numbers have to be the ones behind THAT pixel. The readout quotes
// the row under the pointer, not the newest row: hovering a streak from a
// minute ago and being told what is on the air right now is the picture lying
// about what it is showing.
void testTheHoverReadoutQuotesTheRowUnderThePointer()
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

    // openOnBand() already leaves more than one row behind it (see
    // testSpatialPayloadPaintsARowAndPhaseDrivesHue()'s comment) -- what this
    // test needs is only that the newest row is the second poll's and the one
    // right under it is still the first poll's, not a bare row count.
    const int rowsBeforeSecondPoll = waterfall->rowCount();
    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                        kDiversitySpatialSecondPoll};
    bandTick(a);
    CHECK(waterfall->rowCount() == rowsBeforeSecondPoll + 1);

    waterfall->resize(800, 300);
    const int column = 2;
    const int x = int((double(column) + 0.5) / 8.0 * 800.0);

    // Row 0 is the poll that just landed; row 1 is the one under it, and it
    // still carries its own three numbers.
    const QString newest = waterfall->readoutAt(x, 0);
    CHECK(newest.contains(QStringLiteral("kHz")));
    CHECK(newest.contains(QStringLiteral("phase 30\u00B0")));
    CHECK(newest.contains(QStringLiteral("coherence 0.95")));
    CHECK(newest.contains(QStringLiteral("level -32.0 dB")));
    const QString older = waterfall->readoutAt(x, 1);
    CHECK(older.contains(QStringLiteral("phase -90\u00B0")));
    CHECK(older.contains(QStringLiteral("coherence 0.70")));
    CHECK(older.contains(QStringLiteral("level -55.0 dB")));

    // Below the rows that have actually been lived through there is no
    // measurement, so there is no readout to invent.
    CHECK(waterfall->readoutAt(x, 200).isEmpty());

    // Moving over the picture arms the crosshair on that bin and that row;
    // leaving disarms it, so a stale cross never sits on a live picture.
    QMouseEvent move(QEvent::MouseMove, QPointF(x, 0), QPointF(x, 0), QPointF(x, 0),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(waterfall, &move);
    CHECK(waterfall->hoverColumn() == column);
    CHECK(waterfall->hoverRow() == 0);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(waterfall, &leave);
    CHECK(waterfall->hoverColumn() == -1);
    CHECK(waterfall->hoverRow() == -1);

    w->close();
    settle();
    closedToStart();
}

// (b4) The key keeps its scale and its grey however narrow the window gets --
// those two are what make a pixel readable -- and the row it lives in never
// grows, because the BAND page has no vertical room to give it.
void testTheLegendKeepsItsScaleAndItsGreyAtAnyWidth()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* legend = w->findChild<AetherSDR::DiversitySpatialLegend*>();
    CHECK(legend != nullptr);
    if (!legend)
        return;
    const int rowHeight = 21;
    CHECK(legend->sizeHint().height() == rowHeight);
    CHECK(legend->height() == rowHeight);

    for (int wide : {900, 260}) {
        legend->resize(wide, legend->height());
        const QImage shot = legend->grab().toImage();
        CHECK(shot.height() == rowHeight);
        int hued = 0;
        int greys = 0;
        for (int y = 0; y < shot.height(); ++y) {
            for (int px = 0; px < shot.width(); ++px) {
                const QColor c = shot.pixelColor(px, y);
                if (c.saturation() > 200)
                    ++hued;
                // The "no direction" swatch: no hue at all, and mid-bright, so
                // it reads as grey rather than as dark.
                if (c.saturation() == 0 && c.value() > 120 && c.value() < 170)
                    ++greys;
            }
        }
        CHECK(hued > 100);
        CHECK(greys >= 100);
    }

    w->close();
    settle();
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
    CHECK(cell(table, 0, 2) == QStringLiteral("0.82"));
    CHECK(cell(table, 1, 2) == QStringLiteral("0.55"));
    CHECK(cell(table, 2, 2) == QStringLiteral("0.31"));
    // Signed dB, both directions: the second candidate's pair is COSTING it.
    CHECK(cell(table, 0, 9) == QStringLiteral("+1.4"));
    CHECK(cell(table, 1, 9) == QStringLiteral("-0.3"));
    // 184 s of activity is three minutes four, not "184".
    CHECK(cell(table, 0, 5) == QStringLiteral("3:04"));
    // Somebody talking as we look is "now", not "0 s".
    CHECK(cell(table, 0, 6) == QStringLiteral("now"));
    CHECK(cell(table, 1, 6) == QStringLiteral("12 s"));
    // This gate names nothing: the kind column is a dash on every row, and
    // the strip has no bands to colour.
    CHECK(cell(table, 0, 1) == QStringLiteral("—"));
    CHECK(cell(table, 2, 1) == QStringLiteral("—"));

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
    CHECK(cell(table, 2, 2) == QStringLiteral("0.31"));
    for (int col : {1, 3, 4, 5, 6, 7, 8, 9})
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

// (f) A gate that classifies what it found says so in the row, in the hover
// and in the colour of the strip -- and a verdict with no confidence behind
// it still gets its word rather than an invented number.
void testTheKindColumnNamesWhatTheGateFound()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull, kDiversitySpatial, kDiversityFinderKinds);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QTableWidget* table = finderTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;
    CHECK(table->rowCount() == 3);
    // The word, then how sure -- and "cw" on the wire is CW on the air.
    CHECK(cell(table, 0, 1) == QStringLiteral("voice 0.91"));
    CHECK(cell(table, 1, 1) == QStringLiteral("CW 0.64"));
    // A verdict with no confidence is the word alone, not "carrier 0.00".
    CHECK(cell(table, 2, 1) == QStringLiteral("carrier"));
    // The rank is still the gate's: naming a row does not reorder it.
    CHECK(cell(table, 0, 2) == QStringLiteral("0.82"));

    // The hover is one line: the word and how sure the gate is. The full
    // explanation of what the verdict was made of is the accessible
    // description, so a screen reader still gets it.
    const QString tip = cellTip(table, 1, 1);
    CHECK(tip.contains(QStringLiteral("CW")));
    CHECK(tip.contains(QStringLiteral("0.64")));
    CHECK(tip.length() <= 90);
    const QString longTip =
        table->item(1, 1)->data(Qt::AccessibleDescriptionRole).toString();
    CHECK(longTip.contains(QStringLiteral("keyed hard on and off")));
    CHECK(longTip.contains(QStringLiteral("0.64")));
    // Every other cell keeps the frequency hover it always had.
    CHECK(cellTip(table, 1, 2).isEmpty());

    // Two of the three carry a width, so two stretches of the strip are
    // coloured; the carrier that sent none paints nothing rather than a band
    // of a width we made up.
    auto* strip = w->findChild<AetherSDR::DiversityActivityStrip*>();
    CHECK(strip != nullptr);
    if (strip)
        CHECK(strip->bandCount() == 2);

    // An empty finder takes the bands away with the rows...
    net.routes[QStringLiteral("/diversity/finder")] = {
        QNetworkReply::NoError,
        QByteArrayLiteral(R"({"available": false, "reason": "not aligned"})")};
    for (int i = 0; i < 4; ++i)
        bandTick(a);
    CHECK(table->rowCount() == 0);
    if (strip)
        CHECK(strip->bandCount() == 0);
    // ...and the legend gives its lines to the gate's reason, in words.
    auto* caption = w->findChild<QLabel*>(QStringLiteral("diversityWindowFinderCaption"));
    CHECK(caption != nullptr);
    if (caption) {
        CHECK(caption->text().startsWith(QStringLiteral("nothing to find yet: ")));
        CHECK(caption->text().contains(QStringLiteral("not aligned")));
        CHECK(caption->text().count(QLatin1Char('\n')) == 1);
    }
    // Rows back, legend back.
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError,
                                                       kDiversityFinderKinds};
    for (int i = 0; i < 4; ++i)
        bandTick(a);
    CHECK(table->rowCount() == 3);
    if (caption)
        CHECK(caption->text().startsWith(QStringLiteral("voice / CW / data")));
    closedToStart();
}

// (g) The same promise the SLICE page makes: at the size the window opens at,
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
    testCoherenceGatesTheColourRatherThanScalingIt();
    testTheHoverReadoutQuotesTheRowUnderThePointer();
    testTheLegendKeepsItsScaleAndItsGreyAtAnyWidth();
    testFinderPayloadFillsTheTableInTheGatesOrder();
    testTuneButtonTunesTheRowAndAsksForTrack();
    testOldGatePayloadsRenderWithoutInventingAnything();
    testTheKindColumnNamesWhatTheGateFound();
    testNothingScrollsOnTheBandPageAtTheInitialSize();

    std::printf("\n%d diversity band test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
