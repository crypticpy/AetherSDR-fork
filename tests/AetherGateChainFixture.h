#pragma once

// The socket-free harness the two CHAIN window tests share: the gate payloads,
// a gate whose replies do not all arrive at once, and the half-dozen helpers
// that open the window and read a label off it.
//
// It exists for the same reason tests/DiversityGateFixture.h does. The CHAIN
// window has two separate stories to tell -- the array contract in design §0.1,
// and the eight things the operator asked for in §0.3 -- and one file holding
// both was over the 800-line limit AGENTS.md sets. The payloads are what both
// stories are told against, so they live here rather than being copied.
//
// Everything is header-defined and nothing opens a port.

#include "DiversityGateFixture.h"
#include "TestSettingsProfile.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateChainModes.h"
#include "gui/AetherGateChainStage.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/AetherGateChainWindow.h"
#include "gui/DiversityBandPoller.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTest>
#include <QTimer>

#include <cstdio>

using AetherSDR::AetherGateApplet;
using AetherSDR::AetherGateChainControl;
using AetherSDR::AetherGateChainStrip;
using AetherSDR::AetherGateChainTile;
using AetherSDR::AetherGateChainWindow;
using AetherSDR::ChainMode;
using AetherSDR::DiversityBandPoller;
using AetherSDR::chainPreset;

using namespace DiversityGateFixture;

namespace AetherGateChainFixture {

inline int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

// A gate that authors its own chain[], with one row of every kind the contract
// defines: two selects (one of them the digital roof, which is the row that
// carries the radio menus and the free entry), a toggle, a fixed row, and a
// value row the app has no built-in knowledge of at all.
const QByteArray kChainFilter = R"JSON({
  "low_hz": 350, "high_hz": 2400,
  "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000},
  "chain": [
    {"id": "roof_rf", "name": "ROOFING · RF", "kind": "select", "fixed": false,
     "enabled": true, "detail": "200 kHz", "value": 200000,
     "options": [200000, 300000, 600000, 1536000],
     "measured": {"in_db": null, "out_db": -97.4},
     "action": {"label": "SET", "route": "/filter/set", "query": "roof_hz="}},
    {"id": "roof_digital", "name": "ROOFING · DIGITAL", "kind": "select",
     "enabled": true, "detail": "3.0 kHz", "value": 3000,
     "options": [12000, 6000, 3000, 1200, 600, 300],
     "measured": {"in_db": -97.4, "out_db": -101.2},
     "action": {"label": "SET", "route": "/filter/set", "query": "digital_roof_hz="}},
    {"id": "nb", "name": "NB", "kind": "toggle", "enabled": false,
     "detail": "12.0 dB · 0.0 % blanked · auto: idle",
     "action": {"label": "ON", "route": "/filter/set", "query": "nb=on"}},
    {"id": "lna", "name": "LNA", "kind": "fixed", "fixed": true, "enabled": true,
     "detail": "state 4", "why": "set on the setup page"},
    {"id": "steer", "name": "BEAM STEER", "kind": "value", "enabled": true,
     "detail": "42 degrees"}
  ]})JSON";

// The same payload with the noise blanker on, so a toggle can be shown to move
// on the GATE'S answer and on nothing else.
QByteArray chainFilterNbOn()
{
    QByteArray body = kChainFilter;
    body.replace("\"enabled\": false,\n     \"detail\": \"12.0 dB",
                 "\"enabled\": true,\n     \"detail\": \"12.0 dB");
    return body;
}

