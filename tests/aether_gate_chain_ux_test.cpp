// What the operator asked for after the first build of the CHAIN window --
// design §0.3, all eight items -- including the two BUGS he found, each
// reproduced here before it was fixed:
//
//   * item 5, "contour needs several clicks". The gate is a
//     ThreadingHTTPServer, so a poll issued just before a write can read the
//     status BEFORE the write applies and still answer AFTER the write's own
//     reply; applying every body as it arrived made the tile flick back, and
//     the next click undid the write that had landed. SlowGate reproduces
//     exactly that ordering, and the assertion is that ONE click leaves the
//     stage switched and puts ONE write on the wire.
//   * item 6, "roofing cannot be changed". A refused write must say so ON THE
//     TILE and leave the row where it was; an RF roof this gate cannot move
//     must be visibly inert with its reason on its own face; and a width the
//     gate did not list must be visible and unpickable rather than gone.
//
// And then the rest of the list: the MODE row and its two sets (items 1 and 2),
// what a tile says now that the blue dot is gone (items 3, 4 and 7), the tile
// metrics, and the arrow keys (item 8).
//
// The array contract itself is the other file: tests/aether_gate_chain_test.cpp.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFontMetrics>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTest>

#include <cstdio>

using namespace AetherGateChainFixture;

namespace {

// --------------------------------------------------------------------------
// §0.3 items 3, 4 and 7 -- what a tile says, and how big it is
// --------------------------------------------------------------------------

void testNoBlueDotAnywhereOnTheStrip()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    // The dot duplicated the switch and nobody knew what it meant (§0.3 item
    // 4). It is gone, and the switch says the word instead.
    for (const QString& id : {QStringLiteral("nb"), QStringLiteral("contour"),
                              QStringLiteral("apf"), QStringLiteral("talker")}) {
        CHECK(label(w, QStringLiteral("gateChainDot_") + id) == nullptr);
        auto* toggle = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_") + id);
        CHECK(toggle != nullptr);
        if (toggle) {
            CHECK(toggle->text() == QStringLiteral("ON")
                  || toggle->text() == QStringLiteral("OFF"));
        }
    }
}

// The redesign: FOUR labelled groups, left to right, with the seven front-end
// rows collapsed into ONE summary card instead of seven tiles that each said
// "set on the setup page".
void testTheDiagramIsFourGroupsLeftToRight()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);
    settle();
    w->grab();

    QWidget* columns[4] = {
        w->findChild<QWidget*>(QStringLiteral("gateChainGroup_frontend")),
        w->findChild<QWidget*>(QStringLiteral("gateChainGroup_pair")),
        w->findChild<QWidget*>(QStringLiteral("gateChainGroup_passband")),
        w->findChild<QWidget*>(QStringLiteral("gateChainGroup_out"))};
    for (QWidget* column : columns) {
        CHECK(column != nullptr);
        if (!column)
            return;
        CHECK(column->isVisibleTo(w));
    }
    // Left to right, in signal order, with an arrow in each gutter.
    for (int i = 1; i < 4; ++i)
        CHECK(columns[i]->mapTo(w, QPoint(0, 0)).x()
              > columns[i - 1]->mapTo(w, QPoint(0, 0)).x());
    for (const QString& id : {QStringLiteral("frontend"), QStringLiteral("pair"),
                              QStringLiteral("passband")}) {
        CHECK(label(w, QStringLiteral("gateChainArrow_") + id) != nullptr);
    }

    // The front end is one card with rows in it, not seven cards.
    auto* card = w->findChild<QFrame*>(QStringLiteral("gateChainFrontEndCard"));
    CHECK(card != nullptr);
    for (const QString& id : {QStringLiteral("lna"), QStringLiteral("roof_rf"),
                              QStringLiteral("adc")}) {
        AetherGateChainTile* t = strip(w)->tile(id);
        CHECK(t != nullptr);
        if (t) {
            CHECK(t->shape() == AetherSDR::ChainTileShape::Line);
            CHECK(card != nullptr && card->isAncestorOf(t));
        }
    }
    CHECK(label(w, QStringLiteral("gateChainFrontEndHint")) != nullptr);

    // Every LIVE stage outside the front end is a card of the one width the
    // primary-line format was measured against.
    int cards = 0;
    for (AetherGateChainTile* t : strip(w)->tilesInMode()) {
        if (t->shape() != AetherSDR::ChainTileShape::Card)
            continue;
        ++cards;
        CHECK(t->width() == AetherSDR::kChainCardWidth);
        CHECK(t->height() >= AetherSDR::kChainCardHeight);
    }
    CHECK(cards >= 12);
}

