// The Diversity window's FLOW line: five steps, in order, on one line at the
// foot of the window.
//
// It was five buttons in a strip under the four page tabs until the operator
// met it: "the tabs change what I'm seeing, but there's these flows that look
// like actual tabs for what I am seeing... it currently looks like the tab
// you're supposed to be on, but the tabs are at the top". Five lit rounded
// boxes under four lit rounded boxes is a navigation control whatever the
// words on it say, so the flow moved to the bottom, became one label of rich
// text, and grew a second rule: it is about the page in front of the operator,
// and everything belonging to another page goes dim.
//
// Split out of tests/diversity_flow_test.cpp -- which keeps REALIGN, CAPTURE
// and the window frame -- when that file reached the 800-line budget AGENTS.md
// asks for. Same harness as the other diversity binaries: a real
// AetherGateApplet in front of a fake, socket-free QNetworkAccessManager, and
// a binary of its own because every case here wants a window opened from a
// CLOSED start and DiversityWindowVisible is a process-wide setting.
//
// EVERY RENDERED VALUE HERE CARRIES A MUTATION: a second payload in which that
// value differs, asserted after the first. A line that had been wired to
// nothing and simply printed five plausible steps would pass the first
// assertion of every case below and fail the second, which is the only way to
// tell "reads the gate" from "looks like it reads the gate" in a widget whose
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
#include <QNetworkReply>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>

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
// not prove which field the line read.
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

// The FLOW line: one label of rich text at the foot of the window, where the
// five steps used to be five buttons under the tabs. Every case below reads it
// as a string, because it IS one widget now.
QLabel* flowLine(DiversityWindow* w)
{
    return w->findChild<QLabel*>(QStringLiteral("diversityWindowFlowLine"));
}

QString flowText(DiversityWindow* w)
{
    QLabel* line = flowLine(w);
    return line ? line->text() : QString();
}

bool flowHas(DiversityWindow* w, const QString& needle)
{
    return flowText(w).contains(needle);
}

DiversityFlowStrip* flowStrip(DiversityWindow* w)
{
    return w->findChild<DiversityFlowStrip*>(
        QStringLiteral("diversityWindowFlowStrip"));
}

// "lit" / "normal" / "dim" -- the strip's own word for how it drew one step,
// which is the only readable trace of a colour inlined into rich text.
QString tone(DiversityWindow* w, int step)
{
    DiversityFlowStrip* strip = flowStrip(w);
    return strip ? strip->stepTone(step) : QString();
}

// Which step is the link -- the one to do next -- as an index from 1. Zero
// means none is, which the line must never allow.
int litStep(DiversityWindow* w)
{
    const QString text = flowText(w);
    for (int i = 0; i < DiversityFlowStrip::StepCount; ++i) {
        if (text.contains(QStringLiteral("href=\"step:%1\"").arg(i)))
            return i + 1;
    }
    return 0;
}

// Activating that link, which is what a click on the next step is now.
// QLabel::linkActivated is a signal and moc leaves it invokable, so a test can
// raise it exactly where a mouse would.
void activateHref(DiversityWindow* w, const QString& href)
{
    QLabel* line = flowLine(w);
    if (!line)
        return;
    QMetaObject::invokeMethod(line, "linkActivated", Qt::DirectConnection,
                              Q_ARG(QString, href));
}