// The WHOLE chain, exactly as aether_gate/adapters/chainstatus.py writes it on
// a dual-tuner device: twenty-five rows in signal order, ids and names and
// `detail` shapes copied from that module's own golden list
// (aether_gate/tests/test_chain_status.py GOLDEN_IDS). This is what the four
// groups, the front-end summary card and the primary-line format table are
// measured against -- a five-row payload cannot show that the diagram fits.
const QByteArray kChainFullFilter = R"JSON({
  "low_hz": 350, "high_hz": 2400, "width_hz": 2050, "mode": "USB",
  "roofing": {"analogue_hz": 200000.0, "digital_hz": 12000, "samp_rate_hz": 62500},
  "chain": [
    {"id": "antenna", "name": "ANTENNA", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "Antenna B · 1 of 3 ports",
     "why": "set on the setup page"},
    {"id": "traps", "name": "BC / DAB TRAP", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "MW/FM on · DAB off",
     "why": "set on the setup page"},
    {"id": "lna", "name": "PRE / ATT · LNA", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "state 4 of 0-9", "why": "set on the setup page"},
    {"id": "ifgr", "name": "RF GAIN · IFGR", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "47 dB of 20-59", "why": "set on the setup page"},
    {"id": "rf_agc", "name": "RF AGC", "kind": "fixed", "fixed": true,
     "enabled": false, "detail": "off · set-point -30 dBfs",
     "why": "set on the setup page"},
    {"id": "roof_rf", "name": "ROOFING · RF", "kind": "select", "enabled": true,
     "detail": "200 kHz · following the sample rate", "value": 200000,
     "options": [200000, 300000, 600000, 1536000],
     "measured": {"in_db": null, "out_db": -97.4},
     "action": {"label": "SET", "route": "/filter/set", "query": "roof_hz="}},
    {"id": "adc", "name": "ADC · SAMPLE RATE", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "62.5 kS/s", "value": 62500,
     "why": "the resolution control sets it (/resolution?rate=)"},

    {"id": "align", "name": "ALIGN", "kind": "value", "enabled": true,
     "detail": "lag 4158 samples · locked · peak 0.82", "value": 4158,
     "action": {"label": "REALIGN", "route": "/diversity/align", "query": ""}},
    {"id": "nb", "name": "NB", "kind": "toggle", "enabled": true,
     "detail": "12.0 dB · 0.43 % blanked · auto: idle",
     "action": {"label": "OFF", "route": "/filter/set", "query": "nb=off"}},
    {"id": "roof_digital", "name": "ROOFING · DIGITAL", "kind": "select",
     "enabled": true, "detail": "12 kHz · 255 taps", "value": 12000,
     "options": [12000, 6000, 3000, 1200, 600, 300],
     "measured": {"in_db": -97.4, "out_db": -101.2},
     "action": {"label": "SET", "route": "/filter/set", "query": "digital_roof_hz="}},
    {"id": "combiner", "name": "COMBINER", "kind": "select", "enabled": true,
     "detail": "track · φ 157.3° · -4.6 dB · SNR a 15.2 / b 12.9 → 16.4 dB",
     "value": "track", "options": ["off", "manual", "null", "track"],
     "measured": {"in_db": 15.2, "out_db": 16.4},
     "action": {"label": "SET", "route": "/diversity/set", "query": "mode="}},
    {"id": "subband", "name": "SUB-BAND NULL", "kind": "toggle", "enabled": true,
     "detail": "24 bins · +2.7 dB",
     "action": {"label": "OFF", "route": "/diversity/set", "query": "subband=off"}},
    {"id": "post", "name": "POST-FILTER", "kind": "toggle", "enabled": true,
     "detail": "floor -16.0 dB · mean -18.5 dB",
     "measured": {"in_db": null, "out_db": -18.5},
     "action": {"label": "OFF", "route": "/diversity/set", "query": "post=off"}},

    {"id": "slice", "name": "SLICE FILTER", "kind": "toggle", "enabled": true,
     "detail": "255 taps · soft",
     "action": {"label": "BYPASS", "route": "/filter/set", "query": "bypass=on"}},
    {"id": "passband", "name": "PASSBAND (twin PBT)", "kind": "value",
     "enabled": true, "detail": "350-2400 Hz · asked 300-2700 · USB",
     "value": 2050, "why": "both edges move on the curve"},
    {"id": "auto", "name": "AUTO WIDTH", "kind": "toggle", "enabled": true,
     "detail": "print · 350-2400 Hz",
     "action": {"label": "OFF", "route": "/filter/set", "query": "auto=off"}},
    {"id": "shape", "name": "SHAPE", "kind": "select", "enabled": true,
     "detail": "SOFT · 255 taps · 196 Hz skirt", "value": "soft",
     "options": ["soft", "sharp"],
     "action": {"label": "SET", "route": "/filter/set", "query": "shape="}},
    {"id": "notch", "name": "IF NOTCH", "kind": "toggle", "enabled": true,
     "detail": "2 set · 1200 Hz, 1850 Hz",
     "measured": {"in_db": null, "out_db": -41.0},
     "action": {"label": "OFF", "route": "/filter/set", "query": "notches=off"}},
    {"id": "anf", "name": "ANF · DNF", "kind": "toggle", "enabled": true,
     "detail": "980 Hz, 1460 Hz",
     "measured": {"in_db": null, "out_db": -33.5},
     "action": {"label": "OFF", "route": "/filter/set", "query": "anf=off"}},
    {"id": "contour", "name": "CONTOUR", "kind": "toggle", "enabled": true,
     "detail": "auto · 1450 Hz -2.9 dB, 300 Hz wide",
     "action": {"label": "OFF", "route": "/filter/set",
                "query": "auto_contour=off"}},
    {"id": "apf", "name": "APF", "kind": "toggle", "enabled": false,
     "detail": "600 Hz · 150 Hz wide",
     "action": {"label": "ON", "route": "/filter/set", "query": "apf=on"}},
    {"id": "auto_eq", "name": "RX EQ (auto tilt)", "kind": "toggle",
     "enabled": true, "detail": "tilt +1.5 dB · lean -0.8 dB",
     "action": {"label": "OFF", "route": "/filter/set", "query": "auto_eq=off"}},

    {"id": "detect", "name": "DETECTOR", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "USB product detector",
     "why": "the mode decides it"},
    {"id": "agc", "name": "AGC", "kind": "select", "enabled": true,
     "detail": "med · 5/250/250 ms · AGC-T 20 · -5.8 dB", "value": "med",
     "options": ["fast", "med", "slow", "long", "off"],
     "action": {"label": "SET", "route": "/filter/set", "query": "agc="}},
    {"id": "app", "name": "→ AETHER VOICE", "kind": "fixed", "fixed": true,
     "enabled": true, "detail": "noise reduction and compression run in the app",
     "why": "the app's own chain, downstream of the receiver"}
  ]})JSON";