// The rule the whole format table exists for: the one measured line on a card
// is shortened by whole parts and then whole words, so it NEVER ends in an
// ellipsis and never runs past the card.
void testNoCardEverElidesItsMeasuredLine()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);
    settle();
    w->grab();

    for (AetherGateChainTile* t : strip(w)->tilesInMode()) {
        const QString line = t->primaryText();
        CHECK(!line.contains(QChar(0x2026)));       // no "..." glyph
        CHECK(!line.endsWith(QStringLiteral("...")));
        QLabel* value = t->findChild<QLabel*>(QStringLiteral("gateChainValue_") + t->id());
        CHECK(value != nullptr);
        if (value && value->isVisibleTo(w)) {
            const QFontMetrics fm(value->font());
            CHECK(fm.horizontalAdvance(line) <= value->width());
        }
    }
}

// The initial size is arithmetic, not hope: at 1120x820 the whole diagram and
// the inspector are on screen and neither scrollbar is up.
void testNothingScrollsAtTheInitialSize()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainFullFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 820);
    settle();
    w->grab();

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("gateChainScroll"));
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
}

// The evidence. With CHAIN_RENDER_PNG set, the window is rendered offscreen
// against the twenty-five-row payload and written out, so the redesign can be
// LOOKED at rather than only asserted about.
void testRenderTheWindowWhenAsked()
{
    const QByteArray path = qgetenv("CHAIN_RENDER_PNG");
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
    settle();
    strip(w)->selectStage(QStringLiteral("combiner"));
    settle();
    const QPixmap shot = w->grab();
    CHECK(!shot.isNull());
    CHECK(shot.save(QString::fromLocal8Bit(path)));
}

// --------------------------------------------------------------------------
// §0.3 item 5 -- "contour needs several clicks"
// --------------------------------------------------------------------------

void testOneClickSwitchesAStageEvenWhenAStalePollLandsAfterTheWriteReply()
{
    SlowGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* toggle = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_contour"));
    CHECK(toggle != nullptr);
    if (!toggle)
        return;
    CHECK(toggle->isChecked());          // the gate says the contour is on

    // The gate answers the write at 60 ms with "contour off", and answers a
    // poll that was ALREADY reading the old status at 120 ms with "contour on".
    // Both are true answers; the second one is simply older than it looks.
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                 chainlessContourOff()};
    net.delays[QStringLiteral("/filter/set")] = 60;
    net.delays[QStringLiteral("/filter")] = 120;

    const int writesBefore = countWrites(net);
    toggle->click();
    // A second press while the first is unanswered must not reach the gate:
    // that is how one click became three writes and the operator watched the
    // stage end up back where it started.
    CHECK(!toggle->isEnabled());
    toggle->click();
    filterPollNow(applet);               // the stale poll, now on the wire

    QTest::qWait(300);

    auto* after = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_contour"));
    CHECK(after != nullptr);
    if (after) {
        CHECK(!after->isChecked());      // OFF, and it stayed off
        CHECK(after->text() == QStringLiteral("OFF"));
        CHECK(after->isEnabled());       // the answer came, so the switch works again
    }
    CHECK(countWrites(net) == writesBefore + 1);
    CHECK(net.log.contains(QStringLiteral("/filter/set?contour=off")));
}

void testAStalePollBeforeTheWriteReplyStillEndsInTheNewState()
{
    // The other ordering, for completeness: the poll answers FIRST with the old
    // state and the write's own reply lands after it. This one always worked;
    // it must keep working.
    SlowGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                 chainlessContourOff()};
    net.delays[QStringLiteral("/filter/set")] = 120;
    net.delays[QStringLiteral("/filter")] = 40;

    auto* toggle = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_contour"));
    CHECK(toggle != nullptr);
    if (!toggle)
        return;
    toggle->click();
    filterPollNow(applet);
    QTest::qWait(300);

    auto* after = w->findChild<QPushButton*>(QStringLiteral("gateChainToggle_contour"));
    CHECK(after != nullptr);
    if (after)
        CHECK(!after->isChecked());
}

