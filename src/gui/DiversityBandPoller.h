#pragma once

// DiversityBandPoller -- the transport for the Diversity window's BAND page.
//
// AetherGateApplet owns every other gate request, and this is still ITS
// transport: the poller is constructed with the applet's own
// QNetworkAccessManager (so tests keep driving the whole section through one
// injected manager) and is owned by the applet. It lives in its own file for
// two reasons that are both about honesty rather than tidiness:
//
//   * CADENCE. /status and /diversity share one 1 Hz timer, and the comment on
//     AetherGateApplet::poll() ("piggyback -- never a second timer") is about
//     not splitting one cadence across two clocks. The BAND page genuinely has
//     a different one: /diversity/spatial is a waterfall row and wants 4 Hz,
//     /diversity/finder is a ten-minute summary and wants 1 Hz. A route that
//     needs four times the rate cannot piggyback on a timer that ticks once.
//   * SIZE. AetherGateApplet.cpp is already past the file-size budget
//     AGENTS.md asks for; new transport goes beside it, not into it.
//
// Both routes are non-critical to gate presence, exactly like /diversity and
// /diversity/map: a 404 from a gate that predates them, an {"error": ...} body
// or a refused connection all mean "nothing to draw on that page", never "the
// gate is gone". Nothing here touches the applet's failure count.
//
// It polls only while the BAND page is actually on screen. A closed window, or
// an open one showing SLICE, costs no requests at all -- the same rule
// AetherGateDiversityPanel::wantsMapPoll() applies to /diversity/map.

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

namespace AetherSDR {

class DiversityBandPoller : public QObject {
    Q_OBJECT
public:
    // `net` is the applet's manager and is NOT owned here.
    explicit DiversityBandPoller(QNetworkAccessManager* net, QObject* parent = nullptr);

    // "http://<ip>:<port>", or empty while there is no gate to ask. An empty
    // base stops the timer: a poller with nowhere to ask must not spin.
    void setBaseUrl(const QString& base);

    // True only while the BAND page is on screen. Turning it on fetches
    // immediately rather than waiting out a tick, so switching pages paints a
    // row at once.
    void setEnabled(bool on);

    bool isPolling() const;

signals:
    // One /diversity/spatial answer. An empty object is "nothing to add" -- a
    // failed request, a non-JSON body, or a gate that has no such route.
    void spatialReceived(QJsonObject spatial);
    // One /diversity/finder answer, same contract.
    void finderReceived(QJsonObject finder);

public slots:
    // One tick: always /diversity/spatial, and /diversity/finder on every
    // kFinderEveryTicks'th. A slot rather than a lambda on the timer so a test
    // can step the cadence directly instead of waiting out real milliseconds.
    void poll();

private:
    void restart();
    void fetchSpatial();
    void fetchFinder();

    QNetworkAccessManager* m_net{nullptr};
    QTimer*                m_timer{nullptr};
    QString                m_base;
    bool                   m_enabled{false};
    int                    m_tick{0};
    // A reply still on the wire when the next tick comes round is not a reason
    // to start a second one: at 4 Hz a slow gate would otherwise queue
    // requests faster than it answers them.
    bool                   m_spatialInFlight{false};
    bool                   m_finderInFlight{false};
};

} // namespace AetherSDR
