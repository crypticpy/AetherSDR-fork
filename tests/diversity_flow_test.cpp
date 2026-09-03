// The Diversity window's usability round: the FLOW strip, the two buttons on
// the pair row that now answer back, and the AGC threshold spin the FILTER
// page grew beside them.
//
// Same harness as the other diversity binaries -- a real AetherGateApplet in
// front of a fake, socket-free QNetworkAccessManager -- and a seventh binary
// for the reason all seven are separate: each is at the 800-line budget
// AGENTS.md asks for, and every window case wants the same fresh, process-wide
// AppSettings start.
//
// EVERY RENDERED VALUE HERE CARRIES A MUTATION: a second payload in which that
// value differs, asserted after the first. A FLOW strip that had been wired to
// nothing and simply printed five plausible sentences would pass the first
// assertion of every case below and fail the second, which is the only way to
// tell "reads the gate" from "looks like it reads the gate" in a widget whose
// whole content is derived.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStringList>
#include <QTest>
#include <QTimer>
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

QPushButton* step(DiversityWindow* w, int n)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowFlowStep%1").arg(n));
}

QString stepText(DiversityWindow* w, int n)
{
    QPushButton* b = step(w, n);
    return b ? b->text() : QString();
}

// "next" / "done" / "later" -- the property the strip's style sheet keys off,
// which is the only readable trace of which button is lit.
QString stepState(DiversityWindow* w, int n)
{
    QPushButton* b = step(w, n);
    return b ? b->property("flowState").toString() : QString();
}

// Which step is lit, as an index from 1. Zero means none is, which the strip
// must never allow.
int litStep(DiversityWindow* w)
{
    for (int i = 1; i <= 5; ++i) {
        if (stepState(w, i) == QLatin1String("next"))
            return i;
    }
    return 0;
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
// (a) Five steps, five states, all of them the gate's
// --------------------------------------------------------------------------

void testEveryStepReadsTheGate()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // kDiversityStatusWithKinds: aligned with lag 3, mode track, source
    // combined, six findings of which two still offer an unused button, and no
    // /filter answer yet (the FILTER page has never been up).
    CHECK_EQ(stepText(w, 1), QStringLiteral("1 ALIGN · lag 3"));
    CHECK_EQ(stepText(w, 2), QStringLiteral("2 MODE · track"));
    CHECK_EQ(stepText(w, 3), QStringLiteral("3 HEAR · OUT"));
    CHECK_EQ(stepText(w, 4), QStringLiteral("4 NOISE · 2 findings → SITE"));
    CHECK_EQ(stepText(w, 5), QStringLiteral("5 FILTER · — → FILTER"));
    // NOISE is the first one not done, so it is lit and FILTER is still dim.
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("4"));
    CHECK_EQ(stepState(w, 1), QStringLiteral("done"));
    CHECK_EQ(stepState(w, 5), QStringLiteral("later"));

    // MUTATION: the same site with both actions in force and the mode changed
    // under it. Every one of the four steps that comes off /diversity moves.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusKindsActive};
    tick(a);
    CHECK_EQ(stepText(w, 2), QStringLiteral("2 MODE · null"));
    CHECK_EQ(stepText(w, 4), QStringLiteral("4 NOISE · acting on mains, impulse"));
    // Nothing is left undone, so the lit step walks to the last stop.
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("5"));
    CHECK_EQ(stepState(w, 4), QStringLiteral("done"));

    // MUTATION: a profiled site with no kinds array at all is clean, and one
    // the gate has not profiled yet says so rather than claiming it is.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithSite};
    tick(a);
    CHECK_EQ(stepText(w, 4), QStringLiteral("4 NOISE · clean"));
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithPrint};
    tick(a);
    CHECK_EQ(stepText(w, 4), QStringLiteral("4 NOISE · profiling…"));
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (b) ALIGN is first because nothing else means anything until it is done
// --------------------------------------------------------------------------

void testAlignStepAndWhereTheLitStepGoes()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    // kDiversityStatusSiteNull is the gate up but not aligned, with a lag it
    // has measured and does not trust.
    connectGate(a, net, kDiversityStatusSiteNull);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK_EQ(stepText(w, 1), QStringLiteral("1 ALIGN · not aligned → REALIGN"));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("1"));
    // Everything after the lit step is dim, however true it happens to be.
    CHECK_EQ(stepState(w, 2), QStringLiteral("later"));
    CHECK_EQ(stepState(w, 4), QStringLiteral("later"));

    // MUTATION: mid-realign is its own state, not "not aligned".
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusSiteNull, "\"aligned\": false",
             "\"aligned\": false, \"realigning\": true")};
    tick(a);
    CHECK_EQ(stepText(w, 1), QStringLiteral("1 ALIGN · aligning…"));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("1"));

    // MUTATION: aligned, with the gate's own lag quoted back.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(with(kDiversityStatusSiteNull, "\"aligned\": false", "\"aligned\": true"),
             "\"lag_samples\": 3", "\"lag_samples\": -63")};
    tick(a);
    CHECK_EQ(stepText(w, 1), QStringLiteral("1 ALIGN · lag -63"));
    CHECK(litStep(w) > 1);
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (c) MODE and HEAR say what to do about themselves
// --------------------------------------------------------------------------