// --------------------------------------------------------------------------
// §0.3 item 6 -- the roofs
// --------------------------------------------------------------------------

void testTheRfRoofSaysOnItsOwnFaceThatTheGateHasNotBuiltItYet()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);      // no analogue_options
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    AetherGateChainTile* tile = strip(w)->tile(QStringLiteral("roof_rf"));
    CHECK(tile != nullptr);
    if (!tile)
        return;
    CHECK(tile->stage().fixed);
    CHECK(tile->cursor().shape() == Qt::ArrowCursor);
    CHECK(w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_roof_rf")) == nullptr);

    // It is a FRONT END row, so its reason is not printed a second time under
    // the summary card's own hint -- it is on the hover, and spelled out in
    // the inspector the moment the row is clicked.
    CHECK(tile->stage().why == QStringLiteral("this receiver does not offer it yet"));
    QTest::mouseClick(tile, Qt::LeftButton);
    settle();
    QLabel* aside = label(w, QStringLiteral("gateChainDetailOff"));
    CHECK(aside != nullptr);
    if (aside) {
        CHECK(aside->isVisibleTo(w));
        CHECK(aside->toolTip() == QStringLiteral("this receiver does not offer it yet"));
    }
}

void testAGateThatListsItsAnalogueOptionsGetsALiveRfRoof()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kRoofingFilter);
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kRoofingFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* select = w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_roof_rf"));
    CHECK(select != nullptr);
    if (!select)
        return;
    CHECK(select->isEnabled());
    CHECK(select->currentData().toString() == QStringLiteral("200000"));
    const int idx = select->findData(QStringLiteral("600000"));
    CHECK(idx >= 0);
    if (idx < 0)
        return;
    QMetaObject::invokeMethod(select, "activated", Qt::DirectConnection, Q_ARG(int, idx));
    settle();
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?roof_hz=600000"));
}

void testAWidthTheGateDidNotListStaysOnTheMenuAndCannotBePicked()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kRoofingFilter);   // digital_options: 3000/6000/12000
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* select = w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_roof_digital"));
    CHECK(select != nullptr);
    if (!select)
        return;
    auto* model = qobject_cast<QStandardItemModel*>(select->model());
    CHECK(model != nullptr);
    if (!model)
        return;

    const int offered = select->findData(QStringLiteral("3000"));
    const int notOffered = select->findData(QStringLiteral("300"));
    CHECK(offered >= 0);
    CHECK(notOffered >= 0);            // still on the menu -- it did not vanish
    if (offered >= 0)
        CHECK(model->item(offered)->isEnabled());
    if (notOffered >= 0)
        CHECK(!model->item(notOffered)->isEnabled());
}

void testARefusedWriteLandsOnTheTileAndTheRowDoesNotMove()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    // What the gate on the bench actually answers a digital_roof_hz with.
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        QByteArray(R"({"error": "bad value: unknown filter setting 'digital_roof_hz'"})")};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* select = w->findChild<QComboBox*>(QStringLiteral("gateChainSelect_roof_digital"));
    CHECK(select != nullptr);
    if (!select)
        return;
    const QString before = select->currentData().toString();
    const int idx = select->findData(QStringLiteral("600"));
    CHECK(idx >= 0);
    if (idx < 0)
        return;
    QMetaObject::invokeMethod(select, "activated", Qt::DirectConnection, Q_ARG(int, idx));
    settle();

    // The combo went straight back where the gate had it...
    CHECK(select->currentData().toString() == before);
    // ...and the refusal is ON THE TILE, not only on a status line.
    QLabel* why = label(w, QStringLiteral("gateChainWhy_roof_digital"));
    CHECK(why != nullptr);
    if (why) {
        CHECK(why->isVisibleTo(w));
        CHECK(why->toolTip().contains(QStringLiteral("digital_roof_hz")));
        CHECK(why->property("live").toBool());     // warning-coloured
    }
    // The receiver's own words are in the INSPECTOR, where the operator is
    // already looking. The status line stays the three words it is meant to be.
    QLabel* note = label(w, QStringLiteral("gateChainDetailNote"));
    CHECK(note != nullptr);
    if (note) {
        CHECK(note->isVisibleTo(w));
        CHECK(note->toolTip().contains(QStringLiteral("digital_roof_hz")));
    }
    CHECK(labelText(w, "gateChainStatusLabel") == QStringLiteral("live"));

    // And it survives the next poll: a refusal that flashed for 500 ms would
    // be no better than the status line nobody read.
    filterTick(applet);
    if (why)
        CHECK(why->toolTip().contains(QStringLiteral("digital_roof_hz")));
}

