// AetherGateApplet — the diversity section's own polling, socket-free.
//
// Same harness and the same contract as tests/aether_gate_applet_test.cpp (see
// its header comment): every reply is injected, nothing opens a port. What is
// asserted here is the half of the applet that is about /diversity rather than
// about presence -- the section that hides itself until the gate says it has
// two tuners, the status line the slimmed sidebar was cut down to, the scope
// behind the show-scope key, the /diversity/map cadence, and the rule that
// makes all of it non-critical: a diversity route that fails must never cost
// the gate its presence.

#include "AetherGateAppletFixture.h"

#include "TestSettingsProfile.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityScope.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QWidget>

#include <cmath>
#include <cstdio>

using AetherSDR::DiversityScope;
using namespace AetherGateAppletFixture;

namespace {

// The sidebar section is a door, not the instrument (docs/DIVERSITY-ROADMAP.md
// §3): it stays hidden until a /diversity poll says "available": true, and it
// hides again the moment that stops being true -- no /diversity route (an old
// gate), "available": false (a gate whose device isn't a dual-tuner), a
// non-JSON body, or the gate going away entirely. Hidden, not emptied: an
// empty box still costs a caption and a gap in a 250px column.
void testDiversityHiddenUntilAvailableThenHiddenAgain()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* box = a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"));
    CHECK(a.gatePresent());     // /diversity 404ing must not affect presence
    CHECK(box && box->isHidden());

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityUnavailable};
    tick(a);
    CHECK(box && box->isHidden());

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityManual};
    tick(a);
    CHECK(box && !box->isHidden());

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityUnavailable};
    tick(a);
    CHECK(box && box->isHidden());

    // Available again, and then the gate itself goes away: setPresent(false)
    // hides the section too, rather than leaving the last gate's readout on
    // screen for a radio that is no longer answering.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityManual};
    tick(a);
    CHECK(box && !box->isHidden());

    a.setRadioAddress(QString());
    CHECK(!a.gatePresent());
    CHECK(box && box->isHidden());
    auto* status = a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"));
    CHECK(status && status->text() == QStringLiteral("—"));
}

// The whole sidebar readout in one line: the mode, who is talking (id, plus
// the operator's name for that id when memory carries one), and what the
// combiner is buying over the BETTER loop -- kDiversityTalkerAl's snr_db is
// a=12.3, b=9.8, out=15.1, so the gain is 15.1 - 12.3 = +2.8 dB, not
// 15.1 - 9.8.
void testDiversityStatusLineCarriesModeTalkerAndGain()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityTalkerAl};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* status = a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"));
    CHECK(status && status->text() == QStringLiteral("track · #2 Al · +2.8 dB"));

    // The same gate with the UNNAMED talker on the air: the id alone, never
    // an invented label.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                 kDiversityTalkerUnnamed};
    tick(a);
    CHECK(status && status->text() == QStringLiteral("track · #1 · +2.8 dB"));
}

// "off" is the whole line when the combiner is off -- there is no talker to
// attribute and no gain to claim -- and a leg the gate did not measure is an
// em dash, never an invented 0.0 dB (kDiversityTrack's snr_db legs are all
// null and it carries no talker).
void testDiversityStatusLineSaysOffAndEmDashesWhatWasNotMeasured()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityOff};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* status = a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"));
    CHECK(status && status->text() == QStringLiteral("off"));

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityTrack};
    tick(a);
    CHECK(status && status->text() == QStringLiteral("track · —"));
}

// The status label's minimum width covers the longest line its FIXED parts
// can build, so switching between them never resizes the label -- and its
// horizontal policy is Ignored, so a long operator name clips instead of
// widening the whole sidebar column.
void testDiversityStatusLabelWidthIsFixedAgainstTheWorstCasePhrase()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityTalkerAl};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* status = a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"));
    CHECK(status != nullptr);
    if (!status)
        return;
    const int worst =
        status->fontMetrics().horizontalAdvance(QStringLiteral("manual · #9999 · −99.9 dB"));
    CHECK(status->minimumWidth() >= worst);
    CHECK(status->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored);
}

