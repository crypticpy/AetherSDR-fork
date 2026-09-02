// DiversityWindow — the pop-out Diversity window, driven through a real
// AetherGateApplet and socket-free.
//
// The window owns no transport: it is opened from the sidebar panel's button,
// fed by the applet's own /diversity and /diversity/map polls, and every
// control it has emits a request signal the panel forwards to the applet.
// So the only honest way to test it is through the applet, with the same
// injected QNetworkAccessManager the applet test uses — no port is opened,
// nothing is listened on, and a wrong answer fails an assertion instead of
// hanging on a socket.
//
// Separate binary from aether_gate_applet_test on purpose: opening the window
// writes DiversityWindowVisible into the process-wide AppSettings cache, and
// an applet built afterwards would restore it — which would silently change
// what every later case in that binary is testing.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/ClientCompKnob.h"
#include "gui/DiversityMapStrip.h"
#include "gui/DiversityScope.h"
#include "gui/DiversityTimeline.h"
#include "gui/DiversityWindow.h"
#include "gui/DiversityWindowEvents.h"
#include "gui/DiversityWindowPanels.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>

#include <cmath>
#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::ClientCompKnob;
using AetherSDR::DiversityEventLog;
using AetherSDR::DiversityMapStrip;
using AetherSDR::DiversityScope;
using AetherSDR::DiversitySnapshot;
using AetherSDR::DiversitySnrMeter;
using AetherSDR::DiversityTimeline;
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

// AppSettings is one process-wide cache, and the window's own visibility is
// persisted in it -- so every case starts from a known closed state rather
// than from whatever the previous case left behind.
void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Brings an applet up to "gate present, diversity live" with `diversity` as
// the /diversity body.
void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

// (a) The window is built lazily from the sidebar button, needs no transport
// of its own, and its open/closed state is what DiversityWindowVisible says.
void testOpenButtonBuildsTheWindowAndPersistsItsVisibility()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    CHECK(a.gatePresent());

    // Nothing has asked for it yet, so nothing has been built.
    CHECK(a.diversityPanel()->window() == nullptr);

    auto* button = openButton(a);
    CHECK(button != nullptr);
    CHECK(button->accessibleName() == QStringLiteral("Open the diversity window"));

    const int requestsBefore = net.log.size();
    button->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    CHECK(w && w->isVisible());
    CHECK(AppSettings::instance()
              .value(QStringLiteral("DiversityWindowVisible"))
              .toString() == QStringLiteral("True"));
    // Building and showing it must not have talked to the gate: the window
    // owns no transport, only the applet does.
    CHECK(net.log.size() == requestsBefore);

    button->click();
    settle();
    CHECK(w && !w->isVisible());
    CHECK(AppSettings::instance()
              .value(QStringLiteral("DiversityWindowVisible"))
              .toString() == QStringLiteral("False"));

    // Re-opening reuses the SAME window rather than leaking a second one.
    button->click();
    settle();
    CHECK(a.diversityPanel()->window() == w);
    CHECK(w && w->isVisible());
    closedToStart();
}

// (b) A full payload and an all-nulls one both apply without crashing, and the
// window's scope agrees with the payload's own numbers.
void testFullAndNullPayloadsApplyAndTheScopeAgrees()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* scope = w->findChild<DiversityScope*>(QStringLiteral("diversityWindowScope"));
    CHECK(scope && scope->isLarge());
    // out - max(a, b) = 15.1 - 12.3.
    CHECK(scope && std::abs(scope->lastGainDb() - 2.8) < 1e-9);

    auto* meterA = w->findChild<DiversitySnrMeter*>(QStringLiteral("diversityWindowMeterA"));
    auto* meterOut = w->findChild<DiversitySnrMeter*>(QStringLiteral("diversityWindowMeterOut"));
    CHECK(meterA && std::abs(meterA->shownDb() - 12.3) < 1e-9);
    CHECK(meterOut && std::abs(meterOut->shownDb() - 15.1) < 1e-9);

    auto* talkers = w->findChild<QLabel*>(
        QStringLiteral("diversityWindowTalkersCountLabel"));
    CHECK(talkers
          && talkers->text() == QStringLiteral("2 talkers remembered · nobody talking"));
    // Alignment is one fixed-width line now, not four labelled fields: the
    // panel had exactly one question in it and four boxes made it look like
    // four.
    auto* align = w->findChild<QLabel*>(QStringLiteral("diversityWindowAlignLabel"));
    CHECK(align
          && align->text() == QStringLiteral("aligned · lag 3 · peak 0.910 · steady"));

    // Now the same window fed a payload whose every optional leg is null.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityNulls};
    tick(a);
    CHECK(scope && std::isnan(scope->lastGainDb()));
    CHECK(meterA && std::isnan(meterA->shownDb()));
    // A null lag is "unknown", not zero.
    CHECK(align
          && align->text() == QStringLiteral("not aligned · lag — · peak — · steady"));
    closedToStart();
}

