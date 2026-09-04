// B21 -- what the operator asked for after the CHAIN window's second build:
//
//   "we still need the visualizations on a separate tab from the chain, so
//    you can see the filter change and interact with some of them that way;
//    flip over to the visual filter screen where you can see presets, and the
//    presets are collections of settings on all the filters and their
//    activations; the visualizer needs to be more reactive and performant, it
//    is a little laggy; and the interface a little better."
//
// Four stories, each with the mutation that would fail it beside it:
//
//   * TWO TABS. CHAIN in front means the picture is not fed at all; VISUAL in
//     front means it catches up at once, from the last body, without a poll.
//   * PRESETS. SAVE AS... writes one JSON file per preset under
//     AppDataLocation/chain-presets; LOAD sends the stages back in signal
//     order through the same one-write-at-a-time sequencer the mode sets use;
//     "edited" is a COMPARISON against the preset in force, not a flag; and a
//     stage this receiver does not have is skipped and named.
//   * THE PAINT BUDGET. A 4 096-point shape paints in under 4 ms offscreen
//     (the median of twenty), a poll that says the same thing costs no paint,
//     a spectrum-only body paints without rebuilding the cached layer, and
//     only a change to the FILTER rebuilds it.
//   * DIRECT MANIPULATION. A drag of one edge writes that edge alone; a drag
//     of a notch mark is a clear then an add, in that order, each waited for;
//     and a click on any mark turns the window to that stage's card.
//
// The array contract is tests/aether_gate_chain_test.cpp and the second
// build's eight items are tests/aether_gate_chain_ux_test.cpp; both were at
// the 800-line budget, so this is the third file on the same fixture.
#include "AetherGateChainFixture.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace AetherGateChainFixture;
using AetherSDR::chainPresetDir;

namespace {

constexpr int kTabChain = 0;
constexpr int kTabVisual = 1;

// A window at its initial size, on the tab asked for, with a layout pass done
// so the panel has the geometry a pointer needs.
void bringUp(AetherGateChainWindow* w, int tab)
{
    w->resize(1120, 820);
    w->setCurrentTab(tab);
    settle();
    w->grab();
    settle();
}

// One preset file written by hand, the way an operator drops one into the
// folder: a name, a mode, and the stages in the order they should be set.
QString writePresetFile(const QString& name, const QList<QPair<QString, QString>>& stages)
{
    QJsonObject stageObj;
    QJsonArray order;
    for (const auto& entry : stages) {
        stageObj.insert(entry.first, entry.second);
        order.append(entry.first);
    }
    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("saved"), QStringLiteral("2026-09-03T04:12:07Z"));
    root.insert(QStringLiteral("mode"), QStringLiteral("phone"));
    root.insert(QStringLiteral("stages"), stageObj);
    root.insert(QStringLiteral("order"), order);
    const QString path = chainPresetDir() + QStringLiteral("/") + name.toLower()
                         + QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(QJsonDocument(root).toJson());
    return path;
}

void wipePresets()
{
    QDir dir(chainPresetDir());
    for (const QString& file : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        dir.remove(file);
}

// --------------------------------------------------------------------------
// Two tabs
// --------------------------------------------------------------------------

void testThePictureIsFedOnlyWhileShownAndInFront()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;

    // CHAIN is the tab in front when the window opens, and a body that arrived
    // while it was has not reached the picture.
    CHECK(w->currentTab() == kTabChain);
    CHECK(w->findChild<QWidget*>(QStringLiteral("gateChainTabChain")) != nullptr);
    CHECK(w->findChild<QWidget*>(QStringLiteral("gateChainTabVisual")) != nullptr);
    CHECK(p->lowHz() == 0);

    // Turning to VISUAL catches the picture up from the LAST body: no poll has
    // happened between these two lines.
    const int polls = net.count(QStringLiteral("/filter"));
    w->setCurrentTab(kTabVisual);
    CHECK(net.count(QStringLiteral("/filter")) == polls);
    CHECK(p->lowHz() == 350);
    CHECK(p->highHz() == 2400);
    CHECK(p->notchCount() == 2);

    // A hidden window is not fed either...
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             visualFilter(128, 500, 2400)};
    w->hide();
    filterTick(applet);
    CHECK(p->lowHz() == 350);
    // ...and catches up the moment it is shown again.
    w->show();
    settle();
    CHECK(p->lowHz() == 500);

    // MUTATION: with CHAIN in front a NEW body must not move the picture; feed
    // it without the gate and the first assertion here would fail.
    w->setCurrentTab(kTabChain);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             visualFilter(128, 700, 2400)};
    filterTick(applet);
    CHECK(p->lowHz() == 500);
    w->setCurrentTab(kTabVisual);
    CHECK(p->lowHz() == 700);
}

