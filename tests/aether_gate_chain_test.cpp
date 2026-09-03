// The CHAIN window -- the filter chain as a block diagram -- driven through a
// real AetherGateApplet and socket-free.
//
// Same harness as tests/diversity_band_test.cpp and for the same reason: the
// window owns no transport. It is reached by pressing the applet's own door,
// fed by the applet's DiversityBandPoller, and every click on it leaves as a
// request signal the applet turns into one GET. Driving it any other way would
// be testing a wiring diagram we drew rather than the one that ships.
//
// The cases are the contract in §0.1 of the design, one assertion each:
// the strip renders the gate's array in the gate's order, all four kinds; a
// toggle sends exactly the gate's own query and does NOT move until the reply;
// a select appends its value to a query ending in "="; the free entry refuses a
// width there is no filter to design; a fixed row has no control; an absent
// `measured` is an em dash and never a zero; a chain-less gate still gets
// thirteen honest rows; nothing scrolls at the initial size; every widget the
// window builds has a name the automation bridge can address it by.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateChainStage.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/AetherGateChainWindow.h"
#include "gui/DiversityBandPoller.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateChainControl;
using AetherSDR::AetherGateChainStrip;
using AetherSDR::AetherGateChainTile;
using AetherSDR::AetherGateChainWindow;
using AetherSDR::DiversityBandPoller;

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

// A gate that authors its own chain[], with one row of every kind the contract
// defines: two selects (one of them the digital roof, which is the row that
// carries the radio menus and the free entry), a toggle, a fixed row, and a
// value row the app has no built-in knowledge of at all.
const QByteArray kChainFilter = R"JSON({
  "low_hz": 350, "high_hz": 2400,
  "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000},
  "chain": [
    {"id": "roof_rf", "name": "ROOFING · RF", "kind": "select", "fixed": false,
     "enabled": true, "detail": "200 kHz", "value": 200000,
     "options": [200000, 300000, 600000, 1536000],
     "measured": {"in_db": null, "out_db": -97.4},
     "action": {"label": "SET", "route": "/filter/set", "query": "roof_hz="}},
    {"id": "roof_digital", "name": "ROOFING · DIGITAL", "kind": "select",
     "enabled": true, "detail": "3.0 kHz", "value": 3000,
     "options": [12000, 6000, 3000, 1200, 600, 300],
     "measured": {"in_db": -97.4, "out_db": -101.2},
     "action": {"label": "SET", "route": "/filter/set", "query": "digital_roof_hz="}},
    {"id": "nb", "name": "NB", "kind": "toggle", "enabled": false,
     "detail": "12.0 dB · 0.0 % blanked · auto: idle",
     "action": {"label": "ON", "route": "/filter/set", "query": "nb=on"}},
    {"id": "lna", "name": "LNA", "kind": "fixed", "fixed": true, "enabled": true,
     "detail": "state 4", "why": "set on the setup page"},
    {"id": "steer", "name": "BEAM STEER", "kind": "value", "enabled": true,
     "detail": "42 degrees"}
  ]})JSON";

// The same payload with the noise blanker on, so a toggle can be shown to move
// on the GATE'S answer and on nothing else.
QByteArray chainFilterNbOn()
{
    QByteArray body = kChainFilter;
    body.replace("\"enabled\": false,\n     \"detail\": \"12.0 dB",
                 "\"enabled\": true,\n     \"detail\": \"12.0 dB");
    return body;
}

// GET /filter on the live gate, 2026-09-03: no chain[] anywhere in it. This is
// the payload the fallback exists for.
const QByteArray kChainlessFilter = R"JSON({
  "low_hz": 100, "high_hz": 2900, "width_hz": 2800,
  "set_low_hz": 100, "set_high_hz": 2900,
  "shape": "soft", "taps": 255, "transition_hz": 196, "sideband": "lsb",
  "notches": [],
  "anf": {"enabled": false, "found_hz": [], "depth_db": []},
  "contour": {"enabled": true, "hz": 1450.0, "db": -2.9, "width_hz": 300.0,
              "auto": true, "source": "print"},
  "apf": {"enabled": false, "hz": 600.0, "width_hz": 150.0},
  "auto": {"enabled": false, "source": null, "low_hz": null, "high_hz": null},
  "auto_eq": {"enabled": false, "tilt_db": 0.0, "lean_db": 0.0},
  "nb": {"enabled": false, "threshold_db": 12.0, "blanked_pct": 0.0},
  "agc": {"mode": "med", "attack_ms": 5.0, "decay_ms": 250.0, "hang_ms": 250.0,
          "threshold_db": 20.0, "gain_db": -5.8},
  "talker": {"enabled": true, "snap": "fast", "id": 32,
             "remembered": [30, 31, 32, 33]},
  "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000},
  "available": true, "mode": "LSB"})JSON";

void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& filter)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filter};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

// One more tick of the filter poller, without waiting out 500 ms of real time.
void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