// The one control left in the sidebar. Selecting a mode reaches the gate as a
// plain /diversity/set?mode=<value>, and a poll writes the gate's own mode
// back into the combo without echoing a write.
void testDiversityModeComboChangeSendsModeQueryAndPollWritesBack()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, kDiversityV2};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());

    // kDiversityV2 arrives with mode=manual -- the poll wrote that back.
    auto* mode = a.findChild<QComboBox*>(QStringLiteral("gateDiversityModeCombo"));
    CHECK(mode && mode->currentData().toString() == QStringLiteral("manual"));
    CHECK(mode && mode->accessibleName() == QStringLiteral("Diversity combining mode"));

    const int writes = net.count(QStringLiteral("/diversity/set"));
    mode->setCurrentIndex(mode->findData(QStringLiteral("null")));
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/set?mode=null")));
    // Exactly one write: the read-back must not echo a second one.
    CHECK(net.count(QStringLiteral("/diversity/set")) == writes + 1);
}

// The compact scope is opt-in: AetherGateDiversityPanel_ShowScope (default
// off) is the only thing that shows it, and there is deliberately no UI to
// flip it. It is built and fed either way, so turning the key on shows a
// scope that is already current rather than an empty one.
void testDiversityScopeHiddenUnlessShowScopeKeyIsSet()
{
    const QString key = QStringLiteral("AetherGateDiversityPanel_ShowScope");
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};

    AppSettings::instance().setValue(key, QStringLiteral("False"));
    {
        AetherGateApplet a(nullptr, &net);
        a.setRadioAddress(QStringLiteral("10.0.0.5"));
        settle();
        settle();
        auto* scope = a.findChild<DiversityScope*>(QStringLiteral("gateDiversityScope"));
        CHECK(scope && scope->isHidden());
        // Fed all the same: kDiversityV2's gain is 15.1 - max(12.3, 9.8).
        CHECK(scope && std::abs(scope->lastGainDb() - 2.8) < 1e-9);
    }

    AppSettings::instance().setValue(key, QStringLiteral("True"));
    {
        AetherGateApplet a(nullptr, &net);
        a.setRadioAddress(QStringLiteral("10.0.0.5"));
        settle();
        settle();
        auto* scope = a.findChild<DiversityScope*>(QStringLiteral("gateDiversityScope"));
        CHECK(scope && !scope->isHidden());
    }
    AppSettings::instance().setValue(key, QStringLiteral("False"));
}

// DiversityScope in isolation: a payload with every snr_db leg null must not
// crash and must report NaN gain; a payload with all three legs present
// computes out - max(a, b); a full v2 payload (every optional field at once)
// must not crash either; clear() resets the gain back to NaN.
void testDiversityScopeAcceptsNullsAndFullPayloadWithoutCrashingAndComputesGain()
{
    DiversityScope scope;

    const QJsonObject nulls = QJsonDocument::fromJson(kDiversityTrack).object();
    scope.setState(nulls);
    CHECK(std::isnan(scope.lastGainDb()));

    const QByteArray gainPayload = R"({"available": true, "mode": "manual",
        "phase_deg": 12.0, "ratio_db": 1.0, "snr_db": {"a": 3.0, "b": 5.0, "out": 7.5},
        "aligned": true, "lag_samples": 0, "corr_peak": 0.5, "updates": 1})";
    scope.setState(QJsonDocument::fromJson(gainPayload).object());
    CHECK(std::abs(scope.lastGainDb() - 2.5) < 1e-9);   // 7.5 - max(3.0, 5.0)

    const QJsonObject full = QJsonDocument::fromJson(kDiversityV2).object();
    scope.setState(full);   // every optional v2 field at once -- must not crash
    CHECK(std::abs(scope.lastGainDb() - 2.8) < 1e-9);   // 15.1 - max(12.3, 9.8)

    scope.clear();
    CHECK(std::isnan(scope.lastGainDb()));
}

// The glance-view's hard requirement, unchanged by the slimming: when
// ShowScope IS on, at the sidebar's 250px width and the default font
// DiversityScope's two bottom text lines (talk/moves/mem, noise/coh/nb) must
// fit without eliding -- grab() forces a real paintEvent() against the
// resized widget so bottomLinesElided() reflects an actual QFontMetrics
// measurement, not a guess.
void testDiversityScopeBottomLinesFitAt250pxWithoutEliding()
{
    const QString key = QStringLiteral("AetherGateDiversityPanel_ShowScope");
    AppSettings::instance().setValue(key, QStringLiteral("True"));

    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    a.resize(250, a.sizeHint().height());
    settle();

    auto* box = a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"));
    CHECK(box);
    if (box)
        box->grab();

    auto* scope = a.findChild<DiversityScope*>(QStringLiteral("gateDiversityScope"));
    CHECK(scope && !scope->bottomLinesElided());

    AppSettings::instance().setValue(key, QStringLiteral("False"));
}