// --------------------------------------------------------------------------
// Presets
// --------------------------------------------------------------------------

// W5 (CHAIN redesign §2.6): SETUP -- what this window still calls
// "gateChainPresetRow" -- moved off the VISUAL tab and onto the MODE row, a
// child of "gateChainModeRow", so it reads beside SET UP FOR <mode> and stays
// on screen on both tabs. MUTATION: parenting it back under either tab's own
// box would fail one of the isAncestorOf() checks, or make it disappear on
// CHAIN, this window's own default tab, failing the first isVisibleTo().
void testSetupSitsOnTheModeRowAndIsVisibleOnBothTabs()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabChain);

    auto* row = w->findChild<QWidget*>(QStringLiteral("gateChainPresetRow"));
    auto* modeRow = w->findChild<QWidget*>(QStringLiteral("gateChainModeRow"));
    auto* visualTab = w->findChild<QWidget*>(QStringLiteral("gateChainTabVisual"));
    auto* chainTab = w->findChild<QWidget*>(QStringLiteral("gateChainTabChain"));
    CHECK(row != nullptr && modeRow != nullptr && visualTab != nullptr && chainTab != nullptr);
    if (!row || !modeRow || !visualTab || !chainTab)
        return;
    CHECK(modeRow->isAncestorOf(row));
    CHECK(!visualTab->isAncestorOf(row));
    CHECK(!chainTab->isAncestorOf(row));

    // On CHAIN -- the tab the window opens on -- SETUP is already on screen,
    // beside SET UP FOR <mode>...
    CHECK(row->isVisibleTo(w));
    CHECK(button(w, QStringLiteral("gateChainSetButton_phone"))->isVisibleTo(w));
    // ...and stays visible after flipping to VISUAL and back. MUTATION: a
    // regression that widens SETUP back out to its old VISUAL-tab size would
    // push the window past the no-scroll budget checked at 1120 px elsewhere;
    // pin the window's own floor here so that regression fails close to its
    // cause instead of only in the unrelated visibility test.
    CHECK(w->minimumSizeHint().width() <= 1120);
    bringUp(w, kTabVisual);
    CHECK(row->isVisibleTo(w));
    bringUp(w, kTabChain);
    CHECK(row->isVisibleTo(w));
}

