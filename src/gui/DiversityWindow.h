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
// It has four pages, switched by the START/SLICE/BAND/SITE tabs under the
// pair row. START is the session itself: the four things that have to be
// true before listening means anything, each with what it buys you and when
// it has to be redone, derived by DiversitySessionModel and drawn by
// DiversitySessionPage. SLICE is the window described above -- everything about the
// frequency you are tuned to. BAND is about the SPAN: the spatial waterfall
// and the conversation FINDER, both click-to-tune, built on the gate's
// /diversity/spatial and /diversity/finder routes and polled only while that
// page is on screen (see DiversityWindowBand.cpp). SITE is about neither: it
// is about the STATION -- what kind of noise this address makes, and what the
// beacon project measures your antennas to be worth (see
// DiversityWindowSite.cpp).
//
// Three strips sit above every page and belong to none of them, which is the
// whole point of them being three rows rather than one (see
// DiversityWindowChain.cpp):
//   * tab row    -- START / SLICE / BAND / SITE: where you are, plus OPEN
//                   CHAIN at the right-hand end. There was a fifth tab,
//                   FILTER; every stage switch it carried is in the CHAIN
//                   window now, and that button is the door to it.
//   * pair row   -- MODE (off/manual/null/track), HEAR (combined/A/B/stereo),
//                   the hold-to-compare "Hear A only", REALIGN, and CAPTURE
//                   with its duration. What the two tuners are doing, on
//                   every page.
//   * NEXT strip -- ONE step at the foot of the window: the one thing left to
//                   do, the gate's own words for why, and the one button that
//                   does it. The whole order is the START page's job. See
//                   DiversityNextStrip.h.
//
// Layout of the SLICE page under those, top to bottom:
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

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

// A value member, not a pointer: the session model is a pure derivation with
// no QObject in it, and the window owns exactly one.
#include "gui/DiversitySessionModel.h"

class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QHideEvent;
class QShowEvent;
class QJsonArray;
class QJsonValue;
class QLabel;
class QLineEdit;
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
class DiversityNextStrip;
class DiversityFinderPanel;
class DiversityNoiseProfilePanel;
class DiversityMapStrip;
class DiversityScope;
class DiversitySessionPage;
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

    // The SITE page's second route: one /diversity/compass answer, fetched on
    // the same tick as the beacons. Only its "noise" object is read here --
    // which way the noise the gate has profiled is arriving from, when the
    // compass has enough beacons to have fitted anything at all.
    void applyCompass(const QJsonObject& compass);

    // One /diversity/dig answer -- the gate's timed "dig this out" run, which
    // moves the chain a knob at a time and keeps whatever helped. Read by the
    // FLOW strip's sixth step and by nothing else; fed whenever one arrives,
    // because a run goes on whatever page the operator walks to.
    void applyDig(const QJsonObject& dig);

    // One /filter answer, or the identical status object a /filter/set or
    // /filter/notch write replies with, or {"error": "..."} when the gate
    // refused a value. Nothing in this window DRAWS a filter any more -- the
    // CHAIN window does -- but the START page's STATION step is about whose
    // filter is in force, so the answer is still fed here whenever one
    // arrives.
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
    // True while the SITE page's BEACON CHECK is out (or just home): the
    // /diversity/beacons poll must run then whatever page is showing, or the
    // check's report would be written from stale results.
    bool beaconPollWanted() const;

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

    // Mirrors AetherGateApplet's own presence flag: false clears every
    // readout and greys the status strip, but leaves the window open -- the
    // operator opened it, and a dropped poll is not a reason to take it away.
    void setPresent(bool present);

