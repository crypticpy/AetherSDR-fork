#pragma once

// DiversityWindow -- the RSPduo dual-tuner combiner as a piece of station
// equipment rather than a sidebar strip.
//
// AetherGateDiversityPanel has to live inside a ~250px applet column, and
// every design decision in it is a concession to that: a 176px scope, a
// four-row sources list, phase/ratio on a slider and a spinbox, four
// collapsible blocks so the whole section fits at all. This window is the
// same state with the width to show it properly.
//
// v2 of it exists because the v1 layout answered the wrong question. A ham
// looking at it could not tell WHO the remembered weights belonged to, why
// there were dots on the dial, what the noise panel was measuring, or what
// had just happened while he was tuning. So the second pass is organised
// around those questions rather than around the wire format:
//
//   * TALKERS   -- the memory list as people, not coordinates: a stable id,
//                  an editable name, the phase (two loops give inter-antenna
//                  PHASE, not a bearing -- there is no third baseline to
//                  triangulate from), level, hits, and how long ago each was
//                  heard. The one whose weight is live right now is lit.
//   * the dial  -- every marker numbered to match those rows, with a legend
//                  under it saying what filled/hollow and the two rings mean.
//   * TIMELINE  -- two minutes of A/B/OUT, so "is this helping?" is a glance
//                  rather than an inference from three jittering numbers.
//   * EVENTS    -- poll-to-poll transitions as sentences, because "what just
//                  happened" is not answerable from any instantaneous readout.
//   * NOISE     -- the coherence map with a frequency axis and the receiver's
//                  own passband drawn over it, plus every control explained
//                  in a tooltip written for somebody who has never seen a
//                  diversity combiner.
//
// It owns NO network transport and NO state of its own beyond the event log
// and the timeline's own ring buffer. Every payload arrives through
// applyDiversity()/applyMap()/applyCaptureResult() exactly as the sidebar
// panel receives it, and every write leaves as one of the same request
// signals the panel emits -- DiversityWindow::createFor() wires them straight
// through to the panel's own signals, so AetherGateApplet keeps being the one
// place a gate request is built. That is also why a change made here shows up
// in the sidebar and vice versa: both are views of the same polled state,
// neither echoes locally.
//
// It has four pages, switched by the SLICE/BAND/SITE/FILTER buttons at the
// left of the chain row. SLICE is the window described above -- everything about the
// frequency you are tuned to. BAND is about the SPAN: the spatial waterfall
// and the conversation FINDER, both click-to-tune, built on the gate's
// /diversity/spatial and /diversity/finder routes and polled only while that
// page is on screen (see DiversityWindowBand.cpp). SITE is about neither: it
// is about the STATION -- what kind of noise this address makes, and what the
// beacon project measures your antennas to be worth (see
// DiversityWindowSite.cpp).
//
// Layout of the SLICE page, top to bottom:
//   * chain row  -- SLICE/BAND, MODE (off/manual/null/track), HEAR
//                   (combined/A/B), the hold-to-compare "Hear A only",
//                   REALIGN, and CAPTURE. Shared by both pages.
//   * row 0      -- the scope (two columns) and TALKERS beside it.
//   * row 1      -- the timeline, full width.
//   * row 2      -- ANTENNAS | NOISE | EVENTS.
//   * status strip mirroring the applet's own presence line.
//
// Nothing in it moves or resizes on a poll: every numeric readout has a fixed
// field width, the talkers table has fixed column widths, the timeline has a
// fixed height and a fixed time axis, and no widget is shown or hidden by
// data.

#include "gui/DiversityWindowEvents.h"
#include "gui/PersistentDialog.h"

#include <QString>
#include <QStringList>
#include <QUrlQuery>

class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QHideEvent;
class QShowEvent;
class QJsonArray;
class QJsonObject;
class QJsonValue;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace AetherSDR {

class AetherGateDiversityPanel;
class ClientCompKnob;
class DiversityBeaconPanel;
class DiversityFilterControls;
class DiversityFinderPanel;
class DiversityNoiseProfilePanel;
class DiversityMapStrip;
class DiversityScope;
class DiversitySpatialWaterfall;
class DiversitySnrMeter;
class DiversityTimeline;

class DiversityWindow : public PersistentDialog {
    Q_OBJECT
public:
    explicit DiversityWindow(QWidget* parent = nullptr);

    // Builds a window for `panel` and connects its request signals straight
    // through to the panel's own identically-named ones, so a write made here
    // takes exactly the route a write made in the sidebar does. The window is
    // parented to the panel: it is a top-level either way (a QDialog), but the
    // parent keeps it in front of the main window and gets it destroyed with
    // the applet.
    static DiversityWindow* createFor(AetherGateDiversityPanel* panel);