void testSaveAsWritesOneJsonFilePerPresetUnderAppData()
{
    wipePresets();
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);

    // The row is on the MODE row above the tabs now (W5), with nothing in
    // force yet -- see testSetupSitsOnTheModeRowAndIsVisibleOnBothTabs() for
    // the placement itself.
    auto* row = w->findChild<QWidget*>(QStringLiteral("gateChainPresetRow"));
    CHECK(row != nullptr);
    CHECK(labelText(w, "gateChainPresetState") == QStringLiteral("no preset in force"));

    // SAVE AS... opens an inline field where the menu was; Enter commits.
    button(w, QStringLiteral("gateChainPresetSave"))->click();
    auto* name = w->findChild<QLineEdit*>(QStringLiteral("gateChainPresetName"));
    CHECK(name != nullptr);
    if (!name)
        return;
    CHECK(name->isVisibleTo(w));
    name->setText(QStringLiteral("Net night / 80m"));
    QTest::keyClick(name, Qt::Key_Return);
    settle();

    // The field has gone and the menu is back. MUTATION: a QLineEdit ignores
    // Return after emitting returnPressed so a dialog can press its default
    // button with it; this row is in a QDialog, and that button was SAVE AS...
    // -- which reopened the field with the name it had just saved.
    CHECK(!name->isVisibleTo(w));
    auto* pick = w->findChild<QComboBox*>(QStringLiteral("gateChainPresetPick"));
    CHECK(pick != nullptr && pick->isVisibleTo(w));

    // One file, slugged, under AppDataLocation/chain-presets -- with the
    // macOS <org>/<app> double segment collapsed, as the app's other stores
    // do, so the folder is beside them and not one level deeper.
    const QString expected = chainPresetDir() + QStringLiteral("/net-night-80m.json");
    CHECK(QFile::exists(expected));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString parent = QFileInfo(chainPresetDir()).absolutePath();
    CHECK(parent == appData || parent == QFileInfo(appData).absolutePath());
    CHECK(chainPresetDir().startsWith(QDir::homePath()));
    QFile file(expected);
    CHECK(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    CHECK(root.value(QStringLiteral("name")).toString() == QStringLiteral("Net night / 80m"));
    CHECK(root.value(QStringLiteral("mode")).toString() == QStringLiteral("phone"));
    const QJsonObject stages = root.value(QStringLiteral("stages")).toObject();
    // Every writable toggle and select, as the VALUE IN FORCE...
    CHECK(stages.value(QStringLiteral("nb")).toString() == QStringLiteral("on"));
    CHECK(stages.value(QStringLiteral("apf")).toString() == QStringLiteral("off"));
    CHECK(stages.value(QStringLiteral("shape")).toString() == QStringLiteral("soft"));
    CHECK(stages.value(QStringLiteral("roof_digital")).toString() == QStringLiteral("12000"));
    CHECK(stages.value(QStringLiteral("combiner")).toString() == QStringLiteral("track"));
    // ...which for a toggle is the OPPOSITE of its action. SLICE FILTER is
    // enabled and its action is bypass=on: the value to keep is bypass=OFF.
    // MUTATION: reading enabled as "on" writes bypass=on on load, which
    // switches the filter off to restore a preset that had it on.
    CHECK(stages.value(QStringLiteral("slice")).toString() == QStringLiteral("off"));
    // ...and nothing that is a measurement or is set elsewhere.
    CHECK(!stages.contains(QStringLiteral("passband")));
    CHECK(!stages.contains(QStringLiteral("align")));
    CHECK(!stages.contains(QStringLiteral("lna")));
    CHECK(!stages.contains(QStringLiteral("antenna")));
    // Signal order is kept in its own array.
    const QJsonArray order = root.value(QStringLiteral("order")).toArray();
    CHECK(order.size() == stages.size());
    CHECK(order.first().toString() == QStringLiteral("roof_rf"));
    CHECK(order.last().toString() == QStringLiteral("agc"));

    // What was just saved is what is in force.
    CHECK(presets(w)->currentName() == QStringLiteral("Net night / 80m"));
    CHECK(labelText(w, "gateChainPresetState")
          == QStringLiteral("in force: Net night / 80m"));
    if (pick)
        CHECK(pick->currentText() == QStringLiteral("Net night / 80m · phone"));
    wipePresets();
}

