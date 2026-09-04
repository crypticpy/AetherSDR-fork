// The Diversity window START page's NEXT strip (footer) and the frame's
// no-scroll budget -- split out of tests/diversity_session_page_test.cpp,
// which was over the 800-line budget AGENTS.md asks for.
//
// Same harness as that file -- a real AetherGateApplet in front of a fake,
// socket-free QNetworkAccessManager -- and the same shared fixture
// (constants, the governor/dig payload builders, Bench, and the
// card/strip finder helpers), which lives in
// DiversitySessionPageTestSupport.h so it is not duplicated between the
// two binaries.
//
// EVERY RENDERED VALUE HERE CARRIES A MUTATION: a second payload in which that
// value differs, asserted after the first. A page whose footer or frame were
// five frozen strings would pass the first assertion of every case and fail
// the second -- the only way to tell "reads the gate" from "looks like it
// does".

#include "DiversityGateFixture.h"
#include "DiversitySessionPageTestSupport.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityNextStrip.h"
#include "gui/DiversitySessionModel.h"
#include "gui/DiversitySessionPage.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversitySessionModel;
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

// A guard that is also an assertion: a null the rest of the case would walk
// into is a failure, not a silent skip.
#define REQUIRE(cond)                                                                \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
            return;                                                                  \
        }                                                                            \
    } while (0)

// --- (c) The NEXT line ---------------------------------------------------------------

// The footer quotes ONE step -- the next one -- and offers its one cure.
void nextStripSaysOneStepAndOffersOneButton()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    CHECK_EQ(nextLine(b.w), QStringLiteral("NEXT · SITE NOISE · 2 findings with a button"));
    CHECK(!nextLine(b.w).contains(QStringLiteral("RECEIVER")));
    CHECK(!nextLine(b.w).contains(QStringLiteral("STATION")));
    auto* button = child<QPushButton>(b.w, "diversityWindowNextButton");
    CHECK(button != nullptr && button->isVisibleTo(b.w));
    CHECK_EQ(button->text(), QStringLiteral("GO"));
    CHECK(strip(b.w)->height() == 22);

    // MUTATION: an earlier step falls over. The line moves to it -- the footer
    // never lists two.
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(with(kDiversityStatusWithKinds, "\"aligned\": true", "\"aligned\": false"),
                     governor(true))};
    tick(b.a);
    CHECK_EQ(nextLine(b.w), QStringLiteral("NEXT · RECEIVER · not aligned"));
    CHECK_EQ(button->text(), QStringLiteral("REALIGN"));
    CHECK(!nextLine(b.w).contains(QStringLiteral("SITE NOISE")));
}

// AUTO CLEAN off is a station under manual control: the footer becomes a
// status line and no card offers to press anything.
void manualModeStripHasNoButton()
{
    Bench b(kindsAuto(false));
    REQUIRE(b.w != nullptr);
    CHECK_EQ(nextLine(b.w), QStringLiteral("NEXT · SITE NOISE · 2 findings with a button"));
    CHECK(!child<QPushButton>(b.w, "diversityWindowNextButton")->isVisibleTo(b.w));
    for (int i = 1; i <= DiversitySessionModel::StepCount; ++i) {
        CHECK(!cardCure(b.w, i)->isVisibleTo(b.w));
    }
    CHECK_EQ(card(b.w, 2)->tone(), QStringLiteral("state"));

    // MUTATION: the same site with AUTO CLEAN on. The same sentence, now with
    // the nudge -- what changed is the offer, not the line.
    b.net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kindsAuto()};
    tick(b.a);
    CHECK_EQ(nextLine(b.w), QStringLiteral("NEXT · SITE NOISE · 2 findings with a button"));
    CHECK(child<QPushButton>(b.w, "diversityWindowNextButton")->isVisibleTo(b.w));
    CHECK(cardCure(b.w, 2)->isVisibleTo(b.w));
    CHECK_EQ(card(b.w, 2)->tone(), QStringLiteral("lit"));
}

