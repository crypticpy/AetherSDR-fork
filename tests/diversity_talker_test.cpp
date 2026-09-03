// Per-talker filters, the automatic contour, the voice split and the snapped
// finder -- the round the gate opened at 15aca8f/f1d79b6/a72bcf1.
//
// Same harness as the six diversity binaries before it: a real
// AetherGateApplet in front of a fake, socket-free QNetworkAccessManager, the
// window opened through the sidebar button because there is deliberately no
// other way in, and the band poller driven by hand rather than by waiting out
// real seconds.
//
// A seventh binary because the others are at the 800-line budget AGENTS.md
// asks for, and because opening the window writes DiversityWindowVisible into
// the process-wide AppSettings cache -- every case here starts from a known
// closed state for the same reason every case there does.
//
// What is checked is mostly the exact query string the fake gate saw, not a
// rendered value: every control in this round writes to the gate and takes its
// state back from the gate's answer, so a checkbox that looked right and asked
// for the wrong thing would be invisible in a screenshot and audible on the
// air. The cases that DO assert rendered text carry a mutation beside them --
// a second payload where that text differs -- so none of them can pass against
// a page that never read the gate at all.

#include "DiversityTalkerFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
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

// Gate present, diversity live, every route this round touches answered.
// /filter/set replies with the same status a poll returns, which is what the
// real gate does: a write and the read-back after it are one request.
void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& filter = kDiversityFilterTalkerAuto,
                 const QByteArray& status = kDiversityStatusTalkerFilters,
                 const QByteArray& finder = kDiversityFinderSnapped)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                        kDiversitySpatial};
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError, finder};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filter};
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, filter};
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

// One more tick of the band poller, without waiting out half a second of real
// time. The FILTER page's /filter read rides on the same timer.
void bandTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// The window on its opening page. /diversity is fetched once at connect,
// before there is a window to feed it to, so one applet tick follows.
DiversityWindow* openWindow(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (w)
        tick(a);
    return w;
}

// The window on FILTER, which is also what starts the /filter poll.
DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    DiversityWindow* w = openWindow(a);
    if (!w)
        return nullptr;
    child<QToolButton>(w, "diversityWindowPageFilter")->click();
    settle();
    settle();
    return w;
}

// The window on BAND, likewise for /diversity/finder.
DiversityWindow* openOnBand(AetherGateApplet& a)
{
    DiversityWindow* w = openWindow(a);
    if (!w)
        return nullptr;
    child<QToolButton>(w, "diversityWindowPageBand")->click();
    settle();
    settle();
    return w;
}

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

QString labelText(DiversityWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
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

QStringList eventLines(DiversityWindow* w)
{
    QStringList out;
    auto* list = child<QListWidget>(w, "diversityWindowEventsList");
    if (!list)
        return out;
    for (int i = 0; i < list->count(); ++i)
        out << list->item(i)->text();
    return out;
}

bool anyEventContains(DiversityWindow* w, const QString& needle)
{
    for (const QString& line : eventLines(w)) {
        if (line.contains(needle))
            return true;
    }
    return false;
}

// (a) PER TALKER and FAST/SMOOTH are the gate's own state, and pressing either
// sends the gate's own word for it.
void testPerTalkerReflectsAndWritesTheGate()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* check = child<QCheckBox>(w, "diversityWindowFilterTalkerCheck");
    auto* fast = child<QPushButton>(w, "diversityWindowFilterTalkerSnapFast");
    auto* smooth = child<QPushButton>(w, "diversityWindowFilterTalkerSnapSmooth");
    CHECK(check != nullptr && fast != nullptr && smooth != nullptr);
    if (!check || !fast || !smooth)
        return;

    // The fixture has it on and snapping.
    CHECK(check->isChecked());
    CHECK(fast->isChecked());
    CHECK(!smooth->isChecked());

    // Every one of them says the same thing on hover, and it is the sentence
    // that tells the operator what the two words mean.
    CHECK(check->toolTip().contains(QStringLiteral("FAST snaps")));
    CHECK(check->toolTip().contains(QStringLiteral("SMOOTH glides")));

    smooth->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?talker_snap=smooth"));

    check->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?talker=0"));

    // MUTATION: a gate with it off and gliding. The controls follow the gate,
    // not the click -- both were just pressed the other way.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerOff};
    bandTick(a);
    CHECK(!check->isChecked());
    CHECK(smooth->isChecked());
    CHECK(!fast->isChecked());

    w->close();
    settle();
    closedToStart();
}

// (b) The state line names whose filter is in force, by the name /diversity
// gave the id -- and by the number when it gave none.
void testStateLineNamesTheTalker()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(labelText(w, "diversityWindowFilterForceLabel")
              .contains(QStringLiteral("filter: Ted's (#3)")));

    // MUTATION: the same id with no name in memory falls back to the number.
    // Nothing here may invent "unnamed" or leave the clause out.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerNoPrint};
    bandTick(a);
    CHECK(labelText(w, "diversityWindowFilterForceLabel")
              .contains(QStringLiteral("filter: #4")));

    // PER TALKER off: there is no "whose", so the clause goes away rather than
    // saying whose filter it would have been.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerOff};
    bandTick(a);
    CHECK(!labelText(w, "diversityWindowFilterForceLabel")
               .contains(QStringLiteral("filter: ")));

    w->close();
    settle();
    closedToStart();
}