void testLoadSendsTheStagesInOrderThroughTheSequencerAndEditedIsAComparison()
{
    wipePresets();
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, visualFilter()};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, visualFilter()};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);

    button(w, QStringLiteral("gateChainPresetSave"))->click();
    auto* name = w->findChild<QLineEdit*>(QStringLiteral("gateChainPresetName"));
    name->setText(QStringLiteral("Net night"));
    QTest::keyClick(name, Qt::Key_Return);
    settle();
    CHECK(!presets(w)->edited());

    // The receiver drifts: the blanker goes off and the shape goes sharp,
    // neither of them by this window's hand. The next body says so.
    QByteArray drifted = withStage(visualFilter(), QStringLiteral("nb"), false,
                                   QStringLiteral("nb=on"));
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, drifted};
    filterTick(applet);
    CHECK(presets(w)->edited());
    CHECK(labelText(w, "gateChainPresetState")
          == QStringLiteral("in force: Net night (edited)"));
    auto* pick = w->findChild<QComboBox*>(QStringLiteral("gateChainPresetPick"));
    CHECK(pick != nullptr);
    if (pick)
        CHECK(pick->currentText() == QStringLiteral("Net night · phone (edited)"));

    // Put it back by hand (the gate reports the preset's values again) and
    // the word goes away: "edited" is what the receiver reads, not a memory.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, visualFilter()};
    filterTick(applet);
    CHECK(!presets(w)->edited());
    CHECK(labelText(w, "gateChainPresetState") == QStringLiteral("in force: Net night"));

    // LOAD: one write at a time, in signal order, each waited for.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, drifted};
    filterTick(applet);
    CHECK(presets(w)->edited());
    const int before = net.log.size();
    button(w, QStringLiteral("gateChainPresetLoad"))->click();
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/filter/set?roof_hz=200000"));
    CHECK(labelText(w, "gateChainStatusLabel") == QStringLiteral("applying..."));
    for (int i = 0; i < 60 && labelText(w, "gateChainSetProgressLabel") != QStringLiteral("done");
         ++i) {
        settle();
    }
    CHECK(labelText(w, "gateChainSetProgressLabel") == QStringLiteral("done"));

    QStringList sent;
    for (int i = before; i < net.log.size(); ++i) {
        if (net.log.at(i).contains(QStringLiteral("/set?")))
            sent << net.log.at(i);
    }
    const QStringList expected = {
        QStringLiteral("/filter/set?roof_hz=200000"),
        QStringLiteral("/filter/set?nb=on"),
        QStringLiteral("/filter/set?digital_roof_hz=12000"),
        QStringLiteral("/diversity/set?mode=track"),
        QStringLiteral("/diversity/set?subband=on"),
        QStringLiteral("/diversity/set?post=on"),
        QStringLiteral("/filter/set?bypass=off"),
        QStringLiteral("/filter/set?auto=on"),
        QStringLiteral("/filter/set?shape=soft"),
        QStringLiteral("/filter/set?notches=on"),
        QStringLiteral("/filter/set?anf=on"),
        QStringLiteral("/filter/set?auto_contour=on"),
        QStringLiteral("/filter/set?apf=off"),
        QStringLiteral("/filter/set?auto_eq=on"),
        QStringLiteral("/filter/set?agc=med"),
    };
    // MUTATION: the exact list, in order. A load that sent the same fifteen
    // in key order, or skipped the pair rows, fails here.
    CHECK(sent == expected);
    if (sent != expected)
        std::printf("  sent: %s\n", sent.join(QStringLiteral(" ")).toUtf8().constData());

    // The load's own writes did not mark it edited, and the state line says
    // the preset is back in force.
    CHECK(!presets(w)->edited());
    CHECK(labelText(w, "gateChainPresetState") == QStringLiteral("in force: Net night"));
    CHECK(labelText(w, "gateChainStatusLabel") == QStringLiteral("live"));
    wipePresets();
}

void testLoadSkipsAStageThisReceiverDoesNotHaveAndNamesIt()
{
    wipePresets();
    // Saved on the dual-tuner pair, loaded on a gate with no chain[] at all:
    // the thirteen fallback rows have nb and no subband or post.
    const QString path = writePresetFile(
        QStringLiteral("Pair"), {{QStringLiteral("subband"), QStringLiteral("on")},
                                 {QStringLiteral("nb"), QStringLiteral("on")},
                                 {QStringLiteral("post"), QStringLiteral("on")}});
    CHECK(!path.isEmpty());
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, kChainlessFilter);
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainlessFilter};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    CHECK(presets(w)->currentName() == QStringLiteral("Pair"));

    button(w, QStringLiteral("gateChainPresetLoad"))->click();
    for (int i = 0; i < 20; ++i)
        settle();
    // The one stage this receiver has was set; the two it lacks were named,
    // by id, because the app has never heard of them either.
    CHECK(sentQueries(net, QStringLiteral("/filter/set")) == QStringList{QStringLiteral("nb=on")});
    const QString note = label(w, QStringLiteral("gateChainDetailNote"))->toolTip();
    CHECK(note.contains(QStringLiteral("subband")));
    CHECK(note.contains(QStringLiteral("post")));
    CHECK(note.contains(QStringLiteral("the rest was set")));
    CHECK(labelText(w, "gateChainSetProgressLabel") == QStringLiteral("done"));

    // MUTATION: a preset naming only stages this receiver has leaves the note
    // empty. Without this the case would pass on a window that printed the
    // sentence for every load.
    wipePresets();
    writePresetFile(QStringLiteral("Here"), {{QStringLiteral("nb"), QStringLiteral("off")}});
    // The menu reads the folder when it is built; a file dropped in later is
    // picked up on the next save or delete, so build a fresh window.
    FakeGate net2;
    AetherGateApplet applet2(nullptr, &net2);
    connectGate(applet2, net2, kChainlessFilter);
    net2.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, kChainlessFilter};
    AetherGateChainWindow* w2 = openChain(applet2);
    CHECK(w2 != nullptr);
    if (!w2)
        return;
    bringUp(w2, kTabVisual);
    button(w2, QStringLiteral("gateChainPresetLoad"))->click();
    for (int i = 0; i < 10; ++i)
        settle();
    CHECK(sentQueries(net2, QStringLiteral("/filter/set")) == QStringList{QStringLiteral("nb=off")});
    CHECK(!label(w2, QStringLiteral("gateChainDetailNote"))->isVisibleTo(w2));
    wipePresets();
}

