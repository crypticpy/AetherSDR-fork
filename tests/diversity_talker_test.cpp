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
#include "gui/DiversityAge.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::diversityAgeSince;
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

// The FILTER-page tests this file used to open with -- PER TALKER, the state
// line's "whose filter" clause, AUTO CONTOUR, and APF's CW label -- moved to
// the gate's own CHAIN window with the controls themselves; see
// AetherGateChainStrip.cpp's tests for their successors. What is left here is
// everything that reads /diversity's memory[] and talker fields directly,
// independent of that page: the TALKERS table, the voice-split event, the
// FLOW line's own clause, and the FINDER's snapped estimate.

// (a) The TALKERS table's Filter column: the gate's remembered filter per
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

// (a2) The same Filter column, plus "learned N ago" when the gate says when
// it learned the filter (AGENTS.md, "Keep what the station learned"): the
// filter is a stored measurement like any other remembered value in this
// window, so it carries its own age. #4's filter is null and #7 carries no
// learned_at at all -- both must read exactly as they did before this clause
// existed, since an older gate sends neither key.
void testTalkersTableFilterColumnShowsWhenItWasLearned()
{
    closedToStart();
    // Built at test-run time, 125 s back, rather than frozen into a literal
    // that would drift into a different AGE band on a later run.
    const qint64 learnedAt = QDateTime::currentSecsSinceEpoch() - 125;
    QByteArray status = kDiversityStatusTalkerFilters;
    status.replace("\"shape\": \"soft\", \"auto\": true,\n"
                   "                    \"auto_eq\": false, \"contour\": false, "
                   "\"threshold_db\": 20.0,\n"
                   "                    \"live\": true}},",
                   "\"shape\": \"soft\", \"auto\": true,\n"
                   "                    \"auto_eq\": false, \"contour\": false, "
                   "\"threshold_db\": 20.0,\n"
                   "                    \"live\": true, \"learned_at\": "
                       + QByteArray::number(learnedAt) + "}},");
    CHECK(status != kDiversityStatusTalkerFilters);

    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFilterTalkerAuto, status);
    DiversityWindow* w = openWindow(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* t = child<QTableWidget>(w, "diversityWindowTalkersTable");
    CHECK(t != nullptr);
    if (!t)
        return;
    const int filterCol = t->columnCount() - 2;

    const QString age =
        diversityAgeSince(learnedAt, QDateTime::currentSecsSinceEpoch());
    // #3 (Ted): the same cell as before, plus the age clause on the end.
    CHECK(cell(t, 0, filterCol)
          == QStringLiteral("● 300–2700 soft auto · learned ") + age);
    // #4: no filter at all, still a dash.
    CHECK(cell(t, 1, filterCol) == kDash);
    // #7: a filter, but no learned_at -- exactly the pre-existing wording.
    CHECK(cell(t, 2, filterCol) == QStringLiteral("200–3000 sharp eq contour"));

    w->close();
    settle();
    closedToStart();
}

// (b) The voice split, said out loud. It is the one talker change the operator
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

// (c) was the FLOW line's last step, which quoted whose filter was in force
// and what the automatic contour had settled on. That line is gone (Phase 3a
// WP-B): the footer says ONE step now, and the per-talker filter is the
// STATION card's own state on the START page -- covered by
// tests/diversity_session_page_test.cpp, against the same /filter fixtures.

// (d) The FINDER shows the number you can tune to, and says on hover what it
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

    testTalkersTableShowsTheRememberedFilter();
    testTalkersTableFilterColumnShowsWhenItWasLearned();
    testVoiceSplitIsLogged();
    testFinderShowsTheSnappedFrequencyAndTheEstimate();

    std::printf("\n%d diversity talker test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