// (c) A mode button in the window writes the identical GET the sidebar's mode
// combo does -- the window is a second view of the same state, not a second
// protocol.
void testWindowModeButtonSendsTheSameQueryAsTheSidebarCombo()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* track = w->findChild<QPushButton*>(QStringLiteral("diversityWindowModetrack"));
    CHECK(track != nullptr);
    const int before = net.count(QStringLiteral("/diversity/set?mode=track"));
    if (track)
        track->click();
    settle();
    CHECK(net.count(QStringLiteral("/diversity/set?mode=track")) == before + 1);

    // And the read-back that follows must not turn into a second write: the
    // buttons are checked back with the signal blocked.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityNulls};
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/set?mode=track")) == before + 1);
    CHECK(track && track->isChecked());
    closedToStart();
}

// (d) Phase/ratio are a MANUAL setpoint. In track mode the gate solves for its
// own weight, so the knobs are disabled AND a poll must not move them --
// otherwise the window would be showing a control that looks live and is not.
void testPhaseKnobDisabledInTrackModeAndNotWrittenByAPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* phase = w->findChild<ClientCompKnob*>(QStringLiteral("diversityWindowPhaseKnob"));
    auto* ratio = w->findChild<ClientCompKnob*>(QStringLiteral("diversityWindowRatioKnob"));
    CHECK(phase && phase->isEnabled());          // manual
    CHECK(ratio && ratio->isEnabled());
    CHECK(phase && std::abs(phase->value() - 45.0f) < 1e-3f);

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityNulls};
    tick(a);
    CHECK(phase && !phase->isEnabled());         // track
    CHECK(ratio && !ratio->isEnabled());
    // kDiversityNulls carries phase_deg 10.0; the knob must still read 45.
    CHECK(phase && std::abs(phase->value() - 45.0f) < 1e-3f);

    const int writes = net.count(QStringLiteral("/diversity/set?phase="));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/set?phase=")) == writes);
    closedToStart();
}

// (e) The window's noise panel shows the same map strip much larger, so the
// map poll has to keep running while the window is open even when the
// sidebar's own Noise block is collapsed.
void testMapPollRunsOnlyWhileWindowVisible()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, makeDiversityMap(8)};
    connectGate(a, net, kDiversityFull);

    settle();
    CHECK(!a.diversityPanel()->wantsMapPoll());

    const int collapsed = net.count(QStringLiteral("/diversity/map"));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == collapsed);

    openButton(a)->click();
    settle();
    CHECK(a.diversityPanel()->wantsMapPoll());
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) > collapsed);

    // Closing it again stops the poll once more.
    openButton(a)->click();
    settle();
    CHECK(!a.diversityPanel()->wantsMapPoll());
    const int reclosed = net.count(QStringLiteral("/diversity/map"));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == reclosed);
    closedToStart();
}

