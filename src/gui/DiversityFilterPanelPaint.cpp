#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>

// The picture, split from DiversityFilterPanel.cpp for the reason every other
// file in this window's family is split: AGENTS.md asks for files under 800
// lines, and the widget's two jobs are genuinely two stories. The other file
// holds what the gate said and what the pointer is doing; this one holds how
// that is drawn, which is where the operator's "a little laggy" was fixed.
//
// The three rules it implements are stated in DiversityFilterPanel.h. In short:
// a poll that is not news never gets here at all; everything that changes only
// when the FILTER changes is in one cached transparent pixmap; and a drag
// repaints a handle's own column rather than the widget.

namespace AetherSDR {


// THE CACHED LAYER: everything that changes only when the FILTER changes, on a
// TRANSPARENT pixmap so the one thing that does move every poll -- the spectrum
// -- can be painted underneath it rather than over it. Over it was the shorter
// road and the wrong picture: the shading is the request and the curve is the
// answer, and an incoming band drawn on top of both would win an argument it is
// not in. The only marks this reorders are the gridlines, which now read over
// the area instead of under it, and read better for it.
void DiversityFilterPanel::paintLayer(QPainter& p, const QRectF& r) const
{
    ThemeManager& tm = ThemeManager::instance();
    const QColor grid = tm.color(this, QStringLiteral("color.spectrum.grid"));
    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));

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

    // The AUTO edges, whether or not there is a spectrum to judge them
    // against: thin, so they cannot be mistaken for the two solid handles that
    // are the operator's own, and dash-DOTTED rather than dashed so they
    // cannot be mistaken for the automatic notcher's tones either. Labelled
    // once, at the low edge, because two thin lines on a busy plot are worth
    // nothing if you cannot tell what put them there.
    QColor mark = secondary;
    mark.setAlpha(190);
    for (double edge : {m_autoLowHz, m_autoHighHz}) {
        if (std::isnan(edge))
            continue;
        p.setPen(QPen(mark, 1, Qt::DashDotLine));
        const double x = xForHz(edge);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    if (!std::isnan(m_autoLowHz)) {
        p.setPen(secondary);
        p.drawText(QRectF(xForHz(m_autoLowHz) + 3, r.bottom() - 14, 30, 12),
                   Qt::AlignLeft | Qt::AlignBottom, tr("auto"));
    }

    // The floor tick. Without it the area's height is a number nobody can
    // read; with it the height is decibels over the median.
    if (!m_specDb.isEmpty()) {
        const double floorY = yForDb(kFloorAxisDb);
        QColor floorPen = secondary;
        floorPen.setAlpha(140);
        p.setPen(QPen(floorPen, 1, Qt::DotLine));
        p.drawLine(QPointF(r.left(), floorY), QPointF(r.right(), floorY));
        p.setPen(secondary);
        p.drawText(QRectF(0, floorY - 7, kLeftGutter - 4, 14),
                   Qt::AlignRight | Qt::AlignVCenter, tr("floor"));
    }

    p.setPen(secondary);
    p.drawText(QRectF(r.left(), r.bottom() + 2, 60, kBottomGutter - 2),
               Qt::AlignLeft | Qt::AlignVCenter, QString::number(qint64(m_minHz)));
    p.drawText(QRectF(r.center().x() - 30, r.bottom() + 2, 60, kBottomGutter - 2),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QString::number(qint64((m_minHz + m_maxHz) / 2.0)));
    p.drawText(QRectF(r.right() - 60, r.bottom() + 2, 60, kBottomGutter - 2),
               Qt::AlignRight | Qt::AlignVCenter, tr("%1 Hz").arg(qint64(m_maxHz)));
    // No spectrum is a fact about the GATE, not about the band. Said in the
    // corner rather than left blank, which would read as a dead channel.
    if (m_specDb.isEmpty()) {
        p.drawText(QRectF(r.right() - 90, r.top() + 16, 88, 14),
                   Qt::AlignRight | Qt::AlignTop, tr("no audio yet"));
    }

    const QColor accent = tm.color(this, QStringLiteral("color.accent"));
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
    p.setPen(QPen(tm.color(this, QStringLiteral("color.spectrum.average")), 1,
                  Qt::DashLine));
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
        // Just BELOW the 0 dB line: the curve runs along the top of the plot
        // across the passband, and a label on the top edge sat on it. The true
        // minus sign, the one every other dB readout in this window uses.
        p.drawText(QRectF(x + 3, yForDb(kTopDb) + 5, 46, 12),
                   Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("%1%2")
                       .arg(notch.y() < 0.0 ? QStringLiteral("−") : QString(),
                            QString::number(std::abs(notch.y()), 'f', 0)));
    }

    // Contour and audio-peak centres: short ticks off the bottom axis. They
    // shape the passband rather than cutting it, so they get a tick and not a
    // full-height line.
    p.setPen(QPen(tm.color(this, QStringLiteral("color.accent.bright")), 2));
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
    p.setPen(QPen(tm.color(this, QStringLiteral("color.spectrum.trace")), 2));
    p.drawPolyline(curve);
}

