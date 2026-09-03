// The Diversity window's FILTER page -- the response curve, its two draggable
// edges and the four columns of controls under it -- driven through a real
// AetherGateApplet and socket-free.
//
// Same harness as tests/diversity_window_test.cpp, tests/diversity_band_test
// .cpp and tests/diversity_site_test.cpp, for the same reason: the page owns no
// transport. It is reached by opening the sidebar's window and pressing FILTER,
// it is fed by the applet's DiversityBandPoller, and every control on it leaves
// as a request signal the panel forwards.
//
// This page is the only one of the four whose transport WRITES as well as
// reads, so most of what is checked here is not a rendered value but the exact
// query string the fake gate saw. A control that looks right and asks the gate
// for the wrong thing is the failure mode a screenshot cannot catch.
//
// A fifth binary because the other four are at the 800-line budget AGENTS.md
// asks for, and because opening the window writes DiversityWindowVisible into
// the process-wide AppSettings cache -- every case here starts from a known
// closed state for the same reason every case there does.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>
#include <QUrlQuery>

#include <algorithm>
#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
using AetherSDR::DiversityBandPoller;
using AetherSDR::DiversityFilterPanel;
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

// Brings an applet up to "gate present, diversity live" with every route the
// FILTER page needs answered. /filter/set and /filter/notch reply with the same
// status object a poll returns, which is what the gate does: a write and the
// read-back after it are one request.
void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& filter = kDiversityFilterStatus)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityFull};
    net.routes[QStringLiteral("/diversity/spatial")] = {QNetworkReply::NoError,
                                                        kDiversitySpatial};
    net.routes[QStringLiteral("/diversity/finder")] = {QNetworkReply::NoError,
                                                       kDiversityFinder};
    net.routes[QStringLiteral("/diversity/beacons")] = {QNetworkReply::NoError,
                                                        kDiversityBeacons};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filter};
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError, filter};
    net.routes[QStringLiteral("/filter/notch")] = {QNetworkReply::NoError, filter};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

QToolButton* pageButton(DiversityWindow* w, const char* name)
{
    return w->findChild<QToolButton*>(QString::fromLatin1(name));
}

// One more tick of the band poller, without waiting out half a second of real
// time. The FILTER page's /filter read rides on the same timer.
void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// Opens the window and switches it to FILTER, which is also what starts the
// /filter poll -- there is deliberately no other way in.
DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    pageButton(w, "diversityWindowPageFilter")->click();
    settle();
    settle();
    return w;
}

