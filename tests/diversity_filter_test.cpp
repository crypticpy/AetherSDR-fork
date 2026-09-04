// The Diversity window's FILTER page -- the OPEN CHAIN button and the two
// PAIR STAGES a single receiver could never have (POST-FILTER, sub-band MRC)
// -- driven through a real AetherGateApplet and socket-free.
//
// Same harness as tests/diversity_window_test.cpp, tests/diversity_band_test
// .cpp and tests/diversity_site_test.cpp, for the same reason: the page owns
// no transport. It is reached by opening the sidebar's window and pressing
// FILTER, it is fed by the applet's own /diversity poll (POST-FILTER and MRC)
// and by DiversityBandPoller's /filter poll (the FLOW strip's last step,
// still read here though nothing on this page shows it any more), and every
// control on it leaves as a request signal the panel forwards.
//
// Everything this page used to own -- the response curve, its two draggable
// edges, PRESETS, and the four columns of generic slice-filter controls -- is
// the gate's own CHAIN window now (AetherGateChainWindow, built and tested
// alongside AetherGateChainStrip.cpp/AetherGateChainStage.cpp). Nothing here
// re-tests that window; this file only tests the button that opens it and the
// two stages that stayed.
//
// A fifth binary because the FILTER-adjacent files are at the 800-line budget
// AGENTS.md asks for, and because opening the window writes
// DiversityWindowVisible into the process-wide AppSettings cache -- every case
// here starts from a known closed state for the same reason every case in
// those files does.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
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

const QString kDash = QStringLiteral("—");

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Brings an applet up to "gate present, diversity live" with every route the
// FILTER page needs answered. /filter/set and /filter/notch reply with the
// same status object a poll returns -- they still exist on the wire for the
// CHAIN window and the FLOW strip, even though nothing on THIS page writes
// them any more.
void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& filter = kDiversityFilterStatus,
                 const QByteArray& diversity = kDiversityFull)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
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

QToolButton* pageButton(DiversityWindow* w, const char* name)
{
    return w->findChild<QToolButton*>(QString::fromLatin1(name));
}

// One more tick of the band poller, without waiting out half a second of real
// time. Only the FLOW strip's last step still rides on this page's /filter
// read; POST-FILTER and MRC do not.
void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// Opens the window and switches it to FILTER, which is also what starts the
// /filter poll -- there is deliberately no other way in. /diversity is
// fetched once at connect, before there is a window to feed it to, so one
// applet tick follows -- the same reason tests/diversity_talker_test.cpp's
// openWindow() ticks before returning.
DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    tick(a);
    pageButton(w, "diversityWindowPageFilter")->click();
    settle();
    settle();
    return w;
}

QString labelText(DiversityWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

// A plain /filter poll logs the path with no query; /filter/set and
// /filter/notch always carry one. startsWith() would conflate the three.
int pollCount(const FakeGate& net)
{
    int n = 0;
    for (const QString& s : net.log) {
        if (s == QLatin1String("/filter"))
            ++n;
    }
    return n;
}

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

// (a) /filter is polled only while the FILTER page is on screen: not on SLICE,
// not on BAND, not on SITE, not with the window closed, and never before it has
// been opened at all. Unchanged by the PAIR STAGES round: the FLOW strip still
// wants this page's /filter read, even though nothing drawn on the page itself
// does. FILTER itself never touches /diversity/spatial or /diversity/beacons --
// the two DO get one background-timer fetch apiece the moment the window is
// built (see DiversityBandPoller::backgroundPoll()), but that fires on open,
// before FILTER is ever selected, so what this test actually checks below is
// that switching TO FILTER adds nothing further to either.
void testFilterPageStartsAndStopsTheFilterPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    CHECK(pollCount(net) == 0);

    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    // Opening lands on SLICE, which polls nothing of its own /filter.
    CHECK(pollCount(net) == 0);
    const int spatialOnOpen = net.count(QStringLiteral("/diversity/spatial"));
    const int beaconsOnOpen = net.count(QStringLiteral("/diversity/beacons"));

    pageButton(w, "diversityWindowPageFilter")->click();
    settle();
    const int running = pollCount(net);
    CHECK(running > 0);
    // FILTER's neighbours stay off while it is up: the span and the beacon
    // watch are not what this page is about, so selecting FILTER must not add
    // to either beyond the one background fetch already primed on open.
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == spatialOnOpen);
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == beaconsOnOpen);

    // MUTATION: leave the page. The poll stops where it is, and comes back when
    // the page does -- the page switch is the whole subscription.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    filterTick(a);
    const int parked = pollCount(net);
    CHECK(parked == running);
    pageButton(w, "diversityWindowPageFilter")->click();
    settle();
    CHECK(pollCount(net) > parked);

    // And closing the window stops it again, with no page switch involved.
    const int beforeClose = pollCount(net);
    w->close();
    settle();
    filterTick(a);
    CHECK(pollCount(net) == beforeClose);
    closedToStart();
}

