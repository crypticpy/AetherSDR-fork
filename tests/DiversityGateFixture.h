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
#include <cmath>
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

// The same site as kDiversityStatusWithSite, plus the "kinds" array the SITE
// page's action table is built from: one row per finding, each carrying the
// gate's own verdict and either the action it nominated or the reason there
// is none. Six rows, in the order the gate emits them -- mains, impulse, three
// periodic, then a tone the ANF found -- and every rendered field is a
// different value from every other row's, so a test that asserted the wrong
// row would fail rather than pass by coincidence.
inline const QByteArray kDiversityStatusWithKinds = R"JSON({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42,
    "nb": {"enabled": false, "threshold_db": 18.5, "blanked_pct": 0.0},
    "rn_source": "guard", "noise_coherence": 0.14,
    "subband": {"enabled": true, "bins": 33, "extra_db": 0.0},
    "noise_profile": {"mains_hz": 60.0, "hum_db": 13.7, "harmonics": 2,
                      "impulses_per_s": 1475.1, "impulse_db": 14.8,
                      "periodic": [{"hz": 244.1, "db": 18.6},
                                   {"hz": 488.3, "db": 9.2}],
                      "seconds": 2.0, "window_s": 2.0, "impulse_window_s": 4.0,
      "kinds": [
        {"kind": "mains", "label": "Mains hum · 60 Hz grid",
         "detail": "120 Hz comb, 2 harmonics", "db": 22.0, "window_s": 2.0,
         "action": null,
         "why": "not directional enough to null (coherence 0.14)",
         "active": false},
        {"kind": "impulse", "label": "Impulses · 1475.1/s",
         "detail": "14.8 dB over the floor", "db": 14.8, "window_s": 4.0,
         "action": {"label": "BLANK", "route": "/diversity/set",
                    "query": "nb=on&nb_db=12"},
         "why": null, "active": false},
        {"kind": "periodic", "label": "Periodic · 244.1 Hz",
         "detail": "a modulation rate of the noise, not a tone in the audio",
         "db": 18.6, "window_s": 2.0, "action": null,
         "why": "nothing to notch; ANF handles tones in the passband",
         "active": false},
        {"kind": "periodic", "label": "Periodic · 488.3 Hz",
         "detail": "a modulation rate of the noise, not a tone in the audio",
         "db": 9.2, "window_s": 2.0, "action": null,
         "why": "nothing to notch; ANF handles tones in the passband",
         "active": false},
        {"kind": "periodic", "label": "Periodic · 732.4 Hz",
         "detail": "a modulation rate of the noise, not a tone in the audio",
         "db": 6.1, "window_s": 2.0, "action": null,
         "why": "nothing to notch; ANF handles tones in the passband",
         "active": false},
        {"kind": "tone", "label": "Tone · 1240 Hz",
         "detail": "ANF is holding it 31 dB down", "db": 31.0, "window_s": 2.0,
         "action": {"label": "NOTCH", "route": "/filter/notch",
                    "query": "add=1240&width=160"},
         "why": null, "active": false}]},
    "capture": {"active": false, "path": null}})JSON";