// --------------------------------------------------------------------------
// The paint budget
// --------------------------------------------------------------------------

void testAFourThousandPointShapePaintsInUnderFourMillisecondsAndRebuildsNothing()
{
    DiversityFilterPanel p;
    p.resize(1060, 320);
    p.show();
    settle();
    const QJsonObject body = QJsonDocument::fromJson(visualFilter(4096)).object();
    p.applyStatus(body);
    settle();

    QPixmap target(p.size());
    p.render(&target);
    const int rebuiltOnce = p.staticRebuildCount();
    CHECK(rebuiltOnce >= 1);

    std::vector<qint64> nanos;
    for (int i = 0; i < 20; ++i) {
        QElapsedTimer timer;
        timer.start();
        p.render(&target);
        nanos.push_back(timer.nsecsElapsed());
    }
    std::sort(nanos.begin(), nanos.end());
    const double medianMs = nanos[nanos.size() / 2] / 1.0e6;
    std::printf("  paint of a 4096-point shape: median %.2f ms over 20 (rebuilds %d)\n",
                medianMs, p.staticRebuildCount());
    CHECK(medianMs < 4.0);
    // Twenty paints of the same shape rebuilt the cached layer zero times.
    CHECK(p.staticRebuildCount() == rebuiltOnce);

    // Rule 1: a poll that says the same thing costs no paint at all.
    const int painted = p.paintCount();
    p.applyStatus(body);
    QTest::qWait(60);
    CHECK(p.paintCount() == painted);

    // MUTATION, rule 2: a body whose SPECTRUM moved paints without a rebuild;
    // a body whose FILTER moved rebuilds exactly once.
    p.applyStatus(QJsonDocument::fromJson(visualFilter(4096, 350, 2400, {1200})).object());
    QTest::qWait(60);
    CHECK(p.paintCount() > painted);
    CHECK(p.staticRebuildCount() == rebuiltOnce + 1);
    const int rebuilt = p.staticRebuildCount();
    const int paintedAgain = p.paintCount();
    QJsonObject spectrumOnly = QJsonDocument::fromJson(visualFilter(4096, 350, 2400, {1200})).object();
    QJsonObject spectrum = spectrumOnly.value(QStringLiteral("spectrum")).toObject();
    spectrum.insert(QStringLiteral("floor_db"), -66.0);
    spectrumOnly.insert(QStringLiteral("spectrum"), spectrum);
    p.applyStatus(spectrumOnly);
    QTest::qWait(60);
    CHECK(p.paintCount() > paintedAgain);
    CHECK(p.staticRebuildCount() == rebuilt);

    // Rule 3: a drag repaints the handle's column and rebuilds nothing until
    // the button comes up.
    const int y = p.height() / 2;
    QTest::mousePress(&p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p.xForHz(350)), y));
    for (int hz = 360; hz <= 500; hz += 10)
        QTest::mouseMove(&p, QPoint(int(p.xForHz(hz)), y));
    QTest::qWait(30);
    CHECK(p.staticRebuildCount() == rebuilt);
    CHECK(p.dragging());
    QTest::mouseRelease(&p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p.xForHz(500)), y));
    QTest::qWait(30);
    CHECK(!p.dragging());
    CHECK(p.staticRebuildCount() == rebuilt + 1);
}