public slots:
    // Which noise-finding kinds the operator has dismissed on the SITE page.
    // A slot rather than a plain setter so whoever owns that control can
    // connect straight to it; nothing in this window connects to it itself.
    void setDismissedNoiseKinds(const QSet<QString>& kinds);

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
    // The pair row's OPEN CHAIN button. Left unconnected here on purpose:
    // AetherGateApplet.cpp is what wires this to
    // AetherGateApplet::toggleChainWindow, the same slot its own CHAIN button
    // already calls, so opening the chain from either place toggles the one
    // window rather than two.
    void requestOpenChain();
    // -> GET <path>?<query> for the SITE page: the gate's own route and query,
    // quoted back out of a noise-profile action or built from the station
    // locator field. Answered through applySiteReply().
    void requestSite(QString path, QUrlQuery query);
    // -> GET /diversity/dig?<query>: empty for the status read, seconds= to
    // start, cancel= to stop and put the chain back, verdict= to label the
    // run that just finished. Answered through applyDig().
    void requestDig(QUrlQuery query);
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
    // The two top rows and everything on the second one, defined in
    // DiversityWindowChain.cpp: the page tabs alone on row 1, the pair
    // controls under them on row 2, and the two buttons there that report
    // back rather than only writing.
    QWidget* buildTabRow();
    QWidget* buildChainRow();
    // A card's cure on the START page, or the NEXT strip's one button. Both
    // carry a StepId and the query is re-read from the model here -- see
    // DiversitySessionPage.cpp, where this and the three members below it are
    // defined.
    void     onSessionCure(int stepId);
    // The START page, and the one call that re-derives it and the footer from
    // the payloads this window has already polled.
    QWidget* buildStartPage();
    void     refreshSession();
    void     startRealign();
    void     startCapture();
    void     resetCapture();
    // The part of one /diversity status object that belongs to the pair row:
    // whether the realign this window asked for has finished, and the gate's
    // own capture state. The four alignment values are passed in rather than
    // re-read so applyDiversity() parses them exactly once.
    void     applyChainStatus(const QJsonObject& d, bool aligned, bool realigning,
                              bool haveLag, double lag);
    // The footer strip's two layers -- see DiversityWindowChain.cpp.
    void     setStatusStripBase(const QString& text, bool live);
    void     setStatusStripTransient(const QString& text, int ms);
    // The SLICE/BAND page switch, appended to the tab row's own layout, and
    // the BAND page itself. Both defined in DiversityWindowBand.cpp.
    void     buildPageSwitch(QWidget* row);
    QWidget* buildBandPage();
    // The SITE page and the two members that feed it, defined in
    // DiversityWindowSite.cpp.
    QWidget* buildSitePage();
    // The per-bin weights control, built into the SLICE page's ANTENNAS panel
    // and defined beside the rest of the subband story rather than beside the
    // panel it is added to.
    QWidget* buildSubbandRow(QWidget* parent);
    // The two keys of the /diversity status object the SITE page reads
    // (noise_profile) and the SLICE page's checkbox reflects (subband). One
    // call from applyDiversity(); nothing else in this window touches either.
    void     applySite(const QJsonObject& d);
    void     clearSiteReadouts();
    // The SITE page's two station notes, both defined beside buildSitePage():
    // the free-text ANTENNA line that rides on the site log, and the one line
    // that says which way the noise is coming from.
    QWidget* buildAntennaRow(QWidget* parent);
    // The read-back half of the ANTENNA field: /diversity's "sitelog" object,
    // whose "antenna" key is the note the gate is holding. Never writes.
    void     applyAntennaNote(const QJsonValue& sitelog);
    // Starts, restarts or stops the /diversity/dig status poll. 1 s while a
    // run is going or a verdict is still owed, 10 s otherwise, and nothing at
    // all while the window is hidden or the gate is not answering -- the same
    // "a page nobody is looking at costs no requests" rule the other routes
    // follow, applied to a strip that is on every page. Defined in
    // DiversityWindowFilter.cpp beside the rest of the dig seam.
    void     updateDigPoll();
    // The dig's control at the end of the NEXT row: an empty page while
    // nothing is out, the STOP a running dig wears, and the verdict row a
    // finished run asks for. Built and switched here rather than inside
    // DiversityNextStrip because all of them WRITE, and that strip owns no
    // transport. All three defined in DiversityWindowFilter.cpp.
    QWidget* buildDigControls();
    // The 1/3/5 MIN buttons on their own, for the START page's OFFERS row.
    // Split out of buildDigControls() when the durations moved off the footer:
    // they are an offer, and the footer is for the step you are on.
    QWidget* buildDigDurations();
    void     updateDigControls();
    // Points the tab row's one HELP button at the topic of the page now
    // showing. DiversityHelp::button() hard-codes its topic in the click
    // lambda, so this disconnects and re-connects rather than setting a
    // property.
    void     retargetPageHelp(int page);
    // Index into m_pages: 0 START, 1 SLICE, 2 BAND, 3 SITE -- the same order
    // as DiversitySessionModel::Page, so a step's own page number is this
    // argument without a translation table. Anything outside that range,
    // including the 4 a station that last used the retired FILTER tab still
    // has in AppSettings, lands on START rather than on a blank stack.
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
    // The EVENTS panel's one-line alignment readout: aligned/lag/peak, and
    // (when the gate says align_held) the gate's own held-lock note in place
    // of "realigning…". Every fixed string this line can show lives here,
    // beside the widget buildEventsPanel() builds -- applyDiversity() only
    // hands in the fields it already parsed off the same payload.
    void applyAlign(const QJsonObject& d, bool aligned, bool realigning, bool haveLag, double lag,
                    bool havePeak, double peak);
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
    QToolButton*    m_pageStartButton{nullptr};
    QToolButton*    m_pageSliceButton{nullptr};
    QToolButton*    m_pageBandButton{nullptr};
    QToolButton*    m_pageSiteButton{nullptr};
    // The door to the gate's CHAIN window, at the right-hand end of the tab
    // row rather than on a page: it used to sit at the top of the FILTER tab,
    // one page switch away from wherever the operator actually was. The pair
    // row above would have been the other candidate and has no room -- see
    // buildTabRow() in DiversityWindowChain.cpp for the measurement.
    QPushButton*    m_openChainButton{nullptr};
    // One HELP button at the right-hand end of the tab row, retargeted to
    // whichever page is showing -- four buttons would have been four more lit
    // boxes on the row this window keeps deliberately quiet.
    QPushButton*    m_pageHelpButton{nullptr};
    // The remembered page is restored once, on the first show -- see
    // showEvent() in DiversityWindowBand.cpp.
    bool            m_pageRestored{false};
    DiversitySpatialWaterfall* m_waterfall{nullptr};
    DiversityFinderPanel*      m_finder{nullptr};

    // --- site ------------------------------------------------------------
    DiversityNoiseProfilePanel* m_noiseProfile{nullptr};
    // The operator's own note about what is on the end of the coax -- which
    // loops, and where their control boxes are set. The gate stores it on the
    // site log and gives it back on /diversity, so a beacon sweep read a week
    // later can be read against the switch positions it was taken with.
    QLineEdit*                  m_antennaEdit{nullptr};
    // The note as the gate last reported it (or as it was last sent). Both
    // ends of the hold rule read it: a check-back that matches leaves the
    // field alone, and a second editingFinished carrying the same words does
    // not write the same note twice.
    QString                     m_antennaSent;
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
    // What the MAIN panadapter is drawing, given pan= and whether the two
    // tuners are aligned yet -- display only, fed in applyChainStatus().
    QLabel*       m_spectrumLine{nullptr};
    QString       m_compareResumeMode;
    bool          m_compareDown{false};
    // REALIGN's answer. m_lastLagSamples is the lag from the poll before the
    // request went out, which is the only thing "(was +4032)" can honestly be
    // compared against.
    QTimer* m_realignTimeout{nullptr};
    QTimer* m_resultTimer{nullptr};
    bool    m_realignPending{false};
    bool    m_realignHaveLagBefore{false};
    double  m_realignLagBefore{0.0};
    bool    m_haveLastLag{false};
    double  m_lastLagSamples{0.0};
    // CAPTURE's countdown. m_captureBusy holds from the click until the answer
    // has finished being shown, and is what stops a poll putting "CAPTURE"
    // back over "SAVED".
    QTimer* m_captureCountdown{nullptr};
    bool    m_captureBusy{false};
    int     m_captureRemaining{0};

    // --- session ----------------------------------------------------------
    // The START page and the footer are two views of this one model, refreshed
    // together by refreshSession() on every poll that touches one of the four
    // payloads it reads. The payloads are kept because the model caches
    // nothing between calls: it recomputes every step from scratch, which is
    // what stops a card and the sentence on it disagreeing.
    DiversitySessionModel m_session;
    DiversitySessionPage* m_startPage{nullptr};
    DiversityNextStrip*   m_nextStrip{nullptr};
    QJsonObject m_lastDiversity;
    QJsonObject m_lastFilter;
    QJsonObject m_lastDig;
    QJsonObject m_lastBeacons;
    // The frequency the receiver is on, as last told to setActiveSliceHz().
    // The BAND step is about the amateur band this is in; 0 means nobody has
    // said yet.
    double m_tunedHz{0.0};
    // The dig status poll. The window owns the cadence rather than the poller
    // because it is the thing that knows whether a run is still going; see
    // updateDigPoll().
    QTimer* m_digTimer{nullptr};
    // Set once a /diversity/dig answer has actually landed. Until then the
    // poll runs at the fast cadence whatever the state is, so a window that
    // has just been opened learns whether the gate can dig at all within a
    // second rather than within ten -- and, because it is a TIMER rather than
    // an immediate request, showing the window still costs no gate traffic.
    bool    m_digPrimed{false};
    // Three pages: the durations on offer, the STOP a running dig wears, and
    // the verdict row. A QStackedWidget rather than show/hide so the FLOW row
    // is the same width and height in all three -- it is the last row above
    // the status strip and nothing down there may move when a payload lands.
    QStackedWidget* m_digStack{nullptr};

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
    // What the strip says when nothing is covering it, and the timer that
    // uncovers it.
    QTimer* m_statusTransient{nullptr};
    QString m_statusBase;
    bool    m_statusBaseLive{false};

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
