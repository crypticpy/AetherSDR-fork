// AetherGateApplet — presence state machine and control building, socket-free.
//
// The applet is handed a QNetworkAccessManager whose createRequest() answers
// every URL from a canned table, so each "the gate said X" below is an injected
// reply rather than a peer: no port is opened, nothing is listened on, and a
// wrong answer fails the assertion instead of hanging on a socket.
//
// Every row here is a defect the #5372 review found (or a mutation that would
// re-open one): presence on any HTTP 200, the button that outlives its radio,
// the probe that stops forever after three misses, and numeric controls that
// clamp a device's value to a guessed range.

#include "TestSettingsProfile.h"
#include "gui/AetherGateApplet.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <cstring>

using AetherSDR::AetherGateApplet;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

// A reply that finishes on the next event-loop turn with a fixed body or a
// fixed error, the way a real one would after the socket round trip.
class FakeReply : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest& req, QNetworkReply::NetworkError err,
              const QByteArray& body, const QByteArray& contentType, QObject* parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(req);
        setUrl(req.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (err != QNetworkReply::NoError) {
            setError(err, QStringLiteral("fake error %1").arg(int(err)));
        } else {
            setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(contentType));
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        }
        QTimer::singleShot(0, this, [this, err] {
            if (err != QNetworkReply::NoError)
                emit errorOccurred(err);
            setFinished(true);
            if (!m_body.isEmpty())
                emit readyRead();
            emit finished();
        });
    }

    void abort() override {}
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        return (m_body.size() - m_pos) + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 n = std::min<qint64>(maxSize, m_body.size() - m_pos);
        if (n <= 0)
            return 0;
        std::memcpy(data, m_body.constData() + m_pos, size_t(n));
        m_pos += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_pos{0};
};

struct Canned {
    QNetworkReply::NetworkError error{QNetworkReply::NoError};
    QByteArray body;
    QByteArray contentType{"application/json"};
};

class FakeGate : public QNetworkAccessManager {
public:
    QHash<QString, Canned> routes;      // by URL path
    QStringList log;                    // path?query of every request, in order
    bool down{false};                   // connection refused on everything

    int count(const QString& prefix) const
    {
        int n = 0;
        for (const QString& s : log) {
            if (s.startsWith(prefix))
                ++n;
        }
        return n;
    }

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& req, QIODevice*) override
    {
        const QUrl u = req.url();
        log << u.path() + (u.hasQuery() ? QStringLiteral("?") + u.query() : QString());
        if (down)
            return new FakeReply(req, QNetworkReply::ConnectionRefusedError, {}, {}, this);
        if (!routes.contains(u.path()))
            return new FakeReply(req, QNetworkReply::ContentNotFoundError, {}, {}, this);
        const Canned c = routes.value(u.path());
        return new FakeReply(req, c.error, c.body, c.contentType, this);
    }
};

// Lets the 0 ms reply timers and the applet's finished() handlers run.
void settle()
{
    QTest::qWait(20);
}

void tick(AetherGateApplet& a)
{
    QMetaObject::invokeMethod(&a, "poll", Qt::DirectConnection);
    settle();
}

QTimer* pollTimer(AetherGateApplet& a)
{
    return a.findChild<QTimer*>(QStringLiteral("gatePollTimer"));
}

template <typename W>
W* setting(AetherGateApplet& a, const char* key)
{
    return a.findChild<W*>(QStringLiteral("gateSetting:") + QLatin1String(key));
}

const QByteArray kOldStatus = R"({"connected": false, "peer": null, "streaming": false,
    "paused": false, "pattern": "carrier", "tx": false, "meter_dbm": -130.0})";

const QByteArray kNewStatus = R"({"connected": true, "streaming": true,
    "res": {"bins": 1024, "max_bins": 16384, "span_hz": 2000400.0, "bin_hz": 1953.5,
            "samp_rate": 2000400.0, "rates": [2000000, 2000400, 3200000],
            "can_set_rate": true}})";

