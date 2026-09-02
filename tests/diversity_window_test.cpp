// DiversityWindow — the pop-out Diversity window, driven through a real
// AetherGateApplet and socket-free.
//
// The window owns no transport: it is opened from the sidebar panel's button,
// fed by the applet's own /diversity and /diversity/map polls, and every
// control it has emits a request signal the panel forwards to the applet.
// So the only honest way to test it is through the applet, with the same
// injected QNetworkAccessManager the applet test uses — no port is opened,
// nothing is listened on, and a wrong answer fails an assertion instead of
// hanging on a socket.
//
// Separate binary from aether_gate_applet_test on purpose: opening the window
// writes DiversityWindowVisible into the process-wide AppSettings cache, and
// an applet built afterwards would restore it — which would silently change
// what every later case in that binary is testing.

#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/ClientCompKnob.h"
#include "gui/DiversityScope.h"
#include "gui/DiversityWindow.h"
#include "gui/DiversityWindowPanels.h"

#include <QApplication>
#include <QHash>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::ClientCompKnob;
using AetherSDR::DiversityScope;
using AetherSDR::DiversitySnrMeter;
using AetherSDR::DiversityWindow;

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failed;                                                              \
        }                                                                            \
    } while (0)

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

const QByteArray kStatus = R"({"connected": true, "streaming": true,
    "res": {"bins": 1024, "max_bins": 16384, "span_hz": 2000400.0, "bin_hz": 1953.5,
            "samp_rate": 2000400.0, "rates": [2000000, 2000400, 3200000],
            "can_set_rate": true}})";

const QByteArray kDevice = R"({
    "antenna": {"value": "Antenna B", "options": ["Antenna A", "Antenna B"]},
    "settings": [
      {"key": "lna_state", "name": "LNA state", "type": "1", "value": "2"}
    ]})";

// Every v2/v3 field the window reads, including the two it alone shows
// (steady_qrm, passband) and the memory list its stations table renders.
const QByteArray kDiversityFull = R"({"available": true, "channels": 2,
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
const QByteArray kDiversityNulls = R"({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 10.0, "ratio_db": 0.0,
    "weight": [1.0, 0.0], "lag_samples": null, "aligned": false, "corr_peak": null,
    "snr_db": {"a": null, "b": null, "out": null}, "updates": 0, "slice_id": 0,
    "rn_source": null, "capture": {"active": false, "path": null}})";

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

// AppSettings is one process-wide cache, and the window's own visibility is
// persisted in it -- so every case starts from a known closed state rather
// than from whatever the previous case left behind.
void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Brings an applet up to "gate present, diversity live" with `diversity` as
// the /diversity body.
void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

// (a) The window is built lazily from the sidebar button, needs no transport
// of its own, and its open/closed state is what DiversityWindowVisible says.
void testOpenButtonBuildsTheWindowAndPersistsItsVisibility()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    CHECK(a.gatePresent());

    // Nothing has asked for it yet, so nothing has been built.
    CHECK(a.diversityPanel()->window() == nullptr);

    auto* button = openButton(a);
    CHECK(button != nullptr);
    CHECK(button->accessibleName() == QStringLiteral("Open the diversity window"));

    const int requestsBefore = net.log.size();
    button->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    CHECK(w && w->isVisible());
    CHECK(AppSettings::instance()
              .value(QStringLiteral("DiversityWindowVisible"))
              .toString() == QStringLiteral("True"));
    // Building and showing it must not have talked to the gate: the window
    // owns no transport, only the applet does.
    CHECK(net.log.size() == requestsBefore);

    button->click();
    settle();
    CHECK(w && !w->isVisible());
    CHECK(AppSettings::instance()
              .value(QStringLiteral("DiversityWindowVisible"))
              .toString() == QStringLiteral("False"));

    // Re-opening reuses the SAME window rather than leaking a second one.
    button->click();
    settle();
    CHECK(a.diversityPanel()->window() == w);
    CHECK(w && w->isVisible());
    closedToStart();
}

// (b) A full payload and an all-nulls one both apply without crashing, and the
// window's scope agrees with the payload's own numbers.
void testFullAndNullPayloadsApplyAndTheScopeAgrees()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* scope = w->findChild<DiversityScope*>(QStringLiteral("diversityWindowScope"));
    CHECK(scope && scope->isLarge());
    // out - max(a, b) = 15.1 - 12.3.
    CHECK(scope && std::abs(scope->lastGainDb() - 2.8) < 1e-9);

    auto* meterA = w->findChild<DiversitySnrMeter*>(QStringLiteral("diversityWindowMeterA"));
    auto* meterOut = w->findChild<DiversitySnrMeter*>(QStringLiteral("diversityWindowMeterOut"));
    CHECK(meterA && std::abs(meterA->shownDb() - 12.3) < 1e-9);
    CHECK(meterOut && std::abs(meterOut->shownDb() - 15.1) < 1e-9);

    auto* stations = w->findChild<QLabel*>(
        QStringLiteral("diversityWindowStationsCountLabel"));
    CHECK(stations && stations->text() == QStringLiteral("2 stations remembered"));
    auto* aligned = w->findChild<QLabel*>(QStringLiteral("diversityWindowAlignedLabel"));
    CHECK(aligned && aligned->text() == QStringLiteral("aligned"));
    auto* lag = w->findChild<QLabel*>(QStringLiteral("diversityWindowLagLabel"));
    CHECK(lag && lag->text() == QStringLiteral("3"));

    // Now the same window fed a payload whose every optional leg is null.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityNulls};
    tick(a);
    CHECK(scope && std::isnan(scope->lastGainDb()));
    CHECK(meterA && std::isnan(meterA->shownDb()));
    // A null lag is "unknown", not zero.
    CHECK(lag && lag->text() == QStringLiteral("—"));
    CHECK(aligned && aligned->text() == QStringLiteral("not aligned"));
    closedToStart();
}