// The map moved to the window's noise panel with everything else, so
// /diversity/map is polled only while that window is on screen: a closed
// window costs no map polling at all, and opening one starts it immediately
// rather than waiting out a stale cadence count.
void testMapPollRunsOnlyWhileTheWindowIsVisible()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, makeDiversityMap(8)};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());

    CHECK(!a.diversityPanel()->wantsMapPoll());
    const int closed = net.count(QStringLiteral("/diversity/map"));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == closed);

    auto* open = a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
    CHECK(open != nullptr);
    if (!open)
        return;
    open->click();
    settle();
    CHECK(a.diversityPanel()->wantsMapPoll());
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == closed + 1);

    // A 256-point coherence array and an {"error"} reply (no map yet) both
    // land cleanly -- same non-critical-to-presence contract as /diversity
    // itself.
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError,
                                                     makeDiversityMap(256)};
    tick(a);
    tick(a);
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    tick(a);
    tick(a);
    CHECK(a.gatePresent());

    open->click();          // closed again -- the poll stops
    settle();
    CHECK(!a.diversityPanel()->wantsMapPoll());
    const int reclosed = net.count(QStringLiteral("/diversity/map"));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == reclosed);

    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// The /diversity/map throttle lives in the TIMER-DRIVEN poll path only.
// AetherGateDiversityPanel::applyDiversity() is also the read-back handler
// for onDiversityRequestSet(), so an operator round-tripping an edit through
// it must never itself advance (or trigger) the map's own cadence -- only
// the periodic /status+/diversity poll may (#5372-round-2 finding: the
// throttle used to live inside applyDiversity() itself, so edits counted).
void testDiversityMapCadenceIsPollDrivenNotEditDriven()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, makeDiversityMap(8)};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());

    auto* open = a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
    CHECK(open != nullptr);
    if (!open)
        return;
    const int baseline = net.count(QStringLiteral("/diversity/map"));
    open->click();
    settle();
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == baseline + 1);   // fetched up front

    // Five full onDiversityRequestSet() round trips, each running
    // applyDiversity() on its own read-back -- none of these may advance the
    // map's cadence.
    auto* mode = a.findChild<QComboBox*>(QStringLiteral("gateDiversityModeCombo"));
    CHECK(mode != nullptr);
    for (int i = 0; mode && i < 5; ++i) {
        mode->setCurrentIndex((mode->currentIndex() + 1) % mode->count());
        settle();
    }
    CHECK(net.count(QStringLiteral("/diversity/map")) == baseline + 1);

    // Only the TIMER-DRIVEN poll advances it: kDiversityMapRefreshPolls == 2,
    // so the map is re-fetched on the SECOND subsequent poll, not the first.
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == baseline + 1);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == baseline + 2);

    open->click();
    settle();
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// /diversity and /diversity/map failing repeatedly must never count toward
// m_failures/setPresent() -- only /status decides presence.
void testDiversityAndMapErrorsNeverAffectPresence()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, makeDiversityMap(8)};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::ConnectionRefusedError, {}};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::ConnectionRefusedError, {}};
    for (int i = 0; i < 6; ++i)
        tick(a);

    CHECK(a.gatePresent());
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"))->isHidden());
}
} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_applet_diversity_test"));
    QApplication app(argc, argv);

    testDiversityHiddenUntilAvailableThenHiddenAgain();
    testDiversityStatusLineCarriesModeTalkerAndGain();
    testDiversityStatusLineSaysOffAndEmDashesWhatWasNotMeasured();
    testDiversityStatusLabelWidthIsFixedAgainstTheWorstCasePhrase();
    testDiversityModeComboChangeSendsModeQueryAndPollWritesBack();
    testDiversityScopeHiddenUnlessShowScopeKeyIsSet();
    testDiversityScopeAcceptsNullsAndFullPayloadWithoutCrashingAndComputesGain();
    testDiversityScopeBottomLinesFitAt250pxWithoutEliding();
    testMapPollRunsOnlyWhileTheWindowIsVisible();
    testDiversityMapCadenceIsPollDrivenNotEditDriven();
    testDiversityAndMapErrorsNeverAffectPresence();

    std::printf("\n%d aether gate applet diversity test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
