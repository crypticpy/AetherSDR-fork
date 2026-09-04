#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

// ROOFING · DIGITAL PEAK OFFSET (A1) -- the digital roof's own centre, out of
// Aether-gate's `roofing` object on /filter (offset_hz/offset_enabled/
// offset_applied_hz/offset_max_hz/digital_active; see docs/DIVERSITY.md's
// "ROOFING · DIGITAL -- PEAK OFFSET"). Split from DiversityFilterPanel.cpp and
// DiversityFilterPanelPaint.cpp for the same 800-line reason
// DiversityFilterPanelSqueeze.cpp is its own file.
//
// THE AXIS PROBLEM THIS FILE EXISTS TO SOLVE. The passband and everything
// else this widget draws lives on the plot's own audio axis, 0..~3500 Hz.
// The digital roof's own half-width is NOT a passband-sized number: the
// gate's own `digital_hz` is the whole IF roof (25000 Hz on the wide preset,
// still 3000+ narrowed), so `offset ± digital_hz/2` on that same axis is
// either a full-plot wash (correct -- it says "your audio passband is
// entirely inside the roof") or, if drawn with a HANDLE at that position the
// way the passband edges get one, a handle that is unreachable for half its
// own range: xForHz() clamps its `t` to [0,1], and since the audio axis
// starts at 0, every negative offset maps to the same pixel column as 0 does.
//
// So the band (the WASH -- what the roof does to the spectrum) is drawn on
// the audio axis, same as ever, in paintRoofBand() below; and the HANDLE --
// the thing an operator drags -- is drawn on its OWN axis instead: a thin
// strip along the very top of the plot, offset_hz mapped from
// -offset_max_hz..+offset_max_hz onto the strip's own full width, in
// paintRoofHandle(). The strip reuses plotRect()'s left and width so its
// columns line up with the wash under it; roofHeaderXForOffset() and
// roofOffsetForX() are that one mapping and its inverse, and the drag in
// DiversityFilterPanel.cpp's mouseMoveEvent() reads the pointer through
// roofOffsetForX(x) directly -- never through hzForX(), whose audio axis
// starts at 0 and could only ever reach the positive half of the range.
//
// GATING. digital_active, not just digital_hz > 0, decides whether there is
// a roof to draw at all: a roof that is configured but out of circuit (the
// gate answers `"digital_active": false` whenever the digital filter is not
// actually running -- confirmed against a live gate, not guessed) is not a
// claim this picture should make. m_roofAvailable carries that single flag;
// every paint and hit test below is gated on it.
namespace AetherSDR {

namespace {

// Matches kGrabPx in DiversityFilterPanel.cpp -- its own constant for the
// same reason DiversityFilterPanelSqueeze.cpp keeps its own: nothing about
// the roof handle needs it to move in step with the edge/notch grab distance
// if that one ever does.
constexpr double kRoofGrabPx = 6.0;
// The header strip's own height, along the very top of the plot -- tall
// enough for a 9 px label and a small triangle without eating into the
// curve below it.
constexpr double kRoofStripHeight = 14.0;

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

} // namespace

void DiversityFilterPanel::resetRoof()
{
    m_roofAvailable = false;
    m_roofChecked = false;
    m_roofDigitalHz = 0.0;
    m_roofOffsetHz = 0.0;
    m_roofMaxHz = 0.0;
}

// One /filter object's "roofing" block. Called from applyStatus() alongside
// parseSqueeze(), before the fingerprints are re-taken, so a roof that moved
// is a filter-layer change like any other. `digital_hz` and `offset_max_hz`
// come off "roofing" itself, not off any chain[] row: the band is drawn
// whether or not the gate's chain[] carries roof_digital's own checks[].
void DiversityFilterPanel::parseRoof(const QJsonObject& filter)
{
    resetRoof();
    const QJsonValue rv = filter.value(QStringLiteral("roofing"));
    if (!rv.isObject())
        return;
    const QJsonObject roofing = rv.toObject();

    double v = 0.0;
    if (jsonNumber(roofing, "digital_hz", &v))
        m_roofDigitalHz = v;
    // A roof with no width is not a roof to draw at all -- there is nothing
    // for a centre to be the centre OF. Nor is one the gate itself reports
    // out of circuit: `digital_active` is Aether-gate's own /filter key for
    // "the digital roof is actually running" (not merely configured), and a
    // band drawn while it reads false would be a claim about a filter that
    // is not in the signal path.
    m_roofAvailable = m_roofDigitalHz > 0.0
        && roofing.value(QStringLiteral("digital_active")).toBool();
    m_roofChecked = roofing.value(QStringLiteral("offset_enabled")).toBool();
    if (jsonNumber(roofing, "offset_applied_hz", &v))
        m_roofOffsetHz = v;
    // A roof too narrow for the passband it is meant to carry holds the
    // offset at 0 and reports offset_max_hz: 0 -- roofDraggable() reads that
    // straight off m_roofMaxHz, no handle drawn, no drag possible.
    if (jsonNumber(roofing, "offset_max_hz", &v))
        m_roofMaxHz = v;
}

// Where the header strip's OWN axis (-offset_max_hz..+offset_max_hz, not the
// plot's audio Hz below it) puts `offsetHz`, on the plot's own pixel span --
// the strip reuses plotRect()'s left/width rather than carrying a second
// rectangle, so its columns line up with the wash under it. Unlike xForHz(),
// 0 sits at the CENTRE of this axis: a negative offset is exactly as
// reachable as a positive one.
double DiversityFilterPanel::roofHeaderXForOffset(double offsetHz) const
{
    const QRectF r = plotRect();
    if (m_roofMaxHz <= 0.0 || r.width() <= 0.0)
        return r.left();
    const double t = std::clamp((offsetHz + m_roofMaxHz) / (2.0 * m_roofMaxHz), 0.0, 1.0);
    return r.left() + t * r.width();
}

// Where the handle is drawn/hit right now: the live drag ghost while one is
// in progress, the gate's own offset otherwise -- the same shape every other
// live mark in this widget (the two edge handles, a dragged notch) follows.
// Public so a test can press exactly on it, the way it presses on an edge
// handle via xForHz().
double DiversityFilterPanel::roofHandleX() const
{
    const double hz = m_roofDrag ? m_roofDragHz : m_roofOffsetHz;
    return roofHeaderXForOffset(hz);
}

// The inverse of roofHeaderXForOffset(): the offset the header strip's own
// axis reads at pixel column `x`, clamped to +/- offset_max_hz -- what a
// drag past either end of the strip settles at instead of past it.
double DiversityFilterPanel::roofOffsetForX(double x) const
{
    const QRectF r = plotRect();
    if (m_roofMaxHz <= 0.0 || r.width() <= 0.0)
        return 0.0;
    const double t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return -m_roofMaxHz + t * 2.0 * m_roofMaxHz;
}

// The strip and the handle's tail under it, in widget pixels: what one drag
// step has to repaint. The whole strip rather than the handle's own column,
// because the label at its left end tracks the drag too.
QRect DiversityFilterPanel::roofStripRect() const
{
    const QRectF r = plotRect();
    return QRect(int(r.left()) - 1, int(r.top()) - 1, int(r.width()) + 2,
                 int(kRoofStripHeight) + 10);
}

// Is (x) on the header strip's handle. Only when there is a roof to drag at
// all -- an offset_max_hz of 0 means no handle is drawn, and none can be hit
// either -- and only when the roof is actually in circuit (m_roofAvailable).
bool DiversityFilterPanel::roofHandleHit(double x) const
{
    if (!m_roofAvailable || m_roofMaxHz <= 0.0)
        return false;
    return std::abs(x - roofHandleX()) <= kRoofGrabPx;
}

// Whether the band's low/high edge is a genuine boundary on the plot's own
// audio axis, or one the wash reaches without a line because the true edge
// sits outside the plot entirely -- read back by paintRoofBand() below and
// by tests, the way notchHzAt() is. Against the axis as DRAWN (the passband
// plus its margin), not against the gate's whole array: an edge the zoom has
// pushed off the side is exactly as absent from the picture as one the array
// never covered.
bool DiversityFilterPanel::roofLowEdgeInPlot() const
{
    if (!m_roofAvailable)
        return false;
    const double loHz = m_roofOffsetHz - m_roofDigitalHz / 2.0;
    return loHz >= m_viewMinHz && loHz <= m_viewMaxHz;
}

bool DiversityFilterPanel::roofHighEdgeInPlot() const
{
    if (!m_roofAvailable)
        return false;
    const double hiHz = m_roofOffsetHz + m_roofDigitalHz / 2.0;
    return hiHz >= m_viewMinHz && hiHz <= m_viewMaxHz;
}

// The header strip's own label -- what names the roof width and the offset
// a painted picture otherwise has no other way to say. Live: it tracks the
// drag ghost the same way roofHandleX() does, so the number under the
// pointer during a drag is the number about to be written.
QString DiversityFilterPanel::roofHeaderText() const
{
    if (!m_roofAvailable)
        return QString();
    const double shownOffset = m_roofDrag ? m_roofDragHz : m_roofOffsetHz;
    const QString width = m_roofDigitalHz >= 1000.0
        ? tr("%1 kHz").arg(m_roofDigitalHz / 1000.0, 0, 'f', 1)
        : tr("%1 Hz").arg(m_roofDigitalHz, 0, 'f', 0);
    return tr("ROOF %1 · offset %2%3 Hz")
        .arg(width, shownOffset < 0.0 ? QStringLiteral("−") : QStringLiteral("+"),
             QString::number(std::abs(shownOffset), 'f', 0));
}

// Drawn into the cached filter layer: the WASH the digital roof occupies,
// half-width digital_hz/2 either side of offset_applied_hz, on this panel's
// own audio axis. Solid and tinted when PEAK OFFSET is checked on; dashed
// and fainter, with no fill, when it is held off -- the gate remembers the
// number either way, and the wash says so. An edge that clamps to a gutter
// (digital_hz wider than the audio axis, or an offset that pushes it off
// one side) gets no pinned line: xForHz() already carries the wash to the
// gutter on its own, and a line drawn AT a clamp would claim a boundary is
// there when the true one is off the edge of the picture entirely.
void DiversityFilterPanel::paintRoofBand(QPainter& p, const QRectF& r) const
{
    if (!m_roofAvailable)
        return;
    const double half = m_roofDigitalHz / 2.0;
    const double loX = xForHz(m_roofOffsetHz - half);
    const double hiX = xForHz(m_roofOffsetHz + half);

    ThemeManager& tm = ThemeManager::instance();
    // color.text.label, not color.text.secondary: secondary is the AUTO
    // edges' colour AND the gutter's, so a dashed roof edge in it was a third
    // full-height vertical line no legend could tell from the other two. The
    // roof is meant to be the faintest thing on the plot -- it is a boundary,
    // not a mark -- and label is the dimmest token the theme defines.
    const QColor base = tm.color(this, QStringLiteral("color.text.label"));

    if (m_roofChecked) {
        QColor tint = base;
        tint.setAlpha(28);
        p.fillRect(QRectF(loX, r.top(), std::max(1.0, hiX - loX), r.height()), tint);
        QColor edge = base;
        edge.setAlpha(170);
        p.setPen(QPen(edge, 1, Qt::DashLine));
    } else {
        QColor edge = base;
        edge.setAlpha(110);
        p.setPen(QPen(edge, 1, Qt::DotLine));
    }
    if (roofLowEdgeInPlot())
        p.drawLine(QPointF(loX, r.top()), QPointF(loX, r.bottom()));
    if (roofHighEdgeInPlot())
        p.drawLine(QPointF(hiX, r.top()), QPointF(hiX, r.bottom()));
}

// The header strip and its handle, live: painted in paintEvent() rather than
// the cached layer, so a drag repaints only its own column the way the two
// passband handles do. The label is drawn whenever there is a roof to name
// at all; the triangle only when there is a range to drag it across
// (m_roofMaxHz > 0) -- a roof too narrow for its own passband still gets the
// wash and the words, just not a handle promising a drag that would do
// nothing.
void DiversityFilterPanel::paintRoofHandle(QPainter& p, const QRectF& r) const
{
    if (!m_roofAvailable)
        return;

    ThemeManager& tm = ThemeManager::instance();
    // The strip belongs to the wash under it -- same token, see
    // paintRoofBand(). Only the HANDLE stays bright: it is the one thing on
    // the strip that can be dragged.
    const QColor base = tm.color(this, QStringLiteral("color.text.label"));
    const QRectF strip(r.left(), r.top(), r.width(), kRoofStripHeight);

    QColor backing = base;
    backing.setAlpha(30);
    p.fillRect(strip, backing);

    QFont small = p.font();
    small.setPixelSize(9);
    p.setFont(small);
    p.setPen(base);
    p.drawText(strip.adjusted(4, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, roofHeaderText());

    if (m_roofMaxHz <= 0.0)
        return;

    const double x = roofHandleX();
    const QColor accent = tm.color(this, QStringLiteral("color.accent.bright"));
    p.setPen(QPen(accent, 2));
    p.drawLine(QPointF(x, strip.bottom()), QPointF(x, strip.bottom() + 8));

    QPolygonF triangle;
    triangle << QPointF(x, strip.bottom()) << QPointF(x - 5, strip.bottom() - 6)
              << QPointF(x + 5, strip.bottom() - 6);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawPolygon(triangle);
    p.setBrush(Qt::NoBrush);
}

} // namespace AetherSDR