// GET /filter on the live gate, 2026-09-03: no chain[] anywhere in it. This is
// the payload the fallback exists for. CONTOUR is ON here, which makes it the
// row the "several clicks" case switches OFF.
const QByteArray kChainlessFilter = R"JSON({
  "low_hz": 100, "high_hz": 2900, "width_hz": 2800,
  "set_low_hz": 100, "set_high_hz": 2900,
  "shape": "soft", "taps": 255, "transition_hz": 196, "sideband": "lsb",
  "notches": [],
  "anf": {"enabled": false, "found_hz": [], "depth_db": []},
  "contour": {"enabled": true, "hz": 1450.0, "db": -2.9, "width_hz": 300.0,
              "auto": true, "source": "print"},
  "apf": {"enabled": false, "hz": 600.0, "width_hz": 150.0},
  "auto": {"enabled": false, "source": null, "low_hz": null, "high_hz": null},
  "auto_eq": {"enabled": false, "tilt_db": 0.0, "lean_db": 0.0},
  "nb": {"enabled": false, "threshold_db": 12.0, "blanked_pct": 0.0},
  "agc": {"mode": "med", "attack_ms": 5.0, "decay_ms": 250.0, "hang_ms": 250.0,
          "threshold_db": 20.0, "gain_db": -5.8},
  "talker": {"enabled": true, "snap": "fast", "id": 32,
             "remembered": [30, 31, 32, 33]},
  "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000},
  "available": true, "mode": "LSB"})JSON";

// The same gate one write later: the contour is off.
QByteArray chainlessContourOff()
{
    QByteArray body = kChainlessFilter;
    body.replace("\"contour\": {\"enabled\": true", "\"contour\": {\"enabled\": false");
    return body;
}

