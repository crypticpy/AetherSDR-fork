// B25 AUTO CLEAN visibility -- the operator's own words: "Auto clean should
// be an option somewhere that we can turn on and off but it should be really
// visible when we turn that on." This file is the cross-surface proof: the
// same governor block, read off /diversity and /filter, has to say the same
// thing on the sidebar's own switch (gui/AetherGateDiversityPanel.cpp), the
// Diversity window's FLOW strip banner (gui/DiversityFlowStripAuto.cpp) and
// the CHAIN window's read-only header (gui/AetherGateChainWindowTabs.cpp),
// plus DIG STOP drawn directly on the FLOW strip's own DIG line
// (gui/DiversityFlowStripDig.cpp). It does NOT retest chainAutoNoteForStage()
// or the AUTO CLEAN card's own inspector -- tests/aether_gate_chain_auto_test
// .cpp already covers those; this file is the three-surface indicator alone.
//
// EVERY ASSERTION CARRIES A MUTATION, the same discipline
// tests/diversity_flow_test.cpp keeps: a widget that had been wired to
// nothing would still pass the first read of a governor block and fail the
// second, which is the only way to tell "reads the governor" from "looks
// like it reads the governor".

#include "AetherGateChainFixture.h"
#include "core/AppSettings.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityFlowStrip.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QTimer>

#include <cstdio>

using AetherSDR::AppSettings;
using AetherSDR::DiversityFlowStrip;
using AetherSDR::DiversityWindow;

using namespace AetherGateChainFixture;

namespace {

constexpr int kTabChain = 0;

#define CHECK_EQ(got, want)                                                          \
    do {                                                                             \
        const QString g_ = (got);                                                   \
        const QString w_ = (want);                                                  \
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

// A gate too old to send a governor block at all: kDiversityFull and
// kChainFullFilter both carry no "governor" key, which is the everyday case
// every surface must collapse to nothing for.
QByteArray withGovernor(const QByteArray& body, const QJsonObject& gov)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    root.insert(QStringLiteral("governor"), gov);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// The whole governor block, docs/DIVERSITY.md's own shape -- same fields
// tests/aether_gate_chain_auto_test.cpp's own governor() builds, redefined
// here rather than shared because that helper is local to that file.
QJsonObject governor(bool autoOn, const QString& state, const QString& why,
                     const QJsonArray& holding = {}, const QString& label = {})
{
    QJsonObject g;
    g.insert(QStringLiteral("available"), true);
    g.insert(QStringLiteral("auto"), autoOn);
    g.insert(QStringLiteral("state"), state);
    g.insert(QStringLiteral("why"), why);
    if (!label.isEmpty())
        g.insert(QStringLiteral("state_label"), label);
    g.insert(QStringLiteral("settle_s"), 5.0);
    g.insert(QStringLiteral("margin_db"), 1.0);
    g.insert(QStringLiteral("spread_db"), 2.0);
    g.insert(QStringLiteral("holding"), holding);
    g.insert(QStringLiteral("pending"), QJsonValue());
    g.insert(QStringLiteral("events"), QJsonArray());
    g.insert(QStringLiteral("backoff"), QJsonArray());
    g.insert(QStringLiteral("error"), QString());
    return g;
}

QJsonObject heldDig(const QString& why)
{
    QJsonObject h;
    h.insert(QStringLiteral("tool"), QStringLiteral("dig"));
    h.insert(QStringLiteral("kind"), QStringLiteral("weak"));
    h.insert(QStringLiteral("why"), why);
    h.insert(QStringLiteral("since"), 100.0);
    h.insert(QStringLiteral("delta_db"), QJsonValue());
    return h;
}

// Both routes at once, independently governed -- the 3-arg connectGate()
// AetherGateChainFixture.h already has fixes /diversity to kDiversityFull
// with no governor, which cannot show the sidebar and the CHAIN banner
// disagreeing about it.
void connectGateBoth(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity,
                     const QByteArray& filter)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filter};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* sidebarButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityAutoCleanButton"));
}

