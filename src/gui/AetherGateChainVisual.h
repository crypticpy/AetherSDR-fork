#pragma once

// VISUAL -- the filter as a picture, on its own tab.
//
// The operator's words (B21): "I think we still also need to have the
// visualizations and stuff like on a separate tab from the filter chain", and
// "direct manipulation". The CHAIN tab is a block diagram of twenty-five
// stages, each with one control; this tab is ONE stage -- the passband -- at
// the full width of the window, and everything you can do to it you do by
// pointing at it.
//
//   drag an edge          the passband edge that moved, and only that one
//   double-click          a notch at the frequency under the pointer
//   drag a notch mark     that notch moved to where you let it go
//   right-click a notch   that notch taken away
//   click any mark        the CHAIN tab, turned to that mark's stage
//   left / right arrows   the edge nearest the pointer, 10 Hz (Shift: 100)
//
// IT ADDS NO POLL. The CHAIN window already receives /filter twice a second
// through the applet's DiversityBandPoller, and the same object is handed to
// the picture -- applyFilter() takes exactly what applyFilter() on the window
// takes. A second poller for the same route would double the load on a gate
// that is also running a receiver.
//
// IT OWNS NO TRANSPORT EITHER. Every gesture leaves as requestWrite() or, when
// it is genuinely two writes, requestSequence() -- and the window turns both
// into the same one-at-a-time, wait-for-the-answer path the tiles use, with the
// same settling window. The gate has add= and clear= on /filter/notch and no
// move, so MOVING a notch is a clear followed by an add, in that order, through
// the sequencer. Doing it as two fire-and-forget GETs into a threaded server
// would let the add land first and the clear then delete it.
//
// A HIDDEN TAB COSTS NOTHING. "visualizer more reactive and performant it's a
// little laggy" was the other half of the request. DiversityFilterPanel's own
// three rules answer most of it (see its header); this tab answers the rest by
// simply not feeding the picture at all while the window is closed or the CHAIN
// tab is in front. The last body is kept, so switching to VISUAL shows the
// current filter immediately rather than an empty plot until the next poll.

#include "gui/AetherGateChainModes.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QLabel;
class QPushButton;
class QResizeEvent;

namespace AetherSDR {

class DiversityFilterPanel;

class AetherGateChainVisual : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateChainVisual(QWidget* parent = nullptr);

    // One /filter answer -- the same object the window applies to the diagram.
    // Remembered whether or not the tab is in front; drawn only when it is.
    void applyFilter(const QJsonObject& filter);

    // The window is visible AND this tab is the one on top.
    void setActive(bool active);
    bool active() const { return m_active; }

    // Gate gone. The picture empties: last minute's curve must not sit there
    // looking live.
    void clear();

    // True while a handle or a notch mark is under the pointer. The window
    // checks it before feeding a poll.
    bool dragging() const;

    DiversityFilterPanel* panel() const { return m_panel; }

signals:
    void requestWrite(QString route, QUrlQuery query);
    // Two or more writes that must go in order, each waited for. `why` on each
    // line is what the window shows while the sequence runs.
    void requestSequence(QList<ChainPresetWrite> writes, QString name);
    // A click on a mark: the chain stage id it belongs to. The window turns to
    // the CHAIN tab and selects that stage's card.
    void stageRequested(QString stageId);

protected:
    // The status line under the caption is elided by hand rather than left to
    // wrap: this tab, unlike CHAIN, sits directly in a QTabWidget page with no
    // QScrollArea to absorb an overlong `why` sentence, so a resize is the one
    // moment the elision needs to be redone.
    void resizeEvent(QResizeEvent* ev) override;

private:
    void onEdgesDragged(int lowHz, int highHz);
    void refreshReadout();
    // SQUEEZE (B24): off/armed/held, in the gate's own words. See
    // AetherGateChainVisual.cpp for the exact text each state uses.
    void refreshSqueezeLine();
    QString squeezeLineText() const;

    DiversityFilterPanel* m_panel{nullptr};
    QLabel*               m_readout{nullptr};
    QLabel*               m_cursor{nullptr};
    QPushButton*          m_squeezeComb{nullptr};
    QPushButton*          m_squeezeRelease{nullptr};
    QLabel*               m_squeezeLine{nullptr};
    QJsonObject           m_last;
    // The edges the GATE last reported, which is what a drag is compared
    // against to work out which one the operator moved. Not the panel's own
    // lowHz()/highHz(): those have already moved by the time the drag ends.
    int  m_gateLowHz{0};
    int  m_gateHighHz{0};
    bool m_active{false};
};

} // namespace AetherSDR