// (f) The gate going away clears every readout: a window left open on a dead
// gate must not keep showing the last numbers as if they were live.
void testGateGoingAwayClearsTheWindowsReadouts()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* scope = w->findChild<DiversityScope*>(QStringLiteral("diversityWindowScope"));
    auto* meterB = w->findChild<DiversitySnrMeter*>(QStringLiteral("diversityWindowMeterB"));
    auto* stations = w->findChild<QLabel*>(
        QStringLiteral("diversityWindowTalkersCountLabel"));
    auto* status = w->findChild<QLabel*>(QStringLiteral("diversityWindowStatusLabel"));
    CHECK(meterB && !std::isnan(meterB->shownDb()));
    CHECK(status && status->text() == QStringLiteral("gate connected · diversity live"));

    net.down = true;
    tick(a);
    tick(a);
    tick(a);
    CHECK(!a.gatePresent());

    CHECK(scope && std::isnan(scope->lastGainDb()));
    CHECK(meterB && std::isnan(meterB->shownDb()));
    CHECK(stations
          && stations->text() == QStringLiteral("0 talkers remembered · nobody talking"));
    CHECK(status && status->text() == QStringLiteral("gate not answering"));
    // The operator opened it; a dropped poll is not a reason to take it away.
    CHECK(w->isVisible());
    closedToStart();
}

// (g) Who is talking. The gate names one memory entry as live; that row is
// lit, its id cell gets the same filled dot the dial's marker gets, and the
// header says so. "talker": null takes all of it away again -- an operator
// must never be looking at a highlight for somebody who stopped transmitting.
void testLiveTalkerLightsItsRowAndHeader()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityTalkers);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* table = w->findChild<QTableWidget*>(QStringLiteral("diversityWindowTalkersTable"));
    auto* count = w->findChild<QLabel*>(QStringLiteral("diversityWindowTalkersCountLabel"));
    CHECK(table && table->rowCount() == 3);
    CHECK(count
          && count->text()
                 == QStringLiteral("3 talkers remembered · #2 talking 14 s"));
    if (!table)
        return;

    CHECK(table->item(0, 0) && table->item(0, 0)->text() == QStringLiteral("1"));
    CHECK(table->item(1, 0) && table->item(1, 0)->text() == QStringLiteral("● 2"));
    CHECK(table->item(1, 1) && table->item(1, 1)->text() == QStringLiteral("Al"));
    // Heard/First are durations, not raw seconds.
    CHECK(table->item(1, 5) && table->item(1, 5)->text() == QStringLiteral("3 s"));
    CHECK(table->item(1, 6) && table->item(1, 6)->text() == QStringLiteral("4 m"));
    CHECK(table->item(2, 5) && table->item(2, 5)->text() == QStringLiteral("1 m"));
    CHECK(table->item(2, 6) && table->item(2, 6)->text() == QStringLiteral("2 h"));
    // Exactly one row carries the highlight brush.
    CHECK(table->item(1, 0)->background().style() != Qt::NoBrush);
    CHECK(table->item(0, 0)->background().style() == Qt::NoBrush);
    CHECK(table->item(2, 0)->background().style() == Qt::NoBrush);

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityTalkersIdle};
    tick(a);
    CHECK(count
          && count->text()
                 == QStringLiteral("3 talkers remembered · nobody talking"));
    CHECK(table->item(1, 0) && table->item(1, 0)->text() == QStringLiteral("2"));
    CHECK(table->item(1, 0)->background().style() == Qt::NoBrush);
    closedToStart();
}