    // Same three payload entry points AetherGateDiversityPanel has, with the
    // same contract: every field is independently optional, a missing or
    // malformed one leaves its widget alone, and isJson == false or
    // "available": false clears everything.
    void applyDiversity(const QJsonObject& diversity, bool isJson);
    void applyMap(const QJsonObject& map);
    void applyCaptureResult(bool ok, const QString& pathOrError);

    // The BAND page's own two payloads, same contract as the three above: a
    // missing or malformed field leaves its widget alone, and an
    // "available": false answer is a fact about the gate rather than a reason
    // to invent one. Fed only while that page is on screen -- see
    // bandPageVisible(), which is what gates the polls upstream.
    void applySpatial(const QJsonObject& spatial);
    void applyFinder(const QJsonObject& finder);

    // The SITE page's own payload, same contract again: one
    // /diversity/beacons answer, fed only while that page is on screen -- see
    // sitePageVisible(). (The page's other half, the noise profile, rides on
    // the /diversity status object every page already gets.)
    void applyBeacons(const QJsonObject& beacons);

    // The FILTER page's own payload: one /filter answer, or the identical
    // status object a /filter/set or /filter/notch write replies with, or
    // {"error": "..."} when the gate refused a value. Fed whenever one arrives
    // rather than only while the page is up -- a write is answered wherever
    // the operator made it -- and see filterPageVisible() for when it is
    // POLLED.
    void applyFilter(const QJsonObject& filter);

    // The reply to one of the SITE page's own writes -- a noise-profile action
    // button, or the station locator. Routed to whichever of the two panels
    // asked; a reply nobody asked for is dropped, so the beacon panel never
    // shows the noise panel's refusal.
    void applySiteReply(const QJsonObject& reply);

    // Where the radio is tuned now, in absolute Hz. The SITE page's BEACON
    // CHECK is the only thing that needs it: it is the frequency the check
    // comes home to, and without one the check refuses to start rather than
    // tuning away with no way back.
    void setActiveSliceHz(double hz);

    // Ends a running BEACON CHECK and tunes the slice back at once. Called
    // from closeEvent(): a countdown nobody can see must not be left holding
    // the radio on a beacon frequency. A page switch is NOT this -- it hides
    // the page and the check goes on, which is the whole point of it.
    void endBeaconCheck();

    // True while the window is showing BAND rather than SLICE.
    // AetherGateDiversityPanel::wantsBandPoll() combines it with the window's
    // own visibility, and AetherGateApplet polls /diversity/spatial and
    // /diversity/finder only when both hold.
    bool bandPageVisible() const;

    // True while the window is showing SITE. AetherGateDiversityPanel::
    // wantsSitePoll() combines it with the window's own visibility, and
    // AetherGateApplet polls /diversity/beacons only when both hold.
    bool sitePageVisible() const;

    // True while the window is showing FILTER. AetherGateDiversityPanel::
    // wantsFilterPoll() combines it with the window's own visibility, and the
    // band poller reads /filter at 2 Hz only when both hold.
    bool filterPageVisible() const;

