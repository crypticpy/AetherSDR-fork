// B22 -- the panadapter follows the beacon check's tune.
//
// The SITE page's BEACON CHECK tunes away and comes home, and the gate has no
// tune verb, so the click ends up in AetherGateApplet::onDiversityRequestTune()
// and moves AetherSDR's own active slice. On the air (2026-09-03) the gate log
// read "tuned to 14.131250 MHz" and the slice readout read 14.1 -- while
// SpectrumWidget still reported centerMhz 3.906 and 80 m row edges, painting
// 20 m FFT rows under an 80 m scale.
//
// The cause is one flag. RadioModel::tuneSliceForCat() sends an out-of-span
// tune WITHOUT autopan=0, which asks the RADIO to recentre the pan -- and
// Aether-gate has no autopan: nothing ever writes `display pan set <pan>
// center=`, so the pan model keeps the centre and span it had. The GUI's own
// cross-band tune does not rely on autopan either (applyTuneRequest ->
// revealFrequencyIfNeeded -> applyTuneCenteringWrite -> requestPanCenter), so
// the applet now makes the same call.
//
// Model-level and socket-free: a FLEX-family model -- the family Aether-gate
// presents -- with one pan and one slice claimed from status text. Its
// RadioConnection exists but never reaches TEST-NET-1, which is exactly the
// state a command needs: sendCmd allocates a sequence, so the pan model
// advances on a pan write and stays put without one. Nothing else moves it,
// which is what makes the missing recentre observable at all (an HL2 fixture
// cannot see this bug: its pan centre IS the receiver's NCO, so it follows
// every tune on its own). The applet's transport is the injected fake gate and
// is never used.
#include "DiversityGateFixture.h"

#include "core/RadioDiscovery.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QApplication>
#include <QHostAddress>
#include <QMap>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateDiversityPanel;
using AetherSDR::PanadapterModel;
using AetherSDR::RadioInfo;
using AetherSDR::RadioModel;
using AetherSDR::SliceModel;
using namespace DiversityGateFixture;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failed;                                                          \
        }                                                                        \
    } while (0)

// 80 m, 200 kHz wide: the display the check tuned away from.
constexpr double kHomeMhz = 3.906;
constexpr double kSpanMhz = 0.2;
// The beacon the sweep sat on, in Hz, as the SITE page emits it.
constexpr double kBeaconHz = 14.13125e6;
// The pan the status text claims, and the id every write is addressed to.
const QString kPanId = QStringLiteral("0x40000000");

// A radio with one pan and one active slice on it, built the way a real
// connect builds them: the pan is claimed from a `display pan` status carrying
// our own client handle, then the slice arrives addressed to it.
class Fixture {
public:
    Fixture()
    {
        RadioInfo flex;
        flex.family = QStringLiteral("flex");
        flex.serial = QStringLiteral("0000-0000-0000-0000");
        flex.address = QHostAddress(QStringLiteral("192.0.2.1"));   // TEST-NET-1
        model.connectToRadio(flex);

        QMap<QString, QString> panKvs;
        panKvs[QStringLiteral("client_handle")] = QStringLiteral("0x00000000");
        panKvs[QStringLiteral("center")] = QStringLiteral("3.906000");
        panKvs[QStringLiteral("bandwidth")] = QStringLiteral("0.200000");
        model.handleStatusForTest(QStringLiteral("display pan ") + kPanId, panKvs);

        QMap<QString, QString> sliceKvs;
        sliceKvs[QStringLiteral("in_use")] = QStringLiteral("1");
        sliceKvs[QStringLiteral("active")] = QStringLiteral("1");
        sliceKvs[QStringLiteral("pan")] = kPanId;
        sliceKvs[QStringLiteral("RF_frequency")] = QStringLiteral("3.906000");
        model.handleSliceStatusForTest(0, sliceKvs, false);
    }

    PanadapterModel* pan() const { return model.panadapter(kPanId); }
    SliceModel* slice() const { return model.slice(0); }

