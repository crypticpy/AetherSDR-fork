// B23 -- the front-end linearity guard, on the CHAIN window's FRONT END card.
//
// The gate side (already shipped) answers GET /device with a "frontend" key:
// available, guard on/off, the floor and current LNA state, whether the dBm
// scale is still calibrated for the LNA state in force, headroom and clips
// over the last second, the guard's own state machine, and its event log.
// This is the app half: a HEADROOM row (a measured line, no control, warn
// tone under 3 dB or with a clip), a GUARD row (the one control on this
// card: on/off plus a floor), and a calibration caveat that appears only
// when a guard-moved LNA state has broken the gate's own dBm trim.
//
// Nothing here is optimistic, the same as every other control in this
// window: a click greys the control and sends one write; only a body that
// answers through applyDevice() moves it. The real /device poll is
// AetherGateApplet's, which is out of scope for this change -- every test
// here feeds AetherGateChainWindow::applyDevice() directly, the way the real
// poll will once that wiring lands.
#include "AetherGateChainFixture.h"

#include "gui/AetherGateChainRows.h"
#include "gui/DiversityAge.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

// One /device body with a "frontend" key, applied the way the real poll
// will once AetherGateApplet is wired up to call this.
void feedDevice(AetherGateChainWindow* w, const QJsonObject& fe)
{
    w->applyDevice(QJsonDocument::fromJson(deviceWithFrontend(fe)).object());
    settle();
}

// --------------------------------------------------------------------------
// HEADROOM
// --------------------------------------------------------------------------

void testHeadroomRowTextAndWarnToneUnderThreeDbOrWithClips()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));
    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("frontend_headroom"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    CHECK(tile->primaryText() == QStringLiteral("12.3 dB · clips 0"));
    QLabel* value = w->findChild<QLabel*>(QStringLiteral("gateChainValue_frontend_headroom"));
    CHECK(value != nullptr);
    if (value)
        CHECK(value->property("live").toBool() == false);

    // MUTATION: under 3 dB of headroom wears the warn tone.
    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("1"), true,
                           1.2, 0, QStringLiteral("idle")));
    CHECK(tile->primaryText() == QStringLiteral("1.2 dB · clips 0"));
    if (value)
        CHECK(value->property("live").toBool() == true);

    // MUTATION: plenty of headroom but a clip still wears it.
    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.0, 3, QStringLiteral("idle")));
    CHECK(tile->primaryText() == QStringLiteral("12.0 dB · clips 3"));
    if (value)
        CHECK(value->property("live").toBool() == true);
}

void testHeadroomRowAbsentWhenUnavailable()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    // kDevice carries no "frontend" key at all: absent, not dashed.
    CHECK(strip(w)->tile(QStringLiteral("frontend_headroom")) == nullptr);
    CHECK(strip(w)->tile(QStringLiteral("frontend_guard")) == nullptr);

    // MUTATION: an explicit "available": false must stay absent too, not
    // merely a body with no key at all.
    QJsonObject fe;
    fe.insert(QStringLiteral("available"), false);
    feedDevice(w, fe);
    CHECK(strip(w)->tile(QStringLiteral("frontend_headroom")) == nullptr);
    CHECK(strip(w)->tile(QStringLiteral("frontend_guard")) == nullptr);
}

// --------------------------------------------------------------------------
// GUARD -- the toggle and the floor
// --------------------------------------------------------------------------

void testTogglingGuardSendsGuardOnThenGuardOff()
{
    FakeGate net;
    net.routes[QStringLiteral("/frontend/set")] = {QNetworkReply::NoError,
                                                    QByteArrayLiteral("{}")};
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(false, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));

    auto* guardToggle = w->findChild<QPushButton*>(QStringLiteral("gateChainFrontendGuard"));
    CHECK(guardToggle != nullptr);
    if (!guardToggle)
        return;
    CHECK(!guardToggle->isChecked());

    guardToggle->click();
    settle();
    CHECK(sentQueries(net, QStringLiteral("/frontend/set")) == QStringList{QStringLiteral("guard=on")});
    // Nothing optimistic: the switch has not moved on the click alone.
    CHECK(!guardToggle->isChecked());

    // The gate confirms it: guard is on now, so the SAME control's next
    // click carries a different query.
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(guardToggle->isChecked());

    guardToggle->click();
    settle();
    const QStringList sent = sentQueries(net, QStringLiteral("/frontend/set"));
    CHECK(sent.size() == 2);
    CHECK(sent.value(0) == QStringLiteral("guard=on"));
    CHECK(sent.value(1) == QStringLiteral("guard=off"));
}

