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
// WHAT IS ON SCREEN
//
//   * the MODE row -- PHONE, CW, DATA/OTHER -- and the one set button that
//     mode offers. The mode decides which stages are on the strip and which
//     drop into the collapsed "not for this mode" group; the set button
//     applies that mode's ordered list of writes, one at a time, each waited
//     for. See AetherGateChainModes.h for both tables.
//   * the strip -- one tile per row of the gate's chain[], in the gate's own
//     order, which IS signal order. Name, the gate's headline value, an
//     optional in/out level from `measured`, and the control the gate
//     nominated: a switch on a toggle, a menu on a select, nothing at all on a
//     fixed row (whose `why` is printed on its face). See
//     AetherGateChainStrip.h for the array contract and the built-in fallback.
//   * the detail area -- "SELECTED: <NAME>" in the same accent as the selected
//     tile's frame, the stage's one-sentence description, its control again
//     larger, and the measured in/out.
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

#include "gui/AetherGateChainModes.h"
#include "gui/AetherGateChainStage.h"
#include "gui/PersistentDialog.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrlQuery>

class QFrame;
class QJsonObject;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace AetherSDR {

class AetherGateChainStrip;
class AetherGateChainControl;

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

    // Gate presence, mirrored from the applet. Losing the gate empties the
    // strip: last minute's numbers must never sit there looking live.
    void setPresent(bool present);

    // True when the rows on screen came from the gate's own chain[] rather
    // than from the app's built-in fallback.
    bool chainFromGate() const { return m_fromGate; }

    ChainMode mode() const { return m_mode; }
    void setMode(ChainMode mode);

signals:
    // One write, exactly as the gate authored it: its own route and its own
    // query. The mode sets are the one exception, and even they send the same
    // /filter/set the tiles do, one line at a time.
    void requestWrite(QString route, QUrlQuery query);

private:
    void buildModeRow(QVBoxLayout* root);
    void showStage(const QString& id);
    void setStatus(const QString& text, bool live);
    void onWriteRequested(const QString& route, const QUrlQuery& query);
    // The rows one body describes, with any stage still inside its settling
    // window held at the setting the strip is already showing.
    QList<ChainStage> holdPendingStages(const QList<ChainStage>& fresh);
    void applyBusyToTiles();

    // One write on the wire, per stage.
    struct PendingWrite {
        QString       before;   // ChainStage::settingKey() at the moment of the write
        QElapsedTimer age;
        bool          confirmed{false};
    };

    AetherGateChainStrip*   m_strip{nullptr};
    QLabel*                 m_source{nullptr};
    QLabel*                 m_detailName{nullptr};
    QLabel*                 m_detailText{nullptr};
    QLabel*                 m_detailTip{nullptr};
    QLabel*                 m_detailLevels{nullptr};
    QVBoxLayout*            m_detailControlBox{nullptr};
    AetherGateChainControl* m_detailControl{nullptr};
    QLabel*                 m_status{nullptr};
    QLabel*                 m_modeTip{nullptr};
    // Indexed by int(ChainMode); one entry per mode, always three.
    QList<QPushButton*>     m_modeButtons;
    QList<QPushButton*>     m_setButtons;
    AetherGateChainPreset*  m_preset{nullptr};
    QHash<QString, PendingWrite> m_pending;
    QString                 m_lastWriteStage;
    ChainMode               m_mode{ChainMode::Phone};
    bool                    m_fromGate{false};
    bool                    m_present{false};
};

} // namespace AetherSDR
