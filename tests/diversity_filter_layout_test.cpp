// The FILTER page's second half: what is actually ARRIVING, said in one line
// and drawn under the curve, plus the five whole-filter presets under the four
// columns.
//
// A sixth binary rather than more cases in tests/diversity_filter_test.cpp for
// the reason that file already gives for being the fifth: it is at the
// 800-line budget AGENTS.md asks for. The split is by subject and not by size
// alone -- everything here is about the page as a PICTURE (the spectrum area,
// the state line, the strip under the columns and the room they leave each
// other), where the other file is about the page as a set of controls.
//
// The spectrum area is painted, so there is no child widget to read it back
// from. DiversityFilterPanel exposes what it draws the way DiversitySnrMeter
// exposes shownDb(), and this file asserts on those: the point count, the
// gate's floor, and -- the value that matters -- the dB each point is PLOTTED
// at once the floor has been pinned, which is a different number from the one
// in the payload and is the whole reason the pinning exists.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>

#include <cmath>
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

// Painted dB, compared the way a painted dB has to be.
bool near(double a, double b)
{
    return std::abs(a - b) < 0.01;
}

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& filter = kDiversityFilterStatus)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
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

void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"))->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    w->findChild<QToolButton*>(QStringLiteral("diversityWindowPageFilter"))->click();
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

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

// The index of the fixture's peak bin. The spectrum is on the response's own
// 0-4000 Hz / 128-point grid, so the bin nearest 1 kHz is 32.
constexpr int kPeakBin = 32;

// (a) The spectrum area, pinned to the gate's own floor. The payload is dB
// below the spectrum's OWN peak, so its maximum is always 0.0 -- drawn
// straight onto the panel's 0..-60 axis, a dead channel would paint a
// full-scale slab. What is asserted here is the plotted value, not the
// payload one.
void testSpectrumIsDrawnAgainstTheGatesFloor()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* panel = child<DiversityFilterPanel>(w, "diversityWindowFilterPanel");
    CHECK(panel != nullptr);
    if (!panel)
        return;

    CHECK(panel->hasSpectrum());
    // The same grid as the response curve: one Hz axis, two traces.
    CHECK(panel->spectrumPointCount() == 128);
    CHECK(near(panel->spectrumFloorDb(), -38.4));
    // The floor lands on the floor tick, wherever the gate says the floor is.
    CHECK(near(panel->spectrumAxisDbAt(0), -45.0));
    // And a peak 38.4 dB over that floor is drawn 38.4 dB above the tick --
    // NOT at 0 dB, which is where its own scale puts it.
    CHECK(near(panel->spectrumAxisDbAt(kPeakBin), -6.6));

    // MUTATION: the same shape on a quieter channel -- a floor only 26.5 dB
    // down. Every plotted value moves, because what the area draws is height
    // over the floor and not level.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK(near(panel->spectrumFloorDb(), -26.5));
    CHECK(near(panel->spectrumAxisDbAt(0), -45.0));
    CHECK(near(panel->spectrumAxisDbAt(kPeakBin), -18.5));

    // MUTATION: "spectrum": null. Nothing is drawn and nothing is kept: an
    // area that stopped updating would go on claiming an occupied channel.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterNoSpectrum};
    filterTick(a);
    CHECK(!panel->hasSpectrum());
    CHECK(panel->spectrumPointCount() == 0);
    CHECK(std::isnan(panel->spectrumFloorDb()));
    CHECK(std::isnan(panel->spectrumAxisDbAt(kPeakBin)));

    // MUTATION: and it comes back with the next block, without the window
    // being reopened.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    filterTick(a);
    CHECK(panel->hasSpectrum());
    CHECK(near(panel->spectrumAxisDbAt(kPeakBin), -6.6));
    closedToStart();
}