    // Mirrors AetherGateApplet's own presence flag: false clears every
    // readout and greys the status strip, but leaves the window open -- the
    // operator opened it, and a dropped poll is not a reason to take it away.
    void setPresent(bool present);

signals:
    void requestSet(QUrlQuery query);
    void requestCompareRestore(QUrlQuery query);
    void requestAlign();
    void requestCapture(int seconds);
    void requestMemoryClear();
    // -> GET /diversity/memory/name?id=<id>&name=<urlencoded>. An empty name
    // clears the gate's own label for that talker.
    void requestMemoryName(int id, QString name);
    // -> the ACTIVE SLICE, not the gate: the gate has no tune route of its own
    // (docs/DIVERSITY.md, "Limits and known gaps"), so a click on the spatial
    // waterfall or a FINDER row leaves through AetherGateDiversityPanel to
    // AetherGateApplet, which tunes AetherSDR's own slice. Absolute Hz.
    void requestTune(double hz);
    // -> GET <path>?<query> on the gate: "/filter/set", "/filter/notch", or
    // "/filter" for a plain re-read. A signal of its own rather than a wider
    // requestSet(), which means "/diversity/set" and nothing else.
    void requestFilter(QString path, QUrlQuery query);
    // -> GET <path>?<query> for the SITE page: the gate's own route and query,
    // quoted back out of a noise-profile action or built from the station
    // locator field. Answered through applySiteReply().
    void requestSite(QString path, QUrlQuery query);
    // The visible page changed, or the window opened or closed. Carries
    // whether the two BAND routes should be polled; the SITE page's own route
    // is read back off sitePageVisible() by the same handler, so one signal
    // covers every page switch.
    void bandPageChanged(bool bandVisible);

protected:
    // Persists DiversityWindowVisible; PersistentDialog's own override saves
    // the geometry.
    void closeEvent(QCloseEvent* event) override;
    // The BAND page's two polls follow the window on and off the screen. Both
    // are announced from the show/hide events rather than from the code that
    // calls show()/hide(): those events are delivered AFTER isVisible() has
    // flipped, which is the state AetherGateDiversityPanel::wantsBandPoll()
    // reads, and they catch every path (the close button, a hide() from the
    // sidebar, the window manager) rather than the two we thought of.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QWidget* buildChainRow();
    // The SLICE/BAND page switch, appended to the chain row's own layout, and
    // the BAND page itself. Both defined in DiversityWindowBand.cpp.
    void     buildPageSwitch(QWidget* row);
    QWidget* buildBandPage();
    // The SITE page and the two members that feed it, defined in
    // DiversityWindowSite.cpp.
    QWidget* buildSitePage();
    // The FILTER page, defined in DiversityWindowFilter.cpp beside the one
    // method that reads a /filter status object.
    QWidget* buildFilterPage();
    void     clearFilterReadouts();
    // The per-bin weights control, built into the SLICE page's ANTENNAS panel
    // and defined beside the rest of the subband story rather than beside the
    // panel it is added to.
    QWidget* buildSubbandRow(QWidget* parent);
    // The two keys of the /diversity status object the SITE page reads
    // (noise_profile) and the SLICE page's checkbox reflects (subband). One
    // call from applyDiversity(); nothing else in this window touches either.
    void     applySite(const QJsonObject& d);
    void     clearSiteReadouts();
    // Index into m_pages: 0 SLICE, 1 BAND, 2 SITE, 3 FILTER.
    void     showPage(int page);
    // A click on the waterfall or a FINDER row: emits requestTune() and, when
    // the combiner is not already tracking, asks for mode=track.
    void     tuneTo(double hz);
    void     clearBandReadouts();
    QWidget* buildAntennasPanel();
    QWidget* buildNoisePanel();
    QWidget* buildTalkersPanel();
    QWidget* buildEventsPanel();

    // One exclusive row of checkable buttons (MODE, HEAR, PAN). `values` are
    // the wire values; `key` is the /diversity/set query key each button
    // writes; `tips` is one hover explanation per button. Returns the group so
    // applyDiversity() can check a button back without re-emitting.
    QButtonGroup* addButtonRow(QWidget* row, const QString& caption, const QString& key,
                               const QString& objectPrefix, const QStringList& labels,
                               const QStringList& values, const QStringList& tips);
    // Checks the button carrying `value` without emitting a write.
    static void checkValue(QButtonGroup* group, const QString& value);

    // Rebuilds the talkers table from one "memory" array, lighting the row
    // whose id matches `talkerId` when `haveTalker`.
    void applyTalkers(const QJsonArray& memory, bool haveTalker, int talkerId,
                      double talkerSinceS);
    // The gate's "focus" object (or a non-object when there is none): the
    // station the combiner is pinned on, and what it is doing about everyone
    // else. Drives the LOCKED banner and the Lock/Release button.
    void applyFocus(const QJsonValue& focus, bool haveTalker, int talkerId,
                    const QString& talkerName);
    void updateLockButton();
    int  selectedTalkerId() const;
    // Re-applies the live-talker row brush after a theme switch -- the
    // highlight is a token-backed QBrush on the items, which a stylesheet
    // re-polish cannot reach.
    void restyleTalkerRows();
    // True while the operator has a Name cell open in an editor. A poll must
    // not rebuild the table out from under a half-typed callsign.
    bool talkersBusy() const;
    void onTalkerItemChanged(QTableWidgetItem* item);

    // Stamps and prepends lines to the EVENTS list, capped.
    void addEventLines(const QStringList& lines);
    void endCompareHold();

    void clearReadouts();
    // True while the operator is holding/editing `knob` or its debounce has a
    // write pending -- a poll must not move it out from under either.
    static bool knobBusy(const ClientCompKnob* knob, const QTimer* debounce);

    // --- pages ------------------------------------------------------------
    QStackedWidget* m_pages{nullptr};
    QToolButton*    m_pageSliceButton{nullptr};
    QToolButton*    m_pageBandButton{nullptr};
    QToolButton*    m_pageSiteButton{nullptr};
    QToolButton*    m_pageFilterButton{nullptr};
    DiversitySpatialWaterfall* m_waterfall{nullptr};
    DiversityFinderPanel*      m_finder{nullptr};

