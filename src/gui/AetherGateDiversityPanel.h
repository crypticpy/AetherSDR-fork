#pragma once

#include <QPointer>
#include <QUrlQuery>
#include <QWidget>

class QComboBox;
class QJsonObject;
class QLabel;
class QPushButton;

namespace AetherSDR {

// Same include-surface discipline AetherGateApplet.h already keeps: nothing
// outside the .cpp that builds one needs the full type.
class DiversityScope;
// The pop-out Diversity window this panel's "Open Diversity window" button
// toggles. Built on first use and then kept; see toggleWindow().
class DiversityWindow;

// AetherGateDiversityPanel — the sidebar's ENTRY POINT to the RSPduo
// dual-tuner combiner, and nothing more.
//
// It used to be the whole instrument: mode/phase/ratio/source combos, a
// scope, the blanker, the pan selector, the noise map, the sources list, the
// memory and capture rows, all folded into four collapsible blocks so they
// would fit a ~250px column at all. Every one of those has moved to
// DiversityWindow, which has the width to show them honestly
// (docs/DIVERSITY-ROADMAP.md §3: "the sidebar is a status line and a door;
// the window is the instrument"). What is left here is exactly that:
//
//   * one status line   -- "track · #3 Bob · +1.4 dB", or "off".
//   * the mode selector -- the one control worth reaching without opening
//                          anything.
//   * "Open Diversity window" -- the door.
//
// The compact DiversityScope is still built and still fed, but is shown only
// when the AetherGateDiversityPanel_ShowScope AppSettings key says so
// (default off). There is deliberately no UI to flip it: it exists for the
// operator who wants a glance-view in the sidebar, not as a second place to
// decide the layout from.
//
// It owns NO network transport: AetherGateApplet keeps the
// QNetworkAccessManager (and stays the only file the network-timeout ratchet
// in tools/check_network_timeouts.py has to reason about for the gate
// section). Every write this panel — or the window behind it — wants to make
// is a signal; AetherGateApplet turns each into the matching GET and feeds
// the read-back back in through applyDiversity().
class AetherGateDiversityPanel : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateDiversityPanel(QWidget* parent = nullptr);

    // Applied on every /diversity poll AND every write's read-back — same
    // contract AetherGateApplet::applyDiversity() always had. isJson==false,
    // or an "available": false payload, both mean "nothing to show": the
    // whole panel widget hides itself (it is not merely emptied) and the
    // scope clears. Until the first "available": true poll it stays hidden.
    // Every field is independently optional on the wire; a missing or
    // malformed one leaves its widget at whatever it already showed rather
    // than inventing a value.
    void applyDiversity(const QJsonObject& diversity, bool isJson);

    // Feeds one /diversity/map answer through to the window, which is the
    // only view of the map left — the sidebar's own strip moved with the
    // rest. Kept on the panel because the applet's poll wiring goes through
    // it; see wantsMapPoll() for when that poll runs at all.
    void applyMap(const QJsonObject& map);

    // A capture request's own reply: ok==true with a (possibly empty) file
    // path on success, ok==false with the text to show instead (a network
    // failure, a non-JSON body, or the gate's own {"error": ...}). The
    // window owns the capture button and its result label, so this is now
    // pure forwarding.
    void applyCaptureResult(bool ok, const QString& pathOrError);

    // The BAND page's two payloads, forwarded to the window the same way
    // applyMap() is -- the window is the only view of either. Fed by
    // AetherGateApplet's DiversityBandPoller, which runs only while
    // wantsBandPoll() holds.
    void applySpatial(const QJsonObject& spatial);
    void applyFinder(const QJsonObject& finder);

    // The SITE page's payload, forwarded the same way. Fed by the same
    // DiversityBandPoller, which fetches /diversity/beacons only while
    // wantsSitePoll() holds.
    void applyBeacons(const QJsonObject& beacons);

    // The FILTER page's payload: one /filter answer, or the identical status
    // object a /filter/set or /filter/notch write replies with, or
    // {"error": "..."} when the gate refused a value. Forwarded to the window
    // like every other page's; fed by the same DiversityBandPoller, which
    // polls /filter only while wantsFilterPoll() holds but answers a write at
    // any time.
    void applyFilter(const QJsonObject& filter);

    // The reply to one of the SITE page's own writes. Deliberately NOT fed
    // through applyDiversity(): these replies are a status object most of the
    // time but {"error": "..."} when the gate refuses, and one error body down
    // the status path would read as "diversity went away" for a whole poll.
    void applySiteReply(const QJsonObject& reply);

    // Where the radio is tuned now, in absolute Hz, pushed down once a second
    // from AetherGateApplet -- the only object in this section that can see
    // the SliceModel. The SITE page's BEACON CHECK is what needs it: it is the
    // frequency the check comes home to.
    void setActiveSliceHz(double hz);

    // present/absent — mirrors AetherGateApplet::setPresent(): false hides
    // the panel and resets every readout that must not outlive a reconnect
    // to a different (or older) gate at the same address.
    void setPresent(bool present);

    // The "Hear A only" compare hold moved to the window along with the
    // button that arms it, and the window ends its own hold in its
    // closeEvent(). Nothing in the sidebar can be holding one any more, so
    // this is a no-op — kept because AetherGateApplet calls it
    // unconditionally from setRadioAddress() and because the hold is the
    // kind of state a future sidebar control could reintroduce.
    void restoreCompareHold();