// Mixes every ArgInfo type the gate relays, with and without a reported range.
// "lna_state" carries a value outside the old hardcoded ±1000 on purpose.
const QByteArray kDevice = R"({
    "antenna": {"value": "Antenna B", "options": ["Antenna A", "Antenna B"]},
    "settings": [
      {"key": "agc_setpoint", "name": "AGC set-point", "type": "1", "value": "-30",
       "range": {"min": -72, "max": -20, "step": 1}},
      {"key": "corr_ppm", "name": "Correction", "type": "2", "value": "2.5",
       "range": {"min": -1000, "max": 1000, "step": 0.1}},
      {"key": "lna_state", "name": "LNA state", "type": "1", "value": "2500"},
      {"key": "biasT_ctrl", "name": "Bias-T", "type": "0", "value": "false"},
      {"key": "if_mode", "name": "IF mode", "type": "3", "value": "Low-IF",
       "options": ["Zero-IF", "Low-IF"]},
      {"key": "note", "name": "Note", "type": "3", "value": "hello"}
    ]})";

const QByteArray kHtml = "<html><body>Aether-gate web panel</body></html>";

const QByteArray kDiversityUnavailable = R"({"available": false})";

const QByteArray kDiversityManual = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0})";

const QByteArray kDiversityTrack = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 10.0, "ratio_db": 0.0,
    "weight": [1.0, 0.0], "lag_samples": 0, "aligned": false, "corr_peak": 0.0,
    "snr_db": {"a": null, "b": null, "out": null}, "updates": 0, "slice_id": 0})";

// ---------------------------------------------------------------------------

void testNoAddressNeverAsks()
{
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    tick(a);
    CHECK(net.log.isEmpty());
    CHECK(!pollTimer(a)->isActive());
    CHECK(!a.gatePresent());
}

// An older gate: /status without "res", /device falling through to HTML.
// It is still a gate — present, resolution rows folded away, and a hint
// instead of a silently empty controls box.
void testOldGateIsPresentWithoutResolutionOrControls()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kOldStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kHtml, "text/html"};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(spy.count() == 1 && spy.at(0).at(0).toBool());
    CHECK(net.count(QStringLiteral("/status")) == 1);
    CHECK(net.count(QStringLiteral("/device")) == 1);
    CHECK(a.findChild<QLabel*>(QStringLiteral("gateStatusLabel"))->text().contains(
        QStringLiteral("waiting")));
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateResolutionBox"))->isHidden());
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDeviceBox"))->isHidden());
    CHECK(!a.findChild<QLabel*>(QStringLiteral("gateDeviceHint"))->isHidden());
}

// A port that answers HTTP 200 with something other than a gate's JSON (the
// old code took any success as presence) never confirms one, and the applet
// keeps re-asking slowly instead of giving up.
void testNonJsonAnswerIsNotAGate()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kHtml, "text/html"};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    for (int i = 0; i < 4; ++i)
        tick(a);

    CHECK(!a.gatePresent());
    CHECK(spy.isEmpty());
    CHECK(pollTimer(a)->isActive());
    CHECK(pollTimer(a)->interval() == 15000);
}