// The switch's face is one of exactly two strings. The governor's state and
// its "why" belong in the tooltip and the accessible description, never on a
// control the operator reads at a glance.
void autoCleanSwitchTextIsOnlyTwoStrings()
{
    const QString why = QStringLiteral("waiting 5 s for the level to sit");
    Bench b(withGovernor(kDiversityStatusWithKinds,
                         governor(true, QStringLiteral("settling"), why)));
    REQUIRE(b.w != nullptr);
    auto* sw = child<QPushButton>(b.w, "diversityWindowFlowAutoCleanButton");
    REQUIRE(sw != nullptr);
    CHECK(sw->isVisibleTo(b.w) && strip(b.w)->isAncestorOf(sw));
    CHECK_EQ(sw->text(), QStringLiteral("AUTO CLEAN ON"));
    CHECK(sw->isChecked());
    CHECK(!sw->text().contains(QStringLiteral("settling")));
    CHECK(!sw->text().contains(QStringLiteral("waiting")));
    CHECK(!sw->toolTip().contains(QLatin1Char('\n')));

    // MUTATION: the same governor, switched off. The other of the two, and
    // still not a word of the state on the face.
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(kDiversityStatusWithKinds,
                     governor(false, QStringLiteral("settling"), why))};
    tick(b.a);
    CHECK_EQ(sw->text(), QStringLiteral("AUTO CLEAN"));
    CHECK(!sw->isChecked());
    CHECK(!sw->toolTip().contains(QLatin1Char('\n')));
}

// A dig goes on wherever the operator wandered to, so its STOP is on the one
// row every page keeps on screen -- and there is exactly one of it. (Live,
// with a dig running, this once read REALIGN · STOP · [ STOP ]: the strip's
// own STOP sat beside the dig stack's, which carries the same button.)
void digStopStaysVisibleOnEveryPageWhileARunIsOut()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    digTick(b, kDigRunning);
    auto* stop = child<QPushButton>(b.w, "diversityWindowFlowDigStop");
    REQUIRE(stop != nullptr);
    for (const char* page : kPageButtons) {
        child<QToolButton>(b.w, page)->click();
        settle();
        CHECK(stop->isVisibleTo(b.w));
        int stopCount = 0;
        for (QPushButton* btn : strip(b.w)->findChildren<QPushButton*>()) {
            if (btn->isVisibleTo(b.w) && btn->isEnabled()
                && btn->text() == QStringLiteral("STOP"))
                ++stopCount;
        }
        CHECK(stopCount == 1);
    }
    CHECK(nextLine(b.w).contains(QStringLiteral("DIG 1:12 of 3:00")));

    // MUTATION: nothing out. STOP is gone on every page rather than sitting
    // there greyed -- there is no run to stop.
    digTick(b, kDigIdle);
    for (const char* page : kPageButtons) {
        child<QToolButton>(b.w, page)->click();
        settle();
        CHECK(!stop->isVisibleTo(b.w));
    }
}

// The verdict row is a footer control too: a run judged only from START would
// leave the chain on the dig's own settings while the operator read SITE.
void verdictRowAppearsOnTheFooterNotOnlyOnStart()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    child<QToolButton>(b.w, "diversityWindowPageSite")->click();
    settle();
    digTick(b, kDigDone);
    auto* stack = child<QStackedWidget>(b.w, "diversityWindowFlowDigControls");
    REQUIRE(stack != nullptr);
    CHECK(strip(b.w)->isAncestorOf(stack));
    CHECK(stack->isVisibleTo(b.w));
    CHECK(child<QWidget>(b.w, "diversityWindowFlowDigVerdict") == stack->currentWidget());
    CHECK(child<QPushButton>(b.w, "diversityWindowFlowDigBetter")->isVisibleTo(b.w));
    CHECK(nextLine(b.w).contains(QStringLiteral("DIG done · +4.1 dB — better or worse?")));

    // MUTATION: a run the operator stopped. The chain is already back on their
    // own settings, so there is nothing to be a verdict about.
    QByteArray cancelled = kDigDone;
    cancelled.replace("\"cancelled\": false", "\"cancelled\": true");
    digTick(b, cancelled);
    CHECK(child<QWidget>(b.w, "diversityWindowFlowDigVerdict") != stack->currentWidget());
    CHECK(nextLine(b.w).contains(QStringLiteral("DIG found +4.1 dB (put back)")));
}

