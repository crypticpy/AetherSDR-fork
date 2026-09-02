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
//   * SATURATION is the coherence, 0..1. Sky noise is incoherent between the
//                loops, so it desaturates to grey: the grey is the honest
//                statement "there is no direction here", not a colour we
//                picked for background.
//   * VALUE      is the level, over a fixed kLevelWindowDb window below the
//                brightest bin of the SAME row. A row-relative window, not an
//                absolute dBFS one, so the picture does not black out when the
//                operator changes gain.
//
// A local noise source therefore paints as one flat colour across every bin it
// touches -- which is exactly the thing that is invisible on a normal
// panadapter and obvious here.
//
// It holds no timer and no transport: DiversityWindow feeds it one
// setSpatial() per /diversity/spatial poll and it appends one row. History is
// a fixed kHistoryRows-row QImage that is scrolled down by one row per append,
// so the buffer is bounded by rows rather than by how long the window has been
// open.
//
// Clicking a column emits tuneRequested() with that column's centre frequency;
// hovering it shows the four numbers behind the pixel. Both are the point of
// the widget: a colour you cannot tune to is a decoration.
//
// setSpatial() is not on check_a11y.py's watched-setter list (setLevel/setDbm/
// setFrequency/update*), so the custom paintEvent is exempt from its
// QAccessibleInterface-companion check -- the same exemption DiversityMapStrip
// takes, and for the same reason: the FINDER table beside it exposes the
// candidates this picture is a glance-view of as real text rows.

#include <QColor>
#include <QImage>
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

signals:
    // The operator clicked a column. Carries the column's CENTRE frequency in
    // Hz -- DiversityWindow turns it into a slice tune.
    void tuneRequested(double hz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    // Column under a widget x, or -1 when there is no span to map it onto.
    int columnAt(int x) const;
    // Height of the scrolling picture, i.e. everything above the axis row.
    int waterfallHeight() const;
    // (Re)allocates the history image for `points` columns, discarding what is
    // there -- the span changed, so the old rows describe different bins.
    void resetHistory(int points);

    QImage m_history;
    int    m_points{0};
    int    m_rows{0};
    bool   m_available{false};
    double m_startHz{0.0};
    double m_stepHz{0.0};
    bool   m_havePassband{false};
    double m_passbandLoHz{0.0};
    double m_passbandHiHz{0.0};

    // The newest row's own numbers, kept alongside the pixels so the hover
    // tooltip can quote the measurement rather than reverse-engineer it from a
    // colour.
    QVector<float> m_phaseDeg;
    QVector<float> m_coherence;
    QVector<float> m_levelDb;
    QVector<bool>  m_haveLevel;
};

} // namespace AetherSDR