// The same site with two of the actions already in force: the blanker is on
// (so the impulse row offers UNBLANK and is lit) and the mains direction is
// being nulled (so the mains row offers NULLED and is lit). The lit state is
// the gate's, not the window's -- nothing here is set optimistically when a
// button is pressed.
inline const QByteArray kDiversityStatusKindsActive = R"JSON({"available": true, "channels": 2,
    "mode": "null", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 51,
    "nb": {"enabled": true, "threshold_db": 12.0, "blanked_pct": 0.4},
    "rn_source": "guard", "noise_coherence": 0.62,
    "subband": {"enabled": true, "bins": 33, "extra_db": 0.0},
    "noise_profile": {"mains_hz": 60.0, "hum_db": 13.7, "harmonics": 2,
                      "impulses_per_s": 1475.1, "impulse_db": 14.8,
                      "periodic": [], "seconds": 2.0,
                      "window_s": 2.0, "impulse_window_s": 4.0,
      "kinds": [
        {"kind": "mains", "label": "Mains hum · 60 Hz grid",
         "detail": "120 Hz comb, 2 harmonics", "db": 22.0, "window_s": 2.0,
         "action": {"label": "NULLED", "route": "/diversity/set",
                    "query": "mode=track"},
         "why": null, "active": true},
        {"kind": "impulse", "label": "Impulses · 1475.1/s",
         "detail": "14.8 dB over the floor, blanking 0.4 %", "db": 14.8,
         "window_s": 4.0,
         "action": {"label": "UNBLANK", "route": "/diversity/set",
                    "query": "nb=off"},
         "why": null, "active": true}]},
    "capture": {"active": false, "path": null}})JSON";

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
// A station that has told the gate where it is, with a night's log behind it:
// three beacons heard on 20 m (bearings, distances and heard-of-samples all
// different from each other), one on 15 m so the table's band filter has
// something to exclude, a propagation line for each of the two bands, and a
// three-point pattern. Built at run time for the same reason the one above is:
// every "at" and every "updated" is a wall-clock stamp.
QByteArray makeDiversityBeaconsWithPattern(double heardAgeS = 125.0)
{
    const double at = double(QDateTime::currentSecsSinceEpoch()) - heardAgeS;
    const double updated20 = double(QDateTime::currentSecsSinceEpoch()) - 245.0;
    const double updated15 = double(QDateTime::currentSecsSinceEpoch()) - 3900.0;
    QByteArray body = R"({"available": true, "band_hz": 14100000.0, "slot": 12,
    "station_grid": "EM10",
    "now": {"call": "4X6TU", "location": "Tel Aviv, Israel", "seconds_left": 9.4},
    "results": [
      {"call": "W6WX", "location": "Mt Umunhum, California", "band_hz": 14100000.0,
       "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": true, "snr_db": 12.0, "snr_mean_db": 9.4, "snr_a": 10.1,
       "snr_b": 8.2, "phase_deg": 51.6, "coherence": 0.62, "gain_db": 1.8,
       "steps_heard": 3, "lowest_w": 1.0, "grid": "CM97",
       "bearing_deg": 295, "distance_km": 2405, "samples": 7, "heard_n": 3,
       "last_heard": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(},
      {"call": "KH6RS", "location": "Maui, Hawaii", "band_hz": 14100000.0,
       "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": true, "snr_db": -3.3, "snr_mean_db": -4.8, "snr_a": -5.5,
       "snr_b": -1.1, "phase_deg": -14.7, "coherence": 0.07, "gain_db": 2.0,
       "steps_heard": 1, "lowest_w": 100.0, "grid": "BL10",
       "bearing_deg": 264, "distance_km": 6108, "samples": 9, "heard_n": 2,
       "last_heard": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(},
      {"call": "OH2B", "location": "Lohja, Finland", "band_hz": 14100000.0,
       "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": false, "steps_heard": 0, "grid": "KP20",
       "bearing_deg": 33, "distance_km": 8244, "samples": 6, "heard_n": 0},
      {"call": "ZL6B", "location": "Masterton, New Zealand", "band_hz": 21150000.0,
       "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": true, "snr_db": 4.0, "steps_heard": 4, "lowest_w": 0.1,
       "grid": "RE78", "bearing_deg": 241, "distance_km": 13005,
       "samples": 4, "heard_n": 4}
    ], "last": null,
    "propagation": [
      {"band_hz": 14100000.0, "sampled": 7, "heard": 3, "of": 18, "best_w": 1.0,
       "median_snr_db": -3.3, "updated": )";
    body += QByteArray::number(updated20, 'f', 1);
    body += R"(},
      {"band_hz": 21150000.0, "sampled": 4, "heard": 1, "of": 18, "best_w": 0.1,
       "median_snr_db": 4.0, "updated": )";
    body += QByteArray::number(updated15, 'f', 1);
    body += R"(}],
    "pattern": [
      {"call": "OH2B", "band_hz": 14100000.0, "bearing_deg": 33,
       "distance_km": 8244, "b_minus_a_db": 3.4, "phase_deg": -102.0,
       "snr_db": -8.0},
      {"call": "KH6RS", "band_hz": 14100000.0, "bearing_deg": 264,
       "distance_km": 6108, "b_minus_a_db": 4.4, "phase_deg": -14.7,
       "snr_db": -3.3},
      {"call": "W6WX", "band_hz": 14100000.0, "bearing_deg": 295,
       "distance_km": 2405, "b_minus_a_db": -1.9, "phase_deg": 51.6,
       "snr_db": 12.0}]})";
    return body;
}

inline const QByteArray kDiversityBeaconsWithPattern =
    makeDiversityBeaconsWithPattern();

// The same night's log from a station that has never entered a locator: every
// bearing and distance is null and the pattern is empty, because a bearing
// needs two points and the gate only knows one of them. Not an error and not
// an empty table -- the signal reports are all still there.
QByteArray makeDiversityBeaconsNoGrid(double heardAgeS = 125.0)
{
    const double at = double(QDateTime::currentSecsSinceEpoch()) - heardAgeS;
    QByteArray body = R"({"available": true, "band_hz": 14100000.0, "slot": 12,
    "station_grid": null,
    "now": {"call": "4X6TU", "location": "Tel Aviv, Israel", "seconds_left": 9.4},
    "results": [
      {"call": "W6WX", "location": "Mt Umunhum, California", "band_hz": 14100000.0,
       "at": )";
    body += QByteArray::number(at, 'f', 1);
    body += R"(, "heard": true, "snr_db": 12.0, "snr_mean_db": 9.4,
       "steps_heard": 3, "lowest_w": 1.0, "grid": "CM97",
       "bearing_deg": null, "distance_km": null, "samples": 7, "heard_n": 3}
    ], "last": null,
    "propagation": [
      {"band_hz": 14100000.0, "sampled": 7, "heard": 3, "of": 18,
       "best_w": null, "median_snr_db": null, "updated": null}],
    "pattern": []})";
    return body;
}

