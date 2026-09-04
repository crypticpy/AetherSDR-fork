// The Diversity window's SITE page -- the NOISE PROFILE panel, the BEACONS
// watch and the per-bin weights checkbox the SLICE page grew beside them --
// driven through a real AetherGateApplet and socket-free.
//
// Same harness as tests/diversity_window_test.cpp and tests/diversity_band_test
// .cpp, and for the same reason: the page owns no transport. It is reached by
// opening the sidebar's window and pressing SITE, its beacon watch is fed by
// the applet's DiversityBandPoller, and the one control on it leaves as a
// request signal the panel forwards. Driving it any other way would be testing
// a wiring diagram we drew rather than the one that ships.
//
// A fourth binary because both of the others are at the 800-line budget
// AGENTS.md asks for, and because opening the window writes
// DiversityWindowVisible into the process-wide AppSettings cache -- every case
// here starts from a known closed state for the same reason every case there
// does.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDateTime>
#include <QCheckBox>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>

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

const QString kDash = QStringLiteral("—");

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Brings an applet up to "gate present, diversity live" with every route the
// SITE page needs answered.
// /diversity/compass, reduced to the one block the SITE page reads. The gate
// answers phase and coherence long before it can turn either into a bearing --
// that needs beacons of known bearing on two bands -- so the with-bearing and
// phase-only forms below are both ordinary, and the page has to say which it
// has rather than printing a direction nothing measured.
const QByteArray kCompassBearing = R"({"available": true,
    "noise": {"available": true, "kind": "hum", "phase_deg": 106.5,
              "coherence": 0.415, "bearing_deg": 212.0, "mirror_deg": 32.0,
              "bins": 147, "since": 1756867200, "reason": ""}})";

const QByteArray kCompassPhaseOnly = R"({"available": true,
    "noise": {"available": true, "kind": "hum", "phase_deg": 106.5,
              "coherence": 0.415, "bearing_deg": null, "mirror_deg": null,
              "bins": 147, "since": null,
              "reason": "hum on 147 bins at 0.42, phase only: 0 beacon(s) with a bearing and a ratio, 4 needed over 2 bands"}})";

void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity,
                 const QByteArray& beacons = kDiversityBeacons,
                 const QByteArray& compass = kCompassBearing)
{
    if (!compass.isEmpty())
        net.routes[QStringLiteral("/diversity/compass")] = {QNetworkReply::NoError,
                                                             compass};
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                        kDiversitySpatial};
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError,
                                                       kDiversityFinder};
    if (!beacons.isEmpty())
        net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                            beacons};
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

// Opens the window and switches it to SITE, which is also what starts the
// beacon poll -- there is deliberately no other way in.
DiversityWindow* openOnSite(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    pageButton(w, "diversityWindowPageSite")->click();
    settle();
    // The window is built empty and filled by the next poll -- opening it does
    // not replay the last one, exactly as it does not in the real applet.
    tick(a);
    return w;
}

// One more tick of the poller, without waiting out a second of real time.
void siteTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