// (b) The AUTO edges get their own marks, so what the tracker chose can be
// read against the energy it chose it from.
void testAutoEdgesAreMarkedOnlyWhileAutoIsOn()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    auto* panel = child<DiversityFilterPanel>(w, "diversityWindowFilterPanel");
    CHECK(panel != nullptr);
    if (!panel)
        return;

    CHECK(near(panel->autoLowHz(), 300.0));
    CHECK(near(panel->autoHighHz(), 2700.0));

    // MUTATION: a different tracker source is a different pair of marks.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK(near(panel->autoLowHz(), 210.0));
    CHECK(near(panel->autoHighHz(), 2840.0));

    // MUTATION: AUTO off draws no marks at all. The gate still reports the
    // last pair it chose; drawing them would claim they were in force.
    QByteArray off = kDiversityFilterStatus;
    off.replace("\"auto\": {\"enabled\": true", "\"auto\": {\"enabled\": false");
    CHECK(off != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, off};
    filterTick(a);
    CHECK(std::isnan(panel->autoLowHz()));
    CHECK(std::isnan(panel->autoHighHz()));
    closedToStart();
}

// (c) The state line between the curve and the columns. Every clause on it is
// also somewhere below it; the point is that the answer to "what is switched
// on?" does not cost four columns of reading.
void testStateLineSaysTheWholeFilterInOneLine()
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
          == QStringLiteral("in force 100–2900 Hz (asked 100–2900) · AUTO print "
                            "300–2700 · ANF 2 tones · notches 2 · AGC med −1.9 dB "
                            "· NB 0.4 %"));

    // MUTATION: the tracker driving from the spectrum. The edges in force are
    // no longer the edges asked for -- which is the one thing this line exists
    // to make impossible to miss -- and every other clause changes with it.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterAutoSpectrum};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterForceLabel")
          == QStringLiteral("in force 210–2840 Hz (asked 100–2900) · AUTO spectrum "
                            "210–2840 · ANF off · notches 0 · AGC slow −4.5 dB "
                            "· NB off"));

    // MUTATION: an ANF that is running and holding nothing is not an ANF that
    // is off, and the line says so in different words.
    QByteArray searching = kDiversityFilterStatus;
    searching.replace("\"found_hz\": [1240.0, 2010.0],\n            "
                      "\"depth_db\": [-34.0, -31.0]",
                      "\"found_hz\": [], \"depth_db\": []");
    CHECK(searching != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, searching};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterForceLabel").contains(
        QStringLiteral("ANF no tones")));

    // MUTATION: one tone is singular. A line that read "1 tones" would be the
    // only place in this window that did.
    QByteArray oneTone = kDiversityFilterStatus;
    oneTone.replace("\"found_hz\": [1240.0, 2010.0]", "\"found_hz\": [1240.0]");
    CHECK(oneTone != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, oneTone};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterForceLabel").contains(
        QStringLiteral("ANF 1 tone ·")));

    // MUTATION: the gain and the blanked share on this line are re-read on
    // every poll, not written once when the page opened.
    QByteArray moved = kDiversityFilterStatus;
    moved.replace("\"gain_db\": -1.9", "\"gain_db\": -12.4");
    moved.replace("\"blanked_pct\": 0.4", "\"blanked_pct\": 3.7");
    CHECK(moved != kDiversityFilterStatus);
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, moved};
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterForceLabel").contains(
        QStringLiteral("AGC med −12.4 dB · NB 3.7 %")));
    // And the two column readouts it summarises moved with it.
    CHECK(labelText(w, "diversityWindowFilterGainLabel")
          == QStringLiteral("gain −12.4 dB"));
    CHECK(labelText(w, "diversityWindowFilterBlankedLabel")
          == QStringLiteral("blanked 3.7 %"));

    // A dropped poll is not news. The line keeps saying what the gate last
    // actually said, for the reason every other readout on this page does:
    // blanking it would claim the filter had changed.
    const QString held = labelText(w, "diversityWindowFilterForceLabel");
    net.routes.remove(QStringLiteral("/filter"));
    filterTick(a);
    CHECK(labelText(w, "diversityWindowFilterForceLabel") == held);
    closedToStart();
}