    // --- site ------------------------------------------------------------
    // --- filter ------------------------------------------------------------
    // The whole FILTER page. It keeps its own state and this window keeps none
    // of it -- see DiversityFilterControls.h.
    DiversityFilterControls* m_filter{nullptr};

    DiversityNoiseProfilePanel* m_noiseProfile{nullptr};
    DiversityBeaconPanel*       m_beacons{nullptr};
    // The per-bin weights control lives on the SLICE page (ANTENNAS), because
    // it is a control over the weight the rest of that page is about; the SITE
    // page states the same subband numbers as a sentence.
    QCheckBox*                  m_subbandCheck{nullptr};
    QLabel*                     m_subbandValue{nullptr};

    // --- chain row --------------------------------------------------------
    QButtonGroup* m_modeGroup{nullptr};
    QButtonGroup* m_hearGroup{nullptr};
    QPushButton*  m_compareButton{nullptr};
    QPushButton*  m_realignButton{nullptr};
    QPushButton*  m_captureButton{nullptr};
    QSpinBox*     m_captureSpin{nullptr};
    QString       m_compareResumeMode;
    bool          m_compareDown{false};

    // --- scope / timeline -------------------------------------------------
    DiversityScope*    m_scope{nullptr};
    DiversityTimeline* m_timeline{nullptr};

    // --- antennas ---------------------------------------------------------
    DiversitySnrMeter* m_meterA{nullptr};
    DiversitySnrMeter* m_meterB{nullptr};
    DiversitySnrMeter* m_meterOut{nullptr};
    QLabel*            m_manualCaption{nullptr};
    ClientCompKnob*    m_phaseKnob{nullptr};
    ClientCompKnob*    m_ratioKnob{nullptr};
    QTimer*            m_phaseDebounce{nullptr};
    QTimer*            m_ratioDebounce{nullptr};
    // BALANCE block: the three numbers that decide whether the second loop is
    // buying anything, and a one-word verdict derived from them.
    QLabel* m_balanceDelta{nullptr};
    QLabel* m_balanceCoherence{nullptr};
    QLabel* m_balancePassband{nullptr};
    QLabel* m_balanceVerdict{nullptr};

    // --- noise ------------------------------------------------------------
    QPushButton*       m_nbButton{nullptr};
    ClientCompKnob*    m_nbKnob{nullptr};
    QTimer*            m_nbDebounce{nullptr};
    QButtonGroup*      m_panGroup{nullptr};
    DiversityMapStrip* m_mapStrip{nullptr};
    QListWidget*       m_sourcesList{nullptr};
    QPushButton*       m_nullSourceButton{nullptr};
    QLabel*            m_noiseStatus{nullptr};

    // --- talkers ----------------------------------------------------------
    QTableWidget* m_talkers{nullptr};
    // Last rendered table content, one packed row per entry -- the table is
    // rebuilt only when this changes, so an unchanged memory list does not
    // drop the operator's selection or scroll position on every poll.
    QStringList   m_talkerRows;
    // Row currently lit as the live talker, or -1. Kept so a theme switch can
    // re-brush it without a rebuild.
    int           m_talkerLiveRow{-1};
    QLabel*       m_talkersCount{nullptr};
    QPushButton*  m_memoryClearButton{nullptr};
    QPushButton*  m_lockButton{nullptr};
    QLabel*       m_focusLine{nullptr};
    bool          m_haveFocus{false};
    int           m_focusId{-1};
    // Set while applyTalkers() is writing items, so the itemChanged() that
    // commits a Name edit can tell an operator's typing from our own paint.
    bool          m_talkersRebuilding{false};

    // --- events -----------------------------------------------------------
    QLabel*           m_alignLine{nullptr};
    QLabel*           m_captureResult{nullptr};
    QListWidget*      m_events{nullptr};
    QPushButton*      m_eventsClearButton{nullptr};
    DiversityEventLog m_eventLog;

    QLabel* m_statusStrip{nullptr};

    bool m_present{false};
    // Set by applyCaptureResult(false, ...) -- while set, a poll's own
    // (possibly stale) capture.path must not overwrite the error this
    // request just reported. Same guard the sidebar panel keeps.
    bool m_captureLocalResult{false};
    // Last capture basename announced into the event list, so a poll that
    // keeps reporting the same finished capture does not announce it again.
    QString m_lastCaptureAnnounced;
};

} // namespace AetherSDR