QString labelText(DiversityWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

QTableWidget* beaconTable(DiversityWindow* w)
{
    return w->findChild<QTableWidget*>(QStringLiteral("diversityWindowBeaconTable"));
}

QString cell(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->text() : QString();
}

// (a) /diversity/beacons is polled at SITE's own 1 Hz only while the SITE
// page is on screen -- not on SLICE, not on BAND, not with the window closed,
// and never before it has been opened at all. Its neighbour /diversity/compass
// follows the same rule. Underneath that, the BACKGROUND timer primes both
// once every half minute (starting at once, not a wait for its own first
// tick) whenever the window exists and the gate is dual-tuner, REGARDLESS of
// page -- that half is the B-SITE-1 fix and is what this test's SLICE/BAND
// legs below are actually checking for: the background half firing, and
// SITE's own foreground poll not double-counting it once the operator gets
// there.
void testSitePageStartsAndStopsTheBeaconPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    CHECK(a.gatePresent());

    // Nothing opened: nothing asked, background included -- there is no
    // window yet for either reply to reach.
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == 0);
    CHECK(!a.diversityPanel()->wantsSitePoll());

    // Open on SLICE: SITE's own foreground poll has no reason to run, but the
    // background timer starts at once (B-SITE-1) and primes the beacon table
    // immediately even though nobody has ever opened SITE this session.
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(!w->sitePageVisible());
    CHECK(net.count(QStringLiteral("/diversity/beacons")) >= 1);
    const int beaconsAfterOpen = net.count(QStringLiteral("/diversity/beacons"));
    tick(a);
    // The applet's own /status+/diversity poll (tick()) does not itself touch
    // /diversity/beacons, and 20 real ms is not another background tick.
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsAfterOpen);

    // BAND draws neither the noise profile nor the beacons, so its own
    // foreground poll must not pay for either -- the background priming above
    // already covered them, and BAND becoming visible does not trigger
    // another background tick on its own.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    CHECK(!w->sitePageVisible());
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsAfterOpen);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) >= 1);

    // SITE: the beacon route, immediately -- not one tick later. And the BAND
    // page's own two routes stop, because nobody is looking at them.
    pageButton(w, "diversityWindowPageSite")->click();
    settle();
    CHECK(w->sitePageVisible());
    CHECK(!w->bandPageVisible());
    CHECK(a.diversityPanel()->wantsSitePoll());
    CHECK(net.count(QStringLiteral("/diversity/beacons")) >= 1);
    auto* timer = a.findChild<QTimer*>(QStringLiteral("gateDiversityBandTimer"));
    CHECK(timer != nullptr && timer->isActive());
    // 1 Hz, not the BAND page's 4 Hz: there is no waterfall on this page.
    CHECK(timer != nullptr && timer->interval() == 1000);

    const int spatialOnSite = net.count(QStringLiteral("/diversity/spatial"));
    const int beaconsBefore = net.count(QStringLiteral("/diversity/beacons"));
    for (int i = 0; i < 3; ++i)
        siteTick(a);
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsBefore + 3);
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialOnSite);

    // Back to SLICE: the timer stops and nothing more is asked.
    pageButton(w, "diversityWindowPageSlice")->click();
    settle();
    CHECK(timer != nullptr && !timer->isActive());
    const int beaconsOnSlice = net.count(QStringLiteral("/diversity/beacons"));
    tick(a);
    settle();
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsOnSlice);

    // Back to SITE, then hide the whole window: same again. A hidden window is
    // not looking at anything, whichever page it was left on.
    pageButton(w, "diversityWindowPageSite")->click();
    settle();
    CHECK(timer->isActive());
    w->hide();
    settle();
    CHECK(!timer->isActive());
    CHECK(!a.diversityPanel()->wantsSitePoll());
    const int beaconsHidden = net.count(QStringLiteral("/diversity/beacons"));
    tick(a);
    settle();
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsHidden);
    closedToStart();
}

