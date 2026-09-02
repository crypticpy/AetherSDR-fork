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
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
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

// A v2 gate: everything kDiversityManual carries, plus every field this PR
// adds -- nb, pan, sources, memory, rn_source, talk_mod, capture.
const QByteArray kDiversityV2 = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0,
    "nb": {"enabled": true, "threshold_db": 18.5, "blanked_pct": 3.2},
    "pan": "nulled",
    "sources": [
      {"lo_hz": 3512000.0, "hi_hz": 3560000.0, "phase_deg": 141.0, "ratio_db": -2.1,
       "coherence": 0.82, "level_db": -40.0},
      {"lo_hz": 7030000.0, "hi_hz": 7040000.0, "phase_deg": 10.0, "ratio_db": 1.0,
       "coherence": 0.55, "level_db": -55.0}
    ],
    "memory": [
      {"phase_deg": 141.0, "ratio_db": -2.1, "age_s": 5.0, "hits": 12},
      {"phase_deg": 10.0, "ratio_db": 1.0, "age_s": 20.0, "hits": 3}
    ],
    "rn_source": "guard", "talk_mod": 0.62,
    "capture": {"active": false, "path": null}})";

// Same as kDiversityV2 but with a capture in flight -- the button/label must
// read the gate's own live state, not whatever the trigger response said.
const QByteArray kDiversityV2Capturing = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0,
    "capture": {"active": true, "path": null}})";

// A /diversity/map answer with `points` coherence samples and one source.
QByteArray makeDiversityMap(int points)
{
    QByteArray coherence = "[";
    for (int i = 0; i < points; ++i) {
        if (i)
            coherence += ",";
        coherence += QByteArray::number(double(i) / double(points), 'f', 4);
    }
    coherence += "]";
    QByteArray body = "{\"start_hz\": 3500000.0, \"step_hz\": 100.0, \"coherence\": ";
    body += coherence;
    body += R"(, "level_db": [], "sources": [
        {"lo_hz": 3512000.0, "hi_hz": 3560000.0}]})";
    return body;
}

const QByteArray kDiversityMapError = R"({"error": "no map yet"})";

// Same 2 sources as kDiversityV2 but with the SECOND one (7.030-7.040 MHz)
// dropped -- for the shrinking-array test below.
const QByteArray kDiversityV2ShrunkSources = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 43, "slice_id": 0,
    "sources": [
      {"lo_hz": 3512000.0, "hi_hz": 3560000.0, "phase_deg": 141.0, "ratio_db": -2.1,
       "coherence": 0.82, "level_db": -40.0}
    ]})";

// capture.active == false with a real (non-null) "path" -- the last
// SUCCESSFUL capture's own result, as opposed to kDiversityV2's null one.
const QByteArray kDiversityV2WithOldCapturePath = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0,
    "capture": {"active": false, "path": "/tmp/old_capture.wav"}})";

// "nb" as a bare bool rather than the {"enabled", "threshold_db"} object --
// toObject() on this silently returns {}, which without a guard would read
// as "blanker off, threshold 0" and stomp both nb widgets.
const QByteArray kDiversityV2MalformedNb = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0,
    "nb": true})";

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

// Every v2 field is independently optional -- a gate that carries all of
// them at once parses each into its own widget.
void testDiversityV2FieldsParseIntoWidgets()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(!a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"))->isHidden());

    auto* nbCheck = a.findChild<QCheckBox*>(QStringLiteral("gateDiversityNbCheck"));
    auto* nbSpin = a.findChild<QDoubleSpinBox*>(QStringLiteral("gateDiversityNbSpin"));
    CHECK(nbCheck && nbCheck->isChecked());
    CHECK(nbSpin && nbSpin->value() == 18.5);

    auto* pan = a.findChild<QComboBox*>(QStringLiteral("gateDiversityPanCombo"));
    CHECK(pan && pan->currentData().toString() == QStringLiteral("nulled"));

    auto* sources = a.findChild<QListWidget*>(QStringLiteral("gateDiversitySourcesList"));
    CHECK(sources && sources->count() == 2);
    CHECK(sources->item(0)->text()
          == QStringLiteral("3.512–3.560 MHz · coh 0.82 · 141° · −2.1 dB"));
    CHECK(sources->item(1)->text()
          == QStringLiteral("7.030–7.040 MHz · coh 0.55 · 10° · 1.0 dB"));

    auto* memory = a.findChild<QLabel*>(QStringLiteral("gateDiversityMemoryLabel"));
    CHECK(memory && memory->text() == QStringLiteral("memory: 2 talkers"));

    auto* captureButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityCaptureButton"));
    auto* captureLabel = a.findChild<QLabel*>(QStringLiteral("gateDiversityCaptureLabel"));
    CHECK(captureButton && captureButton->isEnabled());       // capture.active == false
    CHECK(captureLabel && captureLabel->text() == QStringLiteral("—"));  // path == null

    auto* status = a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"));
    CHECK(status && status->text().contains(QStringLiteral("rn guard")));
    CHECK(status->text().contains(QStringLiteral("mod 0.62")));
}

