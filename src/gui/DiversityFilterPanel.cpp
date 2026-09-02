#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace AetherSDR {

namespace {

// The window the curve is drawn in. 0 dB at the top is the convention every
// filter plot in every radio manual uses; -60 dB at the bottom is where a
// 1023-tap sharp filter's stopband already is, so a deeper floor would be
// sixty pixels of nothing.
constexpr double kTopDb = 0.0;
constexpr double kBottomDb = -60.0;

// Gutters. The left one carries the dB scale, the bottom one the Hz scale.
constexpr int kLeftGutter = 32;
constexpr int kBottomGutter = 16;
constexpr int kTopMargin = 6;
constexpr int kRightMargin = 8;

// How near a handle the pointer has to be, in pixels, to grab it. Six is about
// a fingertip's worth of slop at this scale and still narrow enough that the
// two handles of a 100 Hz CW filter do not overlap.
constexpr double kGrabPx = 6.0;

// Every edge the operator sets is a multiple of ten. The gate accepts any
// integer, but a passband edge is not a thing anybody wants to the Hz, and
// snapping is what makes a drag land on a round number instead of 2913.
constexpr int kSnapHz = 10;
constexpr int kArrowStepHz = 10;
constexpr int kArrowFastStepHz = 50;

// The narrowest passband a drag may produce. Below this the two handles are
// the same handle and the operator cannot get out of it by dragging.
constexpr int kMinSpanHz = 50;
constexpr int kMaxEdgeHz = 20000;

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

int snapped(double hz)
{
    return int(std::lround(hz / double(kSnapHz))) * kSnapHz;
}

} // namespace

DiversityFilterPanel::DiversityFilterPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFilterPanel"));
    setAccessibleName(tr("Filter response"));
    setAccessibleDescription(
        tr("The slice filter's measured response, with the passband you asked "
           "for shaded over it. Drag either edge to move it, double-click "
           "anywhere on the curve to notch that frequency, and use the left "
           "and right arrow keys (hold Shift for five times the step) to move "
           "the edge you last touched. Up and down choose which edge that is."));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    m_cursorHz = std::numeric_limits<double>::quiet_NaN();
    m_contourHz = std::numeric_limits<double>::quiet_NaN();
    m_apfHz = std::numeric_limits<double>::quiet_NaN();
}

void DiversityFilterPanel::clear()
{
    m_available = false;
    m_hz.clear();
    m_db.clear();
    m_notches.clear();
    m_anf.clear();
    m_minHz = 0.0;
    m_maxHz = 0.0;
    m_lowHz = 0;
    m_highHz = 0;
    m_contourHz = std::numeric_limits<double>::quiet_NaN();
    m_apfHz = std::numeric_limits<double>::quiet_NaN();
    m_drag = Edge::None;
    update();
}