// (b) The noise profile is read as sentences, not as a field dump: a mains
// verdict, an impulse rate with a size, the lines that are not mains
// harmonics, and the per-bin refinement. A null profile says so.
void testNoiseProfileRendersTheVerdictImpulsesAndLines()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // 60 Hz grid: the comb is at TWICE the mains rate, which is the rectifier
    // signature the caption explains.
    CHECK(labelText(w, "diversityWindowNoiseProfileVerdictLabel")
          == QStringLiteral("60 Hz grid: 120 Hz hum 13.7 dB, 2 harmonics"));
    // 14.8 a second rounds to 15, and the size is the second half of the fact.
    CHECK(labelText(w, "diversityWindowNoiseProfileImpulsesLabel")
          == QStringLiteral("impulses: 15 /s at 12.5 dB"));
    CHECK(labelText(w, "diversityWindowNoiseProfilePeriodicLabel")
          == QStringLiteral("lines: 182.1 Hz 18.6 dB · 431.0 Hz 9.2 dB"));
    CHECK(labelText(w, "diversityWindowNoiseProfileSecondsLabel")
          == QStringLiteral("measured over 2.0 s"));
    CHECK(labelText(w, "diversityWindowSubbandLineLabel")
          == QStringLiteral("Per-bin weights: on, 33 bins refined, +0.0 dB"));

    // MUTATION: a 50 Hz grid with one harmonic and no impulses at all. Every
    // derived number has to follow -- the 100 Hz comb, the singular, "none".
    QByteArray fifty = kDiversityStatusWithSite;
    fifty.replace("\"mains_hz\": 60.0", "\"mains_hz\": 50.0");
    fifty.replace("\"harmonics\": 2", "\"harmonics\": 1");
    fifty.replace("\"impulses_per_s\": 14.8", "\"impulses_per_s\": 0.0");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, fifty};
    tick(a);
    CHECK(labelText(w, "diversityWindowNoiseProfileVerdictLabel")
          == QStringLiteral("50 Hz grid: 100 Hz hum 13.7 dB, 1 harmonic"));
    CHECK(labelText(w, "diversityWindowNoiseProfileImpulsesLabel")
          == QStringLiteral("impulses: none"));

    // MUTATION: no mains lock at all. "no mains-locked hum" is a measurement;
    // "0 Hz grid" would be a nonsense.
    QByteArray unlocked = kDiversityStatusWithSite;
    unlocked.replace("\"mains_hz\": 60.0", "\"mains_hz\": null");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, unlocked};
    tick(a);
    CHECK(labelText(w, "diversityWindowNoiseProfileVerdictLabel")
          == QStringLiteral("no mains-locked hum"));

    // A gate that is up but has not profiled yet: dashes and a sentence saying
    // why, never a profile of silence. And subband off is "off", not a dash.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                               kDiversityStatusSiteNull};
    tick(a);
    CHECK(labelText(w, "diversityWindowNoiseProfileVerdictLabel")
              .startsWith(QStringLiteral("no noise profile yet")));
    CHECK(labelText(w, "diversityWindowNoiseProfileImpulsesLabel")
          == QStringLiteral("impulses: %1").arg(kDash));
    CHECK(labelText(w, "diversityWindowNoiseProfilePeriodicLabel")
          == QStringLiteral("lines: %1").arg(kDash));
    CHECK(labelText(w, "diversityWindowSubbandLineLabel")
          == QStringLiteral("Per-bin weights: off"));

    // A gate too old to have either key: the profile and the refinement both
    // say so rather than reading as measured zeros.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityOldGate};
    tick(a);
    CHECK(labelText(w, "diversityWindowNoiseProfileVerdictLabel")
              .startsWith(QStringLiteral("no noise profile yet")));
    CHECK(labelText(w, "diversityWindowSubbandLineLabel")
          == QStringLiteral("Per-bin weights: %1").arg(kDash));
    closedToStart();
}

// (c) The beacon table is the SCHEDULE, always eighteen rows in transmission
// order, with the results filled in where there are any -- and only for the
// band the schedule frequency is on.
void testBeaconTableIsInScheduleOrderAndFillsWhatWasHeard()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    QTableWidget* table = beaconTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;

    // Eighteen rows, in the order they transmit -- NOT in the gate's results
    // order (which sent KH6RS, OH2B, ZL6B) and not sorted by signal.
    CHECK(table->rowCount() == 18);
    CHECK(cell(table, 0, 0) == QStringLiteral("4U1UN"));
    CHECK(cell(table, 0, 1) == QStringLiteral("United Nations, New York"));
    CHECK(cell(table, 3, 0) == QStringLiteral("KH6RS"));
    CHECK(cell(table, 4, 0) == QStringLiteral("ZL6B"));
    CHECK(cell(table, 12, 0) == QStringLiteral("4X6TU"));
    CHECK(cell(table, 17, 0) == QStringLiteral("YV5B"));

    // The header is the schedule, read as a sentence.
    const QString header = labelText(w, "diversityWindowBeaconHeaderLabel");
    CHECK(header.contains(QStringLiteral("14.100 MHz")));
    CHECK(header.contains(QStringLiteral("slot 12")));
    CHECK(header.contains(QStringLiteral("now: 4X6TU Tel Aviv, Israel")));
    CHECK(header.contains(QStringLiteral("9 s left")));

    // KH6RS was heard, one step down: the mark is filled, every number is the
    // gate's, and the ladder shows one of four lit.
    CHECK(cell(table, 3, 2) == QStringLiteral("●"));
    CHECK(cell(table, 3, 3) == QStringLiteral("-3.3"));
    CHECK(cell(table, 3, 4) == QStringLiteral("-5.5"));
    CHECK(cell(table, 3, 5) == QStringLiteral("-1.1"));
    CHECK(cell(table, 3, 6) == QStringLiteral("-15"));
    CHECK(cell(table, 3, 7) == QStringLiteral("0.07"));
    CHECK(cell(table, 3, 8) == QStringLiteral("+2.0"));
    CHECK(cell(table, 3, 9) == QStringLiteral("●○○○"));
    CHECK(cell(table, 3, 10) == QStringLiteral("2 min ago"));

    // OH2B came round and was NOT heard: a hollow mark and no ladder at all,
    // which is a different claim from "not tried yet".
    CHECK(cell(table, 13, 0) == QStringLiteral("OH2B"));
    CHECK(cell(table, 13, 2) == QStringLiteral("○"));
    CHECK(cell(table, 13, 3) == kDash);
    CHECK(cell(table, 13, 9) == QStringLiteral("○○○○"));

    // ZL6B's result is from 15 m. It is remembered, but drawing it here would
    // be a lie about 20 m.
    CHECK(cell(table, 4, 2) == kDash);
    CHECK(cell(table, 4, 9) == kDash);
    // A beacon nobody has heard on any band is all dashes and nothing else.
    CHECK(cell(table, 0, 2) == kDash);
    CHECK(cell(table, 0, 10) == kDash);

    // The beacon transmitting right now is lit, and it alone.
    QTableWidgetItem* now = table->item(12, 0);
    QTableWidgetItem* other = table->item(11, 0);
    CHECK(now != nullptr && other != nullptr);
    if (now && other)
        CHECK(now->background() != other->background());

    // MUTATION: the operator retunes to 15 m. The gate sends no results at all
    // with the new band, so what appears can only have come from memory:
    // ZL6B's 15 m result becomes the one that belongs on screen, and KH6RS's
    // 20 m one goes back to dashes.
    const QByteArray fifteen = R"({"available": true, "band_hz": 21150000.0,
        "slot": 3, "now": null, "results": [], "last": null})";
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError, fifteen};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
              .contains(QStringLiteral("21.150 MHz")));
    CHECK(cell(table, 4, 2) == QStringLiteral("●"));
    CHECK(cell(table, 4, 9) == QStringLiteral("●●●●"));
    CHECK(cell(table, 3, 2) == kDash);
    closedToStart();
}