DiversityWindow* openWindow(AetherGateApplet& a)
{
    auto* door = a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
    if (!door)
        return nullptr;
    door->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (w)
        tick(a);
    return w;
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

void fire(QTimer* timer)
{
    if (!timer)
        return;
    const bool once = timer->isSingleShot();
    if (once)
        timer->stop();
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
}

DiversityFlowStrip* flowStrip(DiversityWindow* w)
{
    return w->findChild<DiversityFlowStrip*>(QStringLiteral("diversityWindowFlowStrip"));
}

bool flowHas(DiversityWindow* w, const QString& needle)
{
    auto* line = child<QLabel>(w, "diversityWindowFlowLine");
    return line && line->text().contains(needle);
}

// The last request whose path starts with `prefix` -- not simply the last
// request of any kind, because a status poll can land on the wire in the
// same 20 ms settle() as the write under test.
QString lastRequestFor(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

// --------------------------------------------------------------------------
// (1) The sidebar's own AUTO CLEAN switch
// --------------------------------------------------------------------------

void testSidebarIndicatorAndToggle()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    // isVisible() reflects the whole ancestor chain -- an unshown applet
    // makes every child report invisible no matter what setVisible() it
    // was given, so this test (unlike most of this file) needs the real
    // top-level shown, same as aether_gate_applet_test.cpp's own
    // testVisibleCadence() does for the same reason.
    a.show();
    settle();
    connectGateBoth(a, net, kDiversityFull, kChainFullFilter);   // no governor
    QPushButton* button = sidebarButton(a);
    CHECK(button != nullptr);
    if (!button)
        return;
    CHECK(!button->isVisible());

    // MUTATION: the governor arrives, off.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull, governor(false, QStringLiteral("idle"), QStringLiteral("")))};
    tick(a);
    CHECK(button->isVisible());
    CHECK(!button->isChecked());
    CHECK_EQ(button->text(), QStringLiteral("AUTO CLEAN"));

    // MUTATION: turned on -- the face is the bare "AUTO CLEAN ON" and
    // nothing more (the operator's own words: no status message on a
    // switch); the state label rides, short, in the accessible description,
    // and the tooltip is one fixed short line. Neither carries the sentence.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull,
                     governor(true, QStringLiteral("settling"),
                              QStringLiteral("mains/squeeze backing off until 12:46"),
                              {}, QStringLiteral("trying a null on the mains hum")))};
    tick(a);
    CHECK(button->isChecked());
    CHECK_EQ(button->text(), QStringLiteral("AUTO CLEAN ON"));
    CHECK_EQ(button->accessibleDescription(),
             QStringLiteral("AUTO CLEAN ON · trying a null on the mains hum"));
    CHECK(!button->accessibleDescription().contains(QStringLiteral("backing off")));
    CHECK_EQ(button->toolTip(),
             QStringLiteral("The chain is adjusting itself. Click to turn it off."));
    CHECK(!button->toolTip().contains(QStringLiteral("backing off")));

    // MUTATION: a gate too old to send state_label falls back to the raw
    // state in the accessible description; the face stays the bare
    // "AUTO CLEAN ON" either way.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull,
                     governor(true, QStringLiteral("settling"),
                              QStringLiteral("mains/squeeze backing off until 12:46")))};
    tick(a);
    CHECK_EQ(button->text(), QStringLiteral("AUTO CLEAN ON"));
    CHECK_EQ(button->accessibleDescription(), QStringLiteral("AUTO CLEAN ON · settling"));

    // Pressing it while ON writes auto=off, exactly.
    button->click();
    settle();
    CHECK_EQ(lastRequestFor(net, QStringLiteral("/diversity/set")), QStringLiteral("/diversity/set?auto=off"));

    // MUTATION: back off, read back through the same poll -- the button
    // never sets itself optimistically. Then pressing it while OFF writes
    // auto=on, exactly.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull, governor(false, QStringLiteral("idle"), QStringLiteral("")))};
    tick(a);
    CHECK(!button->isChecked());
    button->click();
    settle();
    CHECK_EQ(lastRequestFor(net, QStringLiteral("/diversity/set")), QStringLiteral("/diversity/set?auto=on"));
}

