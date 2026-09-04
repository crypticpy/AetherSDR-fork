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
//
// The talker table (live/idle highlight, naming, station lock), the
// timeline and the derived event log moved out to
// tests/diversity_window_talkers_test.cpp: this file was at the 800-line
// budget AGENTS.md asks for. closedToStart()/connectGate()/openButton() are
// shared by both binaries and live in DiversityWindowTestSupport.h so they
// are not duplicated between them.

#include "DiversityGateFixture.h"
#include "DiversityWindowTestSupport.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/ClientCompKnob.h"
#include "gui/DiversityMapStrip.h"
#include "gui/DiversityScope.h"
#include "gui/DiversityWindow.h"
#include "gui/DiversityWindowPanels.h"

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QToolButton>
#include <QWidget>

#include <cmath>
#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::ClientCompKnob;
using AetherSDR::DiversityMapStrip;
using AetherSDR::DiversityScope;
using AetherSDR::DiversitySnrMeter;
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
    // Only the applet's OWN background timer (priming BAND/SITE) may follow.
    for (int i = requestsBefore; i < net.log.size(); ++i)
        CHECK(net.log.at(i).startsWith(QStringLiteral("/diversity/")));

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
    // The tab row is four pages again now FILTER is retired; the hint beside
    // it says so rather than counting a tab that is not there.
    auto* pagesHint = w->findChild<QLabel*>(QStringLiteral("diversityWindowPagesHint"));
    CHECK(pagesHint && pagesHint->toolTip().contains(QStringLiteral("four")));
    CHECK(pagesHint && !pagesHint->toolTip().contains(QStringLiteral("five")));

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

// (b2) align_held renders "aligned · held · lag N · peak P" with the gate's
// own note in the tooltip (elided to <= 90 chars, full note in the
// accessible description), and reverts cleanly once held clears.
void testAlignHeldRendersNoteAndClearsWhenHeldEnds()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    const QString note = QStringLiteral(
        "held lag 3 through overflow on channel A while the background "
        "re-measure keeps looking for a better window (peak 0.910, need 0.500)");
    QByteArray held = kDiversityFull;
    held.replace("\"aligned\": true, \"corr_peak\": 0.91,",
                 QByteArray("\"aligned\": true, \"align_held\": true, \"align_note\": \"")
                     + note.toUtf8() + "\", \"corr_peak\": 0.91,");
    connectGate(a, net, held);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* align = w->findChild<QLabel*>(QStringLiteral("diversityWindowAlignLabel"));
    CHECK(align != nullptr);
    if (!align) {
        closedToStart();
        return;
    }
    CHECK(align->text() == QStringLiteral("aligned · held · lag 3 · peak 0.910"));
    CHECK(align->toolTip().size() <= 90);
    CHECK(align->toolTip().endsWith(QStringLiteral("…")));
    CHECK(note.startsWith(align->toolTip().chopped(1)));
    CHECK(align->accessibleDescription() == note);

    // MUTATION: the same gate, held clears -- the line and its tooltip go
    // back to the plain four-field sentence, no stale note left behind.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    tick(a);
    CHECK(align->text() == QStringLiteral("aligned · lag 3 · peak 0.910 · steady"));
    CHECK(!align->toolTip().contains(QStringLiteral("channel A")));
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
    // START is the first page now; the budget under test is SLICE's.
    w->findChild<QAbstractButton*>(QStringLiteral("diversityWindowPageSlice"))->click();
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


// (n) OPEN CHAIN is on the header block -- the tab row, beside the pair row
// above it -- rather than on a page: it used to be at the top of the retired
// FILTER tab, one page switch away from wherever the operator was. So the
// button has to be found from START, from BAND, from anywhere, and every
// press has to be another ask rather than a toggle that latches.
void testOpenChainSitsOnTheHeaderRowAndAsksOnEveryPage()
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

    auto* button =
        w->findChild<QPushButton*>(QStringLiteral("diversityWindowOpenChain"));
    auto* tabRow = w->findChild<QWidget*>(QStringLiteral("diversityWindowTabRow"));
    auto* pages = w->findChild<QStackedWidget*>(QStringLiteral("diversityWindowPages"));
    CHECK(button != nullptr);
    CHECK(tabRow != nullptr);
    if (!button || !tabRow || !pages)
        return;
    // On the header row itself, not on any page inside the stack -- a button
    // on a page is a button three pages cannot reach.
    CHECK(button->parentWidget() == tabRow);
    CHECK(!pages->isAncestorOf(button));
    CHECK(button->text() == QStringLiteral("OPEN CHAIN"));
    CHECK(button->toolTip().length() <= 90);
    CHECK(!button->isCheckable());

    QSignalSpy spy(w, &DiversityWindow::requestOpenChain);
    button->click();
    settle();
    CHECK(spy.count() == 1);

    // MUTATION: from another page, the same button, still there and still
    // asking -- which is the whole reason it left the page it was on.
    w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageBand"))->click();
    settle();
    CHECK(button->isVisible());
    button->click();
    settle();
    CHECK(spy.count() == 2);
    closedToStart();
}

// (o) The persisted page key holds an int. A station that last used the
// retired FILTER tab has a 4 in it, and a build with four pages has no page
// 4 -- that must land on START, never on a blank stack. Same for anything
// else out of range, and for a value that is not a number at all.
void testStoredPageOutOfRangeFallsBackToStart()
{
    const QString key = QStringLiteral("DiversityWindowPage");
    for (const QString& stored : {QStringLiteral("4"), QStringLiteral("99"),
                                  QStringLiteral("-1"), QStringLiteral("filter"),
                                  QString()}) {
        closedToStart();
        AppSettings::instance().setValue(key, stored);
        FakeGate net;
        AetherGateApplet a(nullptr, &net);
        connectGate(a, net, kDiversityFull);
        openButton(a)->click();
        settle();

        DiversityWindow* w = a.diversityPanel()->window();
        CHECK(w != nullptr);
        if (!w)
            continue;
        auto* pages = w->findChild<QStackedWidget*>(QStringLiteral("diversityWindowPages"));
        CHECK(pages != nullptr);
        if (!pages)
            continue;
        // Four pages, START showing, and the key rewritten to the page that
        // is actually up -- a stale 4 must not survive to be read again.
        CHECK(pages->count() == 4);
        CHECK(pages->currentIndex() == 0);
        CHECK(pages->currentWidget() != nullptr);
        CHECK(w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageStart"))
                  ->isChecked());
        CHECK(w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageFilter"))
              == nullptr);
        CHECK(AppSettings::instance().value(key).toString() == QStringLiteral("0"));
        w->close();
        settle();
    }
    AppSettings::instance().setValue(key, QString());
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_window_test"));
    QApplication app(argc, argv);

    testOpenButtonBuildsTheWindowAndPersistsItsVisibility();
    testFullAndNullPayloadsApplyAndTheScopeAgrees();
    testAlignHeldRendersNoteAndClearsWhenHeldEnds();
    testWindowModeButtonSendsTheSameQueryAsTheSidebarCombo();
    testPhaseKnobDisabledInTrackModeAndNotWrittenByAPoll();
    testMapPollRunsOnlyWhileWindowVisible();
    testGateGoingAwayClearsTheWindowsReadouts();
    testMapPassbandParsesOnlyWhenTheGateSendsIt();
    testNothingScrollsAtTheInitialSize();
    testOpenChainSitsOnTheHeaderRowAndAsksOnEveryPage();
    testStoredPageOutOfRangeFallsBackToStart();

    std::printf("\n%d diversity window test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