// --------------------------------------------------------------------------
// §0.3 items 1 and 2 -- MODE, and the sets
// --------------------------------------------------------------------------

void testTheModeChoosesWhatIsOnTheStrip()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    AetherGateChainStrip* s = strip(w);

    // PHONE: everything except the CW peak filter.
    CHECK(w->mode() == ChainMode::Phone);
    CHECK(s->tilesInMode().size() == 12);
    CHECK(s->tile(QStringLiteral("apf")) != nullptr);      // still built...
    CHECK(!s->tile(QStringLiteral("apf"))->isVisibleTo(w)); // ...just not on the strip
    CHECK(button(w, QStringLiteral("gateChainSetButton_phone"))->isVisibleTo(w));
    CHECK(button(w, QStringLiteral("gateChainSetButton_phone"))->text()
          == QStringLiteral("SET UP FOR PHONE"));
    CHECK(!button(w, QStringLiteral("gateChainSetButton_cw"))->isVisibleTo(w));

    // CW: the APF comes up and the speech-fitted stages go into the group.
    button(w, QStringLiteral("gateChainMode_cw"))->click();
    settle();
    CHECK(w->mode() == ChainMode::Cw);
    CHECK(s->tilesInMode().size() == 9);
    CHECK(s->tile(QStringLiteral("apf"))->isVisibleTo(w));
    CHECK(!s->tile(QStringLiteral("contour"))->isVisibleTo(w));
    auto* aside = button(w, QStringLiteral("gateChainNotForModeToggle"));
    CHECK(aside != nullptr);
    if (aside) {
        CHECK(aside->text().contains(QStringLiteral("(4)")));
        CHECK(aside->text().startsWith(QStringLiteral("STAGES THIS MODE DOES NOT USE")));
        // Collapsed by default; expanding it brings the tiles back into view
        // without turning anything on or off.
        aside->click();
        settle();
        CHECK(s->tile(QStringLiteral("contour"))->isVisibleTo(w));
    }

    // DATA/OTHER offers no set, and says so rather than offering a dead button
    // that looks live.
    button(w, QStringLiteral("gateChainMode_data"))->click();
    settle();
    CHECK(w->mode() == ChainMode::Data);
    auto* dataSet = button(w, QStringLiteral("gateChainSetButton_data"));
    CHECK(dataSet != nullptr);
    if (dataSet) {
        CHECK(dataSet->isVisibleTo(w));
        CHECK(!dataSet->isEnabled());
        CHECK(dataSet->toolTip() == QStringLiteral("No set for data yet."));
    }
    CHECK(chainPreset(ChainMode::Data).isEmpty());
}

void testTheModeSetsSendTheirTableInOrderOneWriteAtATime()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainlessFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    const QList<AetherSDR::ChainPresetWrite> voice = chainPreset(ChainMode::Phone);
    CHECK(voice.size() == 12);
    const int before = net.log.size();
    button(w, QStringLiteral("gateChainSetButton_phone"))->click();

    // The button says what it is doing while it does it.
    CHECK(button(w, QStringLiteral("gateChainSetButton_phone"))->text()
          == QStringLiteral("SETTING UP..."));
    // One write leaves at a time: before any reply there is exactly one.
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/filter/set?auto=off"));

    for (int i = 0; i < 40 && net.count(QStringLiteral("/filter/set")) < voice.size(); ++i)
        settle();

    QStringList sent;
    for (const QString& entry : net.log) {
        if (entry.startsWith(QStringLiteral("/filter/set?")))
            sent << entry.mid(QStringLiteral("/filter/set?").size());
    }
    CHECK(sent.size() == voice.size());
    for (int i = 0; i < voice.size() && i < sent.size(); ++i)
        CHECK(sent.at(i) == voice.at(i).query);
    CHECK(labelText(w, "gateChainSetProgressLabel") == QStringLiteral("done"));

    // The CW table is its own list and reaches the gate the same way.
    const QList<AetherSDR::ChainPresetWrite> cw = chainPreset(ChainMode::Cw);
    CHECK(cw.size() == 14);
    CHECK(cw.first().query == QStringLiteral("auto=off"));
    CHECK(cw.at(2).query == QStringLiteral("high=850"));
    CHECK(cw.at(4).query == QStringLiteral("apf=on"));
}

