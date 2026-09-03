// The CHAIN window's array contract -- design §0.1 -- driven through a real
// AetherGateApplet and socket-free.
//
// Same harness as tests/diversity_band_test.cpp and for the same reason: the
// window owns no transport. It is reached by pressing the applet's own door,
// fed by the applet's DiversityBandPoller, and every click on it leaves as a
// request signal the applet turns into one GET. Driving it any other way would
// be testing a wiring diagram we drew rather than the one that ships.
//
// One assertion each: the strip renders the gate's array in the gate's order,
// all four kinds; a toggle sends exactly the gate's own query and does NOT move
// until the reply; a select appends its value to a query ending in "="; the
// free entry refuses a width there is no filter to design; a fixed row has no
// control and says why on its own face; an absent `measured` is an em dash and
// never a zero; a chain-less gate still gets thirteen honest rows; selecting a
// tile fills the detail area; nothing scrolls at the initial size; every widget
// the window builds has a name the automation bridge can address it by.
//
// The eight things the operator asked for after the first build are the other
// file: tests/aether_gate_chain_ux_test.cpp.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

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
    // rather than being dropped -- the entire point of a gate-authored array --
    // and it is on the strip in EVERY mode, because the app cannot reason about
    // whether a stage it has never seen is for phone or for CW.
    AetherGateChainTile* unknown = s->tile(QStringLiteral("steer"));
    CHECK(unknown != nullptr);
    if (unknown) {
        CHECK(unknown->stage().name == QStringLiteral("BEAM STEER"));
        CHECK(unknown->stage().detail == QStringLiteral("42 degrees"));
    }
    CHECK(s->tilesInMode().size() == 5);
}

void testToggleSendsTheGatesQueryAndDoesNotFlipUntilTheReply()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFilter);
    // The write answers with the noise blanker still OFF, which is the whole
    // point: a gate that refused, or that has not applied it yet, must not see
    // the switch move.
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
    CHECK(toggle->text() == QStringLiteral("OFF"));

    toggle->click();
    // Before the event loop has turned: one write is on the wire and the switch
    // has not moved.
    CHECK(!toggle->isChecked());
    settle();
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?nb=on"));
    CHECK(!toggle->isChecked());

    // Now the gate says it happened, and only now does the switch move.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, chainFilterNbOn()};
    filterTick(applet);
    auto* after = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_nb"));
    CHECK(after != nullptr);
    if (after) {
        CHECK(after->isChecked());
        CHECK(after->text() == QStringLiteral("ON"));
    }
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

void testAFixedRowCarriesNoControlAndSaysWhyOnItsOwnFace()
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
    if (tile) {
        CHECK(tile->toolTip() == QStringLiteral("set on the setup page"));
        // Visibly inert (design §0.3 item 3): no hand, and the reason printed
        // on the tile rather than hidden on a hover.
        CHECK(tile->cursor().shape() == Qt::ArrowCursor);
        CHECK(tile->property("fixed").toBool());
    }
    // Its reason is the one the FRONT END card prints ONCE, under all of its
    // rows, so the row itself stays quiet: seven rows each repeating "set on
    // the setup page" was what the operator read as noise.
    QLabel* why = label(w, QStringLiteral("gateChainWhy_lna"));
    CHECK(why != nullptr);
    if (why)
        CHECK(!why->isVisibleTo(w));
    QLabel* hint = label(w, QStringLiteral("gateChainFrontEndHint"));
    CHECK(hint != nullptr);
    if (hint) {
        CHECK(hint->isVisibleTo(w));
        CHECK(hint->text().contains(QStringLiteral("SETUP PAGE")));
    }
    // And an ACTIONABLE row carries the hand it promises.
    AetherGateChainTile* live = strip(w)->tile(QStringLiteral("nb"));
    CHECK(live != nullptr);
    if (live)
        CHECK(live->cursor().shape() == Qt::PointingHandCursor);
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

    // The levels moved off the card and into the inspector: a card has room
    // for ONE measured line and the setting is the one an operator reads.
    CHECK(w->findChild<QLabel*>(QStringLiteral("gateChainLevels_roof_rf")) == nullptr);

    // roof_rf's in_db is null and its out_db is -97.4: one dash, one number.
    QTest::mouseClick(strip(w)->tile(QStringLiteral("roof_rf")), Qt::LeftButton);
    settle();
    QLabel* levels = label(w, QStringLiteral("gateChainDetailLevels"));
    CHECK(levels != nullptr);
    if (levels) {
        CHECK(levels->toolTip().contains(QStringLiteral("—")));
        CHECK(levels->toolTip().contains(QStringLiteral("-97.4")));
        CHECK(!levels->toolTip().contains(QStringLiteral("0.0 · out")));
    }
    // A row with no `measured` at all shows dashes rather than a plausible zero.
    QTest::mouseClick(strip(w)->tile(QStringLiteral("steer")), Qt::LeftButton);
    settle();
    if (levels) {
        CHECK(levels->toolTip().contains(QStringLiteral("—")));
        CHECK(!levels->toolTip().contains(QStringLiteral("0.0")));
    }
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

