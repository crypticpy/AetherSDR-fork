#pragma once

// The socket-free gate harness the diversity window tests run against: the
// fake QNetworkAccessManager tests/aether_gate_applet_test.cpp uses, plus the
// /diversity payloads this window is the only reader of.
//
// It lives in its own header because diversity_window_test.cpp is a test of
// widget behaviour and the transport is not part of that story -- and because
// the payload constants are long enough that having them inline pushed the
// case list off the bottom of the file.
//
// Everything here has internal linkage by construction (inline constants,
// header-defined classes); nothing opens a port or touches a real gate.

#include "gui/AetherGateApplet.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace DiversityGateFixture {

using AetherSDR::AetherGateApplet;

// Same fake transport as tests/aether_gate_applet_test.cpp: a reply that
// finishes on the next event-loop turn with a fixed body or a fixed error.
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

void settle()
{
    QTest::qWait(20);
}

void tick(AetherGateApplet& a)
{
    QMetaObject::invokeMethod(&a, "poll", Qt::DirectConnection);
    settle();
}

inline const QByteArray kStatus = R"({"connected": true, "streaming": true,
    "res": {"bins": 1024, "max_bins": 16384, "span_hz": 2000400.0, "bin_hz": 1953.5,
            "samp_rate": 2000400.0, "rates": [2000000, 2000400, 3200000],
            "can_set_rate": true}})";

inline const QByteArray kDevice = R"({
    "antenna": {"value": "Antenna B", "options": ["Antenna A", "Antenna B"]},
    "settings": [
      {"key": "lna_state", "name": "LNA state", "type": "1", "value": "2"}
    ]})";

// Every v2/v3 field the window reads, including the two it alone shows
// (steady_qrm, passband) and the memory list its stations table renders.
inline const QByteArray kDiversityFull = R"({"available": true, "channels": 2,
    "mode": "manual", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "weight": [0.7, 0.1], "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42, "slice_id": 0,
    "nb": {"enabled": true, "threshold_db": 18.5, "blanked_pct": 3.2},
    "pan": "nulled",
    "sources": [
      {"lo_hz": 3512000.0, "hi_hz": 3560000.0, "phase_deg": 141.0, "ratio_db": -2.1,
       "coherence": 0.82, "level_db": -40.0}
    ],
    "memory": [
      {"phase_deg": 141.0, "ratio_db": -2.1, "age_s": 5.0, "hits": 12},
      {"phase_deg": 10.0, "ratio_db": 1.0, "age_s": 20.0, "hits": 3}
    ],
    "rn_source": "guard", "noise_coherence": 0.07, "talk_mod": 0.62,
    "steady_qrm": true,
    "passband": {"flatness": 0.87, "phase_slope_deg_per_khz": -2.1, "coherence": 0.62},
    "capture": {"active": false, "path": null}})";

// Same shape with every optional leg NULL and no v2/v3 blocks at all -- the
// "gate is here but has nothing measured yet" payload.
inline const QByteArray kDiversityNulls = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 10.0, "ratio_db": 0.0,
    "weight": [1.0, 0.0], "lag_samples": null, "aligned": false, "corr_peak": null,
    "snr_db": {"a": null, "b": null, "out": null}, "updates": 0, "slice_id": 0,
    "rn_source": null, "capture": {"active": false, "path": null}})";

// v3 memory: stable ids, operator names, first_seen_s, and a live talker.
inline const QByteArray kDiversityTalkers = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42,
    "nb": {"enabled": true, "threshold_db": 18.5, "blanked_pct": 3.2},
    "memory": [
      {"id": 1, "name": null, "phase_deg": 141.0, "ratio_db": -2.1,
       "age_s": 5.0, "first_seen_s": 300.0, "hits": 12},
      {"id": 2, "name": "Al", "phase_deg": 10.0, "ratio_db": 1.0,
       "age_s": 3.0, "first_seen_s": 240.0, "hits": 3},
      {"id": 5, "name": "Kay", "phase_deg": 200.0, "ratio_db": -4.0,
       "age_s": 90.0, "first_seen_s": 7200.0, "hits": 1}
    ],
    "talker": {"id": 2, "since_s": 14.0},
    "rn_source": "guard", "noise_coherence": 0.42,
    "steady_qrm": false,
    "passband": {"flatness": 0.98, "phase_slope_deg_per_khz": -0.7, "coherence": 0.62},
    "capture": {"active": false, "path": null}})";