inline const QByteArray kDiversityBeaconsNoGrid = makeDiversityBeaconsNoGrid();

// What /diversity/set replies with when the locator will not parse. Same shape
// as every other refusal on this page.
inline const QByteArray kDiversityBadGrid =
    R"({"error": "not a Maidenhead locator: 'ZZ99'"})";


// --- /filter ---------------------------------------------------------------
// The FILTER page's payloads. The response curve is GENERATED rather than
// written out because a hand-typed one would either be short enough to type
// (and so not the 128 points the panel actually draws) or a row of zeros (and
// so not a shape any assertion about the curve could mean anything against).
// What comes out is a fourth-order 100-2900 Hz bandpass across 0-4000 Hz:
// flat in the passband, -3 dB at each edge, real skirts, floored at the -60 dB
// the panel's bottom gridline sits on.
inline QByteArray makeFilterResponse()
{
    QByteArray hz;
    QByteArray db;
    for (int i = 0; i < 128; ++i) {
        const double f = 4000.0 * double(i) / 127.0;
        const double lo = 100.0;
        const double hi = 2900.0;
        const double order = 4.0;
        const double highpass =
            -10.0 * std::log10(1.0 + std::pow(lo / std::max(f, 1.0), 2.0 * order));
        const double lowpass =
            -10.0 * std::log10(1.0 + std::pow(f / hi, 2.0 * order));
        const double value = std::max(highpass + lowpass, -60.0);
        if (i > 0) {
            hz += ", ";
            db += ", ";
        }
        hz += QByteArray::number(f, 'f', 1);
        db += QByteArray::number(value, 'f', 2);
    }
    QByteArray body = R"("response": {"hz": [)";
    body += hz;
    body += R"(], "db": [)";
    body += db;
    body += R"(]})";
    return body;
}

// The pre-filter spectrum the AUTO width and the ANF read, on the SAME 128-point
// grid as the response -- the panel draws one over the other and a second grid
// would be a picture of interpolation.
//
// A station on a quiet channel: a noise floor at -38.4 dB and a voice-shaped
// bump between 300 and 2700 Hz peaking at 1 kHz. The gate reports dB below the
// spectrum's own peak, so the array is normalised to put its maximum at exactly
// 0.00 -- a fixture whose peak was -0.09 would let a panel that forgot to pin
// the floor still look right.
//
// `floor_db` is the gate's median. This fixture states the floor it was BUILT
// with rather than the median of these 128 numbers: 60 % of a 0-4000 Hz grid is
// inside 300-2700 here, so the true median of the array lands in the middle of
// the bump, and a fixture whose "floor" was half way up the station would be
// testing the arithmetic instead of the picture.
inline QByteArray makeFilterSpectrum(double floorDb = -38.4)
{
    double raw[128];
    double peak = -1e9;
    for (int i = 0; i < 128; ++i) {
        const double f = 4000.0 * double(i) / 127.0;
        double value = floorDb;
        if (f >= 300.0 && f <= 2700.0) {
            const double bump =
                -10.0 + 10.0 * std::exp(-0.5 * std::pow((f - 1000.0) / 600.0, 2.0));
            // Raised-cosine over the first and last 150 Hz of the band, so the
            // energy meets the floor instead of falling off a cliff at 300 and
            // 2700. A fixture with vertical sides is a spectrum no receiver has
            // ever produced, and it hides an off-by-one in the area's end caps.
            double taper = 1.0;
            if (f < 450.0)
                taper = 0.5 * (1.0 - std::cos(M_PI * (f - 300.0) / 150.0));
            else if (f > 2550.0)
                taper = 0.5 * (1.0 - std::cos(M_PI * (2700.0 - f) / 150.0));
            value = std::max(floorDb + (bump - floorDb) * taper, floorDb);
        }
        raw[i] = value;
        peak = std::max(peak, value);
    }

    QByteArray hz;
    QByteArray db;
    for (int i = 0; i < 128; ++i) {
        if (i > 0) {
            hz += ", ";
            db += ", ";
        }
        hz += QByteArray::number(4000.0 * double(i) / 127.0, 'f', 1);
        db += QByteArray::number(raw[i] - peak, 'f', 2);
    }
    QByteArray body = R"("spectrum": {"hz": [)";
    body += hz;
    body += R"(], "db": [)";
    body += db;
    body += R"(], "floor_db": )";
    body += QByteArray::number(floorDb - peak, 'f', 2);
    body += "}";
    return body;
}