void testSelectingATileFillsTheDetailAreaAndSharesItsAccent()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("agc"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    QTest::mouseClick(tile, Qt::LeftButton);
    settle();

    // The pane's title names the stage (design §0.3 item 7) and the tile it
    // names is the one wearing the accent frame.
    CHECK(labelText(w, "gateChainDetailName") == QStringLiteral("AGC"));
    CHECK(tile->isSelected());
    CHECK(tile->property("selected").toBool());
    CHECK(!strip(w)->tile(QStringLiteral("nb"))->property("selected").toBool());

    // The inspector answers four questions in order: what it does to the
    // SOUND, what it is doing now, the control, and what you would hear
    // without it. None of them repeats the card verbatim.
    auto* tip = w->findChild<QLabel*>(QStringLiteral("gateChainDetailTip"));
    CHECK(tip != nullptr);
    if (tip) {
        CHECK(tip->toolTip().contains(QStringLiteral("gain follows the signal")));
        CHECK(!tip->toolTip().contains(QStringLiteral("AGC-T")));
    }
    CHECK(labelText(w, "gateChainDetailText").startsWith(QStringLiteral("now: med")));
    CHECK(w->findChild<QComboBox*>(QStringLiteral("gateChainDetailSelect_agc")) != nullptr);
    QLabel* off = label(w, QStringLiteral("gateChainDetailOff"));
    CHECK(off != nullptr);
    if (off) {
        CHECK(off->isVisibleTo(w));
        CHECK(off->text().startsWith(QStringLiteral("off:")));
    }

    // Nothing selected: the pane asks for a click rather than showing a dash
    // where a sentence goes.
    strip(w)->selectStage(QString());
    settle();
    CHECK(labelText(w, "gateChainDetailTip") == QStringLiteral("Click a stage."));
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

    // The mode row's own widgets, by the names the design fixed.
    for (const QString& id : {QStringLiteral("phone"), QStringLiteral("cw"),
                              QStringLiteral("data")}) {
        CHECK(button(w, QStringLiteral("gateChainMode_") + id) != nullptr);
        CHECK(button(w, QStringLiteral("gateChainSetButton_") + id) != nullptr);
    }
    CHECK(w->findChild<QWidget*>(QStringLiteral("gateChainNotForMode")) != nullptr);
    auto* row = w->findChild<QWidget*>(QStringLiteral("gateChainModeRow"));
    CHECK(row != nullptr);
    if (row)
        CHECK(sweep(row) == 0);
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
    testAFixedRowCarriesNoControlAndSaysWhyOnItsOwnFace();
    testAnAbsentMeasurementIsADashAndNeverAZero();
    testAChainlessGateStillGetsThirteenHonestRows();
    testSelectingATileFillsTheDetailAreaAndSharesItsAccent();
    testNothingScrollsOnTheChainWindowAtTheInitialSize();
    testEveryWidgetTheWindowBuildsHasAName();

    std::printf("\n%d aether gate chain test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
