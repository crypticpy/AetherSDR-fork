// The Diversity window's usability round: the two buttons on the pair row that
// now answer back, the AGC threshold spin the FILTER page grew beside them, and
// the window's own frame -- three sticky rows' worth of height reclaimed with
// nothing on any page behind a scrollbar. The FLOW line's own behaviour is
// tests/diversity_flow_line_test.cpp, split off when this file reached the
// 800-line budget.
//
// Same harness as the other diversity binaries -- a real AetherGateApplet in
// front of a fake, socket-free QNetworkAccessManager -- and a seventh binary
// for the reason all seven are separate: each is at the 800-line budget
// AGENTS.md asks for, and every window case wants the same fresh, process-wide
// AppSettings start.
//
// EVERY RENDERED VALUE HERE CARRIES A MUTATION: a second payload in which that
// value differs, asserted after the first. A readout that had been wired to
// nothing and simply printed a plausible sentence would pass the first
// assertion of every case below and fail the second, which is the only way to
// tell "reads the gate" from "looks like it reads the gate" in a window whose
// whole content is derived.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityFlowStrip.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversityFlowStrip;
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

#define CHECK_EQ(got, want)                                                          \
    do {                                                                             \
        const QString g_ = (got);                                                    \
        const QString w_ = (want);                                                   \
        if (g_ != w_) {                                                              \
            std::printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__,  \
                        qPrintable(g_), qPrintable(w_));                             \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// A copy of a fixture with one wire value swapped. Used rather than another
// frozen payload because what each case needs is the SAME site with one thing
// different -- a fixture that differed in more than the field under test could
// not prove which field the strip read.
QByteArray with(const QByteArray& body, const char* from, const char* to)
{
    QByteArray out = body;
    out.replace(from, to);
    return out;
}

void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& status = kDiversityStatusWithKinds)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/align")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
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

DiversityWindow* openWindow(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (w)
        tick(a);   // /diversity is fetched once before there is a window to feed
    return w;
}

// Makes a timer go off now. QTimer::timeout carries a QPrivateSignal, so it
// cannot be emitted from outside the class -- but moc strips that from the
// meta-method, and the point of every use below is to skip real seconds rather
// than to wait them out in a test.
void fire(QTimer* timer)
{
    if (!timer)
        return;
    const bool once = timer->isSingleShot();
    if (once)
        timer->stop();
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
}

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

QStringList eventLines(DiversityWindow* w)
{
    auto* list = w->findChild<QListWidget*>(QStringLiteral("diversityWindowEventsList"));
    QStringList out;
    if (!list)
        return out;
    for (int i = 0; i < list->count(); ++i)
        out << list->item(i)->text();
    return out;
}

bool anyLineContains(const QStringList& lines, const QString& needle)
{
    for (const QString& line : lines) {
        if (line.contains(needle))
            return true;
    }
    return false;
}

QString stripText(DiversityWindow* w)
{
    auto* label = child<QLabel>(w, "diversityWindowStatusLabel");
    return label ? label->text() : QString();
}

// The FLOW line at the foot of the window: one label of rich text, six steps.
DiversityFlowStrip* flowStrip(DiversityWindow* w)
{
    return w->findChild<DiversityFlowStrip*>(QStringLiteral("diversityWindowFlowStrip"));
}

bool flowHas(DiversityWindow* w, const QString& needle)
{
    auto* line = child<QLabel>(w, "diversityWindowFlowLine");
    return line && line->text().contains(needle);
}

// One more tick of the band poller, which is what reads /filter.
void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// --------------------------------------------------------------------------
// (f) REALIGN answers
// --------------------------------------------------------------------------