QString labelText(DiversityWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

// A plain /filter poll logs the path with no query; /filter/set and
// /filter/notch always carry one. startsWith() would conflate the three.
int pollCount(const FakeGate& net)
{
    int n = 0;
    for (const QString& s : net.log) {
        if (s == QLatin1String("/filter"))
            ++n;
    }
    return n;
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

// The fixture's curve runs 0-4000 Hz, and the panel's plot rect is the widget
// less a 32 px dB gutter on the left and an 8 px margin on the right. That is
// enough to put the pointer on a handle without reaching into the class.
double xForHz(QWidget* panel, double hz)
{
    const double left = 32.0;
    const double span = std::max(1.0, double(panel->width()) - 32.0 - 8.0);
    return left + (hz / 4000.0) * span;
}

void sendMouse(QWidget* w, QEvent::Type type, double x, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    const QPointF pos(x, double(w->height()) / 2.0);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

// Press on the handle currently at `fromHz`, move to `toHz`, let go.
//
// Deliberately does NOT settle: the write goes out synchronously on release,
// but the gate's reply is what puts the handles back where the gate has them,
// and this fake gate replies with the same fixture whatever it is asked. The
// caller reads the handle and the log first, then settles.
void dragEdge(QWidget* panel, double fromHz, double toHz)
{
    sendMouse(panel, QEvent::MouseButtonPress, xForHz(panel, fromHz), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(panel, QEvent::MouseMove, xForHz(panel, toHz), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(panel, QEvent::MouseButtonRelease, xForHz(panel, toHz), Qt::LeftButton,
              Qt::NoButton);
}

// (a) /filter is polled only while the FILTER page is on screen: not on SLICE,
// not on BAND, not on SITE, not with the window closed, and never before it has
// been opened at all.
void testFilterPageStartsAndStopsTheFilterPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    CHECK(pollCount(net) == 0);

    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;
    // Opening lands on SLICE, which polls nothing of its own.
    CHECK(pollCount(net) == 0);

    pageButton(w, "diversityWindowPageFilter")->click();
    settle();
    const int running = pollCount(net);
    CHECK(running > 0);
    // FILTER's neighbours stay off while it is up: the span and the beacon
    // watch are not what this page is about.
    CHECK(net.count(QStringLiteral("/diversity/spatial")) == 0);
    CHECK(net.count(QStringLiteral("/diversity/beacons")) == 0);

    // MUTATION: leave the page. The poll stops where it is, and comes back when
    // the page does -- the page switch is the whole subscription.
    pageButton(w, "diversityWindowPageBand")->click();
    settle();
    filterTick(a);
    const int parked = pollCount(net);
    CHECK(parked == running);
    pageButton(w, "diversityWindowPageFilter")->click();
    settle();
    CHECK(pollCount(net) > parked);

    // And closing the window stops it again, with no page switch involved.
    const int beforeClose = pollCount(net);
    w->close();
    settle();
    filterTick(a);
    CHECK(pollCount(net) == beforeClose);
    closedToStart();
}

// (b) One status object, every readout on the page. This is the case that would
// catch a key renamed on the gate side.
void testCaptionAndReadoutsRenderFromStatus()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(labelText(w, "diversityWindowFilterCaptionLabel")
          == QStringLiteral("LSB · 100–2900 Hz · SHARP 1023 taps · 61 Hz transition"));
    CHECK(labelText(w, "diversityWindowFilterAutoLabel")
          == QStringLiteral("AUTO · print 300–2700"));
    CHECK(labelText(w, "diversityWindowFilterRoofingLabel")
          == QStringLiteral("Roof 200 kHz RF · 25 kHz digital"));
    CHECK(labelText(w, "diversityWindowFilterAnfLabel")
          == QStringLiteral("1240 Hz −34.0 dB, 2010 Hz −31.0 dB"));
    CHECK(labelText(w, "diversityWindowFilterTiltLabel")
          == QStringLiteral("tilt +3.5 dB"));
    CHECK(labelText(w, "diversityWindowFilterGainLabel")
          == QStringLiteral("gain −1.9 dB"));
    CHECK(labelText(w, "diversityWindowFilterBlankedLabel")
          == QStringLiteral("blanked 0.4 %"));

    auto* table = child<QTableWidget>(w, "diversityWindowFilterNotchTable");
    CHECK(table != nullptr);
    if (!table)
        return;
    CHECK(table->rowCount() == 2);
    CHECK(cell(table, 0, 0) == QStringLiteral("1000"));
    CHECK(cell(table, 0, 1) == QStringLiteral("120"));
    CHECK(cell(table, 0, 2) == QStringLiteral("−48.0"));
    CHECK(cell(table, 1, 0) == QStringLiteral("1780"));

    // The shape row and the AGC row are checked back off the same object.
    CHECK(child<QPushButton>(w, "diversityWindowFilterShapesharp")->isChecked());
    CHECK(child<QPushButton>(w, "diversityWindowFilterAgcmed")->isChecked());
    CHECK(child<QCheckBox>(w, "diversityWindowFilterAnfCheck")->isChecked());
    CHECK(child<QCheckBox>(w, "diversityWindowFilterApfCheck")->isChecked() == false);
    // The spin boxes show what was ASKED for, which under AUTO is not what is
    // in force. Here the two agree; the mutation below is where they part.
    CHECK(child<QSpinBox>(w, "diversityWindowFilterLowSpin")->value() == 100);
    CHECK(child<QSpinBox>(w, "diversityWindowFilterHighSpin")->value() == 2900);
    CHECK(child<QSpinBox>(w, "diversityWindowFilterAttackSpin")->value() == 10);
    CHECK(child<QSpinBox>(w, "diversityWindowFilterDecaySpin")->value() == 500);

    // MUTATION: the same filter with the spectrum tracker driving it. Every one
    // of those readouts is a different string, the edges in force are no longer
    // the edges asked for, and the notch table empties.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterCaptionLabel")
          == QStringLiteral("LSB · 210–2840 Hz · SOFT 511 taps · 122 Hz transition"));
    CHECK(labelText(w, "diversityWindowFilterAutoLabel")
          == QStringLiteral("AUTO · spectrum 210–2840"));
    CHECK(labelText(w, "diversityWindowFilterAnfLabel") == QStringLiteral("none"));
    CHECK(labelText(w, "diversityWindowFilterGainLabel")
          == QStringLiteral("gain −4.5 dB"));
    CHECK(labelText(w, "diversityWindowFilterBlankedLabel")
          == QStringLiteral("blanked 0.0 %"));
    CHECK(table->rowCount() == 0);
    CHECK(child<QPushButton>(w, "diversityWindowFilterShapesoft")->isChecked());
    CHECK(child<QPushButton>(w, "diversityWindowFilterAgcslow")->isChecked());
    CHECK(child<QSpinBox>(w, "diversityWindowFilterLowSpin")->value() == 100);
    CHECK(child<QSpinBox>(w, "diversityWindowFilterHighSpin")->value() == 2900);

    // MUTATION: AUTO on with nothing decided yet is its own sentence, not a
    // width of zero and not the last one the tracker had.
    QByteArray warming = kDiversityFilterAutoSpectrum;
    warming.replace("\"source\": \"spectrum\"", "\"source\": null");
    CHECK(warming != kDiversityFilterAutoSpectrum);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, warming};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterAutoLabel")
          == QStringLiteral("AUTO · warming up"));
    closedToStart();
}