// (c) A mode button in the window writes the identical GET the sidebar's mode
// combo does -- the window is a second view of the same state, not a second
// protocol.
void testWindowModeButtonSendsTheSameQueryAsTheSidebarCombo()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* track = w->findChild<QPushButton*>(QStringLiteral("diversityWindowModetrack"));
    CHECK(track != nullptr);
    const int before = net.count(QStringLiteral("/diversity/set?mode=track"));
    if (track)
        track->click();
    settle();
    CHECK(net.count(QStringLiteral("/diversity/set?mode=track")) == before + 1);

    // And the read-back that follows must not turn into a second write: the
    // buttons are checked back with the signal blocked.
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityNulls};
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/set?mode=track")) == before + 1);
    CHECK(track && track->isChecked());
    closedToStart();
}

// (d) Phase/ratio are a MANUAL setpoint. In track mode the gate solves for its
// own weight, so the knobs are disabled AND a poll must not move them --
// otherwise the window would be showing a control that looks live and is not.
void testPhaseKnobDisabledInTrackModeAndNotWrittenByAPoll()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* phase = w->findChild<ClientCompKnob*>(QStringLiteral("diversityWindowPhaseKnob"));
    auto* ratio = w->findChild<ClientCompKnob*>(QStringLiteral("diversityWindowRatioKnob"));
    CHECK(phase && phase->isEnabled());          // manual
    CHECK(ratio && ratio->isEnabled());
    CHECK(phase && std::abs(phase->value() - 45.0f) < 1e-3f);

    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, kDiversityNulls};
    tick(a);
    CHECK(phase && !phase->isEnabled());         // track
    CHECK(ratio && !ratio->isEnabled());
    // kDiversityNulls carries phase_deg 10.0; the knob must still read 45.
    CHECK(phase && std::abs(phase->value() - 45.0f) < 1e-3f);

    const int writes = net.count(QStringLiteral("/diversity/set?phase="));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/set?phase=")) == writes);
    closedToStart();
}

// (e) The window's noise panel shows the same map strip much larger, so the
// map poll has to keep running while the window is open even when the
// sidebar's own Noise block is collapsed.
void testMapPollRunsWhileWindowVisibleWithSidebarNoiseCollapsed()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    net.routes[QStringLiteral("/diversity/map")] = {QNetworkReply::NoError, makeDiversityMap(8)};
    connectGate(a, net, kDiversityFull);

    auto* noise = a.findChild<QToolButton*>(QStringLiteral("gateDiversityNoiseHeader"));
    CHECK(noise != nullptr);
    if (noise)
        noise->setChecked(false);
    settle();
    CHECK(!a.diversityPanel()->wantsMapPoll());

    const int collapsed = net.count(QStringLiteral("/diversity/map"));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == collapsed);

    openButton(a)->click();
    settle();
    CHECK(a.diversityPanel()->wantsMapPoll());
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) > collapsed);

    // Closing it again with Noise still collapsed stops the poll once more.
    openButton(a)->click();
    settle();
    CHECK(!a.diversityPanel()->wantsMapPoll());
    const int reclosed = net.count(QStringLiteral("/diversity/map"));
    tick(a);
    tick(a);
    CHECK(net.count(QStringLiteral("/diversity/map")) == reclosed);
    closedToStart();
}

// (f) The gate going away clears every readout: a window left open on a dead
// gate must not keep showing the last numbers as if they were live.
void testGateGoingAwayClearsTheWindowsReadouts()
{
    closedToStart();
    FakeGate net;
    AetherGateApplet a(nullptr, &net);
    connectGate(a, net, kDiversityFull);
    openButton(a)->click();
    settle();
    tick(a);

    DiversityWindow* w = a.diversityPanel()->window();
    CHECK(w != nullptr);
    if (!w)
        return;

    auto* scope = w->findChild<DiversityScope*>(QStringLiteral("diversityWindowScope"));
    auto* meterB = w->findChild<DiversitySnrMeter*>(QStringLiteral("diversityWindowMeterB"));
    auto* stations = w->findChild<QLabel*>(
        QStringLiteral("diversityWindowStationsCountLabel"));
    auto* status = w->findChild<QLabel*>(QStringLiteral("diversityWindowStatusLabel"));
    CHECK(meterB && !std::isnan(meterB->shownDb()));
    CHECK(status && status->text() == QStringLiteral("gate connected · diversity live"));

    net.down = true;
    tick(a);
    tick(a);
    tick(a);
    CHECK(!a.gatePresent());

    CHECK(scope && std::isnan(scope->lastGainDb()));
    CHECK(meterB && std::isnan(meterB->shownDb()));
    CHECK(stations && stations->text() == QStringLiteral("0 stations remembered"));
    CHECK(status && status->text() == QStringLiteral("gate not answering"));
    // The operator opened it; a dropped poll is not a reason to take it away.
    CHECK(w->isVisible());
    closedToStart();
}

} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("diversity_window_test"));
    QApplication app(argc, argv);

    testOpenButtonBuildsTheWindowAndPersistsItsVisibility();
    testFullAndNullPayloadsApplyAndTheScopeAgrees();
    testWindowModeButtonSendsTheSameQueryAsTheSidebarCombo();
    testPhaseKnobDisabledInTrackModeAndNotWrittenByAPoll();
    testMapPollRunsWhileWindowVisibleWithSidebarNoiseCollapsed();
    testGateGoingAwayClearsTheWindowsReadouts();

    std::printf("\n%d diversity window test(s) failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