void DiversityFilterPanel::applyStatus(const QJsonObject& filter)
{
    if (!filter.value(QStringLiteral("available")).toBool()) {
        clear();
        return;
    }
    m_available = true;

    const QJsonObject response = filter.value(QStringLiteral("response")).toObject();
    const QJsonArray hz = response.value(QStringLiteral("hz")).toArray();
    const QJsonArray db = response.value(QStringLiteral("db")).toArray();
    // Both arrays or neither: a curve drawn from mismatched axes would be a
    // picture of arithmetic rather than of a filter.
    if (!hz.isEmpty() && hz.size() == db.size()) {
        m_hz.resize(hz.size());
        m_db.resize(db.size());
        m_minHz = hz.at(0).toDouble();
        m_maxHz = m_minHz;
        for (int i = 0; i < hz.size(); ++i) {
            m_hz[i] = hz.at(i).toDouble();
            m_db[i] = db.at(i).toDouble();
            m_minHz = std::min(m_minHz, m_hz[i]);
            m_maxHz = std::max(m_maxHz, m_hz[i]);
        }
    }

    double v = 0.0;
    if (jsonNumber(filter, "low_hz", &v))
        m_lowHz = int(std::lround(v));
    if (jsonNumber(filter, "high_hz", &v))
        m_highHz = int(std::lround(v));

    m_notches.clear();
    const QJsonArray notches = filter.value(QStringLiteral("notches")).toArray();
    for (const QJsonValue& entry : notches) {
        const QJsonObject notch = entry.toObject();
        double at = 0.0;
        if (!jsonNumber(notch, "hz", &at))
            continue;
        double depth = 0.0;
        jsonNumber(notch, "depth_db", &depth);
        m_notches.append(QPointF(at, depth));
    }

    m_anf.clear();
    const QJsonObject anf = filter.value(QStringLiteral("anf")).toObject();
    if (anf.value(QStringLiteral("enabled")).toBool()) {
        const QJsonArray found = anf.value(QStringLiteral("found_hz")).toArray();
        const QJsonArray depths = anf.value(QStringLiteral("depth_db")).toArray();
        for (int i = 0; i < found.size(); ++i) {
            m_anf.append(QPointF(found.at(i).toDouble(),
                                 i < depths.size() ? depths.at(i).toDouble() : 0.0));
        }
    }

    const QJsonObject contour = filter.value(QStringLiteral("contour")).toObject();
    m_contourHz = std::numeric_limits<double>::quiet_NaN();
    if (contour.value(QStringLiteral("enabled")).toBool()
        && jsonNumber(contour, "hz", &v)) {
        m_contourHz = v;
    }
    const QJsonObject apf = filter.value(QStringLiteral("apf")).toObject();
    m_apfHz = std::numeric_limits<double>::quiet_NaN();
    if (apf.value(QStringLiteral("enabled")).toBool() && jsonNumber(apf, "hz", &v))
        m_apfHz = v;

    update();
}

// --------------------------------------------------------------------------
// Geometry
// --------------------------------------------------------------------------

QRectF DiversityFilterPanel::plotRect() const
{
    return QRectF(kLeftGutter, kTopMargin,
                  std::max(1, width() - kLeftGutter - kRightMargin),
                  std::max(1, height() - kTopMargin - kBottomGutter));
}

double DiversityFilterPanel::xForHz(double hz) const
{
    const QRectF r = plotRect();
    if (m_maxHz <= m_minHz)
        return r.left();
    const double t = (hz - m_minHz) / (m_maxHz - m_minHz);
    return r.left() + std::clamp(t, 0.0, 1.0) * r.width();
}

double DiversityFilterPanel::hzForX(double x) const
{
    const QRectF r = plotRect();
    if (m_maxHz <= m_minHz || r.width() <= 0.0)
        return 0.0;
    const double t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return m_minHz + t * (m_maxHz - m_minHz);
}

double DiversityFilterPanel::yForDb(double db) const
{
    const QRectF r = plotRect();
    const double t = (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb);
    return r.top() + t * r.height();
}

DiversityFilterPanel::Edge DiversityFilterPanel::edgeAt(double x) const
{
    if (!m_available || m_maxHz <= m_minHz)
        return Edge::None;
    const double dLow = std::abs(x - xForHz(m_lowHz));
    const double dHigh = std::abs(x - xForHz(m_highHz));
    if (dLow <= kGrabPx && dLow <= dHigh)
        return Edge::Low;
    if (dHigh <= kGrabPx)
        return Edge::High;
    return Edge::None;
}

void DiversityFilterPanel::moveEdge(Edge edge, double hz)
{
    const int want = std::clamp(snapped(hz), 0, kMaxEdgeHz);
    if (edge == Edge::Low)
        m_lowHz = std::min(want, m_highHz - kMinSpanHz);
    else if (edge == Edge::High)
        m_highHz = std::max(want, m_lowHz + kMinSpanHz);
    update();
}

// --------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------

void DiversityFilterPanel::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(ev);
        return;
    }
    const Edge edge = edgeAt(ev->position().x());
    if (edge == Edge::None) {
        QWidget::mousePressEvent(ev);
        return;
    }
    m_drag = edge;
    m_focusEdge = edge;
    setFocus(Qt::MouseFocusReason);
    ev->accept();
}