void testASetStopsWhenTheGateRefusesALine()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError, QByteArray(R"({"error": "bad value: 'auto'"})")};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;

    button(w, QStringLiteral("gateChainMode_cw"))->click();
    settle();
    button(w, QStringLiteral("gateChainSetButton_cw"))->click();
    for (int i = 0; i < 10; ++i)
        settle();

    // The first line was refused, so the rest never went out: a set that
    // ploughed on through a receiver that had already said no would be
    // applying half a preset.
    CHECK(net.count(QStringLiteral("/filter/set")) == 1);
    CHECK(labelText(w, "gateChainSetProgressLabel") == QStringLiteral("stopped"));
    CHECK(labelText(w, "gateChainDetailNote").contains(QStringLiteral("bad value")));
}

// --------------------------------------------------------------------------
// §0.3 item 8 -- the keyboard
// --------------------------------------------------------------------------

void testArrowKeysMoveTheSelectionAndSpaceSwitchesTheStage()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainlessFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    AetherGateChainStrip* s = strip(w);
    s->selectStage(QStringLiteral("roof_rf"));
    s->setFocus();
    settle();

    // The diagram is columns now, so DOWN and RIGHT are the same move: the
    // next stage the signal reaches. The walk stops at both ends rather than
    // wrapping -- the chain is not a loop.
    QTest::keyClick(s, Qt::Key_Right);
    CHECK(s->selectedId() == QStringLiteral("roof_digital"));
    QTest::keyClick(s, Qt::Key_Down);
    CHECK(s->selectedId() == QStringLiteral("nb"));
    QTest::keyClick(s, Qt::Key_Left);
    CHECK(s->selectedId() == QStringLiteral("roof_digital"));
    QTest::keyClick(s, Qt::Key_Up);
    CHECK(s->selectedId() == QStringLiteral("roof_rf"));
    QTest::keyClick(s, Qt::Key_Up);
    CHECK(s->selectedId() == QStringLiteral("roof_rf"));

    // Space presses the selected stage's switch -- one write, and nothing on
    // a row that has no switch.
    const int before = net.count(QStringLiteral("/filter/set"));
    QTest::keyClick(s, Qt::Key_Space);           // roof_rf is fixed here
    settle();
    CHECK(net.count(QStringLiteral("/filter/set")) == before);

    s->selectStage(QStringLiteral("nb"));
    QTest::keyClick(s, Qt::Key_Space);
    settle();
    CHECK(net.count(QStringLiteral("/filter/set")) == before + 1);
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?nb=on"));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_ux_test"));
    QApplication app(argc, argv);

    testNoBlueDotAnywhereOnTheStrip();
    testTheDiagramIsFourGroupsLeftToRight();
    testNoCardEverElidesItsMeasuredLine();
    testNothingScrollsAtTheInitialSize();
    testRenderTheWindowWhenAsked();
    testOneClickSwitchesAStageEvenWhenAStalePollLandsAfterTheWriteReply();
    testAStalePollBeforeTheWriteReplyStillEndsInTheNewState();
    testTheRfRoofSaysOnItsOwnFaceThatTheGateHasNotBuiltItYet();
    testAGateThatListsItsAnalogueOptionsGetsALiveRfRoof();
    testAWidthTheGateDidNotListStaysOnTheMenuAndCannotBePicked();
    testARefusedWriteLandsOnTheTileAndTheRowDoesNotMove();
    testTheModeChoosesWhatIsOnTheStrip();
    testTheModeSetsSendTheirTableInOrderOneWriteAtATime();
    testASetStopsWhenTheGateRefusesALine();
    testArrowKeysMoveTheSelectionAndSpaceSwitchesTheStage();

    std::printf("\n%d aether gate chain UX test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
