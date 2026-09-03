// The Diversity window's SITE page, second half: the two things on it that
// WRITE. The noise profile's per-kind action buttons and the station locator.
//
// Same harness as tests/diversity_site_test.cpp -- a real AetherGateApplet in
// front of a fake, socket-free QNetworkAccessManager -- and a sixth binary for
// the reason all six are separate: each is at the 800-line budget AGENTS.md
// asks for, and every window case wants the same fresh, process-wide
// AppSettings start.
//
// What is checked here is mostly NOT a rendered value but the exact query
// string the fake gate saw. Every action on this page is the gate's own,
// quoted back at it verbatim: the window composes none of them, so a button
// that looked right and asked for something else would be invisible in a
// screenshot and fatal on the air. The cases that do assert a rendered value
// carry a mutation beside them -- a second payload where that value differs --
// so none of them can pass against a page that never read the gate at all.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityBeaconPanel.h"
#include "gui/DiversityBeaconPattern.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversityBeaconPanel;
using AetherSDR::DiversityBeaconPattern;
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

const QString kDash = QStringLiteral("—");

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Gate present, diversity live, every route the SITE page reads or writes
// answered. /diversity/set and /filter/notch reply with a status object, which
// is what the real gate does: a write and the read-back after it are one
// request.
void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& status = kDiversityStatusWithKinds,
                 const QByteArray& beacons = kDiversityBeaconsWithPattern)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        beacons};
    net.routes[QStringLiteral("/filter/notch")] = {QNetworkReply::NoError,
                                                   kDiversityFilterStatus};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

// Opens the window and switches it to SITE, which is also what starts the
// /diversity/beacons poll -- there is deliberately no other way in.
DiversityWindow* openOnSite(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    auto* page = w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageSite"));
    if (page)
        page->click();
    settle();
    // One applet poll after the window exists: /diversity is fetched once at
    // connect, before there is a window to feed, and the noise profile lives
    // on that status object.
    tick(a);
    return w;
}

// One more tick of the band poller, without waiting out a second of real time.
void siteTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// One second of a running BEACON CHECK, likewise: waiting out the real 190
// would make this file the slowest test in the tree by two orders of
// magnitude.
void checkTick(DiversityWindow* w)
{
    auto* panel = w->findChild<DiversityBeaconPanel*>();
    if (!panel)
        return;
    QMetaObject::invokeMethod(panel, "checkTick", Qt::DirectConnection);
}

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

QString cell(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->text() : QString();
}

QString cellTip(QTableWidget* t, int row, int col)
{
    QTableWidgetItem* item = t ? t->item(row, col) : nullptr;
    return item ? item->toolTip() : QString();
}

QTableWidget* kinds(DiversityWindow* w)
{
    return child<QTableWidget>(w, "diversityWindowNoiseKindsTable");
}

QPushButton* kindAction(DiversityWindow* w, int row)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowNoiseKindAction%1").arg(row));
}

QString labelText(DiversityWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

// A station that HAS told the gate where it is but has not yet heard anything
// on both loops: the third of the pattern plot's three states, and the only one
// no fixture above can produce.
const QByteArray kBeaconsGridNoPattern = R"({"available": true,
    "band_hz": 14100000.0, "slot": 12, "station_grid": "EM10",
    "now": null, "results": [], "last": null,
    "propagation": [], "pattern": []})";

// (a) One row per finding, carrying the gate's own label, detail, measurement
// window and size. Six rows in, six rows out, in the gate's order.
void testKindRowsRenderTheGatesOwnVerdicts()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QTableWidget* t = kinds(w);
    CHECK(t != nullptr);
    if (!t)
        return;
    CHECK(t->rowCount() == 6);
    CHECK(cell(t, 0, 0) == QStringLiteral("MAINS"));
    CHECK(cell(t, 0, 1) == QStringLiteral("Mains hum · 60 Hz grid"));
    CHECK(cell(t, 0, 2) == QStringLiteral("120 Hz comb, 2 harmonics"));
    CHECK(cell(t, 0, 3) == QStringLiteral("over 2 s"));
    CHECK(cell(t, 0, 4) == QStringLiteral("22.0"));
    // The impulse detector runs over a longer window than the rest, and the
    // row says so rather than the panel assuming one window for the page.
    CHECK(cell(t, 1, 0) == QStringLiteral("IMPULSE"));
    CHECK(cell(t, 1, 1) == QStringLiteral("Impulses · 1475.1/s"));
    CHECK(cell(t, 1, 3) == QStringLiteral("over 4 s"));
    CHECK(cell(t, 1, 4) == QStringLiteral("14.8"));
    CHECK(cell(t, 2, 0) == QStringLiteral("PERIODIC"));
    CHECK(cell(t, 5, 0) == QStringLiteral("TONE"));
    CHECK(cell(t, 5, 1) == QStringLiteral("Tone · 1240 Hz"));
    CHECK(cell(t, 5, 4) == QStringLiteral("31.0"));
    // The detail is the gate's sentence and can outrun the column, so the
    // whole of it is on the hover.
    CHECK(cellTip(t, 2, 2)
          == QStringLiteral("a modulation rate of the noise, not a tone in the "
                            "audio"));

    // MUTATION: a gate old enough to have no "kinds" at all empties the table.
    // Without this the case would pass on a page that rendered six rows of
    // anything.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithSite};
    tick(a);
    CHECK(t->rowCount() == 0);
    closedToStart();
}