// (d) An idle watch and an absent one are two different facts, and neither is
// an empty table with nothing said about it.
void testBeaconMessagesForNoBandAndNoRoute()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite, kDiversityBeaconsNoBand);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    QTableWidget* table = beaconTable(w);
    CHECK(table != nullptr);
    if (!table)
        return;

    // No beacon frequency in the span: say which frequencies would work,
    // because "nothing here" without them is an instrument that has given up.
    const QString idle = labelText(w, "diversityWindowBeaconHeaderLabel");
    CHECK(idle.startsWith(QStringLiteral("no beacon frequency in the span")));
    CHECK(idle.contains(QStringLiteral("14.100")));
    CHECK(idle.contains(QStringLiteral("28.200")));
    CHECK(table->rowCount() == 18);
    CHECK(cell(table, 3, 2) == kDash);

    // MUTATION: the gate starts watching 20 m. The same eighteen rows fill in.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                       kDiversityBeacons};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
              .contains(QStringLiteral("14.100 MHz")));
    CHECK(cell(table, 3, 2) == QStringLiteral("●"));

    // A gate too old for the route answers 404; one that has not aligned says
    // so itself. Both are "not available from this gate", not an idle watch.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                       kDiversityBeaconsUnavailable};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
          == QStringLiteral("beacon watch: not available from this gate"));
    CHECK(cell(table, 3, 2) == kDash);

    net.routes.remove(QStringLiteral("/diversity/beacons"));
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconHeaderLabel")
          == QStringLiteral("beacon watch: not available from this gate"));
    CHECK(table->rowCount() == 18);
    closedToStart();
}

// (e) The per-bin weights checkbox reflects the gate and writes to it, and a
// gate that has never heard of it cannot be made to look as if it had.
void testPerBinCheckboxReflectsStatusAndWrites()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    openButton(a)->click();
    settle();
    tick(a);
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    // It lives on the SLICE page, with the weight it is about.
    auto* check = w->findChild<QCheckBox*>(QStringLiteral("diversityWindowSubbandCheck"));
    CHECK(check != nullptr);
    if (!check)
        return;
    CHECK(check->isEnabled());
    CHECK(check->isChecked());
    CHECK(labelText(w, "diversityWindowSubbandValueLabel")
          == QStringLiteral("33 bins · +0.0 dB"));

    // Unticking it writes subband=off, by the same route every other diversity
    // write takes.
    QSignalSpy sets(a.diversityPanel(), &AetherGateDiversityPanel::requestSet);
    check->click();
    settle();
    CHECK(sets.count() == 1);
    CHECK(net.log.contains(QStringLiteral("/diversity/set?subband=off")));

    // MUTATION: the gate reports it off, with a different bin count and a real
    // gain. The control follows the gate, and the readout follows the numbers.
    QByteArray off = kDiversityStatusWithSite;
    off.replace("\"subband\": {\"enabled\": true, \"bins\": 33, \"extra_db\": 0.0}",
                "\"subband\": {\"enabled\": false, \"bins\": 17, \"extra_db\": 1.5}");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, off};
    tick(a);
    CHECK(!check->isChecked());
    CHECK(labelText(w, "diversityWindowSubbandValueLabel")
          == QStringLiteral("17 bins · +1.5 dB"));
    // A poll must not turn a read-back into another write.
    CHECK(sets.count() == 1);

    // Ticking it back writes subband=on.
    check->click();
    settle();
    CHECK(sets.count() == 2);
    CHECK(net.log.contains(QStringLiteral("/diversity/set?subband=on")));

    // A gate with no subband key at all: the control is dead and says so,
    // rather than sitting there unticked as if it had been measured off.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityOldGate};
    tick(a);
    CHECK(!check->isEnabled());
    CHECK(labelText(w, "diversityWindowSubbandValueLabel") == kDash);
    closedToStart();
}