void DiversityFilterPanel::mouseMoveEvent(QMouseEvent* ev)
{
    m_cursorHz = m_available ? hzForX(ev->position().x())
                             : std::numeric_limits<double>::quiet_NaN();
    if (m_drag != Edge::None) {
        moveEdge(m_drag, hzForX(ev->position().x()));
        ev->accept();
        return;
    }
    // The cursor is the affordance: nothing here says "draggable" except that
    // the pointer changes over the two handles.
    setCursor(edgeAt(ev->position().x()) == Edge::None ? Qt::CrossCursor
                                                       : Qt::SplitHCursor);
    update();
}

void DiversityFilterPanel::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_drag == Edge::None) {
        QWidget::mouseReleaseEvent(ev);
        return;
    }
    m_drag = Edge::None;
    emit edgesDragged(m_lowHz, m_highHz);
    ev->accept();
}

void DiversityFilterPanel::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton || !m_available
        || edgeAt(ev->position().x()) != Edge::None) {
        QWidget::mouseDoubleClickEvent(ev);
        return;
    }
    emit notchRequested(double(snapped(hzForX(ev->position().x()))));
    ev->accept();
}

void DiversityFilterPanel::keyPressEvent(QKeyEvent* ev)
{
    if (!m_available) {
        QWidget::keyPressEvent(ev);
        return;
    }
    // Up/Down pick the edge, Left/Right move it. Two keys rather than a Tab
    // stop each, so the whole control is one stop in the window's focus chain
    // and an operator tabbing past it does not have to pass through two.
    if (ev->key() == Qt::Key_Up) {
        m_focusEdge = Edge::High;
        update();
        ev->accept();
        return;
    }
    if (ev->key() == Qt::Key_Down) {
        m_focusEdge = Edge::Low;
        update();
        ev->accept();
        return;
    }
    if (ev->key() != Qt::Key_Left && ev->key() != Qt::Key_Right) {
        QWidget::keyPressEvent(ev);
        return;
    }
    const int step = (ev->modifiers() & Qt::ShiftModifier) ? kArrowFastStepHz
                                                           : kArrowStepHz;
    const int delta = ev->key() == Qt::Key_Left ? -step : step;
    const int from = m_focusEdge == Edge::High ? m_highHz : m_lowHz;
    moveEdge(m_focusEdge, double(from + delta));
    emit edgesDragged(m_lowHz, m_highHz);
    ev->accept();
}

void DiversityFilterPanel::leaveEvent(QEvent* ev)
{
    m_cursorHz = std::numeric_limits<double>::quiet_NaN();
    update();
    QWidget::leaveEvent(ev);
}

// --------------------------------------------------------------------------
// Paint
// --------------------------------------------------------------------------