// (b) One button per row: the action's own label when there is one, a dash
// when there is not -- and the gate's reason on the dash's hover, because
// "there is nothing to do about this" is a finding too.
void testActionButtonLabelEnabledStateAndWhy()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QPushButton* mains = kindAction(w, 0);
    QPushButton* impulse = kindAction(w, 1);
    QPushButton* tone = kindAction(w, 5);
    CHECK(mains != nullptr && impulse != nullptr && tone != nullptr);
    if (!mains || !impulse || !tone)
        return;

    CHECK(mains->text() == kDash);
    CHECK(!mains->isEnabled());
    CHECK(mains->toolTip()
          == QStringLiteral("not directional enough to null (coherence 0.14)"));
    CHECK(impulse->text() == QStringLiteral("BLANK"));
    CHECK(impulse->isEnabled());
    CHECK(impulse->toolTip().contains(QStringLiteral("/diversity/set")));
    CHECK(tone->text() == QStringLiteral("NOTCH"));
    CHECK(tone->isEnabled());

    // MUTATION: the same two rows on a site where both actions ARE in force
    // read the other way round -- a different label on each, and the mains row
    // now HAS an action.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusKindsActive};
    tick(a);
    CHECK(kindAction(w, 0)->text() == QStringLiteral("NULLED"));
    CHECK(kindAction(w, 0)->isEnabled());
    CHECK(kindAction(w, 1)->text() == QStringLiteral("UNBLANK"));
    closedToStart();
}

// (c) A row whose action the gate says is in force is LIT. The state is the
// gate's: nothing here checks a button because it was pressed.
void testTheActiveRowIsHighlighted()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityStatusKindsActive);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(kindAction(w, 0) != nullptr && kindAction(w, 0)->isChecked());
    CHECK(kindAction(w, 1) != nullptr && kindAction(w, 1)->isChecked());

    // MUTATION: the same two kinds with active false are not lit, so the check
    // is reading the flag rather than lighting every row that has an action.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusWithKinds};
    tick(a);
    CHECK(!kindAction(w, 1)->isChecked());
    closedToStart();
}

// (d) The whole point of the page. BLANK sends the gate's own query verbatim,
// character for character, on the gate's own route.
void testBlankSendsTheGatesOwnQuery()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    kindAction(w, 1)->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?nb=on&nb_db=12"));

    // MUTATION: a disabled row sends nothing at all. A page that fired on
    // every click would pass the assertion above and be wrong here.
    const int before = net.log.size();
    kindAction(w, 0)->click();
    settle();
    CHECK(net.log.size() == before);
    closedToStart();
}

// (e) The same door reaches a different route entirely. Nothing between the
// button and the wire knows that /filter/notch is not /diversity/set, which is
// what lets a gate grow a new kind of action without a new build here.
void testNotchSendsOnTheFilterRoute()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    kindAction(w, 5)->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
          == QStringLiteral("/filter/notch?add=1240&width=160"));
    // MUTATION: it did NOT go out as a diversity set. The two routes are one
    // signal, and a panel that hardcoded the diversity one would still light
    // up the assertion above through the log's own prefix search.
    CHECK(!lastRequest(net, QStringLiteral("/diversity/set"))
               .contains(QStringLiteral("add=1240")));
    closedToStart();
}