// An older gate's /diversity carries none of the v2 keys -- every new widget
// must sit exactly where its constructor left it, not at some invented
// "off"/"cleared" state the applet made up.
void testOldDiversityPayloadLeavesV2WidgetsAtDefaults()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityManual};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    CHECK(a.gatePresent());

    auto* nbCheck = a.findChild<QCheckBox*>(QStringLiteral("gateDiversityNbCheck"));
    auto* nbSpin = a.findChild<QDoubleSpinBox*>(QStringLiteral("gateDiversityNbSpin"));
    CHECK(nbCheck && !nbCheck->isChecked());
    CHECK(nbSpin && nbSpin->value() == 0.0);

    auto* pan = a.findChild<QComboBox*>(QStringLiteral("gateDiversityPanCombo"));
    CHECK(pan && pan->currentIndex() == 0);        // untouched constructor default ("A")

    auto* sources = a.findChild<QListWidget*>(QStringLiteral("gateDiversitySourcesList"));
    CHECK(sources && sources->count() == 0);

    auto* memory = a.findChild<QLabel*>(QStringLiteral("gateDiversityMemoryLabel"));
    CHECK(memory && memory->text() == QStringLiteral("memory: 0 talkers"));

    auto* captureButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityCaptureButton"));
    auto* captureLabel = a.findChild<QLabel*>(QStringLiteral("gateDiversityCaptureLabel"));
    CHECK(captureButton && captureButton->isEnabled());
    CHECK(captureLabel && captureLabel->text() == QStringLiteral("—"));

    auto* status = a.findChild<QLabel*>(QStringLiteral("gateDiversityStatusLabel"));
    CHECK(status && !status->text().contains(QStringLiteral("rn ")));
    CHECK(status && !status->text().contains(QStringLiteral("mod ")));

    // No /diversity/map route configured either -- an old gate 404s it, and
    // that must not crash or affect presence any more than /diversity itself
    // 404ing does.
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDiversityMapStrip")) != nullptr);
}

// nb/nb_db/pan/null_source/capture/memory-clear each produce their own exact
// query string, on the same /diversity/set (or dedicated) route the rest of
// the section already writes through.
void testDiversityV2QueryStrings()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/capture")] = {
        QNetworkReply::NoError, R"({"ok": true, "path": "/tmp/capture.wav"})"};
    net.routes[QStringLiteral("/diversity/memory/clear")] = {QNetworkReply::NoError,
                                                              R"({"ok": true})"};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
    CHECK(a.gatePresent());

    // nb arrived checked (kDiversityV2's "enabled": true) -- toggling it off
    // sends nb=off.
    auto* nbCheck = a.findChild<QCheckBox*>(QStringLiteral("gateDiversityNbCheck"));
    CHECK(nbCheck && nbCheck->isChecked());
    nbCheck->setChecked(false);
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/set?nb=off")));

    auto* nbSpin = a.findChild<QDoubleSpinBox*>(QStringLiteral("gateDiversityNbSpin"));
    nbSpin->setValue(22.0);
    QTest::qWait(250);             // past the ~150ms debounce
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/set?nb_db=22.0")));

    auto* pan = a.findChild<QComboBox*>(QStringLiteral("gateDiversityPanCombo"));
    pan->setCurrentIndex(pan->findData(QStringLiteral("combined")));
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/set?pan=combined")));

    auto* sources = a.findChild<QListWidget*>(QStringLiteral("gateDiversitySourcesList"));
    auto* nullButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityNullSourceButton"));
    CHECK(sources && sources->count() == 2 && nullButton && !nullButton->isEnabled());
    sources->setCurrentRow(1);
    CHECK(nullButton->isEnabled());
    nullButton->click();
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/set?null_source=1")));

    auto* captureButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityCaptureButton"));
    captureButton->click();
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/capture?seconds=10")));
    auto* captureLabel = a.findChild<QLabel*>(QStringLiteral("gateDiversityCaptureLabel"));
    CHECK(captureLabel && captureLabel->text() == QStringLiteral("/tmp/capture.wav"));

    auto* memoryClear =
        a.findChild<QPushButton*>(QStringLiteral("gateDiversityMemoryClearButton"));
    memoryClear->click();
    settle();
    CHECK(net.log.contains(QStringLiteral("/diversity/memory/clear")));
}