void testRealignNarratesOnItsOwnFace()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    // Realigning when the click lands, so the button has to WAIT rather than
    // reading the first status back as an answer.
    connectGate(a, net,
                with(kDiversityStatusWithKinds, "\"aligned\": true",
                     "\"aligned\": true, \"realigning\": true"));
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* button = child<QPushButton>(w, "diversityWindowRealignButton");
    CHECK(button != nullptr);
    if (!button)
        return;
    CHECK_EQ(button->text(), QStringLiteral("REALIGN"));

    button->click();
    settle();
    CHECK_EQ(button->text(), QStringLiteral("ALIGNING…"));
    CHECK(!button->isEnabled());

    // Still realigning: the button holds.
    tick(a);
    CHECK_EQ(button->text(), QStringLiteral("ALIGNING…"));

    // Done, and the lag moved. The gate's own two numbers, in all three
    // places the answer appears.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"lag_samples\": 3", "\"lag_samples\": -63")};
    tick(a);
    CHECK_EQ(button->text(), QStringLiteral("LAG -63 (was +3)"));
    CHECK(button->isEnabled());
    CHECK_EQ(stripText(w), QStringLiteral("realigned · lag -63 · was +3"));
    CHECK(anyLineContains(eventLines(w), QStringLiteral("realign: lag -63, was +3")));

    // MUTATION: a realign that changes nothing says so, and says it
    // differently -- "unchanged" is the answer an operator most needs and the
    // one a button that only ever printed the lag would hide.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(with(kDiversityStatusWithKinds, "\"lag_samples\": 3",
                  "\"lag_samples\": -63"),
             "\"aligned\": true", "\"aligned\": true, \"realigning\": true")};
    button->click();
    settle();
    CHECK_EQ(button->text(), QStringLiteral("ALIGNING…"));
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"lag_samples\": 3", "\"lag_samples\": -63")};
    tick(a);
    CHECK_EQ(button->text(), QStringLiteral("LAG -63"));
    CHECK_EQ(stripText(w), QStringLiteral("realigned · lag -63 · unchanged"));
    CHECK(anyLineContains(eventLines(w), QStringLiteral("realign: lag -63, unchanged")));
    w->close();
    settle();
    closedToStart();
}

void testRealignThatIsNeverAnsweredGivesTheButtonBack()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net,
                with(kDiversityStatusWithKinds, "\"aligned\": true",
                     "\"aligned\": true, \"realigning\": true"));
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* button = child<QPushButton>(w, "diversityWindowRealignButton");
    button->click();
    settle();
    CHECK(!button->isEnabled());
    // Six seconds, without waiting six seconds out.
    auto* timeout = w->findChild<QTimer*>(QStringLiteral("diversityWindowRealignTimeout"));
    CHECK(timeout != nullptr);
    if (!timeout)
        return;
    CHECK(timeout->isActive());
    fire(timeout);
    settle();
    CHECK_EQ(button->text(), QStringLiteral("REALIGN"));
    CHECK(button->isEnabled());
    CHECK(anyLineContains(eventLines(w), QStringLiteral("realign: no answer")));
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (f2) The PAIR strip's spectrum line answers "what is the panadapter
// actually drawing", which pan= and aligned alone do not: pan "combined"
// before the two tuners are aligned is loop A, not the sum.
// --------------------------------------------------------------------------

void testSpectrumLineFollowsPanAndAlignment()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    // kDiversityStatusWithKinds carries no "pan" key at all -- the older-gate
    // case -- and starts aligned.
    const QByteArray anchor = "\"aligned\": true, \"corr_peak\": 0.91,";
    const QByteArray combinedAligned =
        with(kDiversityStatusWithKinds, anchor.constData(),
             (anchor + " \"pan\": \"combined\",").constData());
    connectGate(a, net, combinedAligned);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* spectrum = child<QLabel>(w, "diversityWindowSpectrumLine");
    CHECK(spectrum != nullptr);
    if (!spectrum)
        return;
    CHECK_EQ(spectrum->text(), QStringLiteral("pan: A+B"));

    // MUTATION: the same pan choice, but not yet aligned -- the wire value
    // alone would still say "combined", which is why the line has to look at
    // aligned too.
    const QByteArray combinedUnaligned =
        with(combinedAligned, "\"aligned\": true, \"corr_peak\": 0.91,",
             "\"aligned\": false, \"corr_peak\": null,");
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, combinedUnaligned};
    tick(a);
    CHECK_EQ(spectrum->text(), QStringLiteral("pan: A, not aligned"));

    // MUTATION: pan pointed at the null leg on purpose, aligned again -- a
    // plain wire value with nothing for the readout to second-guess.
    const QByteArray nulled = with(kDiversityStatusWithKinds, anchor.constData(),
                                   (anchor + " \"pan\": \"nulled\",").constData());
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, nulled};
    tick(a);
    CHECK_EQ(spectrum->text(), QStringLiteral("pan: nulled"));

    // MUTATION: an older gate that has never sent "pan" at all -- nothing may
    // be guessed in its place.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithKinds};
    tick(a);
    CHECK_EQ(spectrum->text(), QStringLiteral("pan: —"));

    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (g) CAPTURE answers, from every page