// (b) The OPEN CHAIN button asks the window for the chain window and nothing
// else -- it has no idea AetherGateChainWindow exists, only that DiversityWindow
// wired something to its click.
void testOpenChainButtonEmitsRequestOpenChain()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* button = child<QPushButton>(w, "diversityWindowFilterOpenChain");
    CHECK(button != nullptr);
    if (!button)
        return;

    QSignalSpy spy(w, &DiversityWindow::requestOpenChain);
    button->click();
    settle();
    CHECK(spy.count() == 1);

    // MUTATION: a second click is a second ask, not a no-op toggle -- the
    // button is a plain push, not a checkable one.
    button->click();
    settle();
    CHECK(spy.count() == 2);
    closedToStart();
}

// (c) POST-FILTER: three exclusive buttons write one wire value each, and the
// gate's own answer checks the group back and fills the readout -- "v1" with
// no numbers to show, the full sentence once V2 has measured something, and
// nothing invented for a v2 gate that has not measured anything yet.
void testPostFilterButtonsWriteAndCheckBack()
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
    auto* v1 = child<QPushButton>(w, "diversityWindowFilterPostV1");
    auto* v2 = child<QPushButton>(w, "diversityWindowFilterPostV2");
    CHECK(off != nullptr && v1 != nullptr && v2 != nullptr);
    if (!off || !v1 || !v2)
        return;

    // kDiversityFull carries no "post" object at all: off, and a dash.
    CHECK(off->isChecked());
    CHECK(!v1->isChecked());
    CHECK(!v2->isChecked());
    CHECK(labelText(w, "diversityWindowFilterPostReadout") == kDash);

    v2->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?post=v2"));

    // MUTATION: the other two buttons write the other two values.
    v1->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?post=on"));

    off->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?post=off"));

    // The gate's own answer checks the group back and fills the readout with
    // its own measurement -- not the click that just went out.
    QByteArray withV2 = kDiversityFull;
    withV2.replace("\"capture\"",
                   "\"post\": {\"enabled\": true, \"version\": 2, "
                   "\"snr_in_db\": 7.7, \"snr_out_db\": 10.8, "
                   "\"pause_fraction\": 0.10}, \"capture\"");
    CHECK(withV2 != kDiversityFull);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, withV2};
    tick(a);
    CHECK(v2->isChecked());
    CHECK(!v1->isChecked());
    CHECK(!off->isChecked());
    CHECK(labelText(w, "diversityWindowFilterPostReadout")
          == QStringLiteral("in +7.7 dB -> out +10.8 dB, pauses 10 %"));

    // MUTATION: V1 has no such measurement, so it reads out as its own name
    // rather than a dash that would look like a failed read.
    QByteArray withV1 = kDiversityFull;
    withV1.replace("\"capture\"",
                   "\"post\": {\"enabled\": true, \"version\": 1}, \"capture\"");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, withV1};
    tick(a);
    CHECK(v1->isChecked());
    CHECK(!v2->isChecked());
    CHECK(labelText(w, "diversityWindowFilterPostReadout") == QStringLiteral("v1"));

    // MUTATION: V2 enabled with nothing measured yet reads the same as V1,
    // rather than a half-filled sentence or an invented number.
    QByteArray v2NoNumbers = kDiversityFull;
    v2NoNumbers.replace("\"capture\"",
                        "\"post\": {\"enabled\": true, \"version\": 2}, \"capture\"");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, v2NoNumbers};
    tick(a);
    CHECK(v2->isChecked());
    CHECK(labelText(w, "diversityWindowFilterPostReadout") == QStringLiteral("v1"));
    closedToStart();
}