    RadioModel model;
};

// The path the SITE page's BEACON CHECK takes: DiversityBeaconPanel emits
// requestTune, DiversityWindow forwards it to AetherGateDiversityPanel, and the
// applet is what turns it into a tune. Driven from the panel's own signal so
// the applet's wiring is part of what is under test.
void requestTune(AetherGateApplet& applet, double hz)
{
    emit applet.diversityPanel()->requestTune(hz);
    settle();
}

// ---------------------------------------------------------------------------

void testAnOutOfSpanTuneRecentresThePan()
{
    Fixture f;
    CHECK(f.pan() != nullptr && f.slice() != nullptr);
    if (!f.pan() || !f.slice())
        return;
    CHECK(f.pan()->centerKnown());
    CHECK(!f.pan()->spanContainsMhz(kBeaconHz / 1.0e6));

    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    applet.setRadioModel(&f.model);

    requestTune(applet, kBeaconHz);

    // THE BUG: before the fix the slice moved and this stayed on 3.906.
    CHECK(qFuzzyCompare(f.slice()->frequency(), kBeaconHz / 1.0e6));
    CHECK(qFuzzyCompare(f.pan()->centerMhz(), kBeaconHz / 1.0e6));
    // The centre moved and the span did not: the operator's zoom is not this
    // path's to change.
    CHECK(qFuzzyCompare(f.pan()->bandwidthMhz(), kSpanMhz));
}

void testAnInSpanTuneLeavesTheDisplayAlone()
{
    Fixture f;
    if (!f.pan() || !f.slice())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    applet.setRadioModel(&f.model);

    // 20 kHz up, well inside a 200 kHz span: moving the display here would be
    // the yank the autopan=0 arm exists to prevent.
    const double nearbyHz = (kHomeMhz + 0.02) * 1.0e6;
    requestTune(applet, nearbyHz);

    CHECK(qFuzzyCompare(f.slice()->frequency(), nearbyHz / 1.0e6));
    CHECK(qFuzzyCompare(f.pan()->centerMhz(), kHomeMhz));
}

void testARefusedTuneMovesNeitherTheSliceNorTheDisplay()
{
    Fixture f;
    if (!f.pan() || !f.slice())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    applet.setRadioModel(&f.model);

    // A locked slice refuses inside SliceModel, and the display must not go
    // somewhere the receiver never went.
    f.slice()->setLocked(true);
    requestTune(applet, kBeaconHz);

    CHECK(qFuzzyCompare(f.slice()->frequency(), kHomeMhz));
    CHECK(qFuzzyCompare(f.pan()->centerMhz(), kHomeMhz));
    f.slice()->setLocked(false);

    // Same for a target that is not a frequency at all.
    requestTune(applet, 0.0);
    CHECK(qFuzzyCompare(f.slice()->frequency(), kHomeMhz));
    CHECK(qFuzzyCompare(f.pan()->centerMhz(), kHomeMhz));
}

void testTheTripHomeRecentresToo()
{
    Fixture f;
    if (!f.pan() || !f.slice())
        return;
    FakeGate net;
    AetherGateApplet applet(nullptr, &net);
    applet.setRadioModel(&f.model);

    requestTune(applet, kBeaconHz);
    requestTune(applet, kHomeMhz * 1.0e6);

    // CANCEL and the end of a sweep both come home the same way; the display
    // has to come back with it, not stay on 20 m.
    CHECK(qFuzzyCompare(f.slice()->frequency(), kHomeMhz));
    CHECK(qFuzzyCompare(f.pan()->centerMhz(), kHomeMhz));
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    testAnOutOfSpanTuneRecentresThePan();
    testAnInSpanTuneLeavesTheDisplayAlone();
    testARefusedTuneMovesNeitherTheSliceNorTheDisplay();
    testTheTripHomeRecentresToo();

    std::printf("\n%d aether gate tune test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