// (f) The same promise the SLICE and BAND pages make: at the size the window
// opens at, nothing on the SITE page is behind a scrollbar -- and adding the
// checkbox to ANTENNAS did not put one on SLICE either.
void testNothingScrollsOnTheSitePageAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("diversityWindowSiteScroll"));
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());

    // The SLICE page carries the new checkbox, so it is re-checked here rather
    // than trusted to the other binary's copy of this guard.
    pageButton(w, "diversityWindowPageSlice")->click();
    settle();
    w->grab();
    auto* slice = w->findChild<QScrollArea*>(QStringLiteral("diversityWindowSliceScroll"));
    CHECK(slice != nullptr);
    if (!slice)
        return;
    CHECK(slice->widget()->minimumSizeHint().width() <= slice->viewport()->width());
    CHECK(slice->widget()->minimumSizeHint().height() <= slice->viewport()->height());
    CHECK(!slice->verticalScrollBar()->isVisible());
    CHECK(!slice->horizontalScrollBar()->isVisible());

    // MUTATION: the widest content the page can hold. The "no beacon
    // frequency" line names five frequencies and is the longest string on
    // either panel, and a null profile swaps the verdict for its own sentence.
    // If either of those were a wrapping label, this is where the scrollbar
    // would appear.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusSiteNull};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsNoBand};
    pageButton(w, "diversityWindowPageSite")->click();
    settle();
    tick(a);
    siteTick(a);
    w->grab();
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    closedToStart();
}

// The chain row's HEAR is what reaches the operator's ears; STEREO is loop A
// left and loop B right. A click writes source=stereo by the one route every
// diversity write takes, and a gate that reports stereo lights the button
// without a write of its own.
void testHearRowOffersStereoAndWritesIt()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    openButton(a)->click();
    settle();
    tick(a);
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    QAbstractButton* stereo = nullptr;
    for (auto* b : w->findChildren<QAbstractButton*>()) {
        if (b->property("diversityValue").toString() == QStringLiteral("stereo"))
            stereo = b;
    }
    CHECK(stereo != nullptr);
    if (!stereo)
        return;
    CHECK(!stereo->isChecked());
    // The chain row sits above the pages, outside every scroll area: a fourth
    // HEAR button must not push the window's minimum width past its opening
    // size, or the window would open wider than the tests' 1120 px.
    CHECK(w->minimumSizeHint().width() <= 1120);

    QSignalSpy sets(a.diversityPanel(), &AetherGateDiversityPanel::requestSet);
    stereo->click();
    settle();
    CHECK(sets.count() == 1);
    CHECK(net.log.contains(QStringLiteral("/diversity/set?source=stereo")));

    // MUTATION: the gate reports stereo. The row follows the gate; a poll is
    // not a write.
    QByteArray st = kDiversityStatusWithSite;
    st.replace("\"source\": \"combined\"", "\"source\": \"stereo\"");
    CHECK(st != kDiversityStatusWithSite);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, st};
    tick(a);
    CHECK(stereo->isChecked());
    CHECK(sets.count() == 1);
    closedToStart();
}