// A current gate: resolution rows populated, the running rate picked by its
// RAW value where two labels collide, and every control typed and bounded by
// what the gate reported — with a value beyond the old ±1000 surviving intact.
void testNewGateBuildsTypedBoundedControls()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(!a.findChild<QWidget*>(QStringLiteral("gateResolutionBox"))->isHidden());
    CHECK(a.findChild<QLabel*>(QStringLiteral("gateDeviceHint"))->isHidden());
    CHECK(!a.findChild<QWidget*>(QStringLiteral("gateDeviceBox"))->isHidden());

    auto* span = a.findChild<QComboBox*>(QStringLiteral("gateSpanCombo"));
    CHECK(span->count() == 3);
    CHECK(span->itemText(0) == span->itemText(1));        // the label collision
    CHECK(span->currentIndex() == 1);                     // raw 2000400, not 2000000
    CHECK(span->isEnabled());

    auto* bins = a.findChild<QComboBox*>(QStringLiteral("gateBinsCombo"));
    CHECK(bins->count() == 5);
    CHECK(bins->currentText() == QStringLiteral("1024"));

    auto* antenna = a.findChild<QComboBox*>(QStringLiteral("gateAntennaCombo"));
    CHECK(antenna && antenna->currentText() == QStringLiteral("Antenna B"));

    auto* agc = setting<QSpinBox>(a, "agc_setpoint");
    CHECK(agc && agc->minimum() == -72 && agc->maximum() == -20 && agc->value() == -30);

    auto* corr = setting<QDoubleSpinBox>(a, "corr_ppm");
    CHECK(corr && corr->decimals() == 1 && corr->minimum() == -1000.0
          && corr->maximum() == 1000.0 && corr->value() == 2.5);

    auto* lna = setting<QSpinBox>(a, "lna_state");
    CHECK(lna && lna->value() == 2500);

    auto* bias = setting<QCheckBox>(a, "biasT_ctrl");
    CHECK(bias && !bias->isChecked());

    auto* ifMode = setting<QComboBox>(a, "if_mode");
    CHECK(ifMode && ifMode->currentText() == QStringLiteral("Low-IF"));

    auto* note = setting<QLineEdit>(a, "note");
    CHECK(note && note->text() == QStringLiteral("hello"));

    // Populating from the gate must never look like an operator choice.
    CHECK(net.count(QStringLiteral("/device/set")) == 0);
    CHECK(net.count(QStringLiteral("/resolution")) == 0);
}

// One operator change → one write; the read-back that arrives with the reply
// updates the control without echoing a second write.
void testPushWritesOnceAndReadBackDoesNotEcho()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    QByteArray after = kDevice;
    after.replace("\"value\": \"false\"", "\"value\": \"true\"");
    net.routes[QStringLiteral("/device/set")] = {QNetworkReply::NoError, after};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* bias = setting<QCheckBox>(a, "biasT_ctrl");
    CHECK(bias && !bias->isChecked());
    bias->setChecked(true);
    settle();

    CHECK(net.count(QStringLiteral("/device/set")) == 1);
    CHECK(net.log.contains(QStringLiteral("/device/set?key=biasT_ctrl&value=true")));
    CHECK(bias->isChecked());

    // The read-back must not be re-sent, and a further poll's refresh of the
    // same values must not be either.
    tick(a);
    CHECK(net.count(QStringLiteral("/device/set")) == 1);
}

// Three refused polls off screen: absent, button gone — but the probe keeps
// going at the slow cadence, and the gate's return is noticed unaided.
void testBlipRecoversWithoutHelp()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());
    CHECK(!pollTimer(a)->isActive());        // hidden and found: nothing to learn

    net.down = true;
    tick(a);
    tick(a);
    CHECK(a.gatePresent());                  // two misses are a blip
    tick(a);
    CHECK(!a.gatePresent());                 // three are an absence
    CHECK(spy.count() == 2 && !spy.at(1).at(0).toBool());
    CHECK(pollTimer(a)->isActive());         // ...but the door stays open
    CHECK(pollTimer(a)->interval() == 15000);

    net.down = false;
    tick(a);
    CHECK(a.gatePresent());
    CHECK(spy.count() == 3 && spy.at(2).at(0).toBool());
    CHECK(!pollTimer(a)->isActive());
}

// On screen the cadence is one second; hiding a found gate parks the timer.
void testVisibleCadence()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    const int before = net.count(QStringLiteral("/status"));

    a.show();
    settle();
    CHECK(net.count(QStringLiteral("/status")) == before + 1);   // showEvent polls at once
    CHECK(pollTimer(a)->isActive());
    CHECK(pollTimer(a)->interval() == 1000);

    a.hide();
    CHECK(!pollTimer(a)->isActive());
}

