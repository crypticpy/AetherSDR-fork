#include "gui/DiversityMapStrip.h"

#include "core/ThemeManager.h"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QRectF>
#include <QSizePolicy>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {
// /diversity/map is gate-reported, not bounded by us on the wire — cap
// defensively so a buggy (or malicious) gate cannot make this widget paint or
// allocate proportional to an arbitrarily large array.
constexpr int kMaxCoherencePoints = 4096;

// Axis mode's reserved strip under the bars: one line of small text.
constexpr int kAxisHeight = 14;

// The passband fill is a MARKER over live data, not a surface of its own —
// low enough alpha that a coherence bar under it is still readable.
constexpr double kPassbandFillAlpha = 0.16;
} // namespace

DiversityMapStrip::DiversityMapStrip(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(m_barHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Diversity noise map"));

    // The render paints through raw QPainter keyed off ThemeManager::color(),
    // so applyStyleSheet's reverse-map never sees these — declare them so an
    // Inspect-mode click surfaces the tokens actually read, and repaint on a
    // live theme switch (MiniPanScope.cpp's pattern for the same kind of
    // custom-painted widget).
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.spectrum.trace"),
        QStringLiteral("color.text.secondary"),
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.accent"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversityMapStrip::applyHeight()
{
    setFixedHeight(m_barHeight + (m_axisMode ? kAxisHeight : 0));
}

void DiversityMapStrip::setStripHeight(int px)
{
    m_barHeight = std::max(8, px);
    applyHeight();
}

void DiversityMapStrip::setAxisMode(bool on)
{
    if (m_axisMode == on)
        return;
    m_axisMode = on;
    applyHeight();
    // In axis mode the strip is a fixed part of the noise panel's frame, so
    // it stays put whether or not a map has arrived — a widget that appears
    // and disappears with the data is exactly the reflow the window is built
    // to avoid.
    if (m_axisMode)
        setVisible(true);
    update();
}

void DiversityMapStrip::setMap(const QJsonObject& map)
{
    m_coherence.clear();
    m_sources.clear();
    m_startHz = 0.0;
    m_stepHz = 0.0;
    m_havePassband = false;
    m_passbandLoHz = 0.0;
    m_passbandHiHz = 0.0;
    if (!map.contains(QStringLiteral("error"))) {
        m_startHz = map.value(QStringLiteral("start_hz")).toDouble();
        m_stepHz = map.value(QStringLiteral("step_hz")).toDouble();
        for (const QJsonValue& v : map.value(QStringLiteral("coherence")).toArray()) {
            if (m_coherence.size() >= kMaxCoherencePoints)
                break;
            m_coherence << float(v.toDouble());
        }
        for (const QJsonValue& v : map.value(QStringLiteral("sources")).toArray()) {
            const QJsonObject so = v.toObject();
            m_sources.push_back({so.value(QStringLiteral("lo_hz")).toDouble(),
                                 so.value(QStringLiteral("hi_hz")).toDouble()});
        }
        // "passband_hz": [lo, hi], null on a gate that has not got one, and
        // missing entirely on a gate that predates the key. All three mean
        // "draw no marker" — never a marker at 0 Hz.
        const QJsonValue pb = map.value(QStringLiteral("passband_hz"));
        if (pb.isArray()) {
            const QJsonArray pba = pb.toArray();
            if (pba.size() == 2 && !pba[0].isNull() && !pba[1].isNull()) {
                const double lo = pba[0].toDouble();
                const double hi = pba[1].toDouble();
                if (hi > lo) {
                    m_havePassband = true;
                    m_passbandLoHz = lo;
                    m_passbandHiHz = hi;
                }
            }
        }
    }
    // An empty map (a v1 gate with no /diversity/map route, or one that has
    // not produced a map yet) hides the strip rather than reserving a blank
    // row in AetherGateApplet's form layout. Axis mode keeps its place — see
    // setAxisMode().
    if (!m_axisMode)
        setVisible(!m_coherence.isEmpty());
    update();
}

void DiversityMapStrip::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    const int barsH = m_axisMode ? std::max(1, height() - kAxisHeight) : height();

    if (m_axisMode) {
        p.fillRect(QRect(0, 0, width(), barsH),
                   tm.color(this, QStringLiteral("color.background.spectrum")));
    }
    if (m_coherence.isEmpty())
        return;

    const int n = m_coherence.size();
    const double w = double(width()) / double(n);
    p.setPen(Qt::NoPen);
    p.setBrush(tm.color(this, QStringLiteral("color.spectrum.trace")));
    for (int i = 0; i < n; ++i) {
        const double coh = std::clamp(double(m_coherence[i]), 0.0, 1.0);
        const int barH = int(std::lround(coh * barsH));
        p.drawRect(QRectF(i * w, barsH - barH, std::max(1.0, w - 1.0), double(barH)));
    }
    if (m_stepHz > 0.0) {
        p.setPen(tm.color(this, QStringLiteral("color.text.secondary")));
        // Clamped before the cast to int: a source bracket outside the map's
        // own [start_hz, start_hz + n*step_hz) window (e.g. one the gate
        // reports slightly past the map's edge) must not overflow into a
        // wild x that QPainter draws across the whole widget tree's clip.
        const double xLo = -1.0;
        const double xHi = double(width()) + 1.0;
        for (const auto& src : m_sources) {
            const double x0d = std::clamp((src.first - m_startHz) / m_stepHz * w, xLo, xHi);
            const double x1d = std::clamp((src.second - m_startHz) / m_stepHz * w, xLo, xHi);
            const int x0 = int(std::lround(x0d));
            const int x1 = int(std::lround(x1d));
            p.drawLine(x0, barsH - 1, x1, barsH - 1);
            p.drawLine(x0, barsH - 5, x0, barsH - 1);
            p.drawLine(x1, barsH - 5, x1, barsH - 1);
        }
    }

    if (!m_axisMode)
        return;

    // --- window-only furniture -------------------------------------------
    const double spanHz = m_stepHz * double(n);
    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));

    if (m_havePassband && m_stepHz > 0.0) {
        const double xLo = std::clamp((m_passbandLoHz - m_startHz) / m_stepHz * w,
                                      0.0, double(width()));
        const double xHi = std::clamp((m_passbandHiHz - m_startHz) / m_stepHz * w,
                                      0.0, double(width()));
        QColor accent = tm.color(this, QStringLiteral("color.accent"));
        QColor fill = accent;
        fill.setAlphaF(kPassbandFillAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRect(QRectF(xLo, 0.0, std::max(1.0, xHi - xLo), double(barsH)));
        p.setPen(QPen(accent, 1));
        p.drawLine(QPointF(xLo, 0.0), QPointF(xLo, double(barsH)));
        p.drawLine(QPointF(xHi, 0.0), QPointF(xHi, double(barsH)));
    }

    if (m_stepHz <= 0.0)
        return;
    QFont axisFont = font();
    axisFont.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(axisFont);
    const QFontMetricsF fm(axisFont);
    p.setPen(secondary);
    const double y = double(height()) - 2.0;
    const auto mhz = [](double hz) {
        return QString::number(hz / 1e6, 'f', 3);
    };
    const QString leftText = mhz(m_startHz);
    const QString midText = mhz(m_startHz + spanHz / 2.0);
    const QString rightText = mhz(m_startHz + spanHz);
    p.drawText(QPointF(0.0, y), leftText);
    p.drawText(QPointF(double(width()) / 2.0 - fm.horizontalAdvance(midText) / 2.0, y),
               midText);
    p.drawText(QPointF(double(width()) - fm.horizontalAdvance(rightText), y), rightText);
}

} // namespace AetherSDR
