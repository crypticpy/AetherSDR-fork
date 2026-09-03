#pragma once

// CHAIN -- the receiver's filter chain as a block diagram you can switch.
//
// The operator's request, verbatim: "I'd like to see which roofing filters
// we're adding, or turn all the different filters on and off, simulating all
// the filters in a really high-end radio." The Diversity window's FILTER page
// answered the first half of that in one line of text ("Roof 200 kHz RF · 25
// kHz digital") and docs/DIVERSITY.md said outright that nothing on the page
// could move them.
//
// This is its own window rather than a fifth Diversity page because the chain
// is a RECEIVER feature: it needs IQ, not two tuners. It works on an RSPdx, a
// single-tuner RSPduo, any device the gate fronts. Two-loop rows appear only
// when the gate sends them, and the window shows whatever /filter says
// regardless of device.
//
// WHAT IS ON SCREEN, top to bottom
//
//   * TWO TABS -- CHAIN and VISUAL. The operator's words after the first
//     build: "we still also need to have the visualizations and stuff like on
//     a separate tab from the filter chain". CHAIN is the block diagram below;
//     VISUAL is the passband drawn as a curve at the full width of the window,
//     where every edge, every notch and the band under them are worked on by
//     pointing at them. See AetherGateChainVisual.h. One /filter poll feeds
//     both, and the tab that is not in front is not fed at all.
//   * MODE -- a segmented PHONE / CW / DATA, and ONE button beside it that
//     reads "SET UP FOR PHONE" or "SET UP FOR CW". The mode decides which
//     stages are drawn and which fold into "stages this mode does not use";
//     the button applies that mode's ordered list of writes. Under the row,
//     one plain line about what the set does to the SOUND -- not to the
//     control port. See AetherGateChainModes.h.
//   * PRESETS -- on the VISUAL tab, over the picture: the whole chain as the
//     operator left it, saved under a name and applied by the same
//     one-write-at-a-time machinery the mode sets use, with a line that says
//     which one is in force and whether the receiver has drifted from it.
//     See AetherGateChainPresets.h.
//   * the DIAGRAM -- four labelled groups read left to right with an arrow
//     between them: FRONT END (one summary card, because none of it is
//     switched from here), PAIR, PASSBAND, OUT. Every live stage is a card
//     with its NAME big and bright, ONE measured line that always fits, and
//     one control. See AetherGateChainStrip.h.
//   * the INSPECTOR -- the selected stage's name, one sentence of what it
//     does to the sound, what it is doing now spelled out in full, its
//     control at full size, one line of what you would hear with it off, and
//     the levels the gate measured. It never repeats the card verbatim, and
//     when the receiver refuses a write it is where the receiver's own words
//     are printed.
//
// IT OWNS NO TRANSPORT. AetherGateApplet is still the one place a gate request
// is built: /filter arrives here through the applet's DiversityBandPoller (the
// same 2 Hz poll the FILTER page uses, on the applet's own manager, with the
// same transfer timeout), and every write leaves as requestWrite(), which the
// applet turns into one GET whose reply IS the next status.
//
// WHICH IS WHY THIS WINDOW TRACKS WRITES ITSELF. The applet answers a poll and
// a write through the SAME signal, and the gate is a ThreadingHTTPServer: a
// poll issued just before a write can read the status BEFORE the write applies
// and still answer AFTER it, so applying every body as it arrives makes a
// freshly-switched stage flick back to where it was. That is the "contour
// needs several clicks" the operator reported (design §0.3 item 5): the tile
// reverts, and the next click undoes the write that did land. So a write opens
// a settling window on ITS stage. While that window is open, a body that
// reports the pre-write setting cannot move that row -- it was measured before
// the write and cannot be news -- and the control is greyed until a body
// reports something else. Nothing here is optimistic: the row still shows only
// what a gate answer said, never what was asked for.
//
// Real widgets, not a painted diagram. The automation bridge (dumpTree) and a
// screen reader both address this window by objectName; a custom-painted strip
// would be invisible to both. Nothing scrolls at the initial size, no label
// wraps, every readout reserves the width of its own worst case, and every
// colour comes from a ThemeManager token.

#include "gui/AetherGateChainAuto.h"
#include "gui/AetherGateChainModes.h"
#include "gui/AetherGateChainRows.h"
#include "gui/AetherGateChainStage.h"
#include "gui/PersistentDialog.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrlQuery>

class QFrame;
class QHideEvent;
class QJsonObject;
class QLabel;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QTabWidget;
class QVBoxLayout;

namespace AetherSDR {

class AetherGateChainStrip;
class AetherGateChainControl;
class AetherGateChainPresetBar;
class AetherGateChainVisual;

class AetherGateChainWindow : public PersistentDialog {
    Q_OBJECT
public:
    explicit AetherGateChainWindow(QWidget* parent = nullptr);

    // One /filter answer -- a poll's, or the reply to a write, which is the
    // same object. An empty object, a body that is not a filter status, or an
    // {"error": ...} refusal all leave the strip exactly where it is; the
    // refusal is quoted ON THE TILE that asked for it, where the operator is
    // looking, as well as on the status line.
    void applyFilter(const QJsonObject& filter);

    // GET /device's own "frontend" key -- the B23 linearity guard, which is
    // not part of /filter's chain[] at all. Feeds the same two synthetic
    // FRONT END rows (HEADROOM, GUARD) that applyFilter()'s merge inserts;
    // whichever of the two answers arrives last is the one on screen; a poll
    // is not required to bring both every time.
    // Fed by AetherGateApplet::applyDeviceControls() on every /device
    // read (the periodic poll and each control's read-back).
    void applyDevice(const QJsonObject& device);

