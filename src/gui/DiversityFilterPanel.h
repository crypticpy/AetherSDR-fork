#pragma once

// DiversityFilterPanel -- the gate's slice filter drawn as the thing it
// actually is: a response curve with the passband you asked for laid over the
// response you got.
//
// Every other view of a receiver filter in this application is a pair of
// numbers in two spin boxes. That is enough to SET a filter and useless for
// understanding one: it cannot show you that a 1023-tap sharp filter has a
// 49 Hz skirt while the soft one has 300, that the notch you placed is 40 Hz
// off the heterodyne, that the auto-width tracker has quietly narrowed you to
// 250 Hz above your own setting, or that the APF is sitting outside the
// passband doing nothing. All of those are one glance at a curve.
//
// So the curve is the primary control here, not an illustration of one. The
// two passband edges are draggable handles on it, a double-click drops a notch
// where you clicked, and the arrow keys move the focused edge for anybody who
// cannot or would rather not drag. The spin boxes in the WIDTH column beside
// it are still there for exact numbers -- this is the instrument, they are the
// keypad.
//
// It holds NO transport and NO authority: applyStatus() takes one /filter
// status object and the widget draws exactly what is in it. The gate is the
// source of truth, so a drag does not move the drawn passband permanently --
// it moves the handles, emits edgesDragged(), and the next status object is
// what the widget is actually showing a moment later. The one exception is the
// drag itself: while dragging(), the page does not feed it a status, because
// a 2 Hz poll landing mid-drag would snatch the handle back.
//
// Frequencies are AUDIO Hz throughout, always positive, exactly as the gate
// reports low_hz/high_hz. On LSB the spectrum is inverted at RF and the gate
// still reports 100..2900; drawing that mirrored would be a truer picture of
// the RF and a worse picture of what the operator is about to type into a
// spin box. The sideband is stated in the caption above the widget instead.

#include <QString>
#include <QVector>
#include <QWidget>

class QJsonObject;

namespace AetherSDR {

class DiversityFilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiversityFilterPanel(QWidget* parent = nullptr);

    // One /filter status object. Every field is independently optional: a
    // missing "response" leaves the curve empty rather than flat, a missing
    // "notches" draws none rather than clearing the ones the gate still has.
    // An "available": false object empties the widget -- there is no filter to
    // draw in FM, and a stale curve would be a claim about a filter that is
    // not running.
    void applyStatus(const QJsonObject& filter);

    // Gate gone. Same as an unavailable payload, said by the window instead of
    // by the gate.
    void clear();

    // True from the press on a handle to the release. The page checks it
    // before feeding a poll: see the header comment.
    bool dragging() const { return m_drag != Edge::None; }

    // What the widget currently draws for the two edges. The page reads these
    // back on a release to work out WHICH edge moved, so a drag of the low
    // handle writes low= alone rather than re-asserting a high= the auto-width
    // tracker may own.
    int lowHz() const { return m_lowHz; }
    int highHz() const { return m_highHz; }

signals:
    // A handle has been let go, and the two edges are now `lowHz`/`highHz`,
    // snapped to 10 Hz. Emitted on RELEASE, not per pixel: a set per mouse-move
    // would be dozens of gate writes for one adjustment.
    void edgesDragged(int lowHz, int highHz);

    // A double-click on the curve at `hz`. The page turns it into
    // /filter/notch?add=<hz>, which is the fastest way to kill a carrier you
    // can see: point at it, double-click.
    void notchRequested(double hz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void leaveEvent(QEvent* ev) override;

private:
    enum class Edge { None, Low, High };

    // Plot rectangle in widget coordinates -- the widget minus the dB gutter
    // on the left and the Hz axis along the bottom.
    QRectF plotRect() const;
    double xForHz(double hz) const;
    double hzForX(double x) const;
    double yForDb(double db) const;
    // Which handle is within grab distance of `x`, or None.
    Edge   edgeAt(double x) const;
    // Moves the focused/dragged edge to `hz`, snapped to 10 Hz and kept the
    // right side of its neighbour.
    void   moveEdge(Edge edge, double hz);

    QVector<double> m_hz;
    QVector<double> m_db;
    double m_minHz{0.0};
    double m_maxHz{0.0};

    bool m_available{false};
    int  m_lowHz{0};
    int  m_highHz{0};

    // hz / depth_db of every manual notch, and hz / depth_db of every tone the
    // automatic notcher has found. Drawn differently on purpose: one is a
    // filter the operator placed, the other is one that appeared.
    QVector<QPointF> m_notches;
    QVector<QPointF> m_anf;
    // Centre frequencies of the contour and audio-peaking filters, when they
    // are switched on. NaN when they are not -- an APF that is off has a
    // frequency and no business being drawn.
    double m_contourHz{0.0};
    double m_apfHz{0.0};

    Edge   m_drag{Edge::None};
    Edge   m_focusEdge{Edge::Low};
    // Hz under the pointer for the corner readout, or NaN when the pointer is
    // not over the widget.
    double m_cursorHz{0.0};
};

} // namespace AetherSDR