// --------------------------------------------------------------------------

void testCaptureCountsDownAndNamesTheFileEverywhere()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    net.routes[QStringLiteral("/diversity/capture")] = {
        QNetworkReply::NoError,
        R"({"path": "/home/pi/.aether-gate/captures/div-20260902-2311.wav"})"};
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* button = child<QPushButton>(w, "diversityWindowCaptureButton");
    auto* spin = child<QSpinBox>(w, "diversityWindowCaptureSpin");
    CHECK(button && spin);
    if (!button || !spin)
        return;
    CHECK_EQ(button->text(), QStringLiteral("CAPTURE"));

    // Deliberately NOT settled: the request goes out on the click, and the
    // whole point of the countdown is what the button says in the seconds
    // before the gate answers.
    spin->setValue(3);
    button->click();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/capture")),
             QStringLiteral("/diversity/capture?seconds=3"));
    CHECK_EQ(button->text(), QStringLiteral("REC 3 s"));
    CHECK(!button->isEnabled());
    CHECK(!spin->isEnabled());

    // One second per tick, on the button's own timer rather than in real time.
    auto* countdown =
        w->findChild<QTimer*>(QStringLiteral("diversityWindowCaptureCountdown"));
    CHECK(countdown != nullptr);
    if (!countdown)
        return;
    fire(countdown);
    CHECK_EQ(button->text(), QStringLiteral("REC 2 s"));
    fire(countdown);
    CHECK_EQ(button->text(), QStringLiteral("REC 1 s"));

    // The answer: on the button, on the footer strip -- which is the only
    // thing in this window visible from every page, and the whole reason the
    // operator thought CAPTURE was doing nothing -- and in EVENTS.
    settle();
    CHECK_EQ(button->text(), QStringLiteral("SAVED"));
    CHECK(spin->isEnabled());
    CHECK_EQ(stripText(w), QStringLiteral("capture saved div-20260902-2311.wav"));
    CHECK(anyLineContains(eventLines(w), QStringLiteral("div-20260902-2311.wav")));
    // The basename on the button and the strip; the whole path only in the
    // readout's tooltip, because a path is not a thing to read at a glance.
    auto* readout = child<QLabel>(w, "diversityWindowCaptureLabel");
    CHECK(readout != nullptr);
    if (readout) {
        CHECK(!readout->text().contains(QStringLiteral("/home/pi")));
        CHECK_EQ(readout->toolTip(),
                 QStringLiteral("/home/pi/.aether-gate/captures/div-20260902-2311.wav"));
    }
    // Three seconds later the button is a button again.
    fire(w->findChild<QTimer*>(QStringLiteral("diversityWindowResultTimer")));
    CHECK_EQ(button->text(), QStringLiteral("CAPTURE"));

    // MUTATION: a refusal is a different word on the button, and the strip is
    // not borrowed for it -- there is no file to write down.
    net.routes[QStringLiteral("/diversity/capture")] = {
        QNetworkReply::NoError, R"({"error": "no disc space"})"};
    const QString strip = stripText(w);
    button->click();
    settle();
    CHECK_EQ(button->text(), QStringLiteral("FAILED"));
    CHECK(button->isEnabled());
    CHECK_EQ(stripText(w), strip);
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (h) Two extra rows and a strip, and every page still fits
// --------------------------------------------------------------------------

void testNothingScrollsOnAnyPageAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                        kDiversitySpatial};
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError,
                                                       kDiversityFinder};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsWithPattern};
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();

    // The tab row and the pair row are separate widgets at the top, and the
    // FLOW line is at the bottom rather than a third row under them. If any of
    // the three were folded back together this is what would notice.
    CHECK(child<QWidget>(w, "diversityWindowTabRow") != nullptr);
    CHECK(child<QWidget>(w, "diversityWindowChainRow") != nullptr);
    CHECK(child<QWidget>(w, "diversityWindowFlowStrip") != nullptr);
    CHECK(child<QLabel>(w, "diversityWindowPairCaption") != nullptr);
    CHECK(w->minimumSizeHint().width() <= 1120);

    const char* pages[][2] = {{"diversityWindowPageSlice", "diversityWindowSliceScroll"},
                              {"diversityWindowPageBand", "diversityWindowBandScroll"},
                              {"diversityWindowPageSite", "diversityWindowSiteScroll"},
                              {"diversityWindowPageFilter", "diversityWindowFilterScroll"}};
    for (const auto& page : pages) {
        child<QToolButton>(w, page[0])->click();
        settle();
        tick(a);
        filterTick(a);
        settle();
        w->grab();   // forces a full layout pass on an offscreen platform
        auto* scroll = child<QScrollArea>(w, page[1]);
        CHECK(scroll != nullptr);
        if (!scroll)
            continue;
        if (scroll->widget()->minimumSizeHint().height() > scroll->viewport()->height()
            || scroll->verticalScrollBar()->isVisible()
            || scroll->horizontalScrollBar()->isVisible()) {
            std::printf("FAIL %s scrolls: %dx%d in %dx%d\n", page[1],
                        scroll->widget()->minimumSizeHint().width(),
                        scroll->widget()->minimumSizeHint().height(),
                        scroll->viewport()->width(), scroll->viewport()->height());
            ++g_failed;
        }
    }

    // MUTATION: the longest state string every step can hold, all five at
    // once. A line that let its own text set its minimum width would push the
    // window's minimum past the size it opens at.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(with(kDiversityStatusWithKinds, "\"aligned\": true", "\"aligned\": false"),
             "\"source\": \"combined\"", "\"source\": \"stereo\"")};
    child<QToolButton>(w, "diversityWindowPageSlice")->click();
    tick(a);
    settle();
    w->grab();
    CHECK(w->minimumSizeHint().width() <= 1120);
    auto* scroll = child<QScrollArea>(w, "diversityWindowSliceScroll");
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// The FLOW strip's sixth step: DIG
// --------------------------------------------------------------------------
//
// /diversity/dig is the one thing in this window that MOVES the operator's own
// chain, so every assertion below is either the exact query the fake gate saw
// or the exact sentence the strip drew from the gate's own status. The strip
// composes no number of its own: a run whose gain the window had invented
// would read plausibly and be a lie about what the radio is doing.

// The route's status, one phase per fixture. Field names are the same across
// all of them -- that is the route's contract, and it is why the strip can
// read one object and know which of six states it is in.
const QByteArray kDigIdle = R"({"available": true, "running": false,
    "phase": "idle", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 0.0, "steps": [], "best": {}, "changed": {}})";

const QByteArray kDigRunning = R"({"available": true, "running": true,
    "phase": "searching", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 2.1, "elapsed_s": 72.0, "seconds": 180.0, "remaining_s": 108.0,
    "trials_planned": 24, "trials_done": 9,
    "steps": [{"knob": "post", "kept": true}, {"knob": "width", "kept": false}],
    "best": {"post": "v2"}, "changed": {"post": "v2"}})";

const QByteArray kDigDone = R"({"available": true, "running": false,
    "phase": "done", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 4.1, "objective_before": -3.2, "objective_after": 0.9,
    "steps": [{"knob": "nb", "kept": true}],
    "best": {"post": "v2", "width": [100, 2400], "nb_db": 11.0},
    "changed": {"post": "v2", "width": [100, 2400], "nb_db": 11.0}})";

QByteArray digRoute(FakeGate& net, const QByteArray& body)
{
    net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, body};
    return body;
}

// One status poll of /diversity/dig, without waiting out the window's own
// cadence. The window owns that cadence -- the strip keeps no transport -- so
// this is the same door a real second would come through.
void digTick(DiversityWindow* w)
{
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();
}

// (a) The offer. Three durations, each writing the gate's own key and value,
// and a gate that cannot dig has no step and no buttons at all.
void testDigOffersThreeDurationsAndWritesTheGatesOwnQuery()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    digRoute(net, kDigIdle);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    digTick(w);

    auto* stack = child<QStackedWidget>(w, "diversityWindowFlowDigControls");
    CHECK(stack != nullptr);
    if (!stack)
        return;
    CHECK(stack->isVisibleTo(w));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigOffer") == stack->currentWidget());
    // Drawn as the sixth step of the checklist and never as the next chore:
    // nothing above it is a prerequisite the strip can check.
    CHECK(flowStrip(w)->stepTone(DiversityFlowStrip::StepDig) != QStringLiteral("hidden"));
    CHECK(flowStrip(w)->nextStep() != DiversityFlowStrip::StepDig);

    child<QPushButton>(w, "diversityWindowFlowDig180")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?seconds=180"));
    child<QPushButton>(w, "diversityWindowFlowDig60")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?seconds=60"));
    child<QPushButton>(w, "diversityWindowFlowDig300")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?seconds=300"));

    // MUTATION: a gate that cannot dig. The step and its buttons go away
    // rather than sitting there greyed -- there is nothing about them to
    // explain to somebody whose gate will never do it.
    digRoute(net, QByteArray(R"({"available": false})"));
    digTick(w);
    CHECK(!stack->isVisibleTo(w));
    CHECK_EQ(flowStrip(w)->stepTone(DiversityFlowStrip::StepDig), QStringLiteral("hidden"));
    w->close();
    settle();
    closedToStart();
}

