// The Diversity window's START page and the NEXT line at its foot -- Phase 3a
// WP-B. The model behind both is DiversitySessionModel, whose own arithmetic
// is tests/diversity_session_model_test.cpp; what is asserted here is the
// WINDOW. That START is the first tab and the page a session opens on, that
// five cards say what each step gives you in every state it can be in, that a
// card's cure goes out as exactly one query through the door the window
// already had, and that the footer quotes one step and offers one button.
//
// Same harness as the other diversity binaries -- a real AetherGateApplet in
// front of a fake, socket-free QNetworkAccessManager -- and its own binary
// because each of them is at the 800-line budget AGENTS.md asks for and every
// window case wants the same fresh, process-wide AppSettings start.
//
// EVERY RENDERED VALUE HERE CARRIES A MUTATION: a second payload in which that
// value differs, asserted after the first. A page whose cards were five frozen
// strings would pass the first assertion of every case and fail the second --
// the only way to tell "reads the gate" from "looks like it does".
//
// The NEXT strip cases and the frame (no-scroll) budget moved out to
// tests/diversity_session_page_next_test.cpp: this file was at the
// 800-line budget AGENTS.md asks for. The shared fixture (constants, the
// governor/dig payload builders, Bench, and the card/strip finder
// helpers) lives in DiversitySessionPageTestSupport.h so it is not
// duplicated between the two binaries.

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
#include <QLayout>
#include <QNetworkReply>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTest>
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

// START is the first tab and the page a station that has never opened this
// window lands on.
void startIsTheFirstTabAndTheDefaultOnFirstOpen()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    auto* row = child<QWidget>(b.w, "diversityWindowTabRow");
    REQUIRE(row != nullptr && row->layout() != nullptr && row->layout()->itemAt(0) != nullptr);
    // The five tabs are one nested row of their own -- item 0 of the tab row.
    QLayout* tabs = row->layout()->itemAt(0)->layout();
    REQUIRE(tabs != nullptr && tabs->itemAt(0) != nullptr
            && tabs->itemAt(0)->widget() != nullptr);
    CHECK_EQ(tabs->itemAt(0)->widget()->objectName(),
             QStringLiteral("diversityWindowPageStart"));
    // The page HELP button rides at the right end of the same row.
    CHECK(row->isAncestorOf(child<QPushButton>(b.w, "diversityHelpButtonPage")));
    auto* pages = child<QStackedWidget>(b.w, "diversityWindowPages");
    CHECK(pages != nullptr && pages->currentIndex() == 0);
    CHECK(child<QToolButton>(b.w, "diversityWindowPageStart")->isChecked());
    CHECK(child<QWidget>(b.w, "diversityWindowSessionPage")->isVisibleTo(b.w));

    // MUTATION: another tab. START is a page like the other four, not a banner
    // that is always up -- and the stack follows the button.
    child<QToolButton>(b.w, "diversityWindowPageSite")->click();
    settle();
    CHECK(pages->currentIndex() == 3);
    CHECK(!child<QToolButton>(b.w, "diversityWindowPageStart")->isChecked());
    CHECK(!child<QWidget>(b.w, "diversityWindowSessionPage")->isVisibleTo(b.w));
}

// Where you were is where you come back to: a second session -- a new window
// object, the way a restart makes one -- opens on the remembered page.
void openingOnTheRememberedPageOnAReturn()
{
    {
        Bench b(kindsAuto());
        REQUIRE(b.w != nullptr);
        child<QToolButton>(b.w, "diversityWindowPageSite")->click();
        settle();
        CHECK_EQ(AppSettings::instance().value(QLatin1String(kPageKey), QString()).toString(),
                 QStringLiteral("3"));
    }
    {
        Bench b(kindsAuto(), false);
        REQUIRE(b.w != nullptr);
        CHECK(child<QStackedWidget>(b.w, "diversityWindowPages")->currentIndex() == 3);
        CHECK(child<QToolButton>(b.w, "diversityWindowPageSite")->isChecked());
    }
    // MUTATION: a stored page this build has no tab for. START, rather than an
    // empty stack -- it is never wrong to be sent there.
    AppSettings::instance().setValue(QLatin1String(kPageKey), QStringLiteral("9"));
    {
        Bench b(kindsAuto(), false);
        REQUIRE(b.w != nullptr);
        CHECK(child<QStackedWidget>(b.w, "diversityWindowPages")->currentIndex() == 0);
        CHECK(child<QToolButton>(b.w, "diversityWindowPageStart")->isChecked());
    }
    forgetEverything();
}