AetherGateChainWindow* openChain(AetherGateApplet& a)
{
    auto* door = a.findChild<QPushButton*>(QStringLiteral("gateOpenChainWindowButton"));
    if (!door)
        return nullptr;
    door->click();
    settle();
    filterTick(a);
    return a.chainWindow();
}

AetherGateChainStrip* strip(AetherGateChainWindow* w)
{
    return w ? w->findChild<AetherGateChainStrip*>(QStringLiteral("gateChainStrip"))
             : nullptr;
}

QString lastRequest(const FakeGate& net)
{
    return net.log.isEmpty() ? QString() : net.log.last();
}

QString labelText(AetherGateChainWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

// --------------------------------------------------------------------------

void testStripRendersTheGatesArrayInTheGatesOrder()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(w->chainFromGate());

    AetherGateChainStrip* s = strip(w);
    CHECK(s != nullptr);
    if (!s)
        return;
    CHECK(s->tileCount() == 5);

    const QStringList expect{QStringLiteral("roof_rf"), QStringLiteral("roof_digital"),
                             QStringLiteral("nb"), QStringLiteral("lna"),
                             QStringLiteral("steer")};
    for (int i = 0; i < expect.size() && i < s->tileCount(); ++i)
        CHECK(s->tileAt(i)->id() == expect.at(i));

    // A stage the app has never heard of renders from its own name and detail
    // rather than being dropped -- the entire point of a gate-authored array.
    AetherGateChainTile* unknown = s->tile(QStringLiteral("steer"));
    CHECK(unknown != nullptr);
    if (unknown) {
        CHECK(unknown->stage().name == QStringLiteral("BEAM STEER"));
        CHECK(unknown->stage().detail == QStringLiteral("42 degrees"));
    }
}

void testToggleSendsTheGatesQueryAndDoesNotFlipUntilTheReply()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    // The write answers with the noise blanker still OFF, which is the whole
    // point: a gate that refused, or that has not applied it yet, must not see
    // the latch move.
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* toggle = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_nb"));
    CHECK(toggle != nullptr);
    if (!toggle)
        return;
    CHECK(!toggle->isChecked());

    toggle->click();
    // Before the event loop has turned: one write is on the wire and the latch
    // has not moved.
    CHECK(!toggle->isChecked());
    settle();
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?nb=on"));
    CHECK(!toggle->isChecked());

    // Now the gate says it happened, and only now does the latch move.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, chainFilterNbOn()};
    filterTick(applet);
    auto* after = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_nb"));
    CHECK(after != nullptr);
    if (after)
        CHECK(after->isChecked());
}

void testSelectAppendsItsValueToAQueryEndingInEquals()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* select = w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_roof_digital"));
    CHECK(select != nullptr);
    if (!select)
        return;

    // The digital roof's menu is grouped by the radios operators know, with the
    // radio name as an unselectable header.
    CHECK(select->findText(QStringLiteral("Yaesu FTdx101MP")) >= 0);
    const int idx = select->findData(QStringLiteral("600"));
    CHECK(idx >= 0);
    if (idx < 0)
        return;
    QMetaObject::invokeMethod(select, "activated", Qt::DirectConnection, Q_ARG(int, idx));
    settle();
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?digital_roof_hz=600"));
    // And the list itself has not moved: it shows the 3 kHz the gate reports.
    CHECK(select->currentData().toString() == QStringLiteral("3000"));
}

void testFreeEntryRefusesAWidthThereIsNoFilterFor()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* free = w->findChild<QLineEdit*>(QStringLiteral("gateChainFree_roof_digital"));
    CHECK(free != nullptr);
    if (!free)
        return;

    const int before = net.log.size();
    free->setText(QStringLiteral("50"));           // below 100 Hz
    QTest::keyClick(free, Qt::Key_Return);
    settle();
    CHECK(net.log.size() == before);

    free->setText(QStringLiteral("40000"));        // above 25 kHz
    QTest::keyClick(free, Qt::Key_Return);
    settle();
    CHECK(net.log.size() == before);

    free->setText(QStringLiteral("3000"));
    QTest::keyClick(free, Qt::Key_Return);
    settle();
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?digital_roof_hz=3000"));
}

void testAFixedRowCarriesNoControlAndSaysWhy()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_lna")) == nullptr);
    CHECK(w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_lna")) == nullptr);
    CHECK(w->findChild<QLineEdit*>(QStringLiteral("gateChainFree_lna")) == nullptr);

    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("lna"));
    CHECK(tile != nullptr);
    if (tile)
        CHECK(tile->toolTip() == QStringLiteral("set on the setup page"));
}

void testAnAbsentMeasurementIsADashAndNeverAZero()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    // roof_rf's in_db is null and its out_db is -97.4: one dash, one number.
    auto* levels = w->findChild<QLabel*>(QStringLiteral("gateChainLevels_roof_rf"));
    CHECK(levels != nullptr);
    if (levels) {
        CHECK(levels->toolTip().contains(QStringLiteral("—")));
        CHECK(levels->toolTip().contains(QStringLiteral("-97.4")));
        CHECK(!levels->toolTip().contains(QStringLiteral("0.0 · out")));
    }
    // A row with no `measured` at all shows no meter rather than an empty one.
    auto* none = w->findChild<QLabel*>(QStringLiteral("gateChainLevels_steer"));
    CHECK(none != nullptr);
    if (none)
        CHECK(!none->isVisibleTo(w));
}