// --------------------------------------------------------------------------
// (2) The Diversity window's FLOW strip banner
// --------------------------------------------------------------------------

void testFlowStripIndicatorAndToggle()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGateBoth(a, net, kDiversityFull, kChainFullFilter);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* button =
        child<QPushButton>(w, "diversityWindowFlowAutoCleanButton");
    CHECK(button != nullptr);
    if (!button) {
        w->close();
        settle();
        closedToStart();
        return;
    }
    CHECK(!button->isVisible());

    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull,
                     governor(true, QStringLiteral("measuring"),
                              QStringLiteral("sampling the noise floor"),
                              {}, QStringLiteral("listening")))};
    tick(a);
    CHECK(button->isVisible());
    CHECK(button->isChecked());
    // MUTATION: the face is the bare "AUTO CLEAN ON", the state label rides
    // short in the accessible description, and the tooltip is one fixed
    // short line -- neither carries the why sentence.
    CHECK_EQ(button->text(), QStringLiteral("AUTO CLEAN ON"));
    CHECK_EQ(button->accessibleDescription(), QStringLiteral("AUTO CLEAN ON · listening"));
    CHECK(!button->accessibleDescription().contains(QStringLiteral("sampling")));
    CHECK_EQ(button->toolTip(),
             QStringLiteral("The chain is adjusting itself. Click to turn it off."));
    CHECK(!button->toolTip().contains(QStringLiteral("sampling")));

    button->click();
    settle();
    CHECK_EQ(lastRequestFor(net, QStringLiteral("/diversity/set")), QStringLiteral("/diversity/set?auto=off"));

    // MUTATION: read back off, then on again from OFF.
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull, governor(false, QStringLiteral("idle"), QStringLiteral("")))};
    tick(a);
    CHECK(!button->isChecked());
    CHECK_EQ(button->text(), QStringLiteral("AUTO CLEAN"));
    button->click();
    settle();
    CHECK_EQ(lastRequestFor(net, QStringLiteral("/diversity/set")), QStringLiteral("/diversity/set?auto=on"));

    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (3) The CHAIN window's read-only header banner
// --------------------------------------------------------------------------

void testChainWindowBannerIsReadOnly()
{
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGateBoth(a, net, kDiversityFull,
                    withGovernor(kChainFullFilter,
                                 governor(true, QStringLiteral("settling"),
                                          QStringLiteral("mains/squeeze backing off until 12:46"),
                                          {}, QStringLiteral("trying a null on the mains hum"))));
    AetherGateChainWindow* w = openChain(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);
    w->setCurrentTab(kTabChain);
    settle();

    auto* timer = w->findChild<QTimer*>(QStringLiteral("gateChainAutoCleanBannerTimer"));
    CHECK(timer != nullptr);
    fire(timer);
    settle();
    QLabel* banner = w->findChild<QLabel*>(QStringLiteral("gateChainAutoCleanBanner"));
    CHECK(banner != nullptr);
    if (!banner)
        return;
    CHECK(banner->isVisible());
    // The header has the room: the label AND the sentence (U1).
    CHECK_EQ(banner->text(),
             QStringLiteral("AUTO CLEAN ON · trying a null on the mains hum · "
                            "mains/squeeze backing off until 12:46"));

    // MUTATION: auto goes off -- the banner disappears rather than reading
    // "AUTO CLEAN" (there is no switch here to collapse to; a read-only
    // header with nothing to say says nothing).
    net.routes[QStringLiteral("/filter")] = {
        QNetworkReply::NoError,
        withGovernor(kChainFullFilter,
                     governor(false, QStringLiteral("idle"), QStringLiteral("")))};
    filterTick(a);
    fire(timer);
    settle();
    CHECK(!banner->isVisible());
}

// --------------------------------------------------------------------------
// (4) DIG STOP on the FLOW strip's own line
// --------------------------------------------------------------------------

const QByteArray kDigIdle = R"({"available": true, "running": false,
    "phase": "idle", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 0.0, "steps": [], "best": {}, "changed": {}})";

