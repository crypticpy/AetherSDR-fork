#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

// ROOFING · DIGITAL PEAK OFFSET (A1) -- the digital roof's own centre, out of
// Aether-gate's `roofing` object on /filter (offset_hz/offset_enabled/
// offset_applied_hz/offset_max_hz; see docs/DIVERSITY.md's "ROOFING · DIGITAL
// -- PEAK OFFSET"). Split from DiversityFilterPanel.cpp and
// DiversityFilterPanelPaint.cpp for the same 800-line reason
// DiversityFilterPanelSqueeze.cpp is its own file: this is genuinely its own
// story, and unlike SQUEEZE it needs no sign flip -- offset_applied_hz
// arrives already on this panel's own always-positive Hz axis, the band's
// half-width is simply digital_hz/2 either side of it, and the clamp is
// symmetric (+/- offset_max_hz) rather than mode-dependent.
//
// The band is drawn whether or not PEAK OFFSET is checked ON: unchecked, the
// gate still remembers the offset (offset_applied_hz keeps reporting it) and
// the band is drawn dashed, fainter, to say "this is where it would sit" --
// the same distinction the CHAIN card's own check mark makes for the row.
// The handle is a live drag, painted in paintEvent() rather than the cached
// layer, so a drag repaints only its own column the way an edge drag does;
// the band itself stays in the cached layer, at the gate-confirmed position,
// until the drag's release moves it -- the same split moveEdge()'s own
// comment describes for the passband shading.
namespace AetherSDR {

namespace {

// Matches kGrabPx in DiversityFilterPanel.cpp -- its own constant for the
// same reason DiversityFilterPanelSqueeze.cpp keeps its own: nothing about
// the roof handle needs it to move in step with the edge/notch grab distance
// if that one ever does.
constexpr double kRoofGrabPx = 6.0;

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
    // for a centre to be the centre OF.
    m_roofAvailable = m_roofDigitalHz > 0.0;
    m_roofChecked = roofing.value(QStringLiteral("offset_enabled")).toBool();
    if (jsonNumber(roofing, "offset_applied_hz", &v))
        m_roofOffsetHz = v;
    // A roof too narrow for the passband it is meant to carry holds the
    // offset at 0 and reports offset_max_hz: 0 -- roofDraggable() reads that
    // straight off m_roofMaxHz, no handle drawn, no drag possible.
    if (jsonNumber(roofing, "offset_max_hz", &v))
        m_roofMaxHz = v;
}

// `hz` clamped to the drag's own allowed range. Symmetric, not mode-signed
// the way SQUEEZE's click is: offset_hz is a plain signed Hz off the roof's
// own centre, not a slice-relative frequency that flips with the sideband.
double DiversityFilterPanel::roofClampedHz(double hz) const
{
    if (m_roofMaxHz <= 0.0)
        return 0.0;
    return std::clamp(hz, -m_roofMaxHz, m_roofMaxHz);
}

// Is (x) on the handle. Only when there is a roof to drag at all -- an
// offset_max_hz of 0 means no handle is drawn, and none can be hit either.
bool DiversityFilterPanel::roofHandleHit(double x) const
{
    if (!m_roofAvailable || m_roofMaxHz <= 0.0)
        return false;
    return std::abs(x - xForHz(m_roofOffsetHz)) <= kRoofGrabPx;
}

// Drawn into the cached filter layer: the band the digital roof occupies,
// half-width digital_hz/2 either side of offset_applied_hz, on this panel's
// own axis. Solid and tinted when PEAK OFFSET is checked on; dashed and
// fainter, with no fill, when it is held off -- the gate remembers the
// number either way, and the band says so.
void DiversityFilterPanel::paintRoofBand(QPainter& p, const QRectF& r) const
{
    if (!m_roofAvailable)
        return;
    const double half = m_roofDigitalHz / 2.0;
    const double loX = xForHz(m_roofOffsetHz - half);
    const double hiX = xForHz(m_roofOffsetHz + half);

    ThemeManager& tm = ThemeManager::instance();
    const QColor base = tm.color(this, QStringLiteral("color.text.secondary"));

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
    p.drawLine(QPointF(loX, r.top()), QPointF(loX, r.bottom()));
    p.drawLine(QPointF(hiX, r.top()), QPointF(hiX, r.bottom()));

    p.setPen(base);
    p.drawText(QRectF(loX + 2, r.bottom() - 26, 150, 12), Qt::AlignLeft | Qt::AlignTop,
               tr("roof %1%2 Hz")
                   .arg(m_roofOffsetHz < 0.0 ? QStringLiteral("−") : QStringLiteral("+"),
                        QString::number(std::abs(m_roofOffsetHz), 'f', 0)));
}

// The handle itself, live: painted in paintEvent() the way the two passband
// handles are, so a drag moves only its own column rather than rebuilding
// the cached layer on every pixel of motion. A small diamond rather than the
// passband handles' rectangular caps, so the two families of handle read as
// different controls at a glance.
void DiversityFilterPanel::paintRoofHandle(QPainter& p, const QRectF& r) const
{
    if (!m_roofAvailable || m_roofMaxHz <= 0.0)
        return;
    const double hz = m_roofDrag ? m_roofDragHz : m_roofOffsetHz;
    const double x = xForHz(hz);

    const QColor accent =
        ThemeManager::instance().color(this, QStringLiteral("color.accent.bright"));
    p.setPen(QPen(accent, 2));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.top() + 16));

    QPolygonF diamond;
    diamond << QPointF(x, r.top() + 2) << QPointF(x + 5, r.top() + 8)
            << QPointF(x, r.top() + 14) << QPointF(x - 5, r.top() + 8);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawPolygon(diamond);
    p.setBrush(Qt::NoBrush);
}

} // namespace AetherSDR
