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
//   * the strip -- one tile per row of the gate's chain[], in the gate's own
//     order, which IS signal order. Name, the gate's headline detail, an
//     optional in/out level from `measured`, and the control the gate
//     nominated: a latch on a toggle, a menu on a select, nothing at all on a
//     fixed row (its `why` is on the hover instead). See
//     AetherGateChainStrip.h for the array contract and the built-in fallback.
//   * the detail area -- the selected stage's detail, the sentence explaining
//     it, and the same control again, larger. A click anywhere on a tile
//     selects it.
//
// IT OWNS NO TRANSPORT. AetherGateApplet is still the one place a gate request
// is built: /filter arrives here through the applet's DiversityBandPoller (the
// same 2 Hz poll the FILTER page uses, on the applet's own manager, with the
// same transfer timeout), and every write leaves as requestWrite(), which the
// applet turns into one GET whose reply IS the next status. That is why
// nothing in this window is optimistic: a latch moves when the gate's answer
// says the stage moved, and not one instant earlier.
//
// Real widgets, not a painted diagram. The automation bridge (dumpTree) and a
// screen reader both address this window by objectName; a custom-painted strip
// would be invisible to both. Nothing scrolls at the initial size, no label
// wraps, every readout reserves the width of its own worst case, and every
// colour comes from a ThemeManager token.

#include "gui/PersistentDialog.h"

#include <QString>
#include <QUrlQuery>

class QFrame;
class QJsonObject;
class QLabel;
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
    // refusal is quoted on the status line so a rejected write is visible
    // rather than silent.
    void applyFilter(const QJsonObject& filter);

    // Gate presence, mirrored from the applet. Losing the gate empties the
    // strip: last minute's numbers must never sit there looking live.
    void setPresent(bool present);

    // True when the rows on screen came from the gate's own chain[] rather
    // than from the app's built-in fallback.
    bool chainFromGate() const { return m_fromGate; }

signals:
    // One write, exactly as the gate authored it: its own route and its own
    // query. Nothing here composes a set of its own.
    void requestWrite(QString route, QUrlQuery query);

private:
    void showStage(const QString& id);
    void setStatus(const QString& text, bool live);

    AetherGateChainStrip*   m_strip{nullptr};
    QLabel*                 m_source{nullptr};
    QLabel*                 m_detailName{nullptr};
    QLabel*                 m_detailText{nullptr};
    QLabel*                 m_detailTip{nullptr};
    QLabel*                 m_detailLevels{nullptr};
    QVBoxLayout*            m_detailControlBox{nullptr};
    AetherGateChainControl* m_detailControl{nullptr};
    QLabel*                 m_status{nullptr};
    bool                    m_fromGate{false};
    bool                    m_present{false};
};

} // namespace AetherSDR