void DiversityFilterPanel::paintEvent(QPaintEvent*)
{
    ThemeManager& tm = ThemeManager::instance();
    const QColor grid = tm.color(this, QStringLiteral("color.spectrum.grid"));
    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));
    const QColor accent = tm.color(this, QStringLiteral("color.accent"));
    const QColor trace = tm.color(this, QStringLiteral("color.spectrum.trace"));

    QPainter p(this);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));

    const QRectF r = plotRect();
    // Bars, ticks and gridlines with antialiasing OFF and text with it ON --
    // the painting convention every other instrument in this window keeps.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont small = p.font();
    small.setPixelSize(9);
    p.setFont(small);

    for (int db = int(kTopDb); db >= int(kBottomDb); db -= 10) {
        const double y = yForDb(db);
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(secondary);
        p.drawText(QRectF(0, y - 7, kLeftGutter - 4, 14),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
    }

    if (!m_available || m_maxHz <= m_minHz) {
        p.setPen(secondary);
        p.drawText(r, Qt::AlignCenter, tr("no filter response"));
        return;
    }

    // The passband the operator asked for, shaded. Deliberately drawn UNDER
    // the curve: the shading is the request, the curve is the answer, and
    // where they disagree the curve has to win the eye.
    QColor shade = accent;
    shade.setAlpha(48);
    p.fillRect(QRectF(xForHz(m_lowHz), r.top(), xForHz(m_highHz) - xForHz(m_lowHz),
                      r.height()),
               shade);

    // The tones the automatic notcher found, dashed: they are not filters the
    // operator placed and must not read as if they were.
    QPen anfPen(tm.color(this, QStringLiteral("color.spectrum.average")), 1,
                Qt::DashLine);
    p.setPen(anfPen);
    for (const QPointF& tone : m_anf) {
        const double x = xForHz(tone.x());
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }

    // Manual notches: a solid mark with the depth the gate measured beside it.
    const QColor warning = tm.color(this, QStringLiteral("color.accent.warning"));
    for (const QPointF& notch : m_notches) {
        const double x = xForHz(notch.x());
        p.setPen(QPen(warning, 1));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        // The true minus sign, the same one the notch table and every other dB
        // readout in this window uses -- a hyphen here would be the only one.
        p.drawText(QRectF(x + 2, r.top() + 1, 46, 12), Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("%1%2")
                       .arg(notch.y() < 0.0 ? QStringLiteral("−") : QString(),
                            QString::number(std::abs(notch.y()), 'f', 0)));
    }

    // Contour and audio-peak centres: short ticks off the bottom axis. They
    // shape the passband rather than cutting it, so they get a tick and not a
    // full-height line.
    const QColor bright = tm.color(this, QStringLiteral("color.accent.bright"));
    p.setPen(QPen(bright, 2));
    for (double centre : {m_contourHz, m_apfHz}) {
        if (std::isnan(centre))
            continue;
        const double x = xForHz(centre);
        p.drawLine(QPointF(x, r.bottom() - 8), QPointF(x, r.bottom()));
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    QPolygonF curve;
    curve.reserve(m_hz.size());
    for (int i = 0; i < m_hz.size(); ++i)
        curve << QPointF(xForHz(m_hz[i]), yForDb(m_db[i]));
    p.setPen(QPen(trace, 2));
    p.drawPolyline(curve);
    p.setRenderHint(QPainter::Antialiasing, false);

    // The two handles, with the focused one heavier so the arrow keys have a
    // visible subject.
    const auto drawHandle = [&](Edge edge, int hz) {
        const double x = xForHz(hz);
        const bool focusedEdge = hasFocus() && m_focusEdge == edge;
        p.setPen(QPen(accent, focusedEdge ? 3 : 2));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.fillRect(QRectF(x - 3, r.top(), 6, 8), accent);
        p.fillRect(QRectF(x - 3, r.bottom() - 8, 6, 8), accent);
    };
    drawHandle(Edge::Low, m_lowHz);
    drawHandle(Edge::High, m_highHz);

    // The Hz scale, and the pointer's own frequency in the corner. The corner
    // readout is the whole reason a double-click notch is trustworthy: you can
    // see which frequency you are about to kill before you kill it.
    p.setPen(secondary);
    p.drawText(QRectF(r.left(), r.bottom() + 2, 60, kBottomGutter - 2),
               Qt::AlignLeft | Qt::AlignVCenter, QString::number(qint64(m_minHz)));
    p.drawText(QRectF(r.center().x() - 30, r.bottom() + 2, 60, kBottomGutter - 2),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QString::number(qint64((m_minHz + m_maxHz) / 2.0)));
    p.drawText(QRectF(r.right() - 60, r.bottom() + 2, 60, kBottomGutter - 2),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("%1 Hz").arg(qint64(m_maxHz)));
    if (!std::isnan(m_cursorHz)) {
        p.setPen(bright);
        p.drawText(QRectF(r.right() - 80, r.top() + 2, 78, 14),
                   Qt::AlignRight | Qt::AlignTop,
                   tr("%1 Hz").arg(qint64(std::lround(m_cursorHz))));
    }
}

} // namespace AetherSDR
