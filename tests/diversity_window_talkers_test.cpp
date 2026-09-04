// DiversityWindow talker table, timeline and derived event log -- split
// out of tests/diversity_window_test.cpp, which was over the 800-line
// budget AGENTS.md asks for. Same harness as that file: a real
// AetherGateApplet in front of a fake, socket-free QNetworkAccessManager,
// no port opened, nothing listened on.
//
// What lives here: who is talking (the memory table's live-row highlight
// and header), naming a talker, locking on a station, the timeline of
// accumulated samples, the pure DiversityEventLog transition test, and an
// old-gate payload that predates ids/names/talker/passband.
//
// closedToStart()/connectGate()/openButton() are shared with
// diversity_window_test.cpp and live in DiversityWindowTestSupport.h so
// they are not duplicated between the two binaries.

#include "DiversityGateFixture.h"
#include "DiversityWindowTestSupport.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityMapStrip.h"
#include "gui/DiversityTimeline.h"
#include "gui/DiversityWindow.h"
#include "gui/DiversityWindowEvents.h"

#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTest>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversityEventLog;
using AetherSDR::DiversityMapStrip;
using AetherSDR::DiversitySnapshot;
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

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_window_talkers_test"));
    QApplication app(argc, argv);

    testLiveTalkerLightsItsRowAndHeader();
    testLockOnAStationWritesFocusAndShowsTheBanner();
    testNamingATalkerWritesThroughAndSurvivesAPoll();
    testTimelineAccumulatesAgesOutAndClears();
    testEventLogDerivesOneLinePerTransition();
    testOldGatePayloadRendersWithoutInventingAnything();

    std::printf("\n%d diversity window talkers test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
