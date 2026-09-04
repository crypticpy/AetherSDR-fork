// The FILTER page as a PICTURE rather than a set of controls: what order the
// three things on it sit in, what the note under the button actually says,
// and that the PAIR STAGES box carries both of the stages it promises --
// where tests/diversity_filter_test.cpp is about the exact query string each
// control sends and tests/diversity_filter_hold_test.cpp is about a status
// update never turning into a write.
//
// A separate binary rather than more cases in diversity_filter_test.cpp for
// the reason that file already gives for being its own binary: this family of
// windows stays near the 800-line budget AGENTS.md asks for, and every case
// wants the same fresh, process-wide AppSettings start. This file used to be
// about the response curve DiversityFilterPanel painted and the five PRESETS
// under it -- both gone to the gate's own CHAIN window now -- so what is left
// worth checking as a picture is smaller, but it is the same kind of check.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityWindow.h"

#include <QApplication>
#include <QLabel>
#include <QNetworkReply>
#include <QPoint>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::AppSettings;
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

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

void connectGate(AetherGateApplet& a, FakeGate& net)
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
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

DiversityWindow* openOnFilter(AetherGateApplet& a)
{
    a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"))->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (!w)
        return nullptr;
    tick(a);
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

// (a) The note under the button says exactly where the generic stages went --
// this is the one sentence on the whole page an operator who remembers the
// old layout will actually read.
void testMovedLabelNamesWhereTheStagesWent()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    CHECK(labelText(w, "diversityWindowFilterMovedLabel")
          == QStringLiteral(
              "roofing, blanker, shape, notch, APF, AGC: in the CHAIN window"));
    closedToStart();
}

// (b) OPEN CHAIN sits above the note, and the note sits above PAIR STAGES --
// "at the top of the page" is a position claim as much as a words one, and a
// button buried under the group box would not read as an entry point.
void testOpenChainButtonSitsAboveTheNoteAbovePairStages()
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

    auto* button = child<QPushButton>(w, "diversityWindowFilterOpenChain");
    auto* note = w->findChild<QWidget*>(QStringLiteral("diversityWindowFilterMovedLabel"));
    auto* box = w->findChild<QWidget*>(QStringLiteral("diversityWindowFilterPairStagesBox"));
    CHECK(button != nullptr && note != nullptr && box != nullptr);
    if (!button || !note || !box)
        return;

    auto* body = button->parentWidget();
    CHECK(body != nullptr);
    if (!body)
        return;
    const int buttonY = button->mapTo(body, QPoint(0, 0)).y();
    const int noteY = note->mapTo(body, QPoint(0, 0)).y();
    const int boxY = box->mapTo(body, QPoint(0, 0)).y();
    CHECK(buttonY < noteY);
    CHECK(noteY < boxY);
    closedToStart();
}

// (c) The PAIR STAGES box carries both of the stages it promises, each under
// its own caption -- not two anonymous rows an operator has to guess between.
void testPairStagesBoxCarriesBothCaptions()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* box = w->findChild<QWidget*>(QStringLiteral("diversityWindowFilterPairStagesBox"));
    CHECK(box != nullptr);
    if (!box)
        return;

    QStringList captions;
    for (QLabel* label : box->findChildren<QLabel*>())
        captions << label->text();
    CHECK(captions.contains(QStringLiteral("POST-FILTER")));
    CHECK(captions.contains(QStringLiteral("SUB-BAND MRC")));

    CHECK(child<QPushButton>(w, "diversityWindowFilterPostOff") != nullptr);
    CHECK(child<QPushButton>(w, "diversityWindowFilterPostV1") != nullptr);
    CHECK(child<QPushButton>(w, "diversityWindowFilterPostV2") != nullptr);
    CHECK(child<QPushButton>(w, "diversityWindowFilterMrc") != nullptr);
    closedToStart();
}

// (d) The V2 and MRC hovers say WHEN to reach for each one, in one line;
// the longer plain-words explanation an operator asked for is the accessible
// description, where a screen reader still gets all of it.
void testTooltipsSayWhenToReachForEachStage()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net);
    DiversityWindow* w = openOnFilter(a);
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* v2 = child<QPushButton>(w, "diversityWindowFilterPostV2");
    auto* mrc = child<QPushButton>(w, "diversityWindowFilterMrc");
    CHECK(v2 != nullptr && mrc != nullptr);
    if (!v2 || !mrc)
        return;

    CHECK(v2->toolTip().contains(QStringLiteral("hissy SSB")));
    CHECK(v2->toolTip().length() <= 90);
    CHECK(v2->accessibleDescription().contains(QStringLiteral("faint SSB")));
    CHECK(v2->accessibleDescription().contains(QStringLiteral("noise between words")));
    CHECK(mrc->toolTip().length() <= 90);
    CHECK(mrc->accessibleDescription().contains(QStringLiteral("small gain")));
    CHECK(mrc->accessibleDescription().contains(QStringLiteral("lab switch")));
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_filter_layout_test"));
    QApplication app(argc, argv);

    testMovedLabelNamesWhereTheStagesWent();
    testOpenChainButtonSitsAboveTheNoteAbovePairStages();
    testPairStagesBoxCarriesBothCaptions();
    testTooltipsSayWhenToReachForEachStage();

    std::printf("\n%d diversity filter layout test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