// The same three talkers with nobody on the air, and #2 renamed by somebody
// else -- the payload a poll arriving mid-edit would carry.
inline const QByteArray kDiversityTalkersIdle = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 43,
    "memory": [
      {"id": 1, "name": null, "phase_deg": 141.0, "ratio_db": -2.1,
       "age_s": 6.0, "first_seen_s": 301.0, "hits": 12},
      {"id": 2, "name": "Zed", "phase_deg": 10.0, "ratio_db": 1.0,
       "age_s": 4.0, "first_seen_s": 241.0, "hits": 3},
      {"id": 5, "name": "Kay", "phase_deg": 200.0, "ratio_db": -4.0,
       "age_s": 91.0, "first_seen_s": 7201.0, "hits": 1}
    ],
    "talker": null,
    "rn_source": "guard", "noise_coherence": 0.42,
    "capture": {"active": false, "path": null}})";

// Locked on #2 while #5 is on the air: the gate is nulling Kay, not
// steering at her, and says so.
inline const QByteArray kDiversityFocusNulling = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 2.1}, "updates": 44,
    "memory": [
      {"id": 1, "name": null, "phase_deg": 141.0, "ratio_db": -2.1,
       "age_s": 6.0, "first_seen_s": 301.0, "hits": 12},
      {"id": 2, "name": "Al", "phase_deg": 10.0, "ratio_db": 1.0,
       "age_s": 40.0, "first_seen_s": 241.0, "hits": 3},
      {"id": 5, "name": "Kay", "phase_deg": 200.0, "ratio_db": -4.0,
       "age_s": 2.0, "first_seen_s": 7201.0, "hits": 2}
    ],
    "talker": {"id": 5, "since_s": 2.0},
    "focus": {"id": 2, "name": "Al", "since_s": 30.0, "live": false, "nulling": true,
              "overs": 3, "nulled": 5, "best_db": 7.2},
    "rn_source": "guard", "noise_coherence": 0.42,
    "capture": {"active": false, "path": null}})";

// A gate that predates every v3 key: no talker, no memory ids, no names, no
// first_seen_s, no passband. Nothing here may be invented -- every readout
// those keys would have driven has to say so.
inline const QByteArray kDiversityOldGate = R"({"available": true, "channels": 2,
    "mode": "null", "source": "combined", "phase_deg": 20.0, "ratio_db": 0.5,
    "lag_samples": 12, "aligned": true, "corr_peak": 0.4,
    "snr_db": {"a": 5.0, "b": 4.0, "out": 6.0}, "updates": 3,
    "memory": [{"phase_deg": 30.0, "ratio_db": 0.0, "age_s": 9.0, "hits": 2}],
    "capture": {"active": false, "path": null}})";

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

// The same map with the receiver's own passband inside the mapped span.
QByteArray makeDiversityMapWithPassband(int points)
{
    QByteArray body = makeDiversityMap(points);
    const int close = body.lastIndexOf(']');
    body.insert(close + 1, QByteArray(", \"passband_hz\": [3501200.0, 3504000.0]"));
    return body;
}

// --- BAND page -----------------------------------------------------------
//
// /diversity/spatial: eight bins across 2 kHz, with phases chosen so the
// colour test has two columns that MUST differ (0 deg vs 180 deg) and one that
// must be grey (zero coherence). The receiver's passband sits inside the span.
inline const QByteArray kDiversitySpatial = R"({"available": true,
    "start_hz": 14100000.0, "step_hz": 250.0, "points": 8,
    "phase_deg": [0.0, 180.0, -90.0, 45.0, 0.0, 120.0, -175.0, 30.0],
    "coherence": [0.9, 0.9, 0.7, 0.0, 0.5, 0.8, 0.6, 0.3],
    "level_db": [-40.0, -41.0, -55.0, -70.0, -60.0, -44.0, -80.0, -66.0],
    "passband_hz": [14100500.0, 14101200.0]})";

// The same span with nothing measured yet -- the "waiting for the gate" case.
inline const QByteArray kDiversitySpatialUnavailable = R"({"available": false})";

// /diversity/finder: three candidates, best first, plus the activity strip.
// The third is an OLD gate's row: hz and score only, every other field absent.
// None of those may be rendered as a number.
inline const QByteArray kDiversityFinder = R"({"available": true,
    "span_hz": [14100000.0, 14102000.0], "history_s": 600,
    "activity": [0.0, 0.2, 0.9, 0.4, 0.0, 0.1, 0.6, 0.0],
    "candidates": [
      {"hz": 14100600.0, "width_hz": 2700.0, "score": 0.82, "snr_db": 6.1,
       "syllabic": 0.61, "active_s": 184.0, "last_s": 0.0,
       "phase_deg": 141.0, "coherence": 0.70, "ratio_db": -2.1, "gain_db": 1.4},
      {"hz": 14101450.0, "width_hz": 2400.0, "score": 0.55, "snr_db": -1.2,
       "syllabic": 0.44, "active_s": 42.0, "last_s": 12.0,
       "phase_deg": -30.0, "coherence": 0.21, "ratio_db": 0.4, "gain_db": -0.3},
      {"hz": 14101900.0, "score": 0.31}
    ]})";