// (c) AUTO CONTOUR: the three spinners are the gate's fitted bell and are not
// the operator's to type while it is fitting.
void testAutoContourShowsTheFitAndLocksTheSpinners()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* autoCheck = child<QCheckBox>(w, "diversityWindowFilterAutoContourCheck");
    auto* hz = child<QSpinBox>(w, "diversityWindowFilterContourHzSpin");
    auto* db = child<QSpinBox>(w, "diversityWindowFilterContourDbSpin");
    auto* width = child<QSpinBox>(w, "diversityWindowFilterContourWidthSpin");
    CHECK(autoCheck != nullptr && hz != nullptr && db != nullptr && width != nullptr);
    if (!autoCheck || !hz || !db || !width)
        return;

    CHECK(autoCheck->isChecked());
    CHECK(hz->value() == 550);
    CHECK(db->value() == -3);
    CHECK(width->value() == 500);
    CHECK(!hz->isEnabled());
    CHECK(!db->isEnabled());
    CHECK(!width->isEnabled());
    CHECK(labelText(w, "diversityWindowFilterContourSourceLabel")
          == QStringLiteral("from print"));

    // MUTATION: fitting, with nothing heard yet. hz and width_hz are null, and
    // a null is not zero -- the caption says so instead of the spinners
    // claiming a 0 Hz bell.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerNoPrint};
    bandTick(a);
    CHECK(autoCheck->isChecked());
    CHECK(labelText(w, "diversityWindowFilterContourSourceLabel")
          == QStringLiteral("no print yet"));

    // ...and the operator's own bell: auto off, spinners back, caption says
    // whose bell it is.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerOff};
    bandTick(a);
    CHECK(!autoCheck->isChecked());
    CHECK(hz->isEnabled());
    CHECK(db->isEnabled());
    CHECK(width->isEnabled());
    CHECK(hz->value() == 700);
    CHECK(labelText(w, "diversityWindowFilterContourSourceLabel")
          == QStringLiteral("manual"));

    w->close();
    settle();
    closedToStart();
}

// (c2) The checkbox itself writes the gate's own word, and typing a value into
// a contour spinner takes the fit off -- the operator has just said where the
// bell goes, so nothing may quietly move it back.
void testAutoContourWritesAndATypedValueTakesItOff()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* autoCheck = child<QCheckBox>(w, "diversityWindowFilterAutoContourCheck");
    auto* hz = child<QSpinBox>(w, "diversityWindowFilterContourHzSpin");
    CHECK(autoCheck != nullptr && hz != nullptr);
    if (!autoCheck || !hz)
        return;

    autoCheck->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?auto_contour=0"));
    // ...and the tick is back on, because this fake gate answers every write
    // with the same fitted payload. That is the right order and not an
    // accident of the fixture: the gate is the source of truth, and a control
    // that stayed where it was clicked would be lying about a refused write.
    CHECK(autoCheck->isChecked());

    // A value the operator committed. The gate turns its own fit off when it
    // sees one -- it answers with source "manual" -- so the only thing the
    // window owes is not to go on showing a tick that is already untrue for
    // the half second until the next poll. No second write goes out.
    net.log.clear();
    hz->setValue(820);
    emit hz->editingFinished();
    // Read before settle(): the tick is what the window does with its OWN
    // knowledge, in the gap before the gate answers.
    CHECK(!autoCheck->isChecked());
    settle();
    QStringList sets;
    for (const QString& line : net.log) {
        if (line.startsWith(QLatin1String("/filter/set")))
            sets << line;
    }
    CHECK(sets == QStringList{QStringLiteral("/filter/set?contour_hz=820")});

    w->close();
    settle();
    closedToStart();
}

// (d) APF says what it is for. The operator's complaint was that it made a
// voice sound like talking into a tiny cup, which is exactly what a CW audio
// peak does to speech -- so the label and the hover say CW.
void testApfSaysItIsForCw()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* apf = child<QCheckBox>(w, "diversityWindowFilterApfCheck");
    CHECK(apf != nullptr);
    if (!apf)
        return;
    CHECK(apf->text() == QStringLiteral("APF (CW)"));
    CHECK(apf->toolTip().contains(QStringLiteral("cup")));

    w->close();
    settle();
    closedToStart();
}

