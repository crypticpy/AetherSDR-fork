#include "gui/DiversityBeaconPattern.h"

#include "core/ThemeManager.h"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QStringList>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// A square dial. Small enough to sit beside the eighteen-row schedule table
// without pushing it off the page, large enough that eighteen bearings are
// distinguishable round its rim.
// 196 rather than the 230 it was drawn at: the window grew a tab row and a
// FLOW strip above the pages, and the SITE page's height came out of the one
// square widget on it. The dial is still the tallest thing in its column and
// every ring, label and point on it is laid out as a fraction of the side, so
// nothing about it changes except how much room it takes.
constexpr int kSide = 196;

// The full-scale radius is +-kRangeDb about the 0 dB ring, which sits at half
// radius. Ten decibels is the useful range: a pair that differs by more than
// that on a beacon is not a pattern, it is a broken feedline, and clipping the
// dot to the rim says so more honestly than a dial rescaled by one outlier.
constexpr double kRangeDb = 10.0;

constexpr int kDotRadius = 3;

// How near the pointer has to be, in pixels, for a hover to be about a dot.
constexpr double kHoverPx = 9.0;

QString signedDb(double v, int decimals)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', decimals);
    return QStringLiteral("+%1").arg(v, 0, 'f', decimals);
}

QString bandName(double hz)
{
    const int mhz = int(std::lround(hz / 1.0e6));
    switch (mhz) {
    case 14: return QStringLiteral("20 m");
    case 18: return QStringLiteral("17 m");
    case 21: return QStringLiteral("15 m");
    case 24: return QStringLiteral("12 m");
    case 28: return QStringLiteral("10 m");
    default: break;
    }
    return QStringLiteral("%1 MHz").arg(hz / 1.0e6, 0, 'f', 3);
}

} // namespace

DiversityBeaconPattern::DiversityBeaconPattern(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowBeaconPattern"));
    setFixedSize(kSide, kSide);
    setAccessibleName(tr("Beacon pattern"));
    setAccessibleDescription(
        tr("A picture of the A, B and bearing columns of the beacon table: one "
           "dot per beacon heard on both loops, at its own bearing, with loop B "
           "minus loop A as the radius. The ring is loop A and loop B equal; a "
           "dot outside it is a direction where B hears better, one inside is a "
           "direction where A does. Because both loops heard the same signal at "
           "the same instant, propagation cancels and what is left is your "
           "antennas. Read-only."));
    setToolTip(tr("Beacons heard on both loops, plotted by bearing; radius is B minus A."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these -- declare them so Inspect mode surfaces the
    // tokens actually read, and repaint on a live theme switch.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.spectrum.grid"),
        QStringLiteral("color.accent.bright"),
        QStringLiteral("color.text.secondary"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));

    clearPattern();
}

void DiversityBeaconPattern::clearPattern()
{
    m_points.clear();
    m_empty = tr("needs the station grid");
    update();
}

void DiversityBeaconPattern::applyPattern(const QJsonArray& pattern, bool haveGrid)
{
    m_points.clear();
    for (const QJsonValue& v : pattern) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QJsonValue bearing = o.value(QStringLiteral("bearing_deg"));
        const QJsonValue delta = o.value(QStringLiteral("b_minus_a_db"));
        // A point with no bearing has no place on a dial and a point with no
        // difference has no radius. Neither is drawn at a guessed position.
        if (!bearing.isDouble() || !delta.isDouble())
            continue;
        Point p;
        p.call = o.value(QStringLiteral("call")).toString();
        p.bandHz = o.value(QStringLiteral("band_hz")).toDouble();
        p.bearingDeg = int(std::lround(bearing.toDouble()));
        const QJsonValue km = o.value(QStringLiteral("distance_km"));
        p.distanceKm = km.isDouble() ? int(std::lround(km.toDouble())) : -1;
        p.deltaDb = delta.toDouble();
        p.phaseDeg = o.value(QStringLiteral("phase_deg")).toDouble();
        const QJsonValue snr = o.value(QStringLiteral("snr_db"));
        p.haveSnr = snr.isDouble();
        p.snrDb = p.haveSnr ? snr.toDouble() : 0.0;
        m_points.push_back(p);
    }

    // Two different emptinesses. Without a locator the gate cannot compute a
    // bearing for anybody, so the fix is one text field away; with one, the
    // fix is time on the air.
    if (!m_points.isEmpty())
        m_empty.clear();
    else if (!haveGrid)
        m_empty = tr("needs the station grid");
    else
        m_empty = tr("no beacons heard on both loops yet");
    update();
}