// --- (b) The five cards ---------------------------------------------------------------

// The lit card is the first one that is not done, and only that one.
void theLitCardIsTheFirstNotDone()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    // Aligned and tracking, two site findings with a button, no beacon band in
    // the span, nobody talking: SITE NOISE is the first chore left.
    CHECK_EQ(card(b.w, 1)->tone(), QStringLiteral("plain"));
    CHECK_EQ(card(b.w, 2)->tone(), QStringLiteral("lit"));
    CHECK_EQ(card(b.w, 3)->tone(), QStringLiteral("plain"));
    CHECK_EQ(card(b.w, 4)->tone(), QStringLiteral("dim"));
    CHECK_EQ(card(b.w, 5)->tone(), QStringLiteral("plain"));

    // MUTATION: the receiver falls out of alignment. The lit card moves up to
    // it and SITE NOISE goes dim -- a step behind an unmet one is not next.
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(with(kDiversityStatusWithKinds, "\"aligned\": true", "\"aligned\": false"),
                     governor(true))};
    tick(b.a);
    CHECK_EQ(card(b.w, 1)->tone(), QStringLiteral("lit"));
    CHECK_EQ(card(b.w, 2)->tone(), QStringLiteral("dim"));
    CHECK_EQ(card(b.w, 3)->tone(), QStringLiteral("plain"));
}

// A step that is behind you still says what it bought you: the two "gives"
// lines and the "when" line are drawn on every card in every state.
void everyCardShowsItsGivesAndWhenInEveryState()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    QStringList live;
    for (int i = 0; i < DiversitySessionModel::StepCount; ++i) {
        const SessionCopy copy = sessionStepCopy(i);
        const QString want =
            QStringList{copy.gives[0], copy.gives[1], copy.when}.join(QStringLiteral("\n"));
        CHECK_EQ(cardBody(b.w, i + 1)->text(), want);
        CHECK(cardBody(b.w, i + 1)->text().count(QLatin1Char('\n')) == 2);
        CHECK(!cardBody(b.w, i + 1)->wordWrap());
        live << want;
    }
    // Real prose, not five empty joins.
    CHECK(cardBody(b.w, 1)->text().contains(
        QStringLiteral("Nothing below this reads true until it is.")));
    CHECK_EQ(cardState(b.w, 2)->text(), QStringLiteral("2 findings with a button"));

    // MUTATION: the gate stops answering. Every state goes to a dash and the
    // copy does not move -- what a step gives you is not a reading.
    b.net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                  QByteArray(R"({"available": false})")};
    tick(b.a);
    for (int i = 0; i < DiversitySessionModel::StepCount; ++i) {
        CHECK_EQ(cardBody(b.w, i + 1)->text(), live.at(i));
        CHECK_EQ(cardState(b.w, i + 1)->text(), QStringLiteral("—"));
        CHECK_EQ(card(b.w, i + 1)->tone(), QStringLiteral("dim"));
        CHECK(!cardCure(b.w, i + 1)->isVisibleTo(b.w));
    }
}