// The TALKERS table's TX column is the upper edge of the station's audio --
// their rig -- and the row's hover is the whole print. A talker the gate has
// not heard enough of shows a dash and no hover.
void testTalkersTableCarriesTheVoicePrint()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithPrint);
    openButton(a)->click();
    settle();
    tick(a);
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* table = w->findChild<QTableWidget*>(QStringLiteral("diversityWindowTalkersTable"));
    CHECK(table != nullptr);
    if (!table)
        return;
    const int tx = table->columnCount() - 1;
    CHECK(table->horizontalHeaderItem(tx)->text() == QStringLiteral("TX"));
    CHECK(table->rowCount() == 2);
    CHECK(table->item(0, tx) && table->item(0, tx)->text() == QStringLiteral("2.7k"));
    CHECK(table->item(0, 1) && table->item(0, 1)->toolTip().contains(QStringLiteral("4.1 syllables/s")));
    CHECK(table->item(0, 1)->toolTip().contains(QStringLiteral("300–2700 Hz")));
    CHECK(table->item(1, tx) && table->item(1, tx)->text() == QStringLiteral("—"));
    CHECK(table->item(1, 1) && table->item(1, 1)->toolTip().isEmpty());

    // MUTATION: a narrower rig, a slower talker. The cell and the hover follow.
    QByteArray st = kDiversityStatusWithPrint;
    st.replace("\"high_hz\": 2700", "\"high_hz\": 2400");
    st.replace("\"syllabic_hz\": 4.1", "\"syllabic_hz\": 2.9");
    CHECK(st != kDiversityStatusWithPrint);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, st};
    tick(a);
    CHECK(table->item(0, tx)->text() == QStringLiteral("2.4k"));
    CHECK(table->item(0, 1)->toolTip().contains(QStringLiteral("2.9 syllables/s")));
    closedToStart();
}

// The noise bearing line, on the NOISE PROFILE row with the other two facts
// about the same noise. Three states, and only one of them is a direction: the
// phase between two loops becomes a bearing only once the compass has fitted
// the array's geometry against beacons, and until then the line says so in as
// many words instead of printing a number nothing measured.
void testNoiseBearingSaysWhichOfItsThreeStatesItIsIn()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusWithSite);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* line = w->findChild<QLabel*>(QStringLiteral("diversityWindowSiteNoiseBearing"));
    CHECK(line != nullptr);
    if (!line)
        return;
    siteTick(a);

    // (1) A fit. Both the bearing and its reflection about the two loops'
    // baseline, because two elements in a line cannot tell them apart and
    // printing one would be a coin toss.
    const QString clock =
        QDateTime::fromSecsSinceEpoch(1756867200).toString(QStringLiteral("HH:mm"));
    CHECK(line->text()
          == QStringLiteral("hum from 212° (or 32°) · coh 0.41 · since ") + clock);
    // It is one line and it stays one width: a page that re-laid itself out
    // every time a number changed would be unreadable at a glance.
    CHECK(!line->wordWrap());
    const int width = line->minimumWidth();
    CHECK(width > 0);

    // (2) MUTATION: the same noise with no fit yet. The gate's sentence about
    // WHY is on the hover and never on the line -- it is a paragraph.
    net.routes[QStringLiteral("/diversity/compass")] = {QNetworkReply::NoError,
                                                         kCompassPhaseOnly};
    siteTick(a);
    CHECK(line->text()
          == QStringLiteral("hum: direction unknown — no compass fit yet"));
    CHECK(line->toolTip().contains(QStringLiteral("4 needed over 2 bands")));
    CHECK(!line->text().contains(QStringLiteral("needed over")));
    CHECK(line->minimumWidth() == width);

    // (3) MUTATION: a gate too old for the route at all. Not "no direction" --
    // nothing said, which is what a dash means everywhere else in this window.
    net.routes.remove(QStringLiteral("/diversity/compass"));
    siteTick(a);
    CHECK(line->text() == kDash);
    CHECK(line->minimumWidth() == width);
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_site_test"));
    QApplication app(argc, argv);

    testSitePageStartsAndStopsTheBeaconPoll();
    testNoiseProfileRendersTheVerdictImpulsesAndLines();
    testBeaconTableIsInScheduleOrderAndFillsWhatWasHeard();
    testBeaconMessagesForNoBandAndNoRoute();
    testPerBinCheckboxReflectsStatusAndWrites();
    testNothingScrollsOnTheSitePageAtTheInitialSize();
    testHearRowOffersStereoAndWritesIt();
    testTalkersTableCarriesTheVoicePrint();
    testNoiseBearingSaysWhichOfItsThreeStatesItIsIn();

    std::printf("\n%d diversity site test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