// (c) A width preset keeps the low edge and moves the high one. The gate is
// told both, because "width" is not a key it has -- and the low it is told is
// the low in FORCE, which under AUTO is not the one in the spin box.
void testWidthPresetSendsWidthOnly()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    child<QPushButton>(w, "diversityWindowFilterPreset2400")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=100&high=2500"));

    // MUTATION: the same button against a filter whose low edge is somewhere
    // else. A preset that hardcoded 100 would not notice.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                 kDiversityFilterAutoSpectrum};
    filterTick(a);
    child<QPushButton>(w, "diversityWindowFilterPreset2400")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=210&high=2610"));

    // MUTATION: a different preset is a different width off the same low edge.
    child<QPushButton>(w, "diversityWindowFilterPreset3000")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=210&high=3210"));
    closedToStart();
}

// (d) The discrete rows write one key each, immediately.
void testShapeAndAgcButtonsWriteImmediately()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    child<QPushButton>(w, "diversityWindowFilterShapesoft")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?shape=soft"));

    // MUTATION: the other button in the same row writes the other value.
    child<QPushButton>(w, "diversityWindowFilterShapesharp")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?shape=sharp"));

    child<QPushButton>(w, "diversityWindowFilterAgclong")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?agc=long"));

    // MUTATION: a checkbox is the same door with a boolean behind it, and it
    // writes whichever way it was pointing. Switching the fixture is what makes
    // the second click the other way round -- the gate's answer is what the box
    // shows, not the click before it.
    auto* anf = child<QCheckBox>(w, "diversityWindowFilterAnfCheck");
    CHECK(anf->isChecked());
    anf->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?anf=0"));

    QByteArray off = kDiversityFilterStatus;
    off.replace("\"anf\": {\"enabled\": true", "\"anf\": {\"enabled\": false");
    CHECK(off != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, off};
    filterTick(a);
    CHECK(!anf->isChecked());
    anf->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?anf=1"));
    closedToStart();
}

// (e) Dragging a handle writes the edge that moved and ONLY the edge that
// moved: re-asserting the other one would hand the auto-width tracker's own
// answer back to it as an operator setting.
void testDraggingTheLowHandleWritesLowAlone()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();

    auto* panel = child<DiversityFilterPanel>(w, "diversityWindowFilterPanel");
    CHECK(panel != nullptr);
    if (!panel)
        return;
    CHECK(panel->lowHz() == 100);
    CHECK(panel->highHz() == 2900);

    dragEdge(panel, 100.0, 500.0);
    const int dragged = panel->lowHz();
    CHECK(dragged > 300 && dragged < 700);
    CHECK(dragged % 10 == 0);   // every edge a drag produces is a round number
    CHECK(panel->highHz() == 2900);
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=%1").arg(dragged));
    settle();

    // MUTATION: the same handle dragged somewhere else is a different number.
    dragEdge(panel, 100.0, 900.0);
    const int again = panel->lowHz();
    CHECK(again != dragged);
    CHECK(again > 700 && again < 1100);
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=%1").arg(again));
    settle();

    // MUTATION: the other handle writes the other key, and low= is absent.
    dragEdge(panel, 2900.0, 2400.0);
    const int high = panel->highHz();
    CHECK(high < 2900 && high > 2200);
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?high=%1").arg(high));
    settle();

    // The keyboard reaches the same two handles: Down picks the low edge, Left
    // and Right move it by ten hertz, Shift by fifty.
    panel->setFocus(Qt::OtherFocusReason);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(panel, &down);
    const int before = panel->lowHz();
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(panel, &right);
    CHECK(panel->lowHz() == before + 10);
    QKeyEvent fast(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QApplication::sendEvent(panel, &fast);
    CHECK(panel->lowHz() == before + 60);
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=%1").arg(before + 60));
    settle();
    closedToStart();
}

