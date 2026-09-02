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
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityScope.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSignalSpy>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversityScope;

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

// Mode already "off" -- the one state the "Hear A only" compare hold must
// never be armable from (there would be nothing to resume TO).
const QByteArray kDiversityOff = R"({"available": true, "channels": 2,
    "mode": "off", "source": "combined", "phase_deg": 0.0, "ratio_db": 0.0,
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

// A v3 gate: stable talker ids, an operator name for one of them, and a live
// talker -- everything the slimmed sidebar's status line is built from.
const QByteArray kDiversityTalkerAl = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0,
    "memory": [
      {"id": 1, "name": null, "phase_deg": 141.0, "ratio_db": -2.1, "age_s": 5.0,
       "hits": 12},
      {"id": 2, "name": "Al", "phase_deg": 10.0, "ratio_db": 1.0, "age_s": 3.0,
       "hits": 3}
    ],
    "talker": {"id": 2, "since_s": 14.0},
    "capture": {"active": false, "path": null}})";

// The same gate with the UNNAMED talker (#1) on the air instead.
const QByteArray kDiversityTalkerUnnamed = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 43, "slice_id": 0,
    "memory": [
      {"id": 1, "name": null, "phase_deg": 141.0, "ratio_db": -2.1, "age_s": 5.0,
       "hits": 12},
      {"id": 2, "name": "Al", "phase_deg": 10.0, "ratio_db": 1.0, "age_s": 3.0,
       "hits": 3}
    ],
    "talker": {"id": 1, "since_s": 2.0},
    "capture": {"active": false, "path": null}})";

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

    std::printf("\n%d aether gate applet test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