void testFloorSelectSendsFloorFour()
{
    FakeGate net;
    net.routes[QStringLiteral("/frontend/set")] = {QNetworkReply::NoError,
                                                    QByteArrayLiteral("{}")};
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.3, 0, QStringLiteral("idle")));

    auto* floor = w->findChild<QComboBox*>(QStringLiteral("gateChainFrontendFloor"));
    CHECK(floor != nullptr);
    if (!floor)
        return;
    const int idx = floor->findData(QStringLiteral("4"));
    CHECK(idx >= 0);
    if (idx < 0)
        return;
    QMetaObject::invokeMethod(floor, "activated", Qt::DirectConnection, Q_ARG(int, idx));
    settle();
    CHECK(sentQueries(net, QStringLiteral("/frontend/set")).contains(QStringLiteral("floor=4")));
}

// --------------------------------------------------------------------------
// The calibration caveat
// --------------------------------------------------------------------------

void testCalibrationNoteAppearsOnlyWhenNotCalibrated()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QLabel* note = w->findChild<QLabel*>(QStringLiteral("gateChainFrontendCalNote"));
    CHECK(note != nullptr);
    if (!note)
        return;
    CHECK(!note->isVisible());

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(!note->isVisible());

    // MUTATION: calibrated for LNA 0, guard has moved it to 1 -- the note
    // appears, and names both states.
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(note->isVisible());
    CHECK(note->text().contains(QStringLiteral("LNA 0")));
    CHECK(note->text().contains(QStringLiteral("1")));

    // Back to calibrated hides it again.
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("0"), true,
                           12.3, 0, QStringLiteral("idle")));
    CHECK(!note->isVisible());
}

// --------------------------------------------------------------------------
// The inspector
// --------------------------------------------------------------------------

void testInspectorTextForTheGuardRow()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    QJsonObject ev;
    ev.insert(QStringLiteral("t"), 1756871720.0);
    ev.insert(QStringLiteral("from"), QStringLiteral("0"));
    ev.insert(QStringLiteral("to"), QStringLiteral("1"));
    ev.insert(QStringLiteral("reason"), QStringLiteral("clipping"));
    QJsonArray events{ev};

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           1.2, 0, QStringLiteral("holding"), events));

    strip(w)->selectStage(QStringLiteral("frontend_guard"));
    settle();

    CHECK(labelText(w, "gateChainDetailCaption") == QStringLiteral("GUARD — what it does"));
    // 1. What it does.
    const QString tip = labelText(w, "gateChainDetailTip");
    CHECK(tip.contains(QStringLiteral("LNA")));
    // 2. What it is doing now -- the last EVENT, not the card's own on/off
    // sentence.
    const QString now = labelText(w, "gateChainDetailText");
    CHECK(now.contains(QStringLiteral("stepped 0")));
    CHECK(now.contains(QStringLiteral("1")));
    CHECK(now.contains(QStringLiteral("clipping")));
    // 3. The control, at full size.
    CHECK(w->findChild<QPushButton*>(QStringLiteral("gateChainDetailToggle_frontend_guard"))
          != nullptr);
    // 4. The caveat, because this body is not calibrated.
    const QString aside = labelText(w, "gateChainDetailOff");
    CHECK(aside.contains(QStringLiteral("LNA 0")));
    CHECK(aside.contains(QStringLiteral("now 1")));
}

// The fallback: no events yet, so "now" is the card's own sentence.
void testInspectorNowFallsBackToTheCardSentenceBeforeAnyEvent()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), true,
                           12.3, 0, QStringLiteral("idle")));
    strip(w)->selectStage(QStringLiteral("frontend_guard"));
    settle();
    const QString now = labelText(w, "gateChainDetailText");
    CHECK(now.contains(QStringLiteral("on")));
    CHECK(now.contains(QStringLiteral("LNA 1")));
}

// --------------------------------------------------------------------------
// Hygiene: names, wrap, and the card's own arithmetic
// --------------------------------------------------------------------------