// (h) Naming a talker is the window's only new write, and it takes the same
// route every other one does. The half-typed name also has to survive the
// poll that lands while the editor is open -- a 250ms rebuild cadence is
// otherwise long odds against ever finishing a callsign.
// (h2) Station lock. Selecting a row arms the Lock button with that talker's
// id; clicking writes focus=<id>; the gate's focus object drives a LOCKED
// banner (naming who is being nulled meanwhile) and turns the button into
// Release, which writes focus=off. The log says when the lock came and went.
void testLockOnAStationWritesFocusAndShowsTheBanner()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityTalkersIdle);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* table = w->findChild<QTableWidget*>(QStringLiteral("diversityWindowTalkersTable"));
    auto* lock = w->findChild<QPushButton*>(QStringLiteral("diversityWindowLockButton"));
    auto* banner = w->findChild<QLabel*>(QStringLiteral("diversityWindowFocusLabel"));
    auto* events = w->findChild<QListWidget*>(QStringLiteral("diversityWindowEventsList"));
    CHECK(table && lock && banner);
    if (!table || !lock || !banner)
        return;

    // Nothing selected: the button is armed with nothing.
    CHECK(!lock->isEnabled() && lock->text() == QStringLiteral("Lock on station"));
    CHECK(banner->isHidden());
    table->selectRow(1);
    CHECK(lock->isEnabled() && lock->text() == QStringLiteral("Lock on #2"));
    const int before = net.count(QStringLiteral("/diversity/set?focus=2"));
    lock->click();
    settle();
    CHECK(net.count(QStringLiteral("/diversity/set?focus=2")) == before + 1);

    // The gate confirms the lock while Kay is talking over it.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityFocusNulling};
    tick(a);
    CHECK(!banner->isHidden());
    CHECK(banner->text()
          == QStringLiteral("LOCKED on #2 \"Al\" · nulling #5 \"Kay\" · 3 overs · 5 nulled · "
                            "best +7.2 dB"));
    CHECK(lock->isEnabled() && lock->text() == QStringLiteral("Release lock"));
    CHECK(events && events->count() >= 2);
    if (events) {
        QStringList lines;
        for (int i = 0; i < events->count(); ++i)
            lines << events->item(i)->text();
        const QString all = lines.join(QStringLiteral("\n"));
        CHECK(all.contains(QStringLiteral("locked on #2 \"Al\"")));
        CHECK(all.contains(QStringLiteral("nulling #5 \"Kay\" (not the locked station)")));
    }

    const int offBefore = net.count(QStringLiteral("/diversity/set?focus=off"));
    lock->click();
    settle();
    CHECK(net.count(QStringLiteral("/diversity/set?focus=off")) == offBefore + 1);

    // Released on the gate: banner gone, button back to arming on selection.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityTalkersIdle};
    tick(a);
    CHECK(banner->isHidden());
    CHECK(lock->text() != QStringLiteral("Release lock"));
    if (events) {
        QStringList lines;
        for (int i = 0; i < events->count(); ++i)
            lines << events->item(i)->text();
        CHECK(lines.join(QStringLiteral("\n")).contains(QStringLiteral("lock released")));
    }
    closedToStart();
}

void testNamingATalkerWritesThroughAndSurvivesAPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityTalkers);
    net.routes[QStringLiteral("/diversity/memory/name")] = {QNetworkReply::NoError,
                                                            QByteArray("{}")};
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* table = w->findChild<QTableWidget*>(QStringLiteral("diversityWindowTalkersTable"));
    CHECK(table != nullptr);
    if (!table)
        return;

    QTableWidgetItem* nameItem = table->item(1, 1);
    CHECK(nameItem && (nameItem->flags() & Qt::ItemIsEditable));
    if (!nameItem)
        return;
    table->editItem(nameItem);
    settle();
    auto* editor = table->viewport()->findChild<QLineEdit*>();
    CHECK(editor != nullptr);

    // The gate now says this talker is called something else. The poll must
    // not rebuild the table out from under the open editor.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityTalkersIdle};
    tick(a);
    CHECK(table->item(1, 1) == nameItem);
    CHECK(nameItem->text() == QStringLiteral("Al"));
    CHECK(table->viewport()->findChild<QLineEdit*>() == editor);

    const int before = net.count(QStringLiteral("/diversity/memory/name"));
    if (editor) {
        editor->setText(QStringLiteral("Bob"));
        QTest::keyClick(editor, Qt::Key_Return);
    }
    settle();
    CHECK(net.count(QStringLiteral("/diversity/memory/name?id=2&name=Bob"))
          == before + 1);
    CHECK(net.count(QStringLiteral("/diversity/memory/name")) == before + 1);
    closedToStart();
}

// (i) The timeline is the window's only accumulated state. It grows one
// sample per poll, forgets anything older than its own window, and is emptied
// when the gate goes away -- two minutes of history from a dead gate would be
// two minutes of a lie.
void testTimelineAccumulatesAgesOutAndClears()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityTalkers);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* timeline =
        w->findChild<DiversityTimeline*>(QStringLiteral("diversityWindowTimeline"));
    CHECK(timeline && timeline->sampleCount() > 0);
    if (!timeline)
        return;

    const int before = timeline->sampleCount();
    tick(a);
    tick(a);
    CHECK(timeline->sampleCount() == before + 2);

    // A sample past the far edge of the window retires everything behind it.
    DiversityTimeline::Sample far;
    far.haveOut = true;
    far.out = 5.0;
    timeline->addSample(QDateTime::currentMSecsSinceEpoch() + timeline->windowMs() + 1000,
                        far);
    CHECK(timeline->sampleCount() == 1);

    net.down = true;
    tick(a);
    tick(a);
    tick(a);
    CHECK(!a.gatePresent());
    CHECK(timeline->sampleCount() == 0);
    closedToStart();
}

