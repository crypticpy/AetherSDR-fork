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
// It polls only while the page that draws the answer is actually on screen. A
// closed window, or an open one showing SLICE, costs no requests at all -- the
// same rule AetherGateDiversityPanel::wantsMapPoll() applies to /diversity/map.
//
// The SITE page's /diversity/beacons is served from here too rather than from a
// fourth file, because it is the same story: a non-critical route, gated on one
// page being visible, on the applet's own manager. It does NOT share the BAND
// page's cadence -- a beacon slot is ten seconds long and the schedule turns
// once every three minutes, so 1 Hz is already generous -- so the timer runs at
// 250 ms while BAND is up and at 1000 ms when only SITE is. Two pages of one
// window are never on screen at once, so that is one interval at a time, not a
// compromise between two.

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

    // Which of the two pages that need a route of their own is on screen.
    // Both flags in one call rather than a setter each, so a page SWITCH is
    // one state change: setting them separately would, for one instant
    // between the two calls, have both pages visible and fetch a route for
    // the page being left. Turning a page on fetches immediately rather than
    // waiting out a tick, so a switch paints at once.
    void setPages(bool bandVisible, bool siteVisible);

    bool isPolling() const;

signals:
    // One /diversity/spatial answer. An empty object is "nothing to add" -- a
    // failed request, a non-JSON body, or a gate that has no such route.
    void spatialReceived(QJsonObject spatial);
    // One /diversity/finder answer, same contract.
    void finderReceived(QJsonObject finder);
    // One /diversity/beacons answer, same contract. An empty object is a gate
    // that has no such route, not a band with no beacons on it -- the panel
    // tells those two apart from the payload's own "available" flag.
    void beaconsReceived(QJsonObject beacons);

public slots:
    // One tick: while BAND is up, always /diversity/spatial and
    // /diversity/finder on every kSlowEveryTicks'th; while SITE is up,
    // /diversity/beacons (the timer is already at 1 Hz there, so every tick).
    // A slot rather than a lambda on the timer so a test can step the cadence
    // directly instead of waiting out real milliseconds.
    void poll();

private:
    void restart();
    void fetchSpatial();
    void fetchFinder();
    void fetchBeacons();

    QNetworkAccessManager* m_net{nullptr};
    QTimer*                m_timer{nullptr};
    QString                m_base;
    bool                   m_bandEnabled{false};
    bool                   m_siteEnabled{false};
    int                    m_tick{0};
    // A reply still on the wire when the next tick comes round is not a reason
    // to start a second one: at 4 Hz a slow gate would otherwise queue
    // requests faster than it answers them.
    bool                   m_spatialInFlight{false};
    bool                   m_finderInFlight{false};
    bool                   m_beaconsInFlight{false};
};

} // namespace AetherSDR