void activateStep(DiversityWindow* w, int step)
{
    activateHref(w, QStringLiteral("step:%1").arg(step));
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

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
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
    CHECK(flowHas(w, QStringLiteral("✓ align lag 3")));
    CHECK(flowHas(w, QStringLiteral("✓ mode track")));
    CHECK(flowHas(w, QStringLiteral("✓ hear OUT")));
    CHECK(flowHas(w, QStringLiteral("● noise · 2 findings → SITE")));
    // A step still ahead, on a page the operator is not on, is its own name
    // and nothing else: this is a checklist, not five more readouts.
    CHECK(flowHas(w, QStringLiteral("○ filter")));
    CHECK(!flowHas(w, QStringLiteral("→ FILTER")));
    // NOISE is the first one not done, so it is the link. It is on SITE and we
    // are on SLICE, so it is drawn dim all the same -- the line is about the
    // page in front of the operator, and only the ALIGN/MODE/HEAR steps are.
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("4"));
    CHECK_EQ(tone(w, 0), QStringLiteral("normal"));
    CHECK_EQ(tone(w, 3), QStringLiteral("dim"));
    CHECK_EQ(tone(w, 4), QStringLiteral("dim"));

    // MUTATION: the same site with both actions in force and the mode changed
    // under it. Every one of the four steps that comes off /diversity moves.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusKindsActive};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("✓ mode null")));
    CHECK(flowHas(w, QStringLiteral("✓ noise acting on mains, impulse")));
    // Nothing is left undone, so the link walks to the last stop. Nothing has
    // read /filter yet, so it has no state to quote -- and it says where to go
    // and read one instead of printing a dash.
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("5"));
    CHECK(flowHas(w, QStringLiteral("● filter → FILTER")));

    // MUTATION: a profiled site with no kinds array at all is clean, and one
    // the gate has not profiled yet says so rather than claiming it is.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithSite};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("✓ noise clean")));
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithPrint};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("✓ noise profiling…")));
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

    CHECK(flowHas(w, QStringLiteral("● align · not aligned → REALIGN")));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("1"));
    // ALIGN is on the page we are on, so it is the one drawn lit.
    CHECK_EQ(tone(w, 0), QStringLiteral("lit"));
    // Everything after it is its bare name, however true it happens to be.
    CHECK(flowHas(w, QStringLiteral("○ mode")));
    CHECK(flowHas(w, QStringLiteral("○ noise")));

    // MUTATION: mid-realign is its own state, not "not aligned".
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusSiteNull, "\"aligned\": false",
             "\"aligned\": false, \"realigning\": true")};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("● align · aligning…")));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("1"));

    // MUTATION: aligned, with the gate's own lag quoted back.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(with(kDiversityStatusSiteNull, "\"aligned\": false", "\"aligned\": true"),
             "\"lag_samples\": 3", "\"lag_samples\": -63")};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("✓ align lag -63")));
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

    CHECK(flowHas(w, QStringLiteral("● mode · off → pick TRACK")));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("2"));

    // MUTATION: manual is a mode, so the step is done and reads as itself.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"mode\": \"track\"", "\"mode\": \"manual\"")};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("✓ mode manual")));

    // HEAR: on one loop the combiner is solving into a void.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"source\": \"combined\"", "\"source\": \"b\"")};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("● hear · B only → hear OUT")));
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("3"));

    // MUTATION: stereo is the other way of hearing both loops, and counts.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"source\": \"combined\"",
             "\"source\": \"stereo\"")};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("✓ hear STEREO")));
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

    // Nothing has read /filter yet, and from SLICE the step is a bare name --
    // a dash is not a state and "○ filter —" says less than "○ filter".
    CHECK(flowHas(w, QStringLiteral("○ filter")));
    CHECK(!flowHas(w, QStringLiteral("○ filter —")));

    // On the FILTER page the step is the one the operator went there for, so
    // it is the one that quotes what is in force.
    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    filterTick(a);
    CHECK(flowHas(w, QStringLiteral("○ filter 100–2900 sharp · AUTO")));
    CHECK_EQ(tone(w, 4), QStringLiteral("normal"));

    // MUTATION: the auto-width tracker has moved the edges off the asked-for
    // ones, and the step follows what is IN FORCE.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK(flowHas(w, QStringLiteral("○ filter 210–2840 soft · AUTO")));

    // MUTATION: a mode with no slice filter behind it is a fact, not a dash.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterUnavailable};
    filterTick(a);
    CHECK(flowHas(w, QStringLiteral("○ filter no filter for this mode")));
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
    activateStep(w, DiversityFlowStrip::StepAlign);
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

    activateStep(w, DiversityFlowStrip::StepHear);
    settle();
    CHECK_EQ(lastRequest(net, QStringLiteral("/diversity/set")),
             QStringLiteral("/diversity/set?source=combined"));

    // Steps 4 and 5 are page switches and nothing else -- there is a choice to
    // make on both pages and the strip must not make it.
    const int writes = net.count(QStringLiteral("/diversity/set"));
    activateStep(w, DiversityFlowStrip::StepNoise);
    settle();
    CHECK(child<QToolButton>(w, "diversityWindowPageSite")->isChecked());
    activateStep(w, DiversityFlowStrip::StepFilter);
    settle();
    CHECK(child<QToolButton>(w, "diversityWindowPageFilter")->isChecked());
    CHECK(net.count(QStringLiteral("/diversity/set")) == writes);
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (f) The link is the only thing that fires, and it fires the step it names
// --------------------------------------------------------------------------

void testTheLinkCarriesItsOwnStep()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    DiversityFlowStrip* strip = flowStrip(w);
    CHECK(strip != nullptr);
    if (!strip)
        return;

    int seen = -1;
    int count = 0;
    QObject::connect(strip, &DiversityFlowStrip::stepActivated,
                     strip, [&seen, &count](int step) {
                         seen = step;
                         ++count;
                     });

    activateStep(w, DiversityFlowStrip::StepNoise);
    CHECK_EQ(QString::number(seen), QString::number(DiversityFlowStrip::StepNoise));
    CHECK_EQ(QString::number(count), QStringLiteral("1"));

    // MUTATION: a different href is a different step, so this cannot pass
    // against a handler that emits a constant.
    activateStep(w, DiversityFlowStrip::StepMode);
    CHECK_EQ(QString::number(seen), QString::number(DiversityFlowStrip::StepMode));
    CHECK_EQ(QString::number(count), QStringLiteral("2"));

    // Anything that is not this line's scheme, or names a step that does not
    // exist, is dropped rather than guessed at.
    activateHref(w, QStringLiteral("https://example.invalid/"));
    activateHref(w, QStringLiteral("step:9"));
    activateHref(w, QStringLiteral("step:not-a-number"));
    CHECK_EQ(QString::number(count), QStringLiteral("2"));
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (g) The line is about the page you are on
// --------------------------------------------------------------------------