// capture.active is the gate's OWN live state (not our click), so the label
// and button follow it even on a poll where the operator did nothing.
void testDiversityCaptureActiveShowsRecordingAndDisablesButton()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2Capturing};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    auto* captureLabel = a.findChild<QLabel*>(QStringLiteral("gateDiversityCaptureLabel"));
    auto* captureButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityCaptureButton"));
    CHECK(captureLabel && captureLabel->text() == QStringLiteral("recording…"));
    CHECK(captureButton && !captureButton->isEnabled());
}

// The map strip takes a full 256-point coherence array without crashing, and
// an {"error"} reply (no map yet) clears it just as cleanly -- same
// non-critical-to-presence contract as every other diversity route.
void testDiversityMapStripAcceptsFullArrayAndErrorWithoutCrashing()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError,
                                                     makeDiversityMap(256)};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDiversityMapStrip")) != nullptr);
    CHECK(net.count(QStringLiteral("/diversity/map")) == 1);

    // Switch to an {"error"} reply for the next throttled fetch
    // (kDiversityMapRefreshPolls == 2 ticks) and confirm it lands cleanly.
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    tick(a);
    tick(a);
    settle();
    CHECK(net.count(QStringLiteral("/diversity/map")) == 2);
    CHECK(a.gatePresent());
}

// The /diversity/map throttle lives in the TIMER-DRIVEN poll path only.
// applyDiversity() is also the read-back handler for sendDiversitySet(), so
// an operator round-tripping an edit through it must never itself advance
// (or trigger) the map's own cadence -- only the periodic /status+/diversity
// poll may (#5372-round-2 finding: the throttle used to live inside
// applyDiversity() itself, so edits counted).
void testDiversityMapCadenceIsPollDrivenNotEditDriven()
{
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
    settle();
    CHECK(a.gatePresent());
    CHECK(net.count(QStringLiteral("/diversity/map")) == 1);   // fetched once, up front

    // Five full sendDiversitySet() round trips, each running applyDiversity()
    // on its own read-back -- none of these may advance the map's cadence.
    auto* pan = a.findChild<QComboBox*>(QStringLiteral("gateDiversityPanCombo"));
    for (int i = 0; i < 5; ++i) {
        pan->setCurrentIndex((pan->currentIndex() + 1) % pan->count());
        settle();
    }
    CHECK(net.count(QStringLiteral("/diversity/map")) == 1);

    // Only the TIMER-DRIVEN poll advances it: kDiversityMapRefreshPolls == 2,
    // so the map is re-fetched on the SECOND subsequent poll, not the first.
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == 1);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == 2);
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
    settle();
    CHECK(a.gatePresent());

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::ConnectionRefusedError, {}};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::ConnectionRefusedError, {}};
    for (int i = 0; i < 6; ++i)
        tick(a);

    CHECK(a.gatePresent());
    CHECK(a.findChild<QWidget*>(QStringLiteral("gateDiversityBox"))->isHidden());
}