// (b) A run: the progress readout while it goes, the one-line report when it
// lands, the verdict row that only exists between a finished run and a word
// about it -- and WORSE, which is the one verdict that puts the chain back.
void testDigNarratesTheRunTheReportAndTheVerdict()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    digRoute(net, kDigRunning);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    digTick(w);

    // Elapsed of asked-for, what it has bought so far, and the knob it is on
    // -- which is the knob of the last step it appended, because there is no
    // separate "trying" field and inventing one would be inventing a fact.
    CHECK(flowHas(w, QStringLiteral("digging 1:12 of 3:00 · +2.1 dB so far · trying width")));
    auto* stack = child<QStackedWidget>(w, "diversityWindowFlowDigControls");
    CHECK(child<QWidget>(w, "diversityWindowFlowDigRunning") == stack->currentWidget());
    child<QPushButton>(w, "diversityWindowFlowDigStop")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?cancel=1"));

    // MUTATION: the run lands. The report is built from "changed" alone --
    // what it MOVED, not what the chain now is -- in the order the chain runs.
    digRoute(net, kDigDone);
    digTick(w);
    CHECK(flowHas(w, QStringLiteral("+4.1 dB: post v2, width 100-2400, nb 11 dB")));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") == stack->currentWidget());
    child<QPushButton>(w, "diversityWindowFlowDigWorse")->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/dig")),
             QStringLiteral("/diversity/dig?verdict=worse"));

    // MUTATION: the word is given. The report stays and wears it, and the
    // verdict row is gone -- there is nothing left to decide.
    QByteArray judged = kDigDone;
    judged.replace("\"verdict\": \"\"", "\"verdict\": \"better\"");
    CHECK(judged != kDigDone);
    digRoute(net, judged);
    digTick(w);
    CHECK(flowHas(w, QStringLiteral("+4.1 dB: post v2, width 100-2400, nb 11 dB · BETTER")));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") != stack->currentWidget());

    // MUTATION: a run that found nothing, and a run put back. Neither is a
    // report to be judged, so neither offers the three words.
    QByteArray nothing = kDigDone;
    nothing.replace("\"changed\": {\"post\": \"v2\", \"width\": [100, 2400], \"nb_db\": 11.0}",
                    "\"changed\": {}");
    nothing.replace("\"gain_db\": 4.1", "\"gain_db\": 0.0");
    CHECK(nothing != kDigDone);
    digRoute(net, nothing);
    digTick(w);
    CHECK(flowHas(w, QStringLiteral("nothing beat your settings")));

    QByteArray cancelled = kDigDone;
    cancelled.replace("\"cancelled\": false", "\"cancelled\": true");
    digRoute(net, cancelled);
    digTick(w);
    CHECK(flowHas(w, QStringLiteral("found +4.1 dB (put back)")));
    CHECK(child<QWidget>(w, "diversityWindowFlowDigVerdict") != stack->currentWidget());

    // MUTATION: a gate that refused. Its own words, and nothing else.
    QByteArray refused = kDigDone;
    refused.replace("\"error\": \"\"", "\"error\": \"no talker to measure against\"");
    digRoute(net, refused);
    digTick(w);
    CHECK(flowHas(w, QStringLiteral("no talker to measure against")));
    w->close();
    settle();
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_flow_test"));
    QApplication app(argc, argv);

    testRealignNarratesOnItsOwnFace();
    testRealignThatIsNeverAnsweredGivesTheButtonBack();
    testSpectrumLineFollowsPanAndAlignment();
    testCaptureCountsDownAndNamesTheFileEverywhere();
    testDigOffersThreeDurationsAndWritesTheGatesOwnQuery();
    testDigNarratesTheRunTheReportAndTheVerdict();
    testNothingScrollsOnAnyPageAtTheInitialSize();

    std::printf("\n%d diversity flow test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
