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
// It has two pages, switched by the SLICE/BAND buttons at the left of the
// chain row. SLICE is the window described above -- everything about the
// frequency you are tuned to. BAND is about the SPAN: the spatial waterfall
// and the conversation FINDER, both click-to-tune, built on the gate's
// /diversity/spatial and /diversity/finder routes and polled only while that
// page is on screen (see DiversityWindowBand.cpp).
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
class DiversityFinderPanel;
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

    // True while the window is showing BAND rather than SLICE.
    // AetherGateDiversityPanel::wantsBandPoll() combines it with the window's
    // own visibility, and AetherGateApplet polls /diversity/spatial and
    // /diversity/finder only when both hold.
    bool bandPageVisible() const;

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
    // The BAND page came on screen, or went off it (a page switch or the
    // window closing). Carries whether the two BAND routes should be polled.
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
    void     showPage(bool band);
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
    DiversitySpatialWaterfall* m_waterfall{nullptr};
    DiversityFinderPanel*      m_finder{nullptr};

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