// --------------------------------------------------------------------------
// Direct manipulation
// --------------------------------------------------------------------------

void testDraggingAnEdgeWritesThatEdgeAlone()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                 visualFilter(128, 500, 2400)};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr && p->width() > 600);
    if (!p)
        return;

    const int y = p->height() / 2;
    const int before = countWrites(net);
    QTest::mousePress(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(350)), y));
    CHECK(p->dragging());
    // A poll landing mid-drag must not snatch the handle back.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, visualFilter()};
    filterTick(applet);
    QTest::mouseMove(p, QPoint(int(p->xForHz(420)), y));
    QTest::mouseMove(p, QPoint(int(p->xForHz(500)), y));
    CHECK(p->lowHz() == 500);
    QTest::mouseRelease(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(500)), y));
    settle();

    // One write, the low edge only. MUTATION: re-asserting high= as well
    // would put two on the wire and fight the auto-width tracker.
    CHECK(countWrites(net) == before + 1);
    CHECK(lastRequest(net) == QStringLiteral("/filter/set?low=500"));
    CHECK(!net.log.contains(QStringLiteral("/filter/set?high=2400")));
    CHECK(p->lowHz() == 500);       // the gate's answer, not the drag
}

void testDraggingANotchMarkIsAClearThenAnAddInThatOrder()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    net.routes[QStringLiteral("/filter/notch")] = {QNetworkReply::NoError,
                                                   visualFilter(128, 350, 2400, {1300, 1850})};
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;

    const int y = p->height() / 2;
    const int before = net.log.size();
    QTest::mousePress(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(1200)), y));
    CHECK(p->dragging());
    QTest::mouseMove(p, QPoint(int(p->xForHz(1250)), y));
    QTest::mouseMove(p, QPoint(int(p->xForHz(1300)), y));
    QTest::mouseRelease(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(1300)), y));
    // The clear is on the wire alone; the add waits for its answer.
    CHECK(net.log.size() == before + 1);
    CHECK(net.log.last() == QStringLiteral("/filter/notch?clear=1200"));
    for (int i = 0; i < 20 && net.count(QStringLiteral("/filter/notch")) < 2; ++i)
        settle();
    CHECK(sentQueries(net, QStringLiteral("/filter/notch"))
          == (QStringList{QStringLiteral("clear=1200"), QStringLiteral("add=1300")}));
    CHECK(p->notchHzAt(0) == 1300.0);

    // MUTATION: a press on the mark that never moves is not a move. It is a
    // click, and a click is the door to the IF NOTCH card.
    const int writes = net.count(QStringLiteral("/filter/notch"));
    QTest::mouseClick(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(1850)), y));
    settle();
    CHECK(net.count(QStringLiteral("/filter/notch")) == writes);
    CHECK(w->currentTab() == kTabChain);
    CHECK(strip(w)->selectedId() == QStringLiteral("notch"));
}

void testClickingAMarkTurnsToThatStageOnTheChainTab()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    DiversityFilterPanel* p = panel(w);
    CHECK(p != nullptr);
    if (!p)
        return;
    const int y = p->height() / 2;

    // An ANF tone, mid-height.
    QTest::mouseClick(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(980)), y));
    settle();
    CHECK(w->currentTab() == kTabChain);
    CHECK(strip(w)->selectedId() == QStringLiteral("anf"));
    CHECK(labelText(w, "gateChainDetailName") == QStringLiteral("ANF · DNF"));

    // The contour tick, along the bottom of the plot.
    bringUp(w, kTabVisual);
    QTest::mouseClick(p, Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(p->xForHz(1450)), p->height() - 20));
    settle();
    CHECK(w->currentTab() == kTabChain);
    CHECK(strip(w)->selectedId() == QStringLiteral("contour"));

    // A handle that is pressed and released where it was.
    bringUp(w, kTabVisual);
    const int writes = net.log.size();
    QTest::mouseClick(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(2400)), y));
    settle();
    CHECK(w->currentTab() == kTabChain);
    CHECK(strip(w)->selectedId() == QStringLiteral("passband"));
    CHECK(net.log.size() == writes);          // a click is not a write

    // MUTATION: a click on nothing -- open curve, no mark within reach --
    // stays on VISUAL and selects nothing new.
    bringUp(w, kTabVisual);
    strip(w)->selectStage(QString());
    QTest::mouseClick(p, Qt::LeftButton, Qt::NoModifier, QPoint(int(p->xForHz(2000)), y));
    settle();
    CHECK(w->currentTab() == kTabVisual);
    CHECK(strip(w)->selectedId().isEmpty());
}