// (e) The TALKERS table's Filter column: the gate's remembered filter per
// station, the live one marked, a dash where there is none, and a number where
// there is no name.
void testTalkersTableShowsTheRememberedFilter()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* t = child<QTableWidget>(w, "diversityWindowTalkersTable");
    CHECK(t != nullptr);
    if (!t)
        return;

    // TX stays the last column -- the Filter column went in before it, not
    // after, because the whole window quotes the TX edge from that position.
    const int filterCol = t->columnCount() - 2;
    const int txCol = t->columnCount() - 1;
    CHECK(t->horizontalHeaderItem(filterCol)->text() == QStringLiteral("Filter"));
    CHECK(t->horizontalHeaderItem(txCol)->text() == QStringLiteral("TX"));

    CHECK(t->rowCount() == 3);
    // #3 is on the air and their filter is the one in force: same filled dot
    // the # column uses.
    CHECK(cell(t, 0, filterCol) == QStringLiteral("● 300–2700 soft auto"));
    // #4 has none. Not "default", which would claim a setting nothing made.
    CHECK(cell(t, 1, filterCol) == kDash);
    // #7's is remembered but not in force, and every switch that is on is
    // named while none that is off is.
    CHECK(cell(t, 2, filterCol) == QStringLiteral("200–3000 sharp eq contour"));
    // The summary outruns the column, so the cell's own hover is that summary
    // rather than the row's voice print.
    CHECK(cellTip(t, 2, filterCol) == QStringLiteral("200–3000 sharp eq contour"));

    // A talker with no name is their number, not a blank cell.
    CHECK(cell(t, 0, 1) == QStringLiteral("Ted"));
    CHECK(cell(t, 1, 1) == QStringLiteral("#4"));

    // MUTATION: the next poll moves the live mark to #7 and takes it off #3.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusVoiceSplit};
    tick(a);
    CHECK(cell(t, 0, filterCol) == QStringLiteral("300–2700 soft auto"));
    CHECK(cell(t, 2, filterCol) == QStringLiteral("● 200–3000 sharp eq contour"));

    w->close();
    settle();
    closedToStart();
}

// (f) The voice split, said out loud. It is the one talker change the operator
// cannot hear happening -- nobody stopped talking and the frequency did not
// move -- so the only evidence is the gate's counter going up.
void testVoiceSplitIsLogged()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    // The first poll after the window opened only seeds the log.
    tick(a);
    CHECK(!anyEventContains(w, QStringLiteral("voice split")));

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityStatusVoiceSplit};
    tick(a);
    CHECK(anyEventContains(w,
                           QStringLiteral("voice split: not Ted's voice → Ann")));

    // MUTATION: a poll where the count did NOT move produces no second line,
    // even though everything else about the payload is the same.
    const int before = eventLines(w).size();
    tick(a);
    CHECK(eventLines(w).size() == before);

    w->close();
    settle();
    closedToStart();
}

// (g) The FLOW strip's last step quotes whose filter is in force and what the
// automatic contour has settled on, because "100-2900 soft" is one fact about
// one station once PER TALKER is on.
void testFlowFilterStepQuotesTheTalkerAndContour()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* step5 = child<QPushButton>(w, "diversityWindowFlowStep5");
    CHECK(step5 != nullptr);
    if (!step5)
        return;
    CHECK(step5->text().contains(QStringLiteral("Ted's filter (#3)")));
    CHECK(step5->text().contains(QStringLiteral("auto contour −3 dB at 550 Hz")));

    // MUTATION: fitting with nothing heard yet says so rather than quoting a
    // bell at 0 Hz, and an id with no name is quoted as its number.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerNoPrint};
    bandTick(a);
    CHECK(step5->text().contains(QStringLiteral("filter #4")));
    CHECK(step5->text().contains(QStringLiteral("auto contour: no print yet")));

    // PER TALKER off with a manual bell: neither clause appears at all.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterTalkerOff};
    bandTick(a);
    CHECK(!step5->text().contains(QStringLiteral("filter #")));
    CHECK(!step5->text().contains(QStringLiteral("auto contour")));

    w->close();
    settle();
    closedToStart();
}

// (h) The FINDER shows the number you can tune to, and says on hover what it
// was rounded from. A row that showed only the estimate would put you 130 Hz
// off the dial mark; one that showed only the snap would hide how sure the
// gate is.
void testFinderShowsTheSnappedFrequencyAndTheEstimate()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnBand(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* t = child<QTableWidget>(w, "diversityWindowFinderTable");
    CHECK(t != nullptr);
    if (!t)
        return;
    CHECK(t->rowCount() == 2);
    CHECK(cell(t, 0, 0) == QStringLiteral("3860.50"));
    CHECK(cellTip(t, 0, 0) == QStringLiteral("estimate 3860.37 kHz"));

    // The second candidate is a gate that sent no estimate. Nothing may be
    // invented for it -- no tooltip at all rather than a repeat of the snap.
    CHECK(cell(t, 1, 0) == QStringLiteral("3861.50"));
    CHECK(cellTip(t, 1, 0).isEmpty());

    w->close();
    settle();
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_talker_test"));
    QApplication app(argc, argv);

    testPerTalkerReflectsAndWritesTheGate();
    testStateLineNamesTheTalker();
    testAutoContourShowsTheFitAndLocksTheSpinners();
    testAutoContourWritesAndATypedValueTakesItOff();
    testApfSaysItIsForCw();
    testTalkersTableShowsTheRememberedFilter();
    testVoiceSplitIsLogged();
    testFlowFilterStepQuotesTheTalkerAndContour();
    testFinderShowsTheSnappedFrequencyAndTheEstimate();

    std::printf("\n%d diversity talker test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
