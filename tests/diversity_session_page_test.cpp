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

#include "DiversityGateFixture.h"
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
using AetherSDR::DiversityNextStrip;
using AetherSDR::DiversitySessionCard;
using AetherSDR::DiversitySessionModel;
using AetherSDR::DiversityWindow;
using AetherSDR::SessionCopy;
using AetherSDR::sessionStepCopy;

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

const char* const kPageKey = "DiversityWindowPage";
const char* const kCollapsedKey = "DiversityNextStripCollapsed";
const char* const kPageButtons[] = {
    "diversityWindowPageStart", "diversityWindowPageSlice", "diversityWindowPageBand",
    "diversityWindowPageSite", "diversityWindowPageFilter"};

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// What a station that has never opened this window has. Written rather than
// assumed: these cases share one AppSettings and two are about what it keeps.
void forgetEverything()
{
    closedToStart();
    AppSettings::instance().setValue(QLatin1String(kPageKey), QString());
    AppSettings::instance().setValue(QLatin1String(kCollapsedKey), QStringLiteral("True"));
}

// A copy of a fixture with one wire value swapped: what each case needs is the
// SAME site with one thing different, and a fixture that differed in more than
// the field under test could not prove which field the page read.
QByteArray with(const QByteArray& body, const char* from, const char* to)
{
    QByteArray out = body;
    out.replace(from, to);
    return out;
}

// The governor block, docs/DIVERSITY.md's own shape. Every cure below depends
// on it: the model clears every cure model-wide while AUTO CLEAN is off.
QJsonObject governor(bool autoOn, const QString& state = QStringLiteral("watching"),
                     const QString& why = QString())
{
    QJsonObject g;
    g.insert(QStringLiteral("available"), true);
    g.insert(QStringLiteral("auto"), autoOn);
    g.insert(QStringLiteral("state"), state);
    g.insert(QStringLiteral("why"), why);
    g.insert(QStringLiteral("settle_s"), 5.0);
    g.insert(QStringLiteral("margin_db"), 1.0);
    g.insert(QStringLiteral("spread_db"), 2.0);
    g.insert(QStringLiteral("holding"), QJsonArray());
    g.insert(QStringLiteral("pending"), QJsonValue());
    g.insert(QStringLiteral("events"), QJsonArray());
    g.insert(QStringLiteral("backoff"), QJsonArray());
    g.insert(QStringLiteral("error"), QString());
    return g;
}

QByteArray withGovernor(const QByteArray& body, const QJsonObject& gov)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    root.insert(QStringLiteral("governor"), gov);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// The site every case not about the governor runs on: two findings with a
// button, AUTO CLEAN on so the cures exist at all.
QByteArray kindsAuto(bool autoOn = true)
{
    return withGovernor(kDiversityStatusWithKinds, governor(autoOn));
}

// /filter with a talker locked in -- the one thing that makes the STATION step
// done, and the only field of that payload this page reads.
QJsonObject filterWithTalker(int id)
{
    QJsonObject root = QJsonDocument::fromJson(kDiversityFilterStatus).object();
    QJsonObject talker;
    talker.insert(QStringLiteral("enabled"), true);
    talker.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("talker"), talker);
    return root;
}

// Every chore behind us: aligned and tracking, a clean site, no beacon
// frequency in the span (the tuned Hz stays 0), and a talker to be given a
// filter. What the collapsed footer is for.
const QByteArray kAllChoresDone = R"JSON({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 1.2}, "updates": 42,
    "talking": true, "talker": {"id": 7, "since_s": 30.0},
    "memory": [{"id": 7, "name": "Ann"}, {"id": 8, "name": "Bo"},
               {"id": 9, "name": "Cy"}, {"id": 10, "name": "Di"}],
    "noise_profile": {"mains_hz": 60.0, "hum_db": 3.0, "harmonics": 0,
                      "impulses_per_s": 0.0, "impulse_db": 0.0, "periodic": [],
                      "seconds": 2.0, "window_s": 2.0, "impulse_window_s": 4.0,
                      "kinds": []},
    "capture": {"active": false, "path": null}})JSON";

const QByteArray kDigIdle = R"({"available": true, "running": false,
    "phase": "idle", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 0.0, "steps": [], "best": {}, "changed": {}})";

const QByteArray kDigRunning = R"({"available": true, "running": true,
    "phase": "searching", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 2.1, "elapsed_s": 72.0, "seconds": 180.0, "remaining_s": 108.0,
    "trials_planned": 24, "trials_done": 9, "steps": [],
    "best": {"post": "v2"}, "changed": {"post": "v2"}})";