// --------------------------------------------------------------------------
// Names, and the evidence
// --------------------------------------------------------------------------

void testEveryB21WidgetHasANameAndNoLabelWraps()
{
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    for (const char* name : {"gateChainTabs", "gateChainTabChain", "gateChainTabVisual",
                             "gateChainVisual", "gateChainVisualCaption",
                             "gateChainVisualCursor", "gateChainVisualReadout",
                             "gateChainPresetRow", "gateChainPresetCaption",
                             "gateChainPresetPick", "gateChainPresetName",
                             "gateChainPresetLoad", "gateChainPresetSave",
                             "gateChainPresetDelete", "gateChainPresetState",
                             "gateChainPresetNotice", "diversityWindowFilterPanel"}) {
        CHECK(w->findChild<QWidget*>(QString::fromLatin1(name)) != nullptr);
        if (!w->findChild<QWidget*>(QString::fromLatin1(name)))
            std::printf("  missing: %s\n", name);
    }
    auto* row = w->findChild<QWidget*>(QStringLiteral("gateChainPresetRow"));
    auto* visual = w->findChild<QWidget*>(QStringLiteral("gateChainVisual"));
    for (QWidget* host : {row, visual}) {
        if (!host)
            continue;
        for (QWidget* kid : host->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            CHECK(!kid->objectName().isEmpty());
        for (QLabel* label : host->findChildren<QLabel*>())
            CHECK(!label->wordWrap());
    }
}

// With CHAIN_B21_RENDER_PREFIX=/tmp/chain-b21 set, both tabs are rendered
// against the picture payload with a preset in force and written out as
// <prefix>-chain.png and <prefix>-visual.png, so B21 can be looked at.
void testRenderBothTabsWhenAsked()
{
    const QByteArray prefix = qgetenv("CHAIN_B21_RENDER_PREFIX");
    if (prefix.isEmpty())
        return;
    wipePresets();
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    connectGate(applet, net, visualFilter());
    AetherGateChainWindow* w = openChain(applet);
    CHECK(w != nullptr);
    if (!w)
        return;
    bringUp(w, kTabVisual);
    button(w, QStringLiteral("gateChainPresetSave"))->click();
    auto* name = w->findChild<QLineEdit*>(QStringLiteral("gateChainPresetName"));
    name->setText(QStringLiteral("Net night"));
    QTest::keyClick(name, Qt::Key_Return);
    settle();
    DiversityFilterPanel* p = panel(w);
    if (p)
        QTest::mouseMove(p, QPoint(int(p->xForHz(1200)), p->height() / 2));
    settle();
    const QString base = QString::fromLocal8Bit(prefix);
    CHECK(w->grab().save(base + QStringLiteral("-visual.png")));

    bringUp(w, kTabChain);
    strip(w)->selectStage(QStringLiteral("notch"));
    settle();
    CHECK(w->grab().save(base + QStringLiteral("-chain.png")));
    wipePresets();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_chain_b21_test"));
    QApplication app(argc, argv);

    testThePictureIsFedOnlyWhileShownAndInFront();
    testSetupSitsOnTheModeRowAndIsVisibleOnBothTabs();
    testSaveAsWritesOneJsonFilePerPresetUnderAppData();
    testLoadSendsTheStagesInOrderThroughTheSequencerAndEditedIsAComparison();
    testLoadSkipsAStageThisReceiverDoesNotHaveAndNamesIt();
    testAFourThousandPointShapePaintsInUnderFourMillisecondsAndRebuildsNothing();
    testDraggingAnEdgeWritesThatEdgeAlone();
    testDraggingANotchMarkIsAClearThenAnAddInThatOrder();
    testClickingAMarkTurnsToThatStageOnTheChainTab();
    testEveryB21WidgetHasANameAndNoLabelWraps();
    testRenderBothTabsWhenAsked();

    std::printf("\n%d aether gate chain B21 test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