// A cure is one query through the door the window already had -- never two,
// and never a query the card composed itself.
void cardCureSendsExactlyOneQuery()
{
    Bench b(withGovernor(with(kDiversityStatusWithKinds, "\"mode\": \"track\"",
                              "\"mode\": \"off\""),
                         governor(true)));
    REQUIRE(b.w != nullptr);
    CHECK_EQ(cardCure(b.w, 1)->text(), QStringLiteral("TRACK"));
    CHECK(cardCure(b.w, 1)->isVisibleTo(b.w));
    int mark = b.net.log.size();
    cardCure(b.w, 1)->click();
    settle();
    CHECK_EQ(requestsTo(b.net, QStringLiteral("/diversity/set"), mark)
                 .join(QStringLiteral("|")),
             QStringLiteral("/diversity/set?mode=track"));

    // MUTATION: the same card, a different fault. One query again, and the
    // gate's own key -- a button that had memorised "mode=track" would send it
    // here too.
    b.net.routes[QStringLiteral("/diversity")] = {
        QNetworkReply::NoError,
        withGovernor(with(kDiversityStatusWithKinds, "\"source\": \"combined\"",
                          "\"source\": \"a\""),
                     governor(true))};
    tick(b.a);
    CHECK_EQ(cardCure(b.w, 1)->text(), QStringLiteral("HEAR OUT"));
    mark = b.net.log.size();
    cardCure(b.w, 1)->click();
    settle();
    CHECK_EQ(requestsTo(b.net, QStringLiteral("/diversity/set"), mark)
                 .join(QStringLiteral("|")),
             QStringLiteral("/diversity/set?source=combined"));
}

// BAND's cure is GO, and GO is a page change. A beacon check costs three
// minutes off the station; nothing on this page may start one.
void beaconCardNeverStartsACheck()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    b.net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                          kDiversityBeacons};
    b.w->setActiveSliceHz(14'100'000.0);
    b.w->applyBeacons(QJsonDocument::fromJson(kDiversityBeacons).object());
    settle();
    CHECK_EQ(cardState(b.w, 3)->text(), QStringLiteral("nothing measured on 20 m yet"));
    CHECK_EQ(cardCure(b.w, 3)->text(), QStringLiteral("GO"));

    const int before = writes(b.net);
    cardCure(b.w, 3)->click();
    settle();
    // SITE, where the check's own button is -- and not one write, which is
    // what a started check is made of.
    CHECK(child<QToolButton>(b.w, "diversityWindowPageSite")->isChecked());
    CHECK(!b.w->beaconPollWanted());
    CHECK(writes(b.net) == before);

    // MUTATION: a band measured minutes ago. The step is done, there is no
    // cure to press at all, and still nothing was started.
    b.w->applyBeacons(QJsonDocument::fromJson(makeDiversityBeaconsWithPattern()).object());
    settle();
    CHECK(cardState(b.w, 3)->text().contains(QStringLiteral("3 of 18 heard")));
    CHECK(!cardCure(b.w, 3)->isVisibleTo(b.w));
    CHECK(!b.w->beaconPollWanted());
    CHECK(writes(b.net) == before);
}

// QUICK START is the three writes that make a cold gate listenable, in the
// order the model names them and no other.
void quickStartSendsThreeQueriesInOrder()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    auto* quick = child<QPushButton>(b.w, "diversityWindowQuickStartButton");
    REQUIRE(quick != nullptr);
    CHECK(child<QWidget>(b.w, "diversityWindowSessionOffers")->isAncestorOf(quick));
    int mark = b.net.log.size();
    quick->click();
    settle();
    CHECK_EQ(requestsTo(b.net, QStringLiteral("/diversity/set"), mark)
                 .join(QStringLiteral("|")),
             QStringLiteral("/diversity/set?mode=track|/diversity/set?source=combined|"
                            "/diversity/set?auto=on"));

    // MUTATION: pressed twice. Three more, in the same order -- the button
    // sends a fixed sequence rather than stepping through one query per press.
    mark = b.net.log.size();
    quick->click();
    settle();
    CHECK_EQ(requestsTo(b.net, QStringLiteral("/diversity/set"), mark)
                 .join(QStringLiteral("|")),
             QStringLiteral("/diversity/set?mode=track|/diversity/set?source=combined|"
                            "/diversity/set?auto=on"));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_session_page_test"));
    QApplication app(argc, argv);

    startIsTheFirstTabAndTheDefaultOnFirstOpen();
    openingOnTheRememberedPageOnAReturn();
    theLitCardIsTheFirstNotDone();
    everyCardShowsItsGivesAndWhenInEveryState();
    cardCureSendsExactlyOneQuery();
    beaconCardNeverStartsACheck();
    quickStartSendsThreeQueriesInOrder();

    std::printf("\n%d diversity session page test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