// (f) A refusal is shown by the panel that caused it, in the gate's own words,
// and no row moves.
void testGateRefusalShowsOnTheNoiseStatusLine()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(labelText(w, "diversityWindowNoiseActionStatusLabel").isEmpty());

    net.routes[QStringLiteral("/diversity/set")] = {
        QNetworkReply::NoError, R"({"error": "the blanker is not available"})"};
    kindAction(w, 1)->click();
    settle();
    CHECK(labelText(w, "diversityWindowNoiseActionStatusLabel")
          == QStringLiteral("the blanker is not available"));
    // MUTATION: the beacon panel did not also announce somebody else's
    // refusal. Each panel shows only the reply to its own write.
    CHECK(labelText(w, "diversityWindowBeaconStatusLabel").isEmpty());
    closedToStart();
}

// (g) The locator: SET sends what was typed, FORGET sends "off", and the hint
// says which of the two states the gate is in.
void testGridSetAndForgetSendExactQueries()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* edit = child<QLineEdit>(w, "diversityWindowBeaconGridEdit");
    CHECK(edit != nullptr);
    if (!edit)
        return;
    // The gate's own locator is in the box, because the poll put it there.
    CHECK(edit->text() == QStringLiteral("EM10"));
    CHECK(labelText(w, "diversityWindowBeaconGridHint")
          == QStringLiteral("set: EM10"));

    edit->setText(QStringLiteral("em10bk"));
    child<QPushButton>(w, "diversityWindowBeaconGridSet")->click();
    settle();
    // Case-insensitive on the way in, canonical on the way out.
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?grid=EM10BK"));

    child<QPushButton>(w, "diversityWindowBeaconGridForget")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?grid=off"));

    // MUTATION: a gate with no locator says so instead, so the hint is reading
    // the payload rather than printing a fixed sentence.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsNoGrid};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconGridHint")
          == QStringLiteral("not set — bearings need it"));
    closedToStart();
}

// (h) A locator the gate will not parse comes back as an error body, and the
// beacon panel says it in the gate's words.
void testBadLocatorShowsOnTheBeaconStatusLine()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError,
                                                    kDiversityBadGrid};
    child<QLineEdit>(w, "diversityWindowBeaconGridEdit")
        ->setText(QStringLiteral("ZZ99"));
    child<QPushButton>(w, "diversityWindowBeaconGridSet")->click();
    settle();
    CHECK(labelText(w, "diversityWindowBeaconStatusLabel")
          == QStringLiteral("not a Maidenhead locator: 'ZZ99'"));
    // MUTATION: the noise panel stayed quiet.
    CHECK(labelText(w, "diversityWindowNoiseActionStatusLabel").isEmpty());
    closedToStart();
}

// (i) The three columns the locator made possible, and the mean on the SNR
// cell's hover. Rows are in SCHEDULE order, so W6WX is row 2 whether or not it
// was heard.
void testBearingDistanceAndHeardColumns()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* t = child<QTableWidget>(w, "diversityWindowBeaconTable");
    CHECK(t != nullptr);
    if (!t)
        return;
    CHECK(cell(t, 2, 11) == QStringLiteral("295"));
    CHECK(cell(t, 2, 12) == QStringLiteral("2405"));
    CHECK(cell(t, 2, 13) == QStringLiteral("3/7"));
    CHECK(cell(t, 3, 11) == QStringLiteral("264"));
    CHECK(cell(t, 3, 12) == QStringLiteral("6108"));
    CHECK(cell(t, 3, 13) == QStringLiteral("2/9"));
    // Heard zero times out of six is a measurement, not a blank: the path was
    // sampled and the beacon was not there.
    CHECK(cell(t, 13, 13) == QStringLiteral("0/6"));
    // A result from another band is not this band's row.
    CHECK(cell(t, 4, 11) == kDash);
    CHECK(cellTip(t, 2, 3) == QStringLiteral("mean over 7 pass(es): +9.4 dB"));
    CHECK(cell(t, 2, 3) == QStringLiteral("+12.0"));

    // MUTATION: without a locator the gate sends null for both and the columns
    // dash -- while the heard count, which needs no locator, stays.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsNoGrid};
    siteTick(a);
    CHECK(cell(t, 2, 11) == kDash);
    CHECK(cell(t, 2, 12) == kDash);
    CHECK(cell(t, 2, 13) == QStringLiteral("3/7"));
    closedToStart();
}