// The gate before it has heard a block. Not a flat spectrum and not an absent
// key: an explicit null, which is the only shape that says "I have not measured
// this yet" rather than "there is nothing there".
inline const QByteArray kNoFilterSpectrum = R"("spectrum": null)";

// A working LSB filter with something switched on in every column, so one
// payload can carry every readout the page has: two manual notches, an ANF
// that has found two tones, contour and APF placed, AUTO on with the print
// tracker driving it, an AGC in MED with real numbers, and a blanker biting.
inline QByteArray makeDiversityFilterStatus(const QByteArray& spectrum = makeFilterSpectrum())
{
    QByteArray body = R"({"available": true, "mode": "lsb", "sideband": "lsb",
    "low_hz": 100, "high_hz": 2900, "width_hz": 2800,
    "set_low_hz": 100, "set_high_hz": 2900,
    "shape": "sharp", "taps": 1023, "transition_hz": 61,
    "notches": [{"hz": 1000.0, "width_hz": 120.0, "depth_db": -48.0},
                {"hz": 1780.0, "width_hz": 80.0, "depth_db": -35.5}],
    "anf": {"enabled": true, "found_hz": [1240.0, 2010.0],
            "depth_db": [-34.0, -31.0]},
    "contour": {"enabled": true, "hz": 700.0, "db": -4.0, "width_hz": 400.0},
    "apf": {"enabled": false, "hz": 600.0, "width_hz": 80.0},
    "auto": {"enabled": true, "source": "print", "low_hz": 300.0,
             "high_hz": 2700.0},
    "auto_eq": {"enabled": true, "tilt_db": 3.5},
    "nb": {"enabled": true, "threshold_db": 8.0, "blanked_pct": 0.4},
    "agc": {"mode": "med", "attack_ms": 10, "decay_ms": 500, "hang_ms": 250,
            "gain_db": -1.9},
    "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000.0}, )";
    body += spectrum;
    body += ", ";
    body += makeFilterResponse();
    body += "}";
    return body;
}

inline const QByteArray kDiversityFilterStatus = makeDiversityFilterStatus();

// The same filter with nothing heard yet. The page must draw no area at all
// and say so, rather than drawing a flat one at the bottom of the axis.
inline const QByteArray kDiversityFilterNoSpectrum =
    makeDiversityFilterStatus(kNoFilterSpectrum);

// The same filter with the auto-width tracker driving from the spectrum rather
// than from a voice print, and the edges it has chosen in force -- so low_hz /
// high_hz and set_low_hz / set_high_hz disagree, which is the whole point of
// there being two pairs.
inline QByteArray makeDiversityFilterAutoSpectrum()
{
    QByteArray body = R"({"available": true, "mode": "lsb", "sideband": "lsb",
    "low_hz": 210, "high_hz": 2840, "width_hz": 2630,
    "set_low_hz": 100, "set_high_hz": 2900,
    "shape": "soft", "taps": 511, "transition_hz": 122,
    "notches": [],
    "anf": {"enabled": false, "found_hz": [], "depth_db": []},
    "contour": {"enabled": false, "hz": 700.0, "db": 0.0, "width_hz": 400.0},
    "apf": {"enabled": false, "hz": 600.0, "width_hz": 80.0},
    "auto": {"enabled": true, "source": "spectrum", "low_hz": 210.0,
             "high_hz": 2840.0},
    "auto_eq": {"enabled": false, "tilt_db": 0.0},
    "nb": {"enabled": false, "threshold_db": 8.0, "blanked_pct": 0.0},
    "agc": {"mode": "slow", "attack_ms": 10, "decay_ms": 1200, "hang_ms": 500,
            "gain_db": -4.5},
    "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000.0}, )";
    // A quieter channel: the same shape 12 dB nearer its own floor, which is
    // what the tracker was reading when it chose 210-2840 off the spectrum.
    body += makeFilterSpectrum(-26.5);
    body += ", ";
    body += makeFilterResponse();
    body += "}";
    return body;
}

inline const QByteArray kDiversityFilterAutoSpectrum = makeDiversityFilterAutoSpectrum();

// A mode with no slice filter behind it -- FM, or a gate built without the
// filter core. Not an error and not an empty page: a sentence.
inline const QByteArray kDiversityFilterUnavailable = R"({"available": false})";

// What a refused write replies with. The shape is the same as a status object,
// which is why the page checks for "error" before it checks for "available".
inline const QByteArray kDiversityFilterBadValue =
    R"({"error": "bad value: low"})";

QJsonObject asObject(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

} // namespace DiversityGateFixture