const QByteArray kDigRunning = R"({"available": true, "running": true,
    "phase": "searching", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 2.1, "elapsed_s": 72.0, "seconds": 180.0,
    "steps": [{"knob": "width", "kept": false}], "best": {}, "changed": {}})";

void testDigStopButtonWritesCancel()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGateBoth(a, net, kDiversityFull, kChainFullFilter);
    net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, kDigIdle};
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();

    auto* stop = child<QPushButton>(w, "diversityWindowFlowStripDigStopButton");
    CHECK(stop != nullptr);
    if (!stop) {
        w->close();
        settle();
        closedToStart();
        return;
    }
    CHECK(!stop->isVisible());

    // MUTATION: a run starts -- STOP appears.
    net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, kDigRunning};
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();
    CHECK(stop->isVisible());

    stop->click();
    settle();
    CHECK_EQ(lastRequestFor(net, QStringLiteral("/diversity/dig")), QStringLiteral("/diversity/dig?cancel=1"));

    // MUTATION: back to idle -- STOP goes away again.
    net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, kDigIdle};
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();
    CHECK(!stop->isVisible());

    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (5) Who started the run
// --------------------------------------------------------------------------

void testDigNarratesWhoStartedIt()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    // No governor at all: an operator's own DIG button.
    connectGateBoth(a, net, kDiversityFull, kChainFullFilter);
    net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, kDigRunning};
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();
    CHECK(flowHas(w, QStringLiteral("started by you")));
    CHECK(!flowHas(w, QStringLiteral("started by AUTO")));

    // MUTATION: the same run, but the governor is holding "dig" -- the whole
    // fact this line exists to say.
    const QJsonArray holding = {heldDig(QStringLiteral("weak"))};
    net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityFull,
                     governor(true, QStringLiteral("applying"), QStringLiteral("holding dig"),
                             holding))};
    tick(a);
    fire(child<QTimer>(w, "diversityWindowDigTimer"));
    settle();
    CHECK(flowHas(w, QStringLiteral("started by AUTO")));
    CHECK(!flowHas(w, QStringLiteral("started by you")));

    w->close();
    settle();
    closedToStart();
}

// --------------------------------------------------------------------------
// (6) Name hygiene -- every new widget is a uniquely-named direct child
// --------------------------------------------------------------------------

void testNameHygiene()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGateBoth(
        a, net,
        withGovernor(kDiversityFull, governor(true, QStringLiteral("settling"), QStringLiteral("why"))),
        withGovernor(kChainFullFilter, governor(true, QStringLiteral("settling"), QStringLiteral("why"))));

    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (w) {
        DiversityFlowStrip* strip = flowStrip(w);
        CHECK(strip != nullptr);
        if (strip) {
            for (QWidget* kid : strip->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                CHECK(!kid->objectName().isEmpty());
            QPushButton* autoBtn = child<QPushButton>(w, "diversityWindowFlowAutoCleanButton");
            CHECK(autoBtn != nullptr);
            CHECK(autoBtn
                  && strip->findChild<QPushButton*>(autoBtn->objectName(),
                                                    Qt::FindDirectChildrenOnly)
                         == autoBtn);
            QPushButton* stopBtn = child<QPushButton>(w, "diversityWindowFlowStripDigStopButton");
            CHECK(stopBtn != nullptr);
            CHECK(stopBtn
                  && strip->findChild<QPushButton*>(stopBtn->objectName(),
                                                    Qt::FindDirectChildrenOnly)
                         == stopBtn);
        }
        w->close();
        settle();
    }
    closedToStart();

    AetherGateChainWindow* cw = openChain(a);
    CHECK(cw != nullptr);
    if (cw) {
        QLabel* banner = cw->findChild<QLabel*>(QStringLiteral("gateChainAutoCleanBanner"));
        CHECK(banner != nullptr);
        if (banner) {
            QWidget* parent = banner->parentWidget();
            CHECK(parent != nullptr);
            CHECK(parent
                  && parent->findChild<QLabel*>(banner->objectName(), Qt::FindDirectChildrenOnly)
                         == banner);
        }
    }
}