// Once the four chores are behind you the footer is one line about who is
// talking, and a click opens it back up. The choice is remembered.
void collapsedStripIsOneLineAndExpandsOnClick()
{
    Bench b(withGovernor(kAllChoresDone, governor(true)));
    REQUIRE(b.w != nullptr);
    b.w->applyFilter(filterWithTalker(7));
    settle();
    CHECK(strip(b.w)->collapsed());
    CHECK_EQ(nextLine(b.w),
             QStringLiteral("● listening · Ann talking · OUT +1.2 dB · 4 remembered"));
    CHECK(!nextLine(b.w).contains(QLatin1Char('\n')));
    CHECK(strip(b.w)->height() == 22);

    auto* line = child<QLabel>(b.w, "diversityWindowNextLine");
    REQUIRE(line != nullptr);
    QMetaObject::invokeMethod(line, "linkActivated", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("toggle")));
    settle();
    CHECK(!strip(b.w)->collapsed());
    CHECK_EQ(nextLine(b.w),
             QStringLiteral("NEXT · LISTEN · Ann talking · OUT +1.2 dB · 4 remembered"));
    CHECK_EQ(AppSettings::instance().value(QLatin1String(kCollapsedKey), QString()).toString(),
             QStringLiteral("False"));

    // MUTATION: a chore comes back. There is nothing to collapse while
    // something is still owed, whatever the remembered choice was.
    QMetaObject::invokeMethod(line, "linkActivated", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("toggle")));
    settle();
    CHECK(strip(b.w)->collapsed());
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(with(kAllChoresDone, "\"aligned\": true", "\"aligned\": false"),
                     governor(true))};
    tick(b.a);
    CHECK(!strip(b.w)->collapsed());
    CHECK_EQ(nextLine(b.w), QStringLiteral("NEXT · RECEIVER · not aligned"));
    forgetEverything();
}

// --- (d) The frame ---------------------------------------------------------------

// Nothing on START is behind a scrollbar at the size the window opens at.
void nothingScrollsOnTheStartPageAtTheInitialSize()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    b.w->resize(1120, 860);
    settle();
    b.w->grab();   // forces a full layout pass on an offscreen platform
    auto* scroll = child<QScrollArea>(b.w, "diversityWindowStartScroll");
    REQUIRE(scroll != nullptr);
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());

    // MUTATION: the longest state string any step can hold -- the pair-mode
    // explanation, which is a sentence and a half. A card whose state label
    // wrapped would make the page taller than the frame and put a bar on it.
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(with(kDiversityStatusWithKinds, "\"mode\": \"track\"", "\"mode\": \"off\""),
                     governor(true))};
    tick(b.a);
    b.w->grab();
    CHECK(cardState(b.w, 1)->text().contains(QStringLiteral("track follows the talker")));
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
}

// And it does not push the window's own minimum past the size it opens at,
// which is the other half of the same rule.
void startPageMinimumWidthUnder1120()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    b.w->resize(1120, 860);
    settle();
    b.w->grab();
    CHECK(b.w->minimumSizeHint().width() <= 1120);
    CHECK(child<QLabel>(b.w, "diversityWindowSessionCard1Body")->minimumSizeHint().width()
          <= 1120);

    // MUTATION: the longest state on every card at once. A label that let its
    // own text set its minimum width would fail here and nowhere else.
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(with(kDiversityStatusWithKinds, "\"mode\": \"track\"", "\"mode\": \"off\""),
                     governor(true))};
    tick(b.a);
    b.w->grab();
    CHECK(b.w->minimumSizeHint().width() <= 1120);
    CHECK(child<QLabel>(b.w, "diversityWindowSessionCard1State")->minimumSizeHint().width()
          <= 1120);
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_session_page_next_test"));
    QApplication app(argc, argv);

    nextStripSaysOneStepAndOffersOneButton();
    manualModeStripHasNoButton();
    autoCleanSwitchTextIsOnlyTwoStrings();
    digStopStaysVisibleOnEveryPageWhileARunIsOut();
    verdictRowAppearsOnTheFooterNotOnlyOnStart();
    collapsedStripIsOneLineAndExpandsOnClick();
    nothingScrollsOnTheStartPageAtTheInitialSize();
    startPageMinimumWidthUnder1120();

    std::printf("\n%d diversity session page next test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