// (j) One sentence per band the gate has sampled, in band order, with the
// weakest power step that made it -- which is the whole reason the beacon
// project is an instrument rather than a signal report.
void testPropagationLines()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    const QStringList lines =
        labelText(w, "diversityWindowBeaconPropagationLabel").split(QChar('\n'));
    CHECK(lines.size() == 2);
    if (lines.size() != 2)
        return;
    CHECK(lines.at(0)
          == QStringLiteral("20 m · 3 of 18 heard · weakest 1 W · median −3.3 dB "
                            "· 4 min ago"));
    CHECK(lines.at(1)
          == QStringLiteral("15 m · 1 of 18 heard · weakest 0.1 W · median +4.0 dB "
                            "· 1 h ago"));

    // MUTATION: a band with no power step and no median prints neither rather
    // than printing a zero for both.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsNoGrid};
    siteTick(a);
    CHECK(labelText(w, "diversityWindowBeaconPropagationLabel")
          == QStringLiteral("20 m · 3 of 18 heard"));
    closedToStart();
}

// (k) The dial, and its two different emptinesses: one is four characters of
// typing away, the other is time on the air, and saying so is the difference
// between a hint and a picture that pretends to be a measurement.
void testPatternPlotPointsAndEmptyStates()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* plot = child<DiversityBeaconPattern>(w, "diversityWindowBeaconPattern");
    CHECK(plot != nullptr);
    if (!plot)
        return;
    CHECK(plot->pointCount() == 3);
    CHECK(plot->emptyText().isEmpty());

    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kBeaconsGridNoPattern};
    siteTick(a);
    CHECK(plot->pointCount() == 0);
    CHECK(plot->emptyText() == QStringLiteral("no beacons heard on both loops yet"));

    // MUTATION: the same empty pattern with no locator behind it says the other
    // thing, so the text is reading the grid rather than the point count alone.
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeaconsNoGrid};
    siteTick(a);
    CHECK(plot->pointCount() == 0);
    CHECK(plot->emptyText() == QStringLiteral("needs the station grid"));
    closedToStart();
}

// (l) BEACON CHECK: tune away, count down, come home. The trip home is the
// part that matters -- a check that could not make it is worse than no check.
void testBeaconCheckTunesAwayAndComesBack()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QList<double> tuned;
    QObject::connect(a.diversityPanel(), &AetherGateDiversityPanel::requestTune,
                     [&tuned](double hz) { tuned << hz; });

    // Without a slice frequency the check refuses to start rather than tuning
    // away with no way back.
    child<QPushButton>(w, "diversityWindowBeaconCheck0")->click();
    settle();
    CHECK(tuned.isEmpty());
    CHECK(labelText(w, "diversityWindowBeaconStatusLabel")
          == QStringLiteral("no slice to tune — nowhere to come back to"));

    a.diversityPanel()->setActiveSliceHz(7200000.0);
    child<QPushButton>(w, "diversityWindowBeaconCheck0")->click();
    CHECK(tuned.size() == 1);
    if (tuned.isEmpty())
        return;
    CHECK(qFuzzyCompare(tuned.at(0), 14100000.0));
    CHECK(labelText(w, "diversityWindowBeaconCheckLine")
          == QStringLiteral("CHECK 20 m · 3:10 left"));
    CHECK(child<QPushButton>(w, "diversityWindowBeaconCheckCancel")->isEnabled());

    // MUTATION: the countdown is a countdown, not a fixed string.
    checkTick(w);
    CHECK(labelText(w, "diversityWindowBeaconCheckLine")
          == QStringLiteral("CHECK 20 m · 3:09 left"));

    // A poll while the check runs must not remember the beacon frequency as
    // home: the radio is on it BECAUSE of this panel.
    a.diversityPanel()->setActiveSliceHz(14100000.0);
    child<QPushButton>(w, "diversityWindowBeaconCheckCancel")->click();
    CHECK(tuned.size() == 2);
    CHECK(qFuzzyCompare(tuned.at(1), 7200000.0));
    CHECK(!child<QPushButton>(w, "diversityWindowBeaconCheckCancel")->isEnabled());
    CHECK(labelText(w, "diversityWindowBeaconCheckLine")
              .startsWith(QStringLiteral("idle")));
    closedToStart();
}

// (m) A check that reaches the end of its cycle comes home by itself, and one
// left running when the window closes comes home too.
void testBeaconCheckEndsOnTimeAndOnClose()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    QList<double> tuned;
    QObject::connect(a.diversityPanel(), &AetherGateDiversityPanel::requestTune,
                     [&tuned](double hz) { tuned << hz; });
    a.diversityPanel()->setActiveSliceHz(7200000.0);
    child<QPushButton>(w, "diversityWindowBeaconCheck2")->click();
    CHECK(tuned.size() == 1);
    if (tuned.isEmpty())
        return;
    CHECK(qFuzzyCompare(tuned.at(0), 21150000.0));

    for (int i = 0; i < 189; ++i)
        checkTick(w);
    CHECK(tuned.size() == 1);   // 1 s left: still out there
    checkTick(w);
    CHECK(tuned.size() == 2);
    CHECK(qFuzzyCompare(tuned.at(1), 7200000.0));

    // And the same trip home when the operator closes the window on a running
    // one, which is the case a hideEvent could not have covered: a page switch
    // hides the page too and must NOT cancel.
    child<QPushButton>(w, "diversityWindowBeaconCheck2")->click();
    CHECK(tuned.size() == 3);
    w->close();
    settle();
    CHECK(tuned.size() == 4);
    CHECK(qFuzzyCompare(tuned.at(3), 7200000.0));
    closedToStart();
}