    // True only while the pop-out window is on screen: its noise panel is
    // the only thing that draws the map now, so a closed window costs no
    // /diversity/map polling at all. AetherGateApplet gates its map poll on
    // this.
    bool wantsMapPoll() const;

    // True only while the pop-out window is on screen AND showing its BAND
    // page. /diversity/spatial is a 4 Hz route and /diversity/finder a 1 Hz
    // one; neither is worth a byte while nobody is looking at the page they
    // draw on, so AetherGateApplet gates its band poller on this and on the
    // bandPollChanged() signal below.
    bool wantsBandPoll() const;

    // True only while the pop-out window is on screen AND showing its SITE
    // page. /diversity/beacons answers about a three-minute rota and is worth
    // nothing to a page nobody is looking at, so it is gated exactly the way
    // the two BAND routes are -- and announced by the same bandPollChanged()
    // signal, which is about the visible PAGE rather than about one route.
    bool wantsSitePoll() const;

    // True only while the pop-out window is on screen AND showing its FILTER
    // page. /filter carries a 128-point response curve on every answer and is
    // gated exactly the way the three routes above are.
    bool wantsFilterPoll() const;

    // Test/introspection accessor for the pop-out window -- null until the
    // "Open Diversity window" button has been pressed once (or the persisted
    // DiversityWindowVisible reopened it). The window is a top-level of its
    // own, so findChild() from the applet cannot reach it.
    // Out of line: QPointer<T>'s conversion to T* needs the complete type,
    // and this header is included by files that only forward-declare it.
    DiversityWindow* window() const;

signals:
    // -> GET /diversity/set, guarded by the applet's own presence check —
    // same contract the old sendDiversitySet() enforced internally.
    void requestSet(QUrlQuery query);
    // -> GET /diversity/set for the compare-hold's forced resume. NOT gated
    // on presence — the gate must never be left parked in "off" just
    // because the poll that would have re-enabled the write failed. Emitted
    // by the window, which owns the hold, and routed out through here so
    // every gate write still leaves by one door.
    void requestCompareRestore(QUrlQuery query);
    // -> GET /diversity/align
    void requestAlign();
    // -> GET /diversity/capture?seconds=<seconds>
    void requestCapture(int seconds);
    // -> GET /diversity/memory/clear
    void requestMemoryClear();
    // -> GET /diversity/memory/name?id=<id>&name=<urlencoded>. An empty name
    // clears the gate's label.
    void requestMemoryName(int id, QString name);
    // -> the active slice. The gate has no tune route of its own, so a click
    // on the BAND page's waterfall or FINDER table leaves the window, crosses
    // here, and is turned into a real slice tune by AetherGateApplet -- the
    // one place in this section that can reach the RadioModel. Absolute Hz.
    void requestTune(double hz);
    // -> GET <path>?<query> on the gate, where <path> is "/filter/set" or
    // "/filter/notch" (or "/filter" itself, which is the "read it back now"
    // the page fires after a refusal). A sibling of requestSet() rather than a
    // widening of it: requestSet means "/diversity/set" and nothing else, and
    // a signal that sometimes meant a different route would make every
    // existing connection to it harder to read. Served by
    // DiversityBandPoller::sendFilter(), which owns this page's transport.
    void requestFilter(QString path, QUrlQuery query);
    // -> GET <path>?<query> for the SITE page: the route and query string are
    // the gate's own, quoted verbatim out of a noise-profile action or built
    // from the station-locator field. Its answer comes back through
    // applySiteReply() rather than through the status path. Served by
    // DiversityBandPoller::sendSite().
    void requestSite(QString path, QUrlQuery query);
    // wantsBandPoll() may have changed: the window opened or closed, or its
    // page switched. Polling it once a second off the status timer would leave
    // the BAND page blank for up to a second after it is opened, which is the
    // whole time an operator spends deciding it is broken.
    void bandPollChanged();

private:
    // Shows the pop-out window (building it on first use) or hides it, and
    // persists which of the two under DiversityWindowVisible.
    void toggleWindow();

    QLabel*      m_statusLine{nullptr};
    QComboBox*   m_mode{nullptr};
    QPushButton* m_openWindowButton{nullptr};
    // Built and fed unconditionally; shown only when
    // AetherGateDiversityPanel_ShowScope is set. A hidden widget does not
    // paint, so feeding it costs nothing but the setState() call itself.
    DiversityScope* m_scope{nullptr};

    // The pop-out window, or null until it has been opened once. QPointer
    // rather than a raw pointer: it is a top-level widget the operator can
    // close, and although closing only hides it, nothing in this panel should
    // depend on that staying true.
    QPointer<DiversityWindow> m_window;
    // Last frequency the applet reported for the active slice, in Hz. Kept
    // here so a window opened later starts out knowing it.
    double m_activeSliceHz{0.0};
    // DiversityWindowVisible is restored ONCE, on the first poll that reports
    // diversity available -- reopening at construction would pop a window for
    // a gate that may not even be there, and reopening on every poll would
    // fight the operator closing it.
    bool m_windowRestored{false};

    // present==true implies the applet's baseUrl() is non-empty
    // (setPresent(true) only ever follows a real reply); the window is told
    // the same flag so its own controls can guard on it.
    bool m_present{false};
};

} // namespace AetherSDR
