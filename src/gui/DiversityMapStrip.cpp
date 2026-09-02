#include "gui/DiversityMapStrip.h"

#include "core/ThemeManager.h"

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
} // namespace

DiversityMapStrip::DiversityMapStrip(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(24);
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
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversityMapStrip::setMap(const QJsonObject& map)
{
    m_coherence.clear();
    m_sources.clear();
    m_startHz = 0.0;
    m_stepHz = 0.0;
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
    }
    // An empty map (a v1 gate with no /diversity/map route, or one that has
    // not produced a map yet) hides the strip rather than reserving a blank
    // row in AetherGateApplet's form layout.
    setVisible(!m_coherence.isEmpty());
    update();
}

void DiversityMapStrip::paintEvent(QPaintEvent*)
{
    if (m_coherence.isEmpty())
        return;
    QPainter p(this);
    const int n = m_coherence.size();
    const double w = double(width()) / double(n);
    p.setPen(Qt::NoPen);
    p.setBrush(ThemeManager::instance().color(this, QStringLiteral("color.spectrum.trace")));
    for (int i = 0; i < n; ++i) {
        const double coh = std::clamp(double(m_coherence[i]), 0.0, 1.0);
        const int barH = int(std::lround(coh * height()));
        p.drawRect(QRectF(i * w, height() - barH, std::max(1.0, w - 1.0), double(barH)));
    }
    if (m_stepHz > 0.0) {
        p.setPen(ThemeManager::instance().color(this, QStringLiteral("color.text.secondary")));
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
            p.drawLine(x0, height() - 1, x1, height() - 1);
            p.drawLine(x0, height() - 5, x0, height() - 1);
            p.drawLine(x1, height() - 5, x1, height() - 1);
        }
    }
}

} // namespace AetherSDR