inline const QByteArray kDiversityFinderUnavailable = R"({"available": false})";

// --- SITE page -----------------------------------------------------------
//
// The same live payload as kDiversityFull with the two keys the SITE page and
// the per-bin checkbox read: a 60 Hz grid with a 120 Hz comb, a fence-like
// impulse rate, two periodic lines that are not mains harmonics, and the
// subband refinement switched on.
inline const QByteArray kDiversityStatusWithSite = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42,
    "nb": {"enabled": true, "threshold_db": 18.5, "blanked_pct": 3.2},
    "rn_source": "guard", "noise_coherence": 0.42,
    "subband": {"enabled": true, "bins": 33, "extra_db": 0.0},
    "noise_profile": {"mains_hz": 60.0, "hum_db": 13.7, "harmonics": 2,
                      "impulses_per_s": 14.8, "impulse_db": 12.5,
                      "periodic": [{"hz": 182.1, "db": 18.6},
                                   {"hz": 431.0, "db": 9.2}],
                      "seconds": 2.0},
    "capture": {"active": false, "path": null}})";

// A gate that is up but has not profiled yet, with the refinement switched
// off. noise_profile null is "not measured", NOT "a silent site".
inline const QByteArray kDiversityStatusSiteNull = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": false, "corr_peak": null,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 1,
    "subband": {"enabled": false, "bins": 0, "extra_db": 0.0},
    "noise_profile": null,
    "capture": {"active": false, "path": null}})";

// Two remembered talkers, one with a voice/rig print and one the gate has
// not heard enough of yet (voice null is "not enough overs", not "silent").
inline const QByteArray kDiversityStatusWithPrint = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42,
    "subband": {"enabled": true, "bins": 33, "extra_db": 0.0},
    "noise_profile": null,
    "memory": [
        {"id": 3, "name": "Kay", "phase_deg": 120.0, "ratio_db": 2.0, "age_s": 4.0,
         "first_seen_s": 900.0, "hits": 12,
         "voice": {"centroid_hz": 1350, "low_hz": 300, "high_hz": 2700, "tilt_db": -4.2,
                   "syllabic_hz": 4.1, "over_s": 18.5, "overs": 7}},
        {"id": 5, "name": null, "phase_deg": 30.0, "ratio_db": 0.0, "age_s": 60.0,
         "first_seen_s": 120.0, "hits": 1, "voice": null}],
    "talker": {"id": 3, "since_s": 4.0},
    "capture": {"active": false, "path": null}})";

// Every result the beacon watch reports carries a wall-clock stamp, and the
// age column is the only cell in the window whose text depends on the local
// clock -- so the payload is built at run time rather than frozen into a
// literal that would be "3 years ago" by the time anybody read the test.
QByteArray makeDiversityBeacons(double heardAgeS = 125.0, double bandHz = 14100000.0)
{
    const double at = double(QDateTime::currentSecsSinceEpoch()) - heardAgeS;
    QByteArray body = R"({"available": true, "band_hz": )";
    body += QByteArray::number(bandHz, 'f', 1);
    body += R"(, "slot": 12,
    "now": {"call": "4X6TU", "location": "Tel Aviv, Israel", "seconds_left": 9.4},
    "results": [
      {"call": "KH6RS", "location": "Maui, Hawaii", "band_hz": )";
    body += QByteArray::number(bandHz, 'f', 1);
    body += R"(, "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": true, "snr_db": -3.3, "offset_hz": 0.0, "snr_a": -5.5,
       "snr_b": -1.1, "phase_deg": -14.7, "coherence": 0.07, "gain_db": 2.0,
       "steps_db": [-3.3, -11.6, -12.8, -15.7], "steps_heard": 1,
       "lowest_w": 100.0},
      {"call": "OH2B", "location": "Lohja, Finland", "band_hz": )";
    body += QByteArray::number(bandHz, 'f', 1);
    body += R"(, "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": false, "steps_heard": 0},
      {"call": "ZL6B", "location": "Masterton, New Zealand", "band_hz": 21150000.0,
       "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": true, "snr_db": 4.0, "steps_heard": 4, "lowest_w": 0.1}
    ], "last": null})";
    return body;
}

inline const QByteArray kDiversityBeacons = makeDiversityBeacons();

// A gate watching, with no beacon frequency inside the span: an idle watch is
// a fact about where you are tuned, not an empty log.
inline const QByteArray kDiversityBeaconsNoBand = R"({"available": true,
    "band_hz": null, "slot": 12, "now": null, "results": [], "last": null})";

inline const QByteArray kDiversityBeaconsUnavailable = R"({"available": false})";

QJsonObject asObject(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

} // namespace DiversityGateFixture