// A gate that HAS built the roofing side: it lists what the driver offers for
// the analogue filter and the one width its digital roof can currently make.
const QByteArray kRoofingFilter = R"JSON({
  "low_hz": 100, "high_hz": 2900, "width_hz": 2800,
  "set_low_hz": 100, "set_high_hz": 2900,
  "shape": "soft", "taps": 255, "transition_hz": 196, "sideband": "lsb",
  "notches": [],
  "anf": {"enabled": false, "found_hz": [], "depth_db": []},
  "contour": {"enabled": false, "hz": null, "db": 0.0, "width_hz": null,
              "auto": true, "source": null},
  "apf": {"enabled": false, "hz": 600.0, "width_hz": 150.0},
  "auto": {"enabled": false, "source": null, "low_hz": null, "high_hz": null},
  "auto_eq": {"enabled": false, "tilt_db": 0.0, "lean_db": 0.0},
  "nb": {"enabled": false, "threshold_db": 12.0, "blanked_pct": 0.0},
  "agc": {"mode": "med", "attack_ms": 5.0, "decay_ms": 250.0, "hang_ms": 250.0,
          "threshold_db": 20.0, "gain_db": -5.8},
  "talker": {"enabled": false, "snap": "fast", "id": 0, "remembered": []},
  "roofing": {"analogue_hz": 200000.0, "analogue_options": [200000, 300000, 600000, 1536000],
              "digital_hz": 3000, "digital_options": [3000, 6000, 12000]},
  "available": true, "mode": "LSB"})JSON";

// --------------------------------------------------------------------------
// A gate whose replies do not all arrive at once.
//
// DiversityGateFixture's FakeReply finishes on the next event-loop turn, which
// is the right default and cannot show an out-of-order answer. The real gate
// is a ThreadingHTTPServer answering /filter and /filter/set on two threads,
// each of which reads the status once and then spends real time building the
// response curve -- so the poll that read the OLD status can finish after the
// write that changed it. That is the whole of bug §0.3.5, and it needs a
// reply with a delay on it.
// --------------------------------------------------------------------------

class DelayedReply : public QNetworkReply {
public:
    DelayedReply(const QNetworkRequest& req, const QByteArray& body, int delayMs,
                 QObject* parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(req);
        setUrl(req.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        QTimer::singleShot(delayMs, this, [this] {
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

// FakeGate, plus a per-path delay and a per-path body that can be swapped
// after the request is issued. Everything not listed in `delays` behaves
// exactly as FakeGate does.
class SlowGate : public FakeGate {
public:
    QHash<QString, int> delays;    // by URL path, in milliseconds

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& req,
                                 QIODevice* dev) override
    {
        const QUrl u = req.url();
        if (!delays.contains(u.path()))
            return FakeGate::createRequest(op, req, dev);
        log << u.path() + (u.hasQuery() ? QStringLiteral("?") + u.query() : QString());
        return new DelayedReply(req, routes.value(u.path()).body, delays.value(u.path()),
                                this);
    }
};

void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& filter)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityFull};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, filter};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

// One more tick of the filter poller, without waiting out 500 ms of real time.
void filterTick(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (!poller)
        return;
    QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
    settle();
}

// The same tick with NO settle: the request goes on the wire and the test
// keeps control, which is how an in-flight poll is staged.
void filterPollNow(AetherGateApplet& a)
{
    auto* poller = a.findChild<DiversityBandPoller*>();
    if (poller)
        QMetaObject::invokeMethod(poller, "poll", Qt::DirectConnection);
}

AetherGateChainWindow* openChain(AetherGateApplet& a)
{
    auto* door = a.findChild<QPushButton*>(QStringLiteral("gateOpenChainWindowButton"));
    if (!door)
        return nullptr;
    door->click();
    settle();
    filterTick(a);
    return a.chainWindow();
}

AetherGateChainStrip* strip(AetherGateChainWindow* w)
{
    return w ? w->findChild<AetherGateChainStrip*>(QStringLiteral("gateChainStrip"))
             : nullptr;
}

QString lastRequest(const FakeGate& net)
{
    return net.log.isEmpty() ? QString() : net.log.last();
}

QString labelText(AetherGateChainWindow* w, const char* name)
{
    auto* label = w->findChild<QLabel*>(QString::fromLatin1(name));
    return label ? label->text() : QString();
}

QLabel* label(AetherGateChainWindow* w, const QString& name)
{
    return w->findChild<QLabel*>(name);
}

QPushButton* button(AetherGateChainWindow* w, const QString& name)
{
    return w->findChild<QPushButton*>(name);
}

int countWrites(const FakeGate& net)
{
    return net.count(QStringLiteral("/filter/set"));
}

// --------------------------------------------------------------------------

} // namespace AetherGateChainFixture