void testEveryNewWidgetHasANameAndNothingWraps()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           1.2, 2, QStringLiteral("stepping_up")));

    // Direct children only, and never inside a QComboBox: the bridge
    // addresses the widgets THIS window builds, and a combo's private popup
    // is Qt's. Same convention aether_gate_chain_test.cpp's
    // testEveryWidgetTheWindowBuildsHasAName() already uses.
    const auto sweep = [](QWidget* parent) {
        int missing = 0;
        for (QWidget* kid : parent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (kid->objectName().isEmpty())
                ++missing;
        return missing;
    };

    for (const QString& id :
        {QStringLiteral("frontend_headroom"), QStringLiteral("frontend_guard")}) {
        AetherGateChainTile* tile = strip(w)->tile(id);
        CHECK(tile != nullptr);
        if (!tile)
            continue;
        CHECK(sweep(tile) == 0);
        auto* control = tile->findChild<AetherGateChainControl*>(
            QString(), Qt::FindDirectChildrenOnly);
        if (control)
            CHECK(sweep(control) == 0);
        for (QLabel* l : tile->findChildren<QLabel*>())
            CHECK(!l->wordWrap());
    }

    QLabel* note = w->findChild<QLabel*>(QStringLiteral("gateChainFrontendCalNote"));
    CHECK(note != nullptr);
    if (note)
        CHECK(!note->wordWrap());

    for (const char* name : {"gateChainFrontendGuard", "gateChainFrontendFloor"}) {
        QWidget* widget = w->findChild<QWidget*>(QString::fromLatin1(name));
        CHECK(widget != nullptr);
        if (!widget)
            std::printf("  missing: %s\n", name);
    }
}

// The same check tests/aether_gate_chain_test.cpp runs at the initial size,
// with the two new rows on the card: the viewport must still be big enough
// for its own content, and neither scrollbar shows.
void testNothingScrollsWithTheTwoNewRowsOnTheCard()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                           1.2, 2, QStringLiteral("stepping_up")));
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

// With CHAIN_FRONTEND_RENDER_PNG set, a window with the guard on, 1.2 dB of
// headroom, one event and an uncalibrated scale is written out so B23 can be
// looked at.
void testRenderFrontendWhenAsked()
{
    const QByteArray path = qgetenv("CHAIN_FRONTEND_RENDER_PNG");
    if (path.isEmpty())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);

    QJsonObject ev;
    ev.insert(QStringLiteral("t"), 1756871720.0);
    ev.insert(QStringLiteral("from"), QStringLiteral("0"));
    ev.insert(QStringLiteral("to"), QStringLiteral("1"));
    ev.insert(QStringLiteral("reason"), QStringLiteral("clipping"));
    feedDevice(w, frontend(true, QStringLiteral("0"), QStringLiteral("1"), false,
                          1.2, 1, QStringLiteral("stepping_up"), QJsonArray{ev}));
    strip(w)->selectStage(QStringLiteral("frontend_guard"));
    settle();
    w->grab();
    settle();
    CHECK(w->grab().save(QString::fromLocal8Bit(path)));
}

// --------------------------------------------------------------------------
// PER TALKER -- the value carries "learned N ago" for the current talker
// --------------------------------------------------------------------------
//
// Rows.cpp's own chainFallback() is exercised directly (a pure function, no
// gate or window needed) rather than through kChainlessFilter as it ships:
// that fixture's "talker" block still carries the pre-gate-97fa5e7 bare-id
// remembered[] shape, which is exactly the "older gate" case (c) below pins,
// so a local copy carries the new object shape instead of editing the
// shared fixture out from under its other readers.

// remembered[]'s id, and its filter -- present with an age, present with no
// age at all, or entirely absent (an older gate's own answer for "the gate
// has kept nothing for them").
QJsonObject rememberedEntry(int id, bool withFilter, qint64 learnedAt = -1)
{
    QJsonObject entry;
    entry[QStringLiteral("id")] = id;
    if (withFilter) {
        QJsonObject filter;
        filter[QStringLiteral("low_hz")] = 300;
        filter[QStringLiteral("high_hz")] = 2700;
        if (learnedAt >= 0)
            filter[QStringLiteral("learned_at")] = double(learnedAt);
        entry[QStringLiteral("filter")] = filter;
    }
    return entry;
}

const AetherSDR::ChainStage* findChainRow(const QList<AetherSDR::ChainStage>& rows,
                                          const QString& id)
{
    for (const auto& row : rows) {
        if (row.id == id)
            return &row;
    }
    return nullptr;
}