// (n) The page still fits the window it opens at, with everything on it drawn.
void testNothingScrollsOnTheSitePageAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = child<QScrollArea>(w, "diversityWindowSiteScroll");
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    CHECK(w->minimumSizeHint().width() <= 1120);

    // MUTATION: the widest content the page can hold -- six kind rows with the
    // longest detail the gate writes, a refusal on both status lines and a
    // running check. If any of those labels were word-wrapped they are
    // height-for-width, and this is where the scrollbar would appear.
    net.routes[QStringLiteral("/diversity/set")] = {
        QNetworkReply::NoError,
        R"({"error": "the blanker is not available on this gate build"})"};
    kindAction(w, 1)->click();
    a.diversityPanel()->setActiveSliceHz(7200000.0);
    child<QPushButton>(w, "diversityWindowBeaconCheck0")->click();
    settle();
    w->grab();
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    w->close();
    settle();
    closedToStart();
}

// (j) The antenna note: free text the gate cannot check, filed with the site
// log so a beacon sweep read a week later can be read against the switch
// positions it was taken with. It writes on Enter, clears with "off", and a
// poll's check-back must never turn into a write of its own.
void testAntennaNoteWritesAndReadsBack()
{
    closedToStart();
    FakeGate net;
    QByteArray noted = kDiversityStatusWithKinds;
    noted.replace("{\"available\": true",
                  "{\"sitelog\": {\"path\": \"/tmp/site.jsonl\", \"written\": 12, "
                  "\"skipped\": 0, \"error\": null, \"antenna\": \"K-480WLA pair, SW both\"}, "
                  "\"available\": true");
    CHECK(noted != kDiversityStatusWithKinds);
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, noted);
    DiversityWindow* w = openOnSite(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* edit = child<QLineEdit>(w, "diversityWindowSiteAntennaEdit");
    CHECK(edit != nullptr);
    if (!edit)
        return;
    // The gate's own note is in the box, and eighty characters is the whole
    // room there is -- the gate refuses more.
    CHECK(edit->text() == QStringLiteral("K-480WLA pair, SW both"));
    CHECK(edit->maxLength() == 80);

    // A check-back that agrees with the box is not a write. This is the whole
    // hold rule: the poll runs four times a second and every one of them would
    // otherwise be a /diversity/set.
    const int before = net.count(QStringLiteral("/diversity/set"));
    tick(a);
    siteTick(a);
    CHECK(net.count(QStringLiteral("/diversity/set")) == before);

    // Typed and entered: the gate is told, verbatim.
    edit->setText(QStringLiteral("loop A only, gain noon"));
    QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection);
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?antenna=loop A only, gain noon"));

    // MUTATION: emptied, which is the gate's "forget it" rather than a note
    // that is the empty string.
    edit->setText(QString());
    QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection);
    settle();
    CHECK(lastRequest(net, QStringLiteral("/diversity/set"))
          == QStringLiteral("/diversity/set?antenna=off"));
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_site_actions_test"));
    QApplication app(argc, argv);

    testKindRowsRenderTheGatesOwnVerdicts();
    testActionButtonLabelEnabledStateAndWhy();
    testTheActiveRowIsHighlighted();
    testBlankSendsTheGatesOwnQuery();
    testNotchSendsOnTheFilterRoute();
    testGateRefusalShowsOnTheNoiseStatusLine();
    testGridSetAndForgetSendExactQueries();
    testBadLocatorShowsOnTheBeaconStatusLine();
    testBearingDistanceAndHeardColumns();
    testPropagationLines();
    testPatternPlotPointsAndEmptyStates();
    testBeaconCheckTunesAwayAndComesBack();
    testBeaconCheckEndsOnTimeAndOnClose();
    testAntennaNoteWritesAndReadsBack();
    testNothingScrollsOnTheSitePageAtTheInitialSize();

    std::printf("\n%d diversity site action test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