// A source the operator selected can vanish from the array between polls
// (the gate stopped hearing it). The Null button must never silently
// re-target the SURVIVING-but-different source at that row -- it must
// disable instead, since nothing on screen still names what was selected.
void testShrinkingSourcesNeverRetargetsNullToADifferentSource()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    auto* sources = a.findChild<QListWidget*>(QStringLiteral("gateDiversitySourcesList"));
    auto* nullButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityNullSourceButton"));
    CHECK(sources && sources->count() == 2);
    sources->setCurrentRow(1);                 // the 7.030-7.040 MHz source
    CHECK(nullButton->isEnabled());

    // The gate's next answer drops that source entirely, leaving only the
    // OTHER (3.512-3.560 MHz) one at row 0.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2ShrunkSources};
    tick(a);

    CHECK(sources->count() == 1);
    CHECK(!nullButton->isEnabled());
    CHECK(sources->currentRow() == -1);
}

// setPresent(false) must clear every v2 row/label that could otherwise
// outlive a reconnect to a DIFFERENT (older) gate at the same address.
void testPresentFalseThenV1GateLeavesSourcesEmptyAndNullDisabled()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    auto* sources = a.findChild<QListWidget*>(QStringLiteral("gateDiversitySourcesList"));
    auto* nullButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityNullSourceButton"));
    sources->setCurrentRow(0);
    CHECK(sources->count() == 2);
    CHECK(nullButton->isEnabled());

    a.setRadioAddress(QString());               // present -> false
    CHECK(!a.gatePresent());
    CHECK(sources->count() == 0);
    CHECK(!nullButton->isEnabled());

    // Reconnect to an OLDER gate: /diversity carries none of the v2 keys.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityManual};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    CHECK(a.gatePresent());
    CHECK(sources->count() == 0);
    CHECK(!nullButton->isEnabled());
}

// An {"error": ...} reply from THIS request must survive the very next poll,
// even when that poll's own capture.path is a real (non-empty) "last
// successful capture" that would otherwise silently paper over the error.
void testCaptureErrorTextSurvivesNextPoll()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError,
                                                kDiversityV2WithOldCapturePath};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    net.routes[QStringLiteral("/diversity/capture")] = {
        QNetworkReply::NoError, R"({"error": "already recording"})"};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    auto* captureLabel = a.findChild<QLabel*>(QStringLiteral("gateDiversityCaptureLabel"));
    CHECK(captureLabel->text() == QStringLiteral("/tmp/old_capture.wav"));

    auto* captureButton = a.findChild<QPushButton*>(QStringLiteral("gateDiversityCaptureButton"));
    captureButton->click();
    settle();
    CHECK(captureLabel->text() == QStringLiteral("already recording"));

    // The next poll still reports the SAME stale successful path -- it must
    // not silently replace the error this request itself just reported.
    tick(a);
    CHECK(captureLabel->text() == QStringLiteral("already recording"));
}

// "nb": true is not an object; only apply its fields when it is one, so a
// malformed payload leaves both nb widgets exactly where they were rather
// than reading toObject()'s silent {} as "blanker off, threshold 0".
void testMalformedNonObjectNbLeavesWidgetsUntouched()
{
    FakeGate net;
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kNewStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityV2MalformedNb};
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, kDiversityMapError};
    AetherGateApplet a(nullptr, &net);

    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();

    CHECK(a.gatePresent());
    auto* nbCheck = a.findChild<QCheckBox*>(QStringLiteral("gateDiversityNbCheck"));
    auto* nbSpin = a.findChild<QDoubleSpinBox*>(QStringLiteral("gateDiversityNbSpin"));
    CHECK(nbCheck && !nbCheck->isChecked());
    CHECK(nbSpin && nbSpin->value() == 0.0);
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
    testDiversityV2FieldsParseIntoWidgets();
    testOldDiversityPayloadLeavesV2WidgetsAtDefaults();
    testDiversityV2QueryStrings();
    testDiversityCaptureActiveShowsRecordingAndDisablesButton();
    testDiversityMapStripAcceptsFullArrayAndErrorWithoutCrashing();
    testDiversityMapCadenceIsPollDrivenNotEditDriven();
    testDiversityAndMapErrorsNeverAffectPresence();
    testShrinkingSourcesNeverRetargetsNullToADifferentSource();
    testPresentFalseThenV1GateLeavesSourcesEmptyAndNullDisabled();
    testCaptureErrorTextSurvivesNextPoll();
    testMalformedNonObjectNbLeavesWidgetsUntouched();

    std::printf("\n%d aether gate applet test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