// (j) Event derivation is a pure function of two consecutive snapshots, and
// is tested as one -- no widgets, no network, no timing. One transition, one
// line: an event list that repeats itself every poll is noise, not history.
void testEventLogDerivesOneLinePerTransition()
{
    DiversityEventLog log;
    DiversitySnapshot base;
    base.present = true;
    base.available = true;
    base.mode = QStringLiteral("track");
    base.hear = QStringLiteral("combined");
    base.haveSteadyQrm = true;
    base.memoryIds = {1};
    // The first snapshot has nothing to be a transition from.
    CHECK(log.apply(base).isEmpty());
    CHECK(log.apply(base).isEmpty());

    DiversitySnapshot start = base;
    start.haveTalker = true;
    start.talkerId = 2;
    start.talkerName = QStringLiteral("Bob");
    start.haveTalkerWeight = true;
    start.talkerPhaseDeg = 141.0;
    start.talkerRatioDb = 1.0;
    start.memoryIds = {1, 2};
    QStringList lines = log.apply(start);
    CHECK(lines.size() == 2);
    CHECK(lines.contains(
        QStringLiteral("#2 \"Bob\" started (phase 141°, +1.0 dB)")));
    CHECK(lines.contains(QStringLiteral("new talker #2 remembered")));

    // A talker that keeps talking is not an event.
    DiversitySnapshot talking = start;
    talking.talkerSinceS = 14.0;
    CHECK(log.apply(talking).isEmpty());

    DiversitySnapshot ended = talking;
    ended.haveTalker = false;
    lines = log.apply(ended);
    CHECK(lines == QStringList{QStringLiteral("#2 \"Bob\" ended after 14 s")});

    DiversitySnapshot qrm = ended;
    qrm.steadyQrm = true;
    lines = log.apply(qrm);
    CHECK(lines == QStringList{QStringLiteral("steady carrier nulled")});

    DiversitySnapshot moded = qrm;
    moded.mode = QStringLiteral("null");
    lines = log.apply(moded);
    CHECK(lines == QStringList{QStringLiteral("mode → null")});

    DiversitySnapshot heard = moded;
    heard.hear = QStringLiteral("a");
    lines = log.apply(heard);
    CHECK(lines == QStringList{QStringLiteral("hear → A")});

    DiversitySnapshot gone = heard;
    gone.present = false;
    lines = log.apply(gone);
    CHECK(lines == QStringList{QStringLiteral("gate lost")});

    // Presence is a barrier: coming back says one thing, not a burst of
    // everything that changed while nobody was listening.
    DiversitySnapshot back = base;
    lines = log.apply(back);
    CHECK(lines == QStringList{QStringLiteral("gate back")});
}

// (k) The receiver's own passband over the coherence map. It is drawn only
// when the gate sends it: a marker at a guessed frequency would be worse than
// no marker at all.
void testMapPassbandParsesOnlyWhenTheGateSendsIt()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityTalkers);
    openButton(a)->click();
    settle();

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* strip =
        w->findChild<DiversityMapStrip*>(QStringLiteral("diversityWindowMapStrip"));
    CHECK(strip && !strip->hasPassband());
    if (!strip)
        return;

    w->applyMap(asObject(makeDiversityMapWithPassband(8)));
    CHECK(strip->hasPassband());
    CHECK(std::abs(strip->passbandLoHz() - 3501200.0) < 1.0);
    CHECK(std::abs(strip->passbandHiHz() - 3504000.0) < 1.0);

    // An older gate stops sending it; the marker goes away with it.
    w->applyMap(asObject(makeDiversityMap(8)));
    CHECK(!strip->hasPassband());
    closedToStart();
}