// (f) The notch route's three verbs. They are a route of their own rather than
// three more keys on /filter/set because "add one", "remove one" and "remove
// them all" are not settings.
void testNotchAddAndClear()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* hz = child<QSpinBox>(w, "diversityWindowFilterNotchHzSpin");
    auto* width = child<QSpinBox>(w, "diversityWindowFilterNotchWidthSpin");
    CHECK(hz != nullptr && width != nullptr);
    if (!hz || !width)
        return;
    hz->setValue(1500);
    width->setValue(100);
    child<QPushButton>(w, "diversityWindowFilterNotchAddButton")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
          == QStringLiteral("/filter/notch?add=1500&width=100"));

    // MUTATION: the button carries the spin boxes' values, not a constant.
    hz->setValue(2200);
    width->setValue(60);
    child<QPushButton>(w, "diversityWindowFilterNotchAddButton")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
          == QStringLiteral("/filter/notch?add=2200&width=60"));

    // One row's CLEAR names that row's frequency; CLEAR ALL is clear=1.
    auto* clearOne = child<QPushButton>(w, "diversityWindowFilterNotchClear1000");
    CHECK(clearOne != nullptr);
    if (clearOne) {
        clearOne->click();
        settle();
        CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
              == QStringLiteral("/filter/notch?clear=1000"));
    }
    // MUTATION: the second row is a different button naming a different notch.
    auto* clearTwo = child<QPushButton>(w, "diversityWindowFilterNotchClear1780");
    CHECK(clearTwo != nullptr);
    if (clearTwo) {
        clearTwo->click();
        settle();
        CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
              == QStringLiteral("/filter/notch?clear=1780"));
    }
    child<QPushButton>(w, "diversityWindowFilterNotchClearAllButton")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
          == QStringLiteral("/filter/notch?clear=1"));

    // A double-click on the curve is the same add, at the frequency under the
    // pointer and the width beside the ADD button.
    w->resize(1120, 860);
    settle();
    w->grab();
    auto* panel = child<DiversityFilterPanel>(w, "diversityWindowFilterPanel");
    CHECK(panel != nullptr);
    if (!panel)
        return;
    width->setValue(80);
    sendMouse(panel, QEvent::MouseButtonDblClick, xForHz(panel, 1500.0),
              Qt::LeftButton, Qt::LeftButton);
    const QString added = lastRequest(net, QStringLiteral("/filter/notch"));
    const QUrlQuery addQuery(added.section(QLatin1Char('?'), 1));
    const int at = addQuery.queryItemValue(QStringLiteral("add")).toInt();
    CHECK(at >= 1400 && at <= 1600);
    CHECK(at % 10 == 0);
    CHECK(addQuery.queryItemValue(QStringLiteral("width")) == QStringLiteral("80"));
    closedToStart();
}

// (g) A mode with no slice filter behind it is a sentence and a greyed page,
// not an empty one.
void testUnavailablePayloadDisablesTheControls()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFilterUnavailable);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(labelText(w, "diversityWindowFilterStatusLabel")
          == QStringLiteral("Filter is not available for this mode"));
    CHECK(labelText(w, "diversityWindowFilterCaptionLabel") == kDash);
    CHECK(labelText(w, "diversityWindowFilterAutoLabel") == kDash);
    CHECK(!child<QSpinBox>(w, "diversityWindowFilterLowSpin")->isEnabled());
    CHECK(!child<QPushButton>(w, "diversityWindowFilterPreset2400")->isEnabled());
    CHECK(!child<QCheckBox>(w, "diversityWindowFilterNbCheck")->isEnabled());
    CHECK(!child<DiversityFilterPanel>(w, "diversityWindowFilterPanel")->isEnabled());

    // MUTATION: the filter comes back with the mode. The sentence goes and the
    // controls come alive, without the window being reopened.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterStatusLabel").isEmpty());
    CHECK(labelText(w, "diversityWindowFilterCaptionLabel") != kDash);
    CHECK(child<QSpinBox>(w, "diversityWindowFilterLowSpin")->isEnabled());
    CHECK(child<QPushButton>(w, "diversityWindowFilterPreset2400")->isEnabled());
    CHECK(child<DiversityFilterPanel>(w, "diversityWindowFilterPanel")->isEnabled());
    closedToStart();
}