// The radio's down edge drops presence at once and stops every write; the
// same address coming back is asked again from scratch.
void testDownEdgeDropsPresenceAndReprobesOnReturn()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/device/set")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);
    QSignalSpy spy(&a, &AetherGateApplet::gatePresenceChanged);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());

    a.setRadioAddress(QString());
    CHECK(!a.gatePresent());
    CHECK(spy.count() == 2 && !spy.at(1).at(0).toBool());
    CHECK(!pollTimer(a)->isActive());

    // The widgets are still alive under the cursor for a moment; a change on
    // one must not go anywhere.
    const int writes = net.log.size();
    if (auto* bias = setting<QCheckBox>(a, "biasT_ctrl"))
        bias->setChecked(true);
    settle();
    CHECK(net.log.size() == writes);
    tick(a);
    CHECK(net.log.size() == writes);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    CHECK(a.gatePresent());
    CHECK(spy.count() == 3 && spy.at(2).at(0).toBool());
}

// No /diversity route (an old gate) or "available": false (a gate whose
// device isn't a dual-tuner) both mean no diversity section — not a silently
// empty one.
void testDiversityHiddenWhenUnavailableOrMissing()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    CHECK(a.gatePresent());     // /diversity 404ing must not affect presence
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"))->isHidden());

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityUnavailable};
    tick(a);
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"))->isHidden());
}

// Manual is the only mode that takes a phase/ratio setpoint; Track (and Null,
// Off) solve/hold their own, so editing either there would write a value the
// gate ignores.
void testDiversityManualEnablesPhaseRatioOtherModesDisable()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityManual};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    CHECK(!a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"))->isHidden());
    auto* phase = a.findChild<QSlider*>(QStringLiteral("gateDiversityPhaseSlider"));
    auto* ratio = a.findChild<QDoubleSpinBox*>(QStringLiteral("gateDiversityRatioSpin"));
    CHECK(phase && phase->isEnabled() && phase->value() == 45);
    CHECK(ratio && ratio->isEnabled() && ratio->value() == -2.5);

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityTrack};
    tick(a);
    CHECK(!phase->isEnabled());
    CHECK(!ratio->isEnabled());
}

// snr_db's a/b/out members are individually nullable — the status line must
// show a dash for each, not "0.0" (which reads as a real, terrible reading).
void testDiversityStatusLineRendersNullSnrAsDashes()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityTrack};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    const QString text =
        a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"))->text();
    CHECK(text.contains(QStringLiteral("SNR —/—/— dB")));
    CHECK(text.contains(QStringLiteral("lag 0 samples")));
}

// A phase-slider drag must debounce to one write carrying phase= alone, not
// ratio= riding along on the same request.
void testDiversityPhaseChangeSendsPhaseOnly()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityManual};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, kDiversityManual};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();

    auto* phase = a.findChild<QSlider*>(QStringLiteral("gateDiversityPhaseSlider"));
    CHECK(phase && phase->isEnabled());
    phase->setValue(200);
    QTest::qWait(250);           // past the ~150ms debounce
    settle();

    CHECK(net.count(QStringLiteral("/diversity/set")) == 1);
    CHECK(net.log.contains(QStringLiteral("/diversity/set?phase=200")));
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("aether_gate_applet_test"));
    QApplication app(argc, argv);

    testNoAddressNeverAsks();
    testOldGateIsPresentWithoutResolutionOrControls();
    testNonJsonAnswerIsNotAGate();
    testNewGateBuildsTypedBoundedControls();
    testPushWritesOnceAndReadBackDoesNotEcho();
    testBlipRecoversWithoutHelp();
    testVisibleCadence();
    testDownEdgeDropsPresenceAndReprobesOnReturn();
    testDiversityHiddenWhenUnavailableOrMissing();
    testDiversityManualEnablesPhaseRatioOtherModesDisable();
    testDiversityStatusLineRendersNullSnrAsDashes();
    testDiversityPhaseChangeSendsPhaseOnly();

    std::printf("\n%d aether gate applet test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