void testModeAndHearOfferTheirOwnCure()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, with(kDiversityStatusWithKinds, "\"mode\": \"track\"",
                             "\"mode\": \"off\""));
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK_EQ(stepText(w, 2), QStringLiteral("2 MODE · off → pick TRACK"));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("2"));

    // MUTATION: manual is a mode, so the step is done and reads as itself.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"mode\": \"track\"", "\"mode\": \"manual\"")};
    tick(a);
    CHECK_EQ(stepText(w, 2), QStringLiteral("2 MODE · manual"));

    // HEAR: on one loop the combiner is solving into a void.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"source\": \"combined\"", "\"source\": \"b\"")};
    tick(a);
    CHECK_EQ(stepText(w, 3), QStringLiteral("3 HEAR · B only → hear OUT"));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("3"));

    // MUTATION: stereo is the other way of hearing both loops, and counts.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"source\": \"combined\"",
             "\"source\": \"stereo\"")};
    tick(a);
    CHECK_EQ(stepText(w, 3), QStringLiteral("3 HEAR · STEREO"));
    CHECK(litStep(w) > 3);
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (d) FILTER states what is in force, not what was asked for
// --------------------------------------------------------------------------

void testFilterStepQuotesTheEdgesInForce()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // Nothing has read /filter yet, and the step says so rather than guessing.
    CHECK_EQ(stepText(w, 5), QStringLiteral("5 FILTER · — → FILTER"));

    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    filterTick(a);
    CHECK_EQ(stepText(w, 5), QStringLiteral("5 FILTER · 100–2900 sharp · AUTO"));

    // MUTATION: the auto-width tracker has moved the edges off the asked-for
    // ones, and the step follows what is IN FORCE.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK_EQ(stepText(w, 5), QStringLiteral("5 FILTER · 210–2840 soft · AUTO"));

    // MUTATION: a mode with no slice filter behind it is a fact, not a dash.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterUnavailable};
    filterTick(a);
    CHECK_EQ(stepText(w, 5), QStringLiteral("5 FILTER · no filter for this mode"));
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (e) A step is a button: it goes to its page and does its one thing
// --------------------------------------------------------------------------

void testStepOneAsksForAnAlign()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusSiteNull);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    child<QToolButton>(w, "diversityWindowPageSite")->click();
    settle();
    const int before = net.count(QStringLiteral("/diversity/align"));
    step(w, 1)->click();
    settle();
    CHECK(net.count(QStringLiteral("/diversity/align")) == before + 1);
    // ...and it brought the operator back to the page the answer is on.
    CHECK(child<QToolButton>(w, "diversityWindowPageSlice")->isChecked());
    w->close();
    settle();
    closedToStart();
}

void testStepThreeAsksForTheCombinedOutput()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net,
                with(kDiversityStatusWithKinds, "\"source\": \"combined\"",
                     "\"source\": \"a\""));
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    step(w, 3)->click();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/set")),
             QStringLiteral("/diversity/set?source=combined"));

    // Steps 4 and 5 are page switches and nothing else -- there is a choice to
    // make on both pages and the strip must not make it.
    const int writes = net.count(QStringLiteral("/diversity/set"));
    step(w, 4)->click();
    settle();
    CHECK(child<QToolButton>(w, "diversityWindowPageSite")->isChecked());
    step(w, 5)->click();
    settle();
    CHECK(child<QToolButton>(w, "diversityWindowPageFilter")->isChecked());
    CHECK(net.count(QStringLiteral("/diversity/set")) == writes);
    w->close();
    settle();
    closedToStart();
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
// (h) The AGC threshold spin
// --------------------------------------------------------------------------

void testAgcThresholdSpinReadsAndWrites()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    filterTick(a);

    auto* spin = child<QSpinBox>(w, "diversityWindowFilterAgcThreshold");
    CHECK(spin != nullptr);
    if (!spin)
        return;
    CHECK(spin->isEnabled());
    CHECK_EQ(QString::number(spin->value()), QStringLiteral("20"));

    // MUTATION: a different gate value, so this cannot pass against a spin
    // that was merely constructed at its default.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK_EQ(QString::number(spin->value()), QStringLiteral("34"));

    spin->setValue(28);
    emit spin->editingFinished();
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/filter/set")),
             QStringLiteral("/filter/set?threshold_db=28"));

    // A gate too old to have the key gets a dead control rather than a number
    // nothing is honouring.
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        with(kDiversityFilterStatus, "\"threshold_db\": 20.0, ", "")};
    filterTick(a);
    CHECK(!spin->isEnabled());
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (i) Two extra rows and a strip, and every page still fits
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

    // The tab row and the pair row are separate widgets, and the FLOW strip is
    // under both. If any of the three were folded back into one row this is
    // the assertion that would notice.
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
    // once. A strip whose buttons were sized to their text rather than shared
    // equally would push the window's minimum width past the size it opens at.
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

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_flow_test"));
    QApplication app(argc, argv);

    testEveryStepReadsTheGate();
    testAlignStepAndWhereTheLitStepGoes();
    testModeAndHearOfferTheirOwnCure();
    testFilterStepQuotesTheEdgesInForce();
    testStepOneAsksForAnAlign();
    testStepThreeAsksForTheCombinedOutput();
    testRealignNarratesOnItsOwnFace();
    testRealignThatIsNeverAnsweredGivesTheButtonBack();
    testCaptureCountsDownAndNamesTheFileEverywhere();
    testAgcThresholdSpinReadsAndWrites();
    testNothingScrollsOnAnyPageAtTheInitialSize();

    std::printf("\n%d diversity flow test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