// (h) A refused write says so where the operator is looking, and moves nothing:
// the next poll is what puts the refused control back where the gate has it.
void testGateRefusalShowsOnTheStatusLine()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    CHECK(labelText(w, "diversityWindowFilterStatusLabel").isEmpty());

    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                 kDiversityFilterBadValue};
    child<QPushButton>(w, "diversityWindowFilterPreset1800")->click();
    settle();
    CHECK(labelText(w, "diversityWindowFilterStatusLabel")
          == QStringLiteral("bad value: low"));
    // Nothing moved: the caption still says what the gate actually has.
    CHECK(labelText(w, "diversityWindowFilterCaptionLabel")
          == QStringLiteral("LSB · 100–2900 Hz · SHARP 1023 taps · 61 Hz transition"));
    CHECK(child<QSpinBox>(w, "diversityWindowFilterLowSpin")->isEnabled());

    // MUTATION: a different refusal is a different line, and it is the gate's
    // words rather than one of ours.
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError, R"({"error": "bad value: contour_db"})"};
    child<QCheckBox>(w, "diversityWindowFilterContourCheck")->click();
    settle();
    CHECK(labelText(w, "diversityWindowFilterStatusLabel")
          == QStringLiteral("bad value: contour_db"));

    // MUTATION: a dropped request is not an error and not an empty page. The
    // last good status stays exactly where it was.
    net.routes.remove(QStringLiteral("/filter"));
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterCaptionLabel")
          == QStringLiteral("LSB · 100–2900 Hz · SHARP 1023 taps · 61 Hz transition"));
    closedToStart();
}

// (i) The same promise the other three pages make: at the size the window opens
// at, nothing on the FILTER page is behind a scrollbar.
void testNothingScrollsOnTheFilterPageAtTheInitialSize()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();   // forces a full layout pass on an offscreen platform

    auto* scroll = child<QScrollArea>(w, "diversityWindowFilterScroll");
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    CHECK(w->minimumSizeHint().width() <= 1120);

    // MUTATION: the widest content the page can hold -- a refusal on the status
    // line, an ANF listing two tones and a full notch table all at once. If any
    // of those labels were word-wrapped they are height-for-width, and this is
    // where the scrollbar would appear.
    net.routes[QStringLiteral("/filter/set")] = {
        QNetworkReply::NoError,
        R"({"error": "bad value: low must be below high and both inside 0-20000"})"};
    child<QPushButton>(w, "diversityWindowFilterPreset1800")->click();
    settle();
    w->grab();
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    closedToStart();
}

// (j) The fight between a poll and a hand. A spin box the operator is halfway
// through is the one place the gate is NOT the best available answer.
void testASpinBoxWithFocusIsNotOverwrittenByAPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    w->show();
    settle();

    auto* low = child<QSpinBox>(w, "diversityWindowFilterLowSpin");
    CHECK(low != nullptr);
    if (!low)
        return;
    CHECK(low->value() == 100);
    low->setFocus(Qt::OtherFocusReason);
    settle();
    CHECK(low->hasFocus());
    low->setValue(450);

    // Two polls land while the operator is still in the box. Neither touches it.
    filterTick(a);
    filterTick(a);
    CHECK(low->value() == 450);

    // MUTATION: focus leaving the box commits the edit -- editingFinished
    // fires on focus-out whether or not the operator pressed Enter -- so the
    // value the box now shows is the write's own, held exactly like any other
    // write: the reply on this same settle() is the fixture's unmoved low=100,
    // and the hold is what stops that from being believed. See
    // tests/diversity_filter_hold_test.cpp for the hold on its own; what this
    // case still proves is the other half -- focus is what excludes a poll,
    // and losing it puts the box back within the gate's reach.
    child<QSpinBox>(w, "diversityWindowFilterHighSpin")->setFocus(Qt::OtherFocusReason);
    settle();
    CHECK(!low->hasFocus());
    CHECK(low->value() == 450);

    // Once the hold has run its course a poll owns the box exactly as it
    // always has -- which is the case this mutation exists to prove: without
    // this, the case would pass on a page that never reads the gate at all.
    low->setProperty("pendingUntil", QDateTime::currentMSecsSinceEpoch() - 1);
    filterTick(a);
    CHECK(low->value() == 100);
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_filter_test"));
    QApplication app(argc, argv);

    testFilterPageStartsAndStopsTheFilterPoll();
    testCaptionAndReadoutsRenderFromStatus();
    testWidthPresetSendsWidthOnly();
    testShapeAndAgcButtonsWriteImmediately();
    testDraggingTheLowHandleWritesLowAlone();
    testNotchAddAndClear();
    testUnavailablePayloadDisablesTheControls();
    testGateRefusalShowsOnTheStatusLine();
    testNothingScrollsOnTheFilterPageAtTheInitialSize();
    testASpinBoxWithFocusIsNotOverwrittenByAPoll();

    std::printf("\n%d diversity filter test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