QPointF DiversityBeaconPattern::project(const Point& p, const QPointF& centre,
                                        double radius) const
{
    // North up, east clockwise -- a compass rose, not a mathematician's dial.
    const double theta = (double(p.bearingDeg) - 90.0) * M_PI / 180.0;
    const double clipped = std::clamp(p.deltaDb, -kRangeDb, kRangeDb);
    const double r = radius * (0.5 + 0.5 * clipped / kRangeDb);
    return QPointF(centre.x() + r * std::cos(theta), centre.y() + r * std::sin(theta));
}

void DiversityBeaconPattern::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));

    const QPointF centre(width() / 2.0, height() / 2.0);
    const double radius = std::min(width(), height()) / 2.0 - 12.0;
    const QColor grid = tm.color(this, QStringLiteral("color.spectrum.grid"));
    const QColor text = tm.color(this, QStringLiteral("color.text.secondary"));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(grid, 1.0));
    p.drawEllipse(centre, radius, radius);
    // The 0 dB ring at half radius: the whole point of the picture is which
    // side of THIS circle a dot falls on.
    p.drawEllipse(centre, radius * 0.5, radius * 0.5);
    p.drawLine(QPointF(centre.x(), centre.y() - radius),
               QPointF(centre.x(), centre.y() + radius));
    p.drawLine(QPointF(centre.x() - radius, centre.y()),
               QPointF(centre.x() + radius, centre.y()));

    QFont small = font();
    small.setPointSizeF(std::max(6.0, small.pointSizeF() - 2.0));
    p.setFont(small);
    p.setPen(text);
    const QFontMetrics fm(small);
    p.drawText(QRectF(centre.x() - 20.0, 0.0, 40.0, 12.0), Qt::AlignCenter,
               QStringLiteral("N"));
    p.drawText(QRectF(width() - 14.0, centre.y() - 6.0, 14.0, 12.0), Qt::AlignCenter,
               QStringLiteral("E"));
    p.drawText(QRectF(centre.x() - 20.0, height() - 12.0, 40.0, 12.0), Qt::AlignCenter,
               QStringLiteral("S"));
    p.drawText(QRectF(0.0, centre.y() - 6.0, 14.0, 12.0), Qt::AlignCenter,
               QStringLiteral("W"));

    if (m_points.isEmpty()) {
        p.drawText(QRectF(4.0, centre.y() - 8.0, width() - 8.0, 16.0),
                   Qt::AlignCenter, m_empty);
        return;
    }

    const QColor dot = tm.color(this, QStringLiteral("color.accent.bright"));
    for (Point& point : m_points) {
        point.at = project(point, centre, radius);
        p.setPen(Qt::NoPen);
        p.setBrush(dot);
        p.drawEllipse(point.at, double(kDotRadius), double(kDotRadius));
        if (point.call.isEmpty())
            continue;
        // The call beside the dot rather than in a legend: eighteen rows of
        // legend beside eighteen dots is a lookup table, and the whole value
        // of the picture is not having to do the lookup.
        p.setPen(text);
        p.setBrush(Qt::NoBrush);
        const double w = fm.horizontalAdvance(point.call) + 2.0;
        double x = point.at.x() + kDotRadius + 2.0;
        if (x + w > double(width()))
            x = point.at.x() - kDotRadius - 2.0 - w;
        p.drawText(QRectF(x, point.at.y() - 7.0, w, 14.0),
                   Qt::AlignVCenter | Qt::AlignLeft, point.call);
    }
}

bool DiversityBeaconPattern::event(QEvent* e)
{
    if (e->type() != QEvent::ToolTip)
        return QWidget::event(e);
    auto* help = static_cast<QHelpEvent*>(e);
    const QPointF where = help->pos();
    const Point* best = nullptr;
    double bestDistance = kHoverPx;
    for (const Point& point : m_points) {
        const double dx = point.at.x() - where.x();
        const double dy = point.at.y() - where.y();
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= bestDistance) {
            bestDistance = d;
            best = &point;
        }
    }
    if (!best)
        return QWidget::event(e);

    QStringList parts;
    parts << (best->call.isEmpty() ? tr("unknown") : best->call);
    parts << bandName(best->bandHz);
    parts << tr("%1° true").arg(best->bearingDeg);
    if (best->distanceKm >= 0)
        parts << tr("%1 km").arg(best->distanceKm);
    parts << tr("B−A %1 dB").arg(signedDb(best->deltaDb, 1));
    parts << tr("phase %1°").arg(best->phaseDeg, 0, 'f', 1);
    if (best->haveSnr)
        parts << tr("SNR %1 dB").arg(signedDb(best->snrDb, 1));
    QToolTip::showText(help->globalPos(), parts.join(QStringLiteral(" · ")), this);
    return true;
}

} // namespace AetherSDR