// (d) Each preset is exactly one /filter/set, and the query is the preset.
// This is the case a screenshot cannot replace: a button that looks right and
// sends the wrong shape is indistinguishable in a render.
void testPresetsSendOneExactQueryEach()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    child<QPushButton>(w, "diversityWindowFilterPresetSsbWide")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=100&high=2900&shape=soft"));

    child<QPushButton>(w, "diversityWindowFilterPresetSsbNarrow")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=300&high=2400&shape=sharp"));

    // CW-ISH carries the audio peak with it: the whole point of a preset over
    // a width button is that it brings the tone controls the setting needs.
    child<QPushButton>(w, "diversityWindowFilterPresetCw")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral(
              "/filter/set?low=400&high=1000&shape=sharp&apf=1&apf_hz=700"));

    child<QPushButton>(w, "diversityWindowFilterPresetNet")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=200&high=2700&shape=sharp&auto_eq=1"));

    // RESET is the one that is two requests, and it is two because clearing
    // the notches is a different route rather than another key.
    const int before = net.log.size();
    child<QPushButton>(w, "diversityWindowFilterPresetReset")->click();
    settle();
    CHECK(lastRequest(net, QStringLiteral("/filter/set"))
          == QStringLiteral("/filter/set?low=100&high=2900&shape=soft&anf=0"
                            "&contour=0&apf=0&auto=0&auto_eq=0&nb=0&agc=med"));
    CHECK(lastRequest(net, QStringLiteral("/filter/notch"))
          == QStringLiteral("/filter/notch?clear=1"));
    // The set goes first: the notch route's reply is a status object too, and
    // the operator's last word should be the one about the notches.
    int setAt = -1;
    int notchAt = -1;
    for (int i = before; i < net.log.size(); ++i) {
        if (net.log.at(i).startsWith(QStringLiteral("/filter/set")))
            setAt = i;
        else if (net.log.at(i).startsWith(QStringLiteral("/filter/notch")))
            notchAt = i;
    }
    CHECK(setAt >= 0 && notchAt > setAt);
    closedToStart();
}

// (e) The strip is part of the page, not an appendix to it: it greys with
// everything else when the mode has no filter, and it fits.
void testPresetStripGreysAndFitsWithTheRest()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFilterUnavailable);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;
    w->resize(1120, 860);
    settle();
    w->grab();

    for (const char* name : {"diversityWindowFilterPresetSsbWide",
                             "diversityWindowFilterPresetSsbNarrow",
                             "diversityWindowFilterPresetCw",
                             "diversityWindowFilterPresetNet",
                             "diversityWindowFilterPresetReset"}) {
        auto* button = child<QPushButton>(w, name);
        CHECK(button != nullptr);
        CHECK(button && !button->isEnabled());
    }

    // MUTATION: the filter comes back and so does the strip.
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    filterTick(a);
    CHECK(child<QPushButton>(w, "diversityWindowFilterPresetReset")->isEnabled());

    // The same promise the other four pages make, now with a fifth row of
    // controls on this one: at the size the window opens at, nothing is behind
    // a scrollbar and the window's own minimum still fits in it.
    w->grab();
    auto* scroll = child<QScrollArea>(w, "diversityWindowFilterScroll");
    CHECK(scroll != nullptr);
    if (!scroll)
        return;
    CHECK(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
    CHECK(scroll->widget()->minimumSizeHint().height() <= scroll->viewport()->height());
    CHECK(!scroll->verticalScrollBar()->isVisible());
    CHECK(!scroll->horizontalScrollBar()->isVisible());
    CHECK(w->minimumSizeHint().width() <= 1120);

    // The four boxes are as tall as what is in them: the strip below them has
    // to be ON the page, not pushed off the bottom by four columns stretching
    // into space they have nothing to put in.
    auto* strip = w->findChild<QWidget*>(QStringLiteral("diversityWindowFilterPresetsBox"));
    CHECK(strip != nullptr);
    auto* notchBox = w->findChild<QWidget*>(QStringLiteral("diversityWindowFilterNotchBox"));
    CHECK(notchBox != nullptr);
    if (strip && notchBox) {
        const int stripBottom =
            strip->mapTo(scroll->widget(), QPoint(0, strip->height())).y();
        CHECK(stripBottom <= scroll->viewport()->height());
        // And the strip is under the columns rather than beside them.
        CHECK(strip->mapTo(scroll->widget(), QPoint(0, 0)).y()
              > notchBox->mapTo(scroll->widget(), QPoint(0, 0)).y());
    }
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_filter_layout_test"));
    QApplication app(argc, argv);

    testSpectrumIsDrawnAgainstTheGatesFloor();
    testAutoEdgesAreMarkedOnlyWhileAutoIsOn();
    testStateLineSaysTheWholeFilterInOneLine();
    testPresetsSendOneExactQueryEach();
    testPresetStripGreysAndFitsWithTheRest();

    std::printf("\n%d diversity filter layout test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