    // Gate presence, mirrored from the applet. Losing the gate empties the
    // strip: last minute's numbers must never sit there looking live.
    void setPresent(bool present);

    // True when the rows on screen came from the gate's own chain[] rather
    // than from the app's built-in fallback.
    bool chainFromGate() const { return m_fromGate; }

    ChainMode mode() const { return m_mode; }
    void setMode(ChainMode mode);

    // Which tab is in front: 0 CHAIN, 1 VISUAL. Read back by the tests, and by
    // the window itself when it works out whether the picture is worth feeding.
    int currentTab() const;
    void setCurrentTab(int index);

protected:
    // The picture is fed only while this window is on screen AND VISUAL is the
    // tab in front, so a chain window left open on CHAIN behind the main
    // window costs nothing at all.
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

signals:
    // One write, exactly as the gate authored it: its own route and its own
    // query. The mode sets are the one exception, and even they send the same
    // /filter/set the tiles do, one line at a time.
    void requestWrite(QString route, QUrlQuery query);

private:
    // AetherGateChainWindowTabs.cpp: the two tabs, the PRESETS row, and the
    // one place a gesture on the picture or a preset becomes writes.
    void buildTabs(QVBoxLayout* root);
    void refreshVisualActive();
    void runSequence(const QList<ChainPresetWrite>& writes, const QString& name,
                     const QStringList& missing, bool isPreset);
    // A mark on the picture was clicked: the CHAIN tab, turned to that stage's
    // card, scrolled into view and selected.
    void jumpToStage(const QString& id);

    void buildModeRow(QVBoxLayout* root);
    void buildInspector(QVBoxLayout* hostBox, QWidget* host);
    void showStage(const QString& id);
    // The status line says one of exactly three things -- live, applying,
    // no connection -- because a status line that also carried refusals and
    // set progress was a line nobody read.
    enum class ChainLink { Live, Applying, Gone };
    void setLink(ChainLink link);
    // What the receiver said when it refused. Printed in the inspector, where
    // the operator is already looking, and on the tile that asked.
    void setNote(const QString& text);
    void setSetProgress(const QString& text);
    void onWriteRequested(const QString& route, const QUrlQuery& query);
    // The rows one body describes, with any stage still inside its settling
    // window held at the setting the strip is already showing.
    QList<ChainStage> holdPendingStages(const QList<ChainStage>& fresh);
    void applyBusyToTiles();
    // The merge applyFilter() and applyDevice() share: m_filterStages plus
    // the frontend guard's two synthetic rows, inserted right after the
    // last stage the FRONT END group already owns, held through
    // holdPendingStages() and handed to the strip -- one merge, whichever
    // of the two answers is the one that just changed. Returns what it gave
    // the strip, so a caller that also has to tell the presets bar what
    // changed (applyFilter() does; applyDevice() does not) is not left
    // recomputing the same list.
    QList<ChainStage> refreshStrip();

    // One write on the wire, per stage.
    struct PendingWrite {
        QString       before;   // ChainStage::settingKey() at the moment of the write
        QElapsedTimer age;
        bool          confirmed{false};
    };

    AetherGateChainStrip*     m_strip{nullptr};
    QScrollArea*              m_scroll{nullptr};
    QTabWidget*               m_tabs{nullptr};
    AetherGateChainVisual*    m_visual{nullptr};
    AetherGateChainPresetBar* m_presets{nullptr};
    QLabel*                 m_detailName{nullptr};
    QLabel*                 m_detailText{nullptr};   // what it is doing now
    QLabel*                 m_detailTip{nullptr};    // what it does to the sound
    QLabel*                 m_detailOff{nullptr};    // what you would hear without it
    QLabel*                 m_detailNote{nullptr};   // the receiver's refusal
    QLabel*                 m_detailLevels{nullptr};
    // AUTO CLEAN's own inspector -- state+why, then its event history --
    // shown only while the auto_clean card is selected. See
    // gui/AetherGateChainAuto.h.
    QLabel*                 m_detailAutoState{nullptr};
    QLabel*                 m_autoEvents{nullptr};
    QVBoxLayout*            m_detailControlBox{nullptr};
    AetherGateChainControl* m_detailControl{nullptr};
    QLabel*                 m_status{nullptr};
    QLabel*                 m_modeTip{nullptr};
    QLabel*                 m_setProgress{nullptr};
    // Indexed by int(ChainMode); one entry per mode, always three.
    QList<QPushButton*>     m_modeButtons;
    QList<QPushButton*>     m_setButtons;
    AetherGateChainPreset*  m_preset{nullptr};
    QHash<QString, PendingWrite> m_pending;
    QString                 m_lastWriteStage;
    // The two halves of the merge refreshStrip() draws: /filter's own rows,
    // and GET /device's "frontend" key parsed into ChainFrontendStatus. Kept
    // apart rather than pre-merged so either one can arrive alone -- a
    // /device poll must not blank the chain, and a /filter poll must not
    // blank the guard.
    QList<ChainStage>       m_filterStages;
    ChainFrontendStatus     m_frontend;
    // The governor block off this same /filter body -- see
    // gui/AetherGateChainAuto.h. Kept apart like m_frontend so a
    // /device-only refresh can reapply it to the strip without a fresh
    // /filter poll.
    ChainAutoGovernor       m_governor;
    ChainMode               m_mode{ChainMode::Phone};
    // True only while a PRESET is being applied. Its own writes must not mark
    // the preset "edited" -- a preset that declared itself edited by loading
    // would never once read as loaded.
    bool                    m_loadingPreset{false};
    ChainLink               m_link{ChainLink::Gone};
    bool                    m_fromGate{false};
    bool                    m_present{false};
};

} // namespace AetherSDR