void testAChainlessGateStillGetsThirteenHonestRows()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(!w->chainFromGate());

    AetherGateChainStrip* s = strip(w);
    CHECK(s != nullptr);
    if (!s)
        return;
    CHECK(s->tileCount() == 13);

    const QStringList expect{
        QStringLiteral("roof_rf"),   QStringLiteral("roof_digital"),
        QStringLiteral("nb"),        QStringLiteral("passband"),
        QStringLiteral("shape"),     QStringLiteral("notch"),
        QStringLiteral("contour"),   QStringLiteral("apf"),
        QStringLiteral("auto_width"), QStringLiteral("auto_eq"),
        QStringLiteral("agc"),       QStringLiteral("talker"),
        QStringLiteral("voice")};
    for (int i = 0; i < expect.size() && i < s->tileCount(); ++i)
        CHECK(s->tileAt(i)->id() == expect.at(i));

    // The rows quote the gate's own numbers rather than recomputing them.
    CHECK(s->tile(QStringLiteral("agc"))->stage().detail
          == QStringLiteral("med · 5/250/250 ms · AGC-T 20 · -5.8 dB"));
    CHECK(s->tile(QStringLiteral("passband"))->stage().detail
          == QStringLiteral("100–2900 Hz · asked 100–2900"));
    // And the analogue roof says the honest thing about its own number.
    CHECK(s->tile(QStringLiteral("roof_rf"))->stage().detail.contains(
        QStringLiteral("200 kHz")));
    CHECK(s->tile(QStringLiteral("roof_rf"))->stage().fixed);
    // The digital roof is the row with the menu, and it carries free entry.
    CHECK(w->findChild<QLineEdit*>(QStringLiteral("gateChainFree_roof_digital"))
          != nullptr);
}

void testSelectingATileFillsTheDetailArea()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* name = w->findChild<QToolButton*>(QStringLiteral("gateChainName_agc"));
    CHECK(name != nullptr);
    if (!name)
        return;
    name->click();
    settle();

    CHECK(labelText(w, "gateChainDetailName") == QStringLiteral("AGC"));
    CHECK(labelText(w, "gateChainDetailText").startsWith(QStringLiteral("med")));
    auto* tip = w->findChild<QLabel*>(QStringLiteral("gateChainDetailTip"));
    CHECK(tip != nullptr);
    if (tip)
        CHECK(tip->toolTip().contains(QStringLiteral("AGC-T")));
    // The same control again, larger, under it.
    CHECK(w->findChild<QComboBox*>(QStringLiteral("gateChainDetailSelect_agc")) != nullptr);
}

void testNothingScrollsOnTheChainWindowAtTheInitialSize()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    // The 13-row fallback is the tallest strip the window can be asked to draw.
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("gateChainScroll"));
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
}

void testEveryWidgetTheWindowBuildsHasAName()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    AetherGateChainStrip* s = strip(w);
    CHECK(s != nullptr);
    if (!s)
        return;

    // Direct children only, and never inside a QComboBox: the bridge addresses
    // the widgets THIS window builds, and a combo's private popup is Qt's.
    const auto sweep = [](QWidget* parent) {
        int missing = 0;
        const auto kids =
            parent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* kid : kids) {
            if (kid->objectName().isEmpty())
                ++missing;
        }
        return missing;
    };

    for (int i = 0; i < s->tileCount(); ++i) {
        AetherGateChainTile* tile = s->tileAt(i);
        CHECK(!tile->objectName().isEmpty());
        CHECK(!tile->accessibleName().isEmpty());
        CHECK(sweep(tile) == 0);
        auto* control = tile->findChild<AetherGateChainControl*>(
            QString(), Qt::FindDirectChildrenOnly);
        CHECK(control != nullptr);
        if (control)
            CHECK(sweep(control) == 0);
    }

    auto* pane = w->findChild<QWidget*>(QStringLiteral("gateChainDetailPane"));
    CHECK(pane != nullptr);
    if (pane)
        CHECK(sweep(pane) == 0);
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_test"));
    QApplication app(argc, argv);

    testStripRendersTheGatesArrayInTheGatesOrder();
    testToggleSendsTheGatesQueryAndDoesNotFlipUntilTheReply();
    testSelectAppendsItsValueToAQueryEndingInEquals();
    testFreeEntryRefusesAWidthThereIsNoFilterFor();
    testAFixedRowCarriesNoControlAndSaysWhy();
    testAnAbsentMeasurementIsADashAndNeverAZero();
    testAChainlessGateStillGetsThirteenHonestRows();
    testSelectingATileFillsTheDetailArea();
    testNothingScrollsOnTheChainWindowAtTheInitialSize();
    testEveryWidgetTheWindowBuildsHasAName();

    std::printf("\n%d aether gate chain test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