// --------------------------------------------------------------------------
// (7) No horizontal scrollbar at the initial size, on either window, with
// the longest `why` a gate could plausibly send.
// --------------------------------------------------------------------------

const QString kLongWhy = QStringLiteral(
    "the coupling filter's own drift model needs one more hour of daylight "
    "comb data before it can widen the null without also widening the "
    "passband skirts nobody asked it to touch, so it is holding position "
    "across two ships and a full band scan until then");

void testNoHorizontalScrollbarAtInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGateBoth(
        a, net,
        withGovernor(kDiversityFull, governor(true, QStringLiteral("settling"), kLongWhy)),
        withGovernor(kChainFullFilter, governor(true, QStringLiteral("settling"), kLongWhy)));

    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (w) {
        w->resize(1120, 860);
        settle();
        w->grab();
        CHECK(w->minimumSizeHint().width() <= 1120);
        auto* scroll = child<QScrollArea>(w, "diversityWindowSliceScroll");
        CHECK(scroll == nullptr || !scroll->horizontalScrollBar()->isVisible());
        w->close();
        settle();
    }
    closedToStart();

    AetherGateChainWindow* cw = openChain(a);
    CHECK(cw != nullptr);
    if (cw) {
        cw->resize(1120, 820);
        cw->setCurrentTab(kTabChain);
        settle();
        fire(cw->findChild<QTimer*>(QStringLiteral("gateChainAutoCleanBannerTimer")));
        settle();
        cw->grab();
        CHECK(cw->minimumSizeHint().width() <= 1120);
        auto* scroll = cw->findChild<QScrollArea*>(QStringLiteral("gateChainScroll"));
        CHECK(scroll == nullptr || !scroll->horizontalScrollBar()->isVisible());
    }
}

// --------------------------------------------------------------------------
// (8) With AUTO_VIS_RENDER_PREFIX=/tmp/auto-vis set, all three surfaces are
// rendered to <prefix>-sidebar.png / -flow.png / -chain.png, so the banner
// can be looked at rather than only asserted about.
// --------------------------------------------------------------------------

void testRenderWhenAsked()
{
    const QByteArray prefixEnv = qgetenv("AUTO_VIS_RENDER_PREFIX");
    if (prefixEnv.isEmpty())
        return;
    closedToStart();
    const QString prefix = QString::fromLocal8Bit(prefixEnv);

    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGateBoth(
        a, net,
        withGovernor(kDiversityFull,
                     governor(true, QStringLiteral("settling"),
                              QStringLiteral("mains/squeeze backing off until 12:46"))),
        withGovernor(kChainFullFilter,
                     governor(true, QStringLiteral("settling"),
                              QStringLiteral("mains/squeeze backing off until 12:46"))));
    a.resize(320, 700);
    a.show();
    settle();
    CHECK(a.grab().save(prefix + QStringLiteral("-sidebar.png")));

    DiversityWindow* w = openWindow(a);
    if (w) {
        w->resize(1120, 860);
        settle();
        CHECK(w->grab().save(prefix + QStringLiteral("-flow.png")));
        w->close();
        settle();
    }
    closedToStart();

    AetherGateChainWindow* cw = openChain(a);
    if (cw) {
        cw->resize(1120, 820);
        cw->setCurrentTab(kTabChain);
        settle();
        fire(cw->findChild<QTimer*>(QStringLiteral("gateChainAutoCleanBannerTimer")));
        settle();
        CHECK(cw->grab().save(prefix + QStringLiteral("-chain.png")));
    }
    a.hide();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_auto_visibility_test"));
    QApplication app(argc, argv);

    testSidebarIndicatorAndToggle();
    testFlowStripIndicatorAndToggle();
    testChainWindowBannerIsReadOnly();
    testDigStopButtonWritesCancel();
    testDigNarratesWhoStartedIt();
    testNameHygiene();
    testNoHorizontalScrollbarAtInitialSize();
    testRenderWhenAsked();

    std::printf("\n%d diversity auto-visibility test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