// (d) SUB-BAND MRC: one checkable button writing on/off, following the gate's
// answer for both the checked state and the readout -- a dash whether MRC is
// off, or on with nothing measured, so the two cannot be told apart from a
// glance at the button alone.
void testMrcTogglesAndReadoutFormatting()
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
    CHECK(labelText(w, "diversityWindowFilterMrcReadout") == kDash);

    mrc->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?mrc=on"));

    // MUTATION: the other direction writes the other value.
    mrc->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?mrc=off"));

    // The gate's own answer checks the button back and fills the readout.
    QByteArray withMrc = kDiversityFull;
    withMrc.replace("\"capture\"",
                    "\"mrc\": {\"enabled\": true, \"gain_over_broadband_db\": 0.2, "
                    "\"bins_used\": 120}, \"capture\"");
    CHECK(withMrc != kDiversityFull);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, withMrc};
    tick(a);
    CHECK(mrc->isChecked());
    CHECK(labelText(w, "diversityWindowFilterMrcReadout")
          == QStringLiteral("+0.2 dB over broadband, 120 bins"));

    // MUTATION: enabled with nothing measured yet is a dash, not an invented
    // gain of zero.
    QByteArray mrcNoNumbers = kDiversityFull;
    mrcNoNumbers.replace("\"capture\"", "\"mrc\": {\"enabled\": true}, \"capture\"");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, mrcNoNumbers};
    tick(a);
    CHECK(mrc->isChecked());
    CHECK(labelText(w, "diversityWindowFilterMrcReadout") == kDash);
    closedToStart();
}

// (e) The same promise the other three pages make: at the size the window
// opens at, nothing on the FILTER page is behind a scrollbar -- checked here
// with both readouts filled at once, which is this page's widest content now
// that the four generic columns are gone.
void testNothingScrollsOnTheFilterPageAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = child<QScrollArea>(w, "diversityWindowFilterScroll");
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    CHECK(w->minimumSizeHint().width() <= 1120);

    // MUTATION: the widest content the page can hold -- both readouts filled
    // with a negative, two-digit-fraction reading all at once.
    QByteArray widest = kDiversityFull;
    widest.replace("\"capture\"",
                   "\"post\": {\"enabled\": true, \"version\": 2, "
                   "\"snr_in_db\": -12.3, \"snr_out_db\": -8.4, "
                   "\"pause_fraction\": 0.42}, "
                   "\"mrc\": {\"enabled\": true, \"gain_over_broadband_db\": -3.1, "
                   "\"bins_used\": 128}, \"capture\"");
    CHECK(widest != kDiversityFull);
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, widest};
    tick(a);
    w->grab();
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_filter_test"));
    QApplication app(argc, argv);

    testFilterPageStartsAndStopsTheFilterPoll();
    testOpenChainButtonEmitsRequestOpenChain();
    testPostFilterButtonsWriteAndCheckBack();
    testMrcTogglesAndReadoutFormatting();
    testNothingScrollsOnTheFilterPageAtTheInitialSize();

    std::printf("\n%d diversity filter test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