void DiversityFilterPanel::rebuildLayer()
{
    ++m_staticRebuilds;
    m_layersDirty = false;
    const qreal dpr = devicePixelRatioF();
    m_layer = QPixmap((size() * dpr).expandedTo(QSize(1, 1)));
    m_layer.setDevicePixelRatio(dpr);
    m_layer.fill(Qt::transparent);
    QPainter p(&m_layer);
    paintLayer(p, plotRect());
}

void DiversityFilterPanel::paintSpectrum(QPainter& p, const QRectF& r)
{
    if (m_specAxisDb.isEmpty())
        return;
    if (m_specAreaDirty || m_specArea.isEmpty()) {
        m_specAreaDirty = false;
        m_specArea.clear();
        m_specArea.reserve(int(m_specAxisDb.size()) + 2);
        m_specArea << QPointF(xForHz(m_specHz.first()), r.bottom());
        for (int i = 0; i < int(m_specAxisDb.size()); ++i)
            m_specArea << QPointF(xForHz(m_specHz[i]), yForDb(m_specAxisDb[i]));
        m_specArea << QPointF(xForHz(m_specHz.last()), r.bottom());
    }

    // The area itself: the spectrum's own colour, not the trace's, because the
    // trace is the filter and this is the band.
    const QColor spectrum =
        ThemeManager::instance().color(this, QStringLiteral("color.spectrum.average"));
    QColor fill = spectrum;
    fill.setAlpha(52);
    QColor rim = spectrum;
    rim.setAlpha(150);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPolygon(m_specArea);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(rim, 1));
    p.drawPolyline(m_specArea.mid(1, int(m_specAxisDb.size())));
    p.setRenderHint(QPainter::Antialiasing, false);
}

void DiversityFilterPanel::paintEvent(QPaintEvent* ev)
{
    ++m_paintCount;
    if (m_layersDirty || m_layer.size() != (size() * devicePixelRatioF()))
        rebuildLayer();

    QPainter p(this);
    const QRect target = ev->rect();
    const qreal dpr = m_layer.devicePixelRatio();
    const QRect source(int(target.x() * dpr), int(target.y() * dpr),
                       int(target.width() * dpr), int(target.height() * dpr));
    p.setClipRect(target);
    p.fillRect(target, ThemeManager::instance().color(
                           this, QStringLiteral("color.background.spectrum")));
    const QRectF r = plotRect();
    const bool drawable = m_available && m_maxHz > m_minHz;
    if (drawable)
        paintSpectrum(p, r);
    p.drawPixmap(target, m_layer, source);
    if (!drawable)
        return;                       // the layer already says why

    // The two handles, the focused one heavier so the arrow keys have a visible
    // subject. Live, because they are what moves while the mouse is down.
    const QColor accent =
        ThemeManager::instance().color(this, QStringLiteral("color.accent"));
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

    // The notch being dragged, as a ghost: the mark it came from stays where
    // the gate put it, so one gesture shows both ends of the move.
    if (m_notchDrag >= 0 && !std::isnan(m_notchGhostHz)) {
        QColor ghost =
            ThemeManager::instance().color(this, QStringLiteral("color.accent.warning"));
        ghost.setAlpha(160);
        p.setPen(QPen(ghost, 2, Qt::DashLine));
        const double x = xForHz(m_notchGhostHz);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
}

} // namespace AetherSDR
