#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>

// SQUEEZE (B24) -- the operator's own null or notch on one signal, or one
// comb, out of Aether-gate's `squeeze` block on /filter (see B24-SQUEEZE.md
// / the gate's core/squeeze.py). Split from DiversityFilterPanel.cpp for the
// same reason the picture itself is split from it into
// DiversityFilterPanelPaint.cpp: AGENTS.md's 800-line budget, and this is
// genuinely its own story -- parsing one JSON block, drawing what it says,
// and answering "is this pixel on it".
//
// THE Hz-AXIS CONVERSION THIS FILE EXISTS TO DO. Every OTHER mark this
// widget draws -- low_hz/high_hz, a notch, an ANF tone, contour_hz, apf_hz,
// auto's edges -- arrives from the gate already on this panel's own axis:
// always positive AUDIO Hz, because the gate abs-ifies each of them before
// ever answering /filter (core/filter.py's own status(), `abs(sp.low_hz)`
// and friends) -- see DiversityFilterPanel.h's header comment for why LSB
// makes that necessary. SQUEEZE's own `hz` (and a comb's `teeth_in_band`)
// are NOT abs-ified: they are reported exactly as core/squeeze.py's Squeeze
// keeps them, "an offset from the slice centre... signed, hertz", the same
// frame the demodulated passband's own edges are in internally
// (adapters/diversity_state.py's `_pass_edges`: 0..+2900 on USB, -2900..0
// on LSB) before the gate flips LSB's sign for display.
//
// So this panel does the same flip the gate does for everything else,
// itself, right here: the axis a mark is DRAWN or HIT-TESTED at is always
// abs(the signed value), and the SIGN a Shift+click's write goes out with is
// decided from the filter's own "mode" (LSB negative, everything else --
// USB, CW, the digital modes -- positive, matching _pass_edges). Get this
// wrong and a squeeze placed by clicking a signal on an LSB slice lands on
// the MIRROR frequency instead.
namespace AetherSDR {

namespace {

// Matches kGrabPx in DiversityFilterPanel.cpp -- kept as its own constant
// here rather than shared across the translation unit boundary because it is
// six pixels either way and nothing about SQUEEZE needs it to move in step
// with the notch/handle grab distance if that one ever does.
constexpr double kSqueezeGrabPx = 6.0;
// Matches kSnapHz in DiversityFilterPanel.cpp -- SQUEEZE's own placement is
// a multiple of ten for the same reason a passband edge is: nobody wants a
// null asked for to the Hz, and it is what makes the write's query string
// exact and testable.
constexpr int kSqueezeSnapHz = 10;

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

} // namespace

void DiversityFilterPanel::resetSqueeze()
{
    m_squeezeActive = false;
    m_squeezeHeld = false;
    m_squeezeTarget.clear();
    m_squeezeTool.clear();
    m_squeezeReason.clear();
    m_squeezeWhy.clear();
    m_squeezeHz = std::numeric_limits<double>::quiet_NaN();
    m_squeezeWidthHz = std::numeric_limits<double>::quiet_NaN();
    m_squeezeDepthDb = std::numeric_limits<double>::quiet_NaN();
    m_squeezeCombSpacingHz = std::numeric_limits<double>::quiet_NaN();
    m_squeezeCombTeethSeen = 0;
    m_squeezeTeethHz.clear();
}

// One /filter object's "squeeze" block (and its "mode", for the sign flip
// every future click needs). Called from applyStatus() before the two
// fingerprints are re-taken, so a squeeze that moved is a filter-layer
// change like any other.
void DiversityFilterPanel::parseSqueeze(const QJsonObject& filter)
{
    const QString mode = filter.value(QStringLiteral("mode")).toString();
    if (!mode.isEmpty())
        m_squeezeLsb = mode.startsWith(QLatin1String("LSB"), Qt::CaseInsensitive);

    resetSqueeze();
    const QJsonValue sqv = filter.value(QStringLiteral("squeeze"));
    if (!sqv.isObject())
        return;
    const QJsonObject sq = sqv.toObject();

    // "since" is the gate's own "a target is configured" flag: a float once
    // set_squeeze()/set_comb() has run, null after off(). held/armed/off are
    // told apart from this and "held" alone -- see squeezeArmed()'s comment.
    m_squeezeActive = sq.value(QStringLiteral("since")).isDouble();
    m_squeezeHeld = sq.value(QStringLiteral("held")).toBool();
    m_squeezeTarget = sq.value(QStringLiteral("target")).toString();
    if (sq.value(QStringLiteral("tool")).isString())
        m_squeezeTool = sq.value(QStringLiteral("tool")).toString();
    if (sq.value(QStringLiteral("reason")).isString())
        m_squeezeReason = sq.value(QStringLiteral("reason")).toString();
    if (sq.value(QStringLiteral("why")).isString())
        m_squeezeWhy = sq.value(QStringLiteral("why")).toString();

    double v = 0.0;
    if (jsonNumber(sq, "hz", &v))
        m_squeezeHz = v;
    if (jsonNumber(sq, "width_hz", &v))
        m_squeezeWidthHz = v;
    if (jsonNumber(sq, "depth_db", &v))
        m_squeezeDepthDb = v;

    const QJsonValue combv = sq.value(QStringLiteral("comb"));
    if (!combv.isObject())
        return;
    const QJsonObject comb = combv.toObject();
    if (jsonNumber(comb, "spacing_hz", &v))
        m_squeezeCombSpacingHz = v;
    m_squeezeCombTeethSeen = comb.value(QStringLiteral("teeth_seen")).toInt();
    const QJsonArray teeth = comb.value(QStringLiteral("teeth_in_band")).toArray();
    m_squeezeTeethHz.reserve(teeth.size());
    for (const QJsonValue& t : teeth)
        m_squeezeTeethHz.append(t.toDouble());
}

// The bracket's own two edges, on this panel's positive axis: abs() of each
// slice-relative edge, min/max rather than assuming order survives the flip
// (it always does in practice -- width_hz is positive and hz's magnitude is
// always well over half of it, or the target would already read "outside
// the passband" -- but min/max costs nothing and does not assume it).
double DiversityFilterPanel::squeezeBracketLowHz() const
{
    if (m_squeezeTarget != QLatin1String("signal") || std::isnan(m_squeezeHz))
        return std::numeric_limits<double>::quiet_NaN();
    const double half = std::isnan(m_squeezeWidthHz) ? 0.0 : m_squeezeWidthHz / 2.0;
    return std::min(std::abs(m_squeezeHz - half), std::abs(m_squeezeHz + half));
}

double DiversityFilterPanel::squeezeBracketHighHz() const
{
    if (m_squeezeTarget != QLatin1String("signal") || std::isnan(m_squeezeHz))
        return std::numeric_limits<double>::quiet_NaN();
    const double half = std::isnan(m_squeezeWidthHz) ? 0.0 : m_squeezeWidthHz / 2.0;
    return std::max(std::abs(m_squeezeHz - half), std::abs(m_squeezeHz + half));
}

double DiversityFilterPanel::squeezeToothHzAt(int index) const
{
    if (index < 0 || index >= int(m_squeezeTeethHz.size()))
        return std::numeric_limits<double>::quiet_NaN();
    return std::abs(m_squeezeTeethHz[index]);
}

// Is (x) on the bracket, or on one of the comb's teeth. Full plot height,
// like the ANF/AUTO marks it is asked alongside -- there is no "along the
// bottom only" reading of a null or a notch the way there is for the
// contour/APF ticks.
bool DiversityFilterPanel::squeezeHit(double x) const
{
    if (!m_squeezeHeld)
        return false;
    if (m_squeezeTarget == QLatin1String("comb")) {
        for (double hz : m_squeezeTeethHz) {
            if (std::abs(x - xForHz(std::abs(hz))) <= kSqueezeGrabPx)
                return true;
        }
        return false;
    }
    const double lo = squeezeBracketLowHz();
    const double hi = squeezeBracketHighHz();
    if (std::isnan(lo) || std::isnan(hi))
        return false;
    const double loX = xForHz(lo);
    const double hiX = xForHz(hi);
    return x >= loX - kSqueezeGrabPx && x <= hiX + kSqueezeGrabPx;
}

// The inverse of the abs() every drawn/hit-tested squeeze Hz goes through:
// a click's own panel-axis Hz (always positive), snapped to ten the same
// way a notch or an edge is, signed by the mode the last /filter said this
// slice was in (LSB negative, everything else -- USB, CW, the digital modes
// -- positive; see this file's header comment).
double DiversityFilterPanel::squeezeHzForClick(double x) const
{
    const double panelHz = hzForX(x);
    const double snapped = double(std::lround(panelHz / double(kSqueezeSnapHz))) * kSqueezeSnapHz;
    return m_squeezeLsb ? -snapped : snapped;
}

// Drawn into the cached filter layer, right after the manual notches: one
// bracket for a signal target, one thin tooth per line for a comb. Reuses
// the ANF tones' own colour token (this widget adds no new colour literal --
// see the colour ratchet in AGENTS.md) with dash patterns and a fill neither
// ANF nor AUTO use, so the three families of "something is marked here" stay
// visually distinct without a fourth token.
void DiversityFilterPanel::paintSqueeze(QPainter& p, const QRectF& r) const
{
    if (!m_squeezeHeld)
        return;
    ThemeManager& tm = ThemeManager::instance();
    const QColor base = tm.color(this, QStringLiteral("color.spectrum.average"));

    const QString toolWord = m_squeezeTool == QLatin1String("null")   ? tr("NULL")
                             : m_squeezeTool == QLatin1String("notch") ? tr("NOTCH")
                                                                       : QString();
    const QString depthWord =
        std::isnan(m_squeezeDepthDb)
            ? QString()
            : QStringLiteral(" %1%2 dB")
                  .arg(m_squeezeDepthDb < 0.0 ? QStringLiteral("−") : QString(),
                       QString::number(std::abs(m_squeezeDepthDb), 'f', 1));
    const QString label = QStringLiteral("squeeze %1%2").arg(toolWord, depthWord);

    if (m_squeezeTarget == QLatin1String("comb")) {
        QColor tooth = base;
        tooth.setAlpha(210);
        p.setPen(QPen(tooth, 1, Qt::DotLine));
        double firstX = -1.0;
        for (double hz : m_squeezeTeethHz) {
            const double x = xForHz(std::abs(hz));
            if (firstX < 0.0)
                firstX = x;
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        if (firstX >= 0.0) {
            p.setPen(base);
            p.drawText(QRectF(firstX + 3, r.top() + 2, 150, 12), Qt::AlignLeft | Qt::AlignTop,
                       label);
        }
        return;
    }
    if (m_squeezeTarget != QLatin1String("signal"))
        return;
    const double loHz = squeezeBracketLowHz();
    const double hiHz = squeezeBracketHighHz();
    if (std::isnan(loHz) || std::isnan(hiHz))
        return;
    const double loX = xForHz(loHz);
    const double hiX = xForHz(hiHz);

    QColor tint = base;
    tint.setAlpha(40);
    p.fillRect(QRectF(loX, r.top(), std::max(1.0, hiX - loX), r.height()), tint);

    QColor edge = base;
    edge.setAlpha(210);
    p.setPen(QPen(edge, 1, Qt::DashDotDotLine));
    p.drawLine(QPointF(loX, r.top()), QPointF(loX, r.bottom()));
    p.drawLine(QPointF(hiX, r.top()), QPointF(hiX, r.bottom()));
    // The bracket's own cap, close under the top gridline -- what makes the
    // two verticals read as ONE mark rather than two unrelated ones.
    p.drawLine(QPointF(loX, r.top() + 3), QPointF(hiX, r.top() + 3));

    p.setPen(base);
    p.drawText(QRectF(loX + 2, r.top() + 6, 150, 12), Qt::AlignLeft | Qt::AlignTop, label);
}

} // namespace AetherSDR