// (l) A gate that predates ids, names, first_seen_s, talker and passband. The
// window is the newer half of the pair and has to keep working against it --
// rendering, but saying "—" rather than inventing a zero for anything it was
// not told (Principle XI).
void testOldGatePayloadRendersWithoutInventingAnything()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityOldGate);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* count = w->findChild<QLabel*>(QStringLiteral("diversityWindowTalkersCountLabel"));
    CHECK(count
          && count->text()
                 == QStringLiteral("1 talkers remembered · nobody talking"));

    auto* table = w->findChild<QTableWidget*>(QStringLiteral("diversityWindowTalkersTable"));
    CHECK(table && table->rowCount() == 1);
    if (table && table->rowCount() == 1) {
        CHECK(table->item(0, 0) && table->item(0, 0)->text() == QStringLiteral("—"));
        // Nothing to address a name write to, so the cell cannot be edited.
        CHECK(table->item(0, 1) && table->item(0, 1)->text().isEmpty());
        CHECK(table->item(0, 1) && !(table->item(0, 1)->flags() & Qt::ItemIsEditable));
        CHECK(table->item(0, 5) && table->item(0, 5)->text() == QStringLiteral("9 s"));
        CHECK(table->item(0, 6) && table->item(0, 6)->text() == QStringLiteral("—"));
        CHECK(table->item(0, 0)->background().style() == Qt::NoBrush);
    }

    auto* coherence =
        w->findChild<QLabel*>(QStringLiteral("diversityWindowBalanceCoherenceLabel"));
    auto* passband =
        w->findChild<QLabel*>(QStringLiteral("diversityWindowBalancePassbandLabel"));
    auto* verdict =
        w->findChild<QLabel*>(QStringLiteral("diversityWindowBalanceVerdictLabel"));
    auto* noise = w->findChild<QLabel*>(QStringLiteral("diversityWindowNoiseStatusLabel"));
    CHECK(coherence && coherence->text() == QStringLiteral("noise coherence —"));
    CHECK(passband && passband->text() == QStringLiteral("passband —"));
    CHECK(verdict && verdict->text() == QStringLiteral("noise character unknown"));
    CHECK(noise
          && noise->text() == QStringLiteral("noise reference: — · coherence —"));
    // What it DID send still gets rendered.
    auto* delta = w->findChild<QLabel*>(QStringLiteral("diversityWindowBalanceDeltaLabel"));
    CHECK(delta && delta->text() == QStringLiteral("A - B: +1.0 dB"));

    auto* strip =
        w->findChild<DiversityMapStrip*>(QStringLiteral("diversityWindowMapStrip"));
    CHECK(strip && !strip->hasPassband());
    closedToStart();
}

// (m) The layout budget: every panel has a minimum size and their sum has to
// fit 1120x860, or the operator meets a scrollbar the first time the window
// opens. A test rather than a comment because every addition here pays it.
void testNothingScrollsAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityTalkers);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = w->findChild<QScrollArea*>(QStringLiteral("diversityWindowSliceScroll"));  // SLICE, not BAND
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());

    // The LOCKED banner is an extra line in TALKERS; it has to fit too.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityFocusNulling};
    tick(a);
    settle();
    w->grab();
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_window_test"));
    QApplication app(argc, argv);

    testOpenButtonBuildsTheWindowAndPersistsItsVisibility();
    testFullAndNullPayloadsApplyAndTheScopeAgrees();
    testWindowModeButtonSendsTheSameQueryAsTheSidebarCombo();
    testPhaseKnobDisabledInTrackModeAndNotWrittenByAPoll();
    testMapPollRunsOnlyWhileWindowVisible();
    testGateGoingAwayClearsTheWindowsReadouts();
    testLiveTalkerLightsItsRowAndHeader();
    testNamingATalkerWritesThroughAndSurvivesAPoll();
    testLockOnAStationWritesFocusAndShowsTheBanner();
    testTimelineAccumulatesAgesOutAndClears();
    testEventLogDerivesOneLinePerTransition();
    testMapPassbandParsesOnlyWhenTheGateSendsIt();
    testOldGatePayloadRendersWithoutInventingAnything();
    testNothingScrollsAtTheInitialSize();

    std::printf("\n%d diversity window test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
