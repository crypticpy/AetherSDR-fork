#pragma once

// The SITE page's PATTERN plot: the only measured antenna pattern a two-loop
// station can actually get.
//
// Every other view in this window shows what the two loops are doing to one
// signal at one moment. None of them answers the question that decides whether
// a loop is worth keeping up: "is one of my antennas better in some DIRECTIONS
// than the other, and which?" A rotator and a field-strength meter would answer
// it; so does the beacon project, for free and while you sleep:
//
//   * the transmitters are at known bearings from a known grid square, so a
//     beacon result is a sample of the pair's response at a KNOWN angle;
//   * eighteen of them are spread right round the compass, so a night's watch
//     is a sweep rather than a single point;
//   * B minus A on one beacon is a difference between the two loops on the
//     same signal at the same instant, so propagation cancels out of it and
//     what is left is the antennas.
//
// So: bearing round the dial, B-A in decibels as the radius. The ring at mid
// radius is 0 dB -- the two loops equal. Outside it B is winning, inside it A
// is. A pair of broadside loops draws a figure of eight; a loop with a bad
// feedline draws a circle offset toward the other one.
//
// It is a picture of the numbers the beacons table already lists as text (the
// A, B and Brg columns), so it carries nothing a screen reader cannot reach --
// the same "decorative vs. data-carrying" split DiversityMapStrip and
// DiversityActivityStrip take, and the reason applyPattern() is deliberately
// not one of tools/check_a11y.py's watched setter names.
//
// It owns no transport: DiversityBeaconPanel hands it the "pattern" array off
// each /diversity/beacons poll.

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

class QJsonArray;
class QPaintEvent;

namespace AetherSDR {

class DiversityBeaconPattern : public QWidget {
    Q_OBJECT
public:
    explicit DiversityBeaconPattern(QWidget* parent = nullptr);

    // One /diversity/beacons "pattern" array. `haveGrid` is whether the gate
    // knows the station's own locator: without it there are no bearings at
    // all, which is a different emptiness from "no beacon has been heard on
    // both loops yet" and says so.
    void applyPattern(const QJsonArray& pattern, bool haveGrid);

    // Gate gone, or diversity no longer available.
    void clearPattern();

    // What is actually plotted. A painted widget has no child to read back
    // from, and a test has no other way to check that a point survived the
    // parse -- the same accessor DiversitySnrMeter::shownDb() is.
    int pointCount() const { return int(m_points.size()); }

    // The sentence drawn in place of the dial when there is nothing to plot,
    // or empty while there is. Two different facts, never one shrug.
    QString emptyText() const { return m_empty; }

protected:
    void paintEvent(QPaintEvent*) override;
    // QEvent::ToolTip rather than an eventFilter or a mouseMoveEvent: the dots
    // are two pixels across and the hover has to name the one nearest the
    // pointer, which is a query the tooltip event asks at exactly the right
    // moment and a move event would ask sixty times a second.
    bool event(QEvent* e) override;

private:
    struct Point {
        QString call;
        double  bandHz{0.0};
        int     bearingDeg{0};
        int     distanceKm{-1};
        double  deltaDb{0.0};
        double  phaseDeg{0.0};
        double  snrDb{0.0};
        bool    haveSnr{false};
        // Where the dot landed on the last paint, so the hover can find it
        // without redoing the projection.
        QPointF at;
    };

    QPointF project(const Point& p, const QPointF& centre, double radius) const;

    QVector<Point> m_points;
    QString        m_empty;
};

} // namespace AetherSDR
