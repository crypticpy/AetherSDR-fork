#pragma once

// HEAR RAW -- a momentary button, in the CHAIN window's header, that puts the
// slice filter chain out of circuit for exactly as long as it is held, so the
// operator can A/B how much the chain is doing. It is this window's own
// version of the Diversity window's press-and-hold "Hear A only"
// (DiversityWindowChain.cpp's m_compareButton): press sends one write,
// release sends the opposite, and every trigger that could otherwise strand
// the receiver bypassed -- the window hiding, closing on Escape (which hides
// it the same way), or losing focus to another application while the mouse
// is still physically down -- releases it too. The window drives the hide
// and focus-loss cases itself, from its own hideEvent()/changeEvent(); a
// bare mouse release drives the rest.
//
// It rides the gate's own /filter `bypass` boolean, which the SLICE FILTER
// row on the diagram already toggles (AetherGateChainRows.cpp's
// chainFromFilter(), fed by the gate's chainstatus.py _slice_rows()):
// bypass=on takes the WHOLE slice filter chain out of circuit while the
// automatics keep measuring, bypass=off puts it back. This button sends
// exactly that pair of writes and nothing else -- it owns no transport of
// its own, the same rule the rest of the window follows (see
// AetherGateChainWindow.h's own header comment): every write leaves through
// requestWrite(), which the window's onWriteRequested() turns into the one
// door out, the same door AetherGateChainStage.cpp's
// AetherGateChainControl::requestWrite and AetherGateChainVisual's own
// requestWrite already use.
//
// Split into its own file because AetherGateChainWindow.cpp was already at
// AGENTS.md's 800-line file budget before this button existed.

#include <QPushButton>
#include <QUrlQuery>

class QJsonObject;

namespace AetherSDR {

class AetherGateChainHearRawButton : public QPushButton {
    Q_OBJECT
public:
    explicit AetherGateChainHearRawButton(QWidget* parent = nullptr);

    // One /filter body, exactly as AetherGateChainWindow::applyFilter()
    // receives it. Whether THIS body carries a "bypass" key at all is what
    // the button's own visibility rides on -- a gate that has never once
    // mentioned bypass (an older gate, shipped before this feature) never
    // shows the button, and nothing here is optimistic about it: a body
    // that stops carrying the key hides the button on the very next poll,
    // not after some grace period.
    void applyFilter(const QJsonObject& filter);

    // Gate presence, mirrored from the window's own setPresent(). Losing the
    // gate releases a held press -- there is nobody left to answer
    // bypass=off, so the operator must not be left believing it is still on
    // its way -- and puts the button back to its start-of-day state.
    void setPresent(bool present);

    // Called by the window when it is about to disappear from under a held
    // press -- hidden, or the top-level window has just been deactivated --
    // so the receiver can never be left bypassed because the operator
    // looked away. Idempotent: safe to call whether or not a press is
    // actually in progress.
    void releaseIfHeld();

signals:
    // The one door out, identical in shape to every other control this
    // window owns: the window's own onWriteRequested() is the only thing
    // this ever connects to (never a new QNetworkAccessManager, never a
    // second door).
    void requestWrite(QString route, QUrlQuery query);

private:
    void onPressed();
    void onReleased();
    void sendBypass(bool on);
    // What the face says, and whether it is even on screen -- the single
    // place all four states (hidden, held, gate-bypassed, ordinary) are
    // decided, so no caller has to reconstruct that logic itself.
    void refreshFace();

    bool m_present{false};
    bool m_gateHasBypass{false};
    bool m_gateBypassed{false};
    bool m_held{false};
};

} // namespace AetherSDR