void testPerTalkerRowValueCarriesLearnedAgeWhenTheGateSendsIt()
{
    QJsonObject filter = QJsonDocument::fromJson(kChainlessFilter).object();
    QJsonObject talker = filter.value(QStringLiteral("talker")).toObject();
    CHECK(talker.value(QStringLiteral("id")).toInt() == 32);

    const qint64 learnedAt = QDateTime::currentSecsSinceEpoch() - 7300; // ~2 h back
    QJsonArray remembered;
    remembered.append(rememberedEntry(30, true));             // no learned_at at all
    remembered.append(rememberedEntry(32, true, learnedAt));  // the CURRENT talker
    remembered.append(rememberedEntry(33, false));            // no filter kept
    talker[QStringLiteral("remembered")] = remembered;
    filter[QStringLiteral("talker")] = talker;

    const QList<AetherSDR::ChainStage> rows = AetherSDR::chainFallback(filter);
    const AetherSDR::ChainStage* row = findChainRow(rows, QStringLiteral("talker"));
    CHECK(row != nullptr);
    if (!row)
        return;

    const QString age =
        AetherSDR::diversityAgeSince(learnedAt, QDateTime::currentSecsSinceEpoch());
    CHECK(row->detail == QStringLiteral("on · fast · id 32 · 3 kept · learned %1").arg(age));

    // (b) MUTATION: #32 kept a filter but the gate never dated it -- the
    // value reads exactly as it always has. No zero age is ever invented for
    // a filter the gate did not say when it learned.
    QJsonArray rememberedNoAge;
    rememberedNoAge.append(rememberedEntry(30, true));
    rememberedNoAge.append(rememberedEntry(32, true));
    rememberedNoAge.append(rememberedEntry(33, false));
    talker[QStringLiteral("remembered")] = rememberedNoAge;
    filter[QStringLiteral("talker")] = talker;
    const QList<AetherSDR::ChainStage> rowsNoAge = AetherSDR::chainFallback(filter);
    const AetherSDR::ChainStage* rowNoAge = findChainRow(rowsNoAge, QStringLiteral("talker"));
    CHECK(rowNoAge != nullptr);
    if (rowNoAge)
        CHECK(rowNoAge->detail == QStringLiteral("on · fast · id 32 · 3 kept"));

    // (c) MUTATION: an older gate's bare-id remembered[], exactly as
    // kChainlessFilter still ships it -- the count still reads, no entry is
    // ever an object to match the current talker against, and the value is
    // the pre-existing text untouched.
    QJsonArray rememberedOldShape{30, 31, 32, 33};
    talker[QStringLiteral("remembered")] = rememberedOldShape;
    filter[QStringLiteral("talker")] = talker;
    const QList<AetherSDR::ChainStage> rowsOldShape = AetherSDR::chainFallback(filter);
    const AetherSDR::ChainStage* rowOldShape =
        findChainRow(rowsOldShape, QStringLiteral("talker"));
    CHECK(rowOldShape != nullptr);
    if (rowOldShape)
        CHECK(rowOldShape->detail == QStringLiteral("on · fast · id 32 · 4 kept"));

    // The row's own hover is the WHAT + WHAT FOR sentence design §2.7's
    // table gives it, elided rather than wrapped, and within the CHAIN
    // window's own 90-char rule.
    CHECK(row->shortTip
          == QStringLiteral(
                 "Brings back each remembered talker's own filter the moment "
                 "they key up."));
    CHECK(row->shortTip.length() <= 90);
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_frontend_test"));
    QApplication app(argc, argv);

    testHeadroomRowTextAndWarnToneUnderThreeDbOrWithClips();
    testHeadroomRowAbsentWhenUnavailable();
    testTogglingGuardSendsGuardOnThenGuardOff();
    testFloorSelectSendsFloorFour();
    testCalibrationNoteAppearsOnlyWhenNotCalibrated();
    testInspectorTextForTheGuardRow();
    testInspectorNowFallsBackToTheCardSentenceBeforeAnyEvent();
    testEveryNewWidgetHasANameAndNothingWraps();
    testNothingScrollsWithTheTwoNewRowsOnTheCard();
    testRenderFrontendWhenAsked();
    testPerTalkerRowValueCarriesLearnedAgeWhenTheGateSendsIt();

    std::printf("\n%d aether gate chain frontend test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