const QByteArray kDigDone = R"({"available": true, "running": false,
    "phase": "done", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 4.1, "objective_before": -3.2, "objective_after": 0.9,
    "steps": [{"knob": "nb", "kept": true}],
    "best": {"nb_db": 11.0}, "changed": {"nb_db": 11.0}})";

void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& status)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/align")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, kDiversityFilterStatus};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

// One applet, one fake gate, one window, opened and torn down the same way
// every time. `fresh` is false only where the case is ABOUT what the window
// remembers across a session -- which forgetEverything() would throw away.
struct Bench {
    FakeGate net;
    AetherGateApplet a{nullptr, &net};
    DiversityWindow* w{nullptr};

    explicit Bench(const QByteArray& status, bool fresh = true)
    {
        if (fresh) {
            forgetEverything();
        } else {
            closedToStart();
        }
        connectGate(a, net, status);
        a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"))->click();
        settle();
        w = a.diversityPanel()->window();
        if (w)
            tick(a);   // /diversity is fetched once before there is a window to feed
    }
    ~Bench()
    {
        if (w)
            w->close();
        settle();
        closedToStart();
    }
};

// Makes a timer go off now: QTimer::timeout carries a QPrivateSignal, but moc
// strips that from the meta-method, and the point is to skip real seconds.
void fire(QTimer* timer)
{
    if (!timer)
        return;
    if (timer->isSingleShot())
        timer->stop();
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
}

// One /diversity/dig poll, without waiting out the window's own cadence.
void digTick(Bench& b, const QByteArray& body)
{
    b.net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, body};
    fire(child<QTimer>(b.w, "diversityWindowDigTimer"));
    settle();
}

DiversitySessionCard* card(DiversityWindow* w, int index)
{
    return w->findChild<DiversitySessionCard*>(
        QStringLiteral("diversityWindowSessionCard%1").arg(index));
}

QLabel* cardBody(DiversityWindow* w, int index)
{
    return w->findChild<QLabel*>(
        QStringLiteral("diversityWindowSessionCard%1Body").arg(index));
}

QLabel* cardState(DiversityWindow* w, int index)
{
    return w->findChild<QLabel*>(
        QStringLiteral("diversityWindowSessionCard%1State").arg(index));
}

QPushButton* cardCure(DiversityWindow* w, int index)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowSessionCard%1Cure").arg(index));
}

DiversityNextStrip* strip(DiversityWindow* w)
{
    return w->findChild<DiversityNextStrip*>(QStringLiteral("diversityWindowNextStrip"));
}

QString nextLine(DiversityWindow* w)
{
    DiversityNextStrip* s = strip(w);
    return s ? s->lineText() : QString();
}

// Every request to one route since `from`, in the order the gate saw them.
QStringList requestsTo(const FakeGate& net, const QString& prefix, int from)
{
    QStringList out;
    for (int i = from; i < net.log.size(); ++i) {
        if (net.log.at(i).startsWith(prefix))
            out << net.log.at(i);
    }
    return out;
}

// The three write routes. A page change is not one of them.
int writes(const FakeGate& net)
{
    return net.count(QStringLiteral("/diversity/set"))
           + net.count(QStringLiteral("/diversity/align"))
           + net.count(QStringLiteral("/diversity/dig"));
}

// --- (a) The tab row ---------------------------------------------------------------

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
// row every page keeps on screen.
void digStopStaysVisibleOnEveryPageWhileARunIsOut()
{
    Bench b(kindsAuto());
    REQUIRE(b.w != nullptr);
    digTick(b, kDigRunning);
    auto* stop = child<QPushButton>(b.w, "diversityWindowFlowStripDigStopButton");
    REQUIRE(stop != nullptr);
    for (const char* page : kPageButtons) {
        child<QToolButton>(b.w, page)->click();
        settle();
        CHECK(stop->isVisibleTo(b.w));
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
    TestSettingsProfile profile(QStringLiteral("diversity_session_page_test"));
    QApplication app(argc, argv);

    startIsTheFirstTabAndTheDefaultOnFirstOpen();
    openingOnTheRememberedPageOnAReturn();
    theLitCardIsTheFirstNotDone();
    everyCardShowsItsGivesAndWhenInEveryState();
    cardCureSendsExactlyOneQuery();
    beaconCardNeverStartsACheck();
    quickStartSendsThreeQueriesInOrder();
    nextStripSaysOneStepAndOffersOneButton();
    manualModeStripHasNoButton();
    autoCleanSwitchTextIsOnlyTwoStrings();
    digStopStaysVisibleOnEveryPageWhileARunIsOut();
    verdictRowAppearsOnTheFooterNotOnlyOnStart();
    collapsedStripIsOneLineAndExpandsOnClick();
    nothingScrollsOnTheStartPageAtTheInitialSize();
    startPageMinimumWidthUnder1120();

    std::printf("\n%d diversity session page test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
