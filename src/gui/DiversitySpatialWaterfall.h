#pragma once

// DiversitySpatialWaterfall -- the gate's whole span as a scrolling picture of
// WHERE each signal is coming from, not just how strong it is.
//
// An ordinary waterfall answers "is there something on 14.152?". This one
// answers "is that the same thing as the noise on 14.148?" -- because the two
// loops give an inter-antenna phase per bin, and two signals arriving from
// different directions cannot share a phase. So:
//
//   * HUE        is the inter-loop phase, -180..180 degrees mapped once around
//                the hue circle. A station is a coloured streak; a second
//                station from another direction is a DIFFERENT colour.
//   * SATURATION is GATED on the PER-BIN coherence the gate sends: grey below
//                0.5, coming up to full colour by 0.9. Below the gate the
//                phase between the loops is not a direction at all, it is two
//                noise samples that lined up for one poll, and the grey is the
//                honest statement "there is no direction here". Saturation
//                used to be the raw coherence, which painted every noise bin a
//                third of the way to a confident hue and turned the whole span
//                into a pastel wash.
//   * VALUE      is the level, stretched between the 20th and 98th percentiles
//                of the SAME row and smoothstepped. Percentiles rather than
//                min/max because one strong carrier setting the top of the
//                scale squashes every other signal into the bottom of the
//                range; row-relative rather than absolute dBFS so the picture
//                does not black out when the operator changes gain.
//
// A local noise source therefore paints as one flat colour across every bin it
// touches -- which is exactly the thing that is invisible on a normal
// panadapter and obvious here. DiversitySpatialLegend, drawn under it, is what
// makes any of that readable by someone who has not read this comment.
//
// It holds no timer and no transport: DiversityWindow feeds it one
// setSpatial() per /diversity/spatial poll and it appends one row. History is
// a fixed kHistoryRows-row QImage that is scrolled down by one row per append,
// so the buffer is bounded by rows rather than by how long the window has been
// open. Alongside the pixels it keeps the three NUMBERS behind every pixel in
// a ring of the same depth, because a readout that quotes the newest row while
// the pointer is over a row from a minute ago is a picture that lies about
// what it is showing.
//
// Clicking a column emits tuneRequested() with that column's centre frequency;
// moving the pointer over it shows a one-line readout of the four numbers
// behind THAT pixel and draws a crosshair on it. Both are the point of the
// widget: a colour you cannot tune to, or read a number off, is a decoration.
//
// setSpatial() is not on check_a11y.py's watched-setter list (setLevel/setDbm/
// setFrequency/update*), so the custom paintEvent is exempt from its
// QAccessibleInterface-companion check -- the same exemption DiversityMapStrip
// takes, and for the same reason: the FINDER table beside it exposes the
// candidates this picture is a glance-view of as real text rows.

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

class QEvent;
class QJsonObject;
class QMouseEvent;
class QPaintEvent;

namespace AetherSDR {

class DiversitySpatialWaterfall : public QWidget {
    Q_OBJECT
public:
    explicit DiversitySpatialWaterfall(QWidget* parent = nullptr);

    // One /diversity/spatial answer. {"available": false}, an {"error": ...}
    // body, a route an older gate never had, and a malformed payload all mean
    // the same thing: no NEW row. Whatever history is already there keeps
    // scrolling off the bottom rather than being thrown away, and the
    // "waiting for the gate" caption comes back once there is nothing left.
    void setSpatial(const QJsonObject& spatial);

    // Empties the history -- gate gone, or diversity no longer available. A
    // dead gate's last minute of colour must not keep sitting there looking
    // live.
    void clear();

    // Test/introspection accessors: this is a raw QPainter paint with no
    // child widgets, so there is otherwise no way to observe what it holds.
    bool   available() const { return m_available; }
    int    rowCount() const { return m_rows; }
    int    points() const { return m_points; }
    double startHz() const { return m_startHz; }
    double stepHz() const { return m_stepHz; }
    bool   hasPassband() const { return m_havePassband; }
    double passbandLoHz() const { return m_passbandLoHz; }
    double passbandHiHz() const { return m_passbandHiHz; }

    // The colour the newest row painted at `column`, or an invalid QColor when
    // there is no row (or the column is out of range).
    QColor newestColour(int column) const;

    // Centre frequency of `column`, or 0 when the span is unknown.
    double columnHz(int column) const;

    // The one-line readout for a widget position: the frequency of the column
    // under it and the phase, coherence and level of the ROW under it -- not
    // of the newest row. Empty when that pixel is not part of the picture.
    QString readoutAt(int x, int y) const;

    // Column and row the pointer is on, or -1. The crosshair is drawn from
    // these, and they are the only way a test can see that a move landed.
    int hoverColumn() const { return m_hoverColumn; }
    int hoverRow() const { return m_hoverRow; }

signals:
    // The operator clicked a column. Carries the column's CENTRE frequency in
    // Hz -- DiversityWindow turns it into a slice tune.
    void tuneRequested(double hz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    // Column under a widget x, or -1 when there is no span to map it onto.
    int columnAt(int x) const;
    // Row under a widget y, 0 being the newest, or -1 when that y is below the
    // rows actually lived through.
    int rowAt(int y) const;
    // Height of the scrolling picture, i.e. everything above the axis row.
    int waterfallHeight() const;
    // Height the rows actually occupy inside it -- the picture grows down from
    // the top as history accumulates rather than stretching to fill.
    int drawnHeight() const;
    // (Re)allocates the history image and the number rings for `points`
    // columns, discarding what is there -- the span changed, so the old rows
    // describe different bins.
    void resetHistory(int points);
    // One number out of a history ring, NaN when it is not there.
    float sampleAt(const QVector<float>& ring, int row, int column) const;

    QImage m_history;
    int    m_points{0};
    int    m_rows{0};
    bool   m_available{false};
    double m_startHz{0.0};
    double m_stepHz{0.0};
    bool   m_havePassband{false};
    double m_passbandLoHz{0.0};
    double m_passbandHiHz{0.0};

    // The numbers behind the pixels, kept so the readout can QUOTE the
    // measurement rather than reverse-engineer it from a colour -- and kept
    // per row, so it quotes the row under the pointer. Three kHistoryRows *
    // m_points rings written newest-first at m_head, which is a rotate of one
    // index rather than a memmove of a megabyte per poll.
    QVector<float> m_histPhaseDeg;
    QVector<float> m_histCoherence;
    QVector<float> m_histLevelDb;
    int            m_head{0};

    int m_hoverColumn{-1};
    int m_hoverRow{-1};
};

} // namespace AetherSDR