void testTheLineFollowsTheTabYouAreOn()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsWithPattern};
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // kDiversityStatusWithKinds leaves NOISE as the step to do next. On SLICE
    // the three steps that live on SLICE are the ones drawn in full, and NOISE
    // is dim even though it is the link -- it is somewhere else.
    CHECK_EQ(QString::number(litStep(w)), QStringLiteral("4"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepAlign), QStringLiteral("normal"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepHear), QStringLiteral("normal"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepNoise), QStringLiteral("dim"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepFilter), QStringLiteral("dim"));

    // SITE: now NOISE is the step in front of the operator AND the one to do,
    // so it is lit, and the three SLICE steps are the ones that go dim.
    child<QToolButton>(w, "diversityWindowPageSite")->click();
    settle();
    CHECK_EQ(tone(w, DiversityFlowStrip::StepNoise), QStringLiteral("lit"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepAlign), QStringLiteral("dim"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepHear), QStringLiteral("dim"));

    // FILTER: the filter step is the one the operator went there for, and it
    // is the only one that quotes its state while it is still ahead.
    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    filterTick(a);
    CHECK_EQ(tone(w, DiversityFlowStrip::StepFilter), QStringLiteral("normal"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepNoise), QStringLiteral("dim"));
    CHECK(flowHas(w, QStringLiteral("○ filter 100–2900 sharp · AUTO")));

    // BAND owns no step, so "belongs to this page" would dim all five --
    // including the one thing this line exists to say. There, the next step
    // stays lit and says which page it is on.
    child<QToolButton>(w, "diversityWindowPageBand")->click();
    settle();
    CHECK_EQ(tone(w, DiversityFlowStrip::StepNoise), QStringLiteral("lit"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepAlign), QStringLiteral("dim"));
    CHECK_EQ(tone(w, DiversityFlowStrip::StepFilter), QStringLiteral("dim"));
    CHECK(flowHas(w, QStringLiteral("● noise · 2 findings → SITE")));

    // MUTATION: a step whose state does not already name its page gets the
    // page put on the end of the link, so the one thing to do next is
    // readable -- and reachable -- from a page it does not live on.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        with(kDiversityStatusWithKinds, "\"aligned\": true", "\"aligned\": false")};
    tick(a);
    CHECK(flowHas(w, QStringLiteral("● align · not aligned → REALIGN → SLICE")));
    // ...and on SLICE itself the hop is not there, because there is nowhere
    // to hop to.
    child<QToolButton>(w, "diversityWindowPageSlice")->click();
    settle();
    CHECK(flowHas(w, QStringLiteral("● align · not aligned → REALIGN")));
    CHECK(!flowHas(w, QStringLiteral("REALIGN → SLICE")));
    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (h) ...and it is at the BOTTOM, which is the whole point
// --------------------------------------------------------------------------

void testTheLineSitsUnderThePagesAndOverTheStatusStrip()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    w->show();
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* tabs = child<QWidget>(w, "diversityWindowTabRow");
    auto* pages = child<QStackedWidget>(w, "diversityWindowPages");
    auto* strip = flowStrip(w);
    auto* status = child<QLabel>(w, "diversityWindowStatusLabel");
    CHECK(tabs && pages && strip && status);
    if (!tabs || !pages || !strip || !status)
        return;
    // All four are laid out in the same body widget, so their y() are
    // comparable. The order is the fix: tabs, pages, FLOW, gate status. A FLOW
    // strip back above the pages would land between the first two and this is
    // the assertion that would say so.
    CHECK(tabs->y() < pages->y());
    CHECK(strip->y() > pages->y());
    CHECK(status->y() > strip->y());
    CHECK(strip->y() + strip->height() <= status->y());
    w->close();
    settle();
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_flow_line_test"));
    QApplication app(argc, argv);

    testEveryStepReadsTheGate();
    testAlignStepAndWhereTheLitStepGoes();
    testModeAndHearOfferTheirOwnCure();
    testFilterStepQuotesTheEdgesInForce();
    testStepOneAsksForAnAlign();
    testStepThreeAsksForTheCombinedOutput();
    testTheLinkCarriesItsOwnStep();
    testTheLineFollowsTheTabYouAreOn();
    testTheLineSitsUnderThePagesAndOverTheStatusStrip();

    std::printf("\n%d diversity flow line test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
