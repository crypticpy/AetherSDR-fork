#pragma once

// The socket-free harness the two AetherGateApplet tests share: a gate whose
// replies are a canned table rather than a peer, the canned bodies themselves,
// and the handful of helpers that tick the applet's poll and reach a control by
// objectName.
//
// It exists for the same reason tests/AetherGateChainFixture.h does. The applet
// has two separate stories to tell -- the presence state machine and the
// controls it builds out of what the gate reports, and everything the diversity
// section polls -- and one file holding both was over the 800-line limit
// AGENTS.md sets. The gate payloads are what both stories are told against, so
// they live here rather than being copied.
//
// Everything is header-defined and nothing opens a port.

#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <cstring>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;

namespace AetherGateAppletFixture {

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
inline void settle()
{
    QTest::qWait(20);
}

inline void tick(AetherGateApplet& a)
{
    QMetaObject::invokeMethod(&a, "poll", Qt::DirectConnection);
    settle();
}

inline QTimer* pollTimer(AetherGateApplet& a)
{
    return a.findChild<QTimer*>(QStringLiteral("gatePollTimer"));
}

template <typename W>
W* setting(AetherGateApplet& a, const char* key)
{
    return a.findChild<W*>(QStringLiteral("gateSetting:") + QLatin1String(key));
}

inline const QByteArray kOldStatus = R"({"connected": false, "peer": null, "streaming": false,
    "paused": false, "pattern": "carrier", "tx": false, "meter_dbm": -130.0})";

inline const QByteArray kNewStatus = R"({"connected": true, "streaming": true,
    "res": {"bins": 1024, "max_bins": 16384, "span_hz": 2000400.0, "bin_hz": 1953.5,
            "samp_rate": 2000400.0, "rates": [2000000, 2000400, 3200000],
            "can_set_rate": true}})";

// Mixes every ArgInfo type the gate relays, with and without a reported range.
// "lna_state" carries a value outside the old hardcoded ±1000 on purpose.
inline const QByteArray kDevice = R"({
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

inline const QByteArray kHtml = "<html><body>Aether-gate web panel</body></html>";

inline const QByteArray kDiversityUnavailable = R"({"available": false})";

inline const QByteArray kDiversityManual = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0})";

inline const QByteArray kDiversityTrack = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 10.0, "ratio_db": 0.0,
    "weight": [1.0, 0.0], "lag_samples": 0, "aligned": false, "corr_peak": 0.0,
    "snr_db": {"a": null, "b": null, "out": null}, "updates": 0, "slice_id": 0})";

// Mode already "off" -- the one state the "Hear A only" compare hold must
// never be armable from (there would be nothing to resume TO).
inline const QByteArray kDiversityOff = R"({"available": true, "channels": 2,
    "mode": "off", "source": "combined", "phase_deg": 0.0, "ratio_db": 0.0,
    "weight": [1.0, 0.0], "lag_samples": 0, "aligned": false, "corr_peak": 0.0,
    "snr_db": {"a": null, "b": null, "out": null}, "updates": 0, "slice_id": 0})";

// A v2 gate: everything kDiversityManual carries, plus every field this PR
// adds -- nb, pan, sources, memory, rn_source, talk_mod, capture.
inline const QByteArray kDiversityV2 = R"({"available": true, "channels": 2,
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
inline const QByteArray kDiversityTalkerAl = R"({"available": true, "channels": 2,
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
inline const QByteArray kDiversityTalkerUnnamed = R"({"available": true, "channels": 2,
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
inline QByteArray makeDiversityMap(int points)
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

inline const QByteArray kDiversityMapError = R"({"error": "no map yet"})";

} // namespace AetherGateAppletFixture
