#include "gui/DiversityScope.h"

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
#include <limits>

namespace AetherSDR {

namespace {
// "the last ~30 samples" -- item 2's polar trail.
constexpr int kTrailLength = 30;

// The polar plot's fixed dB window: item 2 says ratio_db maps -20..+20 dB
// onto 0..R, same range the ratio spin itself allows.
constexpr double kRatioLoDb = -20.0;
constexpr double kRatioHiDb = 20.0;

// The SNR bars' fixed scale, independent of what the legs happen to read --
// a fixed scale is the whole point: a bar's LENGTH must mean the same dB on
// every poll, or it is just another thing quietly moving.
constexpr double kSnrLoDb = -10.0;
constexpr double kSnrHiDb = 30.0;

// A gain worth having, for the OUT bar's accent-vs-secondary colour switch.
constexpr double kGainGoodDb = 1.0;

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    if (!obj.contains(QLatin1String(key)))
        return false;
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isNull() || v.isUndefined())
        return false;
    *out = v.toDouble();
    return true;
}

} // namespace

DiversityScope::DiversityScope(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setObjectName(QStringLiteral("gateDiversityScope"));
    setAccessibleName(tr("Diversity scope"));
    setAccessibleDescription(
        tr("Live diversity weight, A/B/OUT signal-to-noise, and combiner "
           "status. Read-only -- nothing here can be edited."));

    // Raw QPainter keyed off ThemeManager::color(), same pattern
    // DiversityMapStrip/MiniPanScope already use for a custom-painted widget
    // in this codebase: declare the tokens actually read so Inspect mode
    // surfaces them, and repaint on a live theme switch.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.spectrum.trace"),
        QStringLiteral("color.spectrum.grid"),
        QStringLiteral("color.accent"),
        QStringLiteral("color.text.secondary"),
        QStringLiteral("color.text.primary"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversityScope::setState(const QJsonObject& d)
{
    double phase = 0.0, ratio = 0.0;
    m_haveWeight = jsonNumber(d, "phase_deg", &phase) && jsonNumber(d, "ratio_db", &ratio);
    if (m_haveWeight) {
        m_phaseDeg = phase;
        m_ratioDb = ratio;
        m_trail.push_back({m_phaseDeg, m_ratioDb});
        while (m_trail.size() > kTrailLength)
            m_trail.pop_front();
    }

    m_memory.clear();
    const QJsonArray memoryArray = d.value(QStringLiteral("memory")).toArray();
    for (const QJsonValue& v : memoryArray) {
        const QJsonObject mo = v.toObject();
        double mPhase = 0.0, mRatio = 0.0;
        if (jsonNumber(mo, "phase_deg", &mPhase) && jsonNumber(mo, "ratio_db", &mRatio))
            m_memory.push_back({mPhase, mRatio});
    }
    m_memoryCount = memoryArray.size();

    const QJsonObject snr = d.value(QStringLiteral("snr_db")).toObject();
    m_snrAValid = jsonNumber(snr, "a", &m_snrA);
    m_snrBValid = jsonNumber(snr, "b", &m_snrB);
    m_snrOutValid = jsonNumber(snr, "out", &m_snrOut);
    m_gainDb = (m_snrAValid && m_snrBValid && m_snrOutValid)
        ? m_snrOut - std::max(m_snrA, m_snrB)
        : std::numeric_limits<double>::quiet_NaN();

    if (d.contains(QStringLiteral("mode")))
        m_mode = d.value(QStringLiteral("mode")).toString();
    m_talking = d.value(QStringLiteral("talking")).toBool();

    const QJsonValue talkMod = d.value(QStringLiteral("talk_mod"));
    m_haveTalkMod = d.contains(QStringLiteral("talk_mod"))
        && !talkMod.isNull() && !talkMod.isUndefined();
    if (m_haveTalkMod)
        m_talkMod = talkMod.toDouble();

    const QJsonValue rn = d.value(QStringLiteral("rn_source"));
    m_haveRnSource = d.contains(QStringLiteral("rn_source")) && !rn.isNull() && !rn.isUndefined();
    if (m_haveRnSource)
        m_rnSource = rn.toString();

    // Inter-antenna coherence of the noise: near 0 means isotropic noise
    // (nothing to null), near 1 means one dominant local source.
    m_haveNoiseCoh = jsonNumber(d, "noise_coherence", &m_noiseCoh);

    m_updates = d.value(QStringLiteral("updates")).toInt();
    m_aligned = d.value(QStringLiteral("aligned")).toBool();
    m_lagSamples = d.value(QStringLiteral("lag_samples")).toInt();
    m_corrPeak = d.value(QStringLiteral("corr_peak")).toDouble();

    // Same isObject() guard applyDiversity() uses for "nb" -- a malformed
    // (non-object) value must not read as "0% blanked" via toObject()'s
    // silent {}.
    m_haveNb = d.contains(QStringLiteral("nb")) && d.value(QStringLiteral("nb")).isObject();
    if (m_haveNb) {
        const QJsonObject nb = d.value(QStringLiteral("nb")).toObject();
        m_nbBlankedPct = nb.value(QStringLiteral("blanked_pct")).toDouble();
    }

    m_haveCapture = d.contains(QStringLiteral("capture"))
        && d.value(QStringLiteral("capture")).isObject();
    if (m_haveCapture) {
        const QJsonObject capture = d.value(QStringLiteral("capture")).toObject();
        m_captureActive = capture.value(QStringLiteral("active")).toBool();
    }

    update();
}

void DiversityScope::clear()
{
    m_trail.clear();
    m_memory.clear();
    m_haveWeight = false;
    m_phaseDeg = 0.0;
    m_ratioDb = 0.0;
    m_snrAValid = m_snrBValid = m_snrOutValid = false;
    m_snrA = m_snrB = m_snrOut = 0.0;
    m_gainDb = std::numeric_limits<double>::quiet_NaN();
    m_mode.clear();
    m_talking = false;
    m_haveTalkMod = false;
    m_talkMod = 0.0;
    m_haveRnSource = false;
    m_rnSource.clear();
    m_haveNoiseCoh = false;
    m_noiseCoh = 0.0;
    m_updates = 0;
    m_aligned = false;
    m_lagSamples = 0;
    m_corrPeak = 0.0;
    m_haveNb = false;
    m_nbBlankedPct = 0.0;
    m_memoryCount = 0;
    m_haveCapture = false;
    m_captureActive = false;
    update();
}

void DiversityScope::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));

    const double w = width();
    const double h = height();
    const double textRowH = 18.0;
    const double topH = std::max(0.0, h - textRowH);

    const QRectF weightRect(0, 0, w * 0.40, topH);
    const QRectF barsRect(weightRect.right() + 6.0, 0, w - weightRect.width() - 6.0, topH);
    const QRectF textRect(0, topH, w, textRowH);

    paintWeightPlot(p, weightRect);
    paintSnrBars(p, barsRect);
    paintTextRow(p, textRect);
}

void DiversityScope::paintWeightPlot(QPainter& p, const QRectF& rectArea) const
{
    auto& tm = ThemeManager::instance();
    const QPointF center = rectArea.center();
    const double R = std::max(4.0, std::min(rectArea.width(), rectArea.height()) / 2.0 - 8.0);

    // Grid: outer ring at the +20 dB edge, inner ring at 0 dB -- a fixed
    // frame of reference the dot and trail move inside of, not something
    // that itself changes with the data.
    p.setPen(QPen(tm.color(this, QStringLiteral("color.spectrum.grid")), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, R, R);
    p.drawEllipse(center, R * 0.5, R * 0.5);

    const auto weightPoint = [&](double phaseDeg, double ratioDb) {
        const double t = std::clamp((ratioDb - kRatioLoDb) / (kRatioHiDb - kRatioLoDb), 0.0, 1.0);
        const double r = t * R;
        const double theta = phaseDeg * M_PI / 180.0;
        return center + QPointF(r * std::cos(theta), -r * std::sin(theta));
    };

    // Trail: oldest (most transparent) to newest, a glance at where the
    // solve has been converging rather than just where it is right now.
    const QColor traceColor = tm.color(this, QStringLiteral("color.spectrum.trace"));
    p.setPen(Qt::NoPen);
    for (int i = 0; i < m_trail.size(); ++i) {
        const double age = double(m_trail.size() - 1 - i);
        const double alpha = std::clamp(1.0 - age / double(kTrailLength), 0.08, 1.0);
        QColor c = traceColor;
        c.setAlphaF(alpha * 0.55);
        p.setBrush(c);
        p.drawEllipse(weightPoint(m_trail[i].phaseDeg, m_trail[i].ratioDb), 2.0, 2.0);
    }

    // Memory: hollow markers -- prior talkers the gate remembered a weight
    // for, distinct from the live trail by having no fill.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(tm.color(this, QStringLiteral("color.text.secondary")), 1));
    for (const WeightSample& m : m_memory)
        p.drawEllipse(weightPoint(m.phaseDeg, m.ratioDb), 3.0, 3.0);

    // Current weight: the one filled, accented dot.
    if (m_haveWeight) {
        p.setPen(Qt::NoPen);
        p.setBrush(tm.color(this, QStringLiteral("color.accent")));
        p.drawEllipse(weightPoint(m_phaseDeg, m_ratioDb), 4.0, 4.0);
    }
}

void DiversityScope::paintSnrBars(QPainter& p, const QRectF& rectArea) const
{
    auto& tm = ThemeManager::instance();
    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));
    const QColor accent = tm.color(this, QStringLiteral("color.accent"));
    const QColor track = tm.color(this, QStringLiteral("color.spectrum.grid"));
    const bool gainGood = !std::isnan(m_gainDb) && m_gainDb >= kGainGoodDb;

    QFont barFont = p.font();
    barFont.setPointSizeF(8.0);
    p.setFont(barFont);

    const double barH = 14.0;
    const double gap = 6.0;
    const double labelW = 26.0;
    const double top = rectArea.top() + 4.0;

    const auto drawBar = [&](int row, const QString& label, bool valid, double value,
                              const QColor& fillColor) {
        const double y = top + row * (barH + gap);
        p.setPen(secondary);
        p.drawText(QRectF(rectArea.left(), y, labelW, barH),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
        const double trackX = rectArea.left() + labelW;
        const double trackW = std::max(0.0, rectArea.width() - labelW);
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRect(QRectF(trackX, y, trackW, barH));
        if (valid) {
            const double t = std::clamp((value - kSnrLoDb) / (kSnrHiDb - kSnrLoDb), 0.0, 1.0);
            p.setBrush(fillColor);
            p.drawRect(QRectF(trackX, y, trackW * t, barH));
        }
    };

    drawBar(0, QStringLiteral("A"), m_snrAValid, m_snrA, secondary);
    drawBar(1, QStringLiteral("B"), m_snrBValid, m_snrB, secondary);
    drawBar(2, QStringLiteral("OUT"), m_snrOutValid, m_snrOut, gainGood ? accent : secondary);

    const double gainY = top + 3 * (barH + gap);
    const QString gainText = std::isnan(m_gainDb)
        ? tr("gain —")
        : tr("gain %1%2 dB")
              .arg(m_gainDb >= 0.0 ? QStringLiteral("+") : QString())
              .arg(m_gainDb, 0, 'f', 1);
    p.setPen(gainGood ? accent : secondary);
    p.drawText(QRectF(rectArea.left(), gainY, rectArea.width(), barH),
               Qt::AlignLeft | Qt::AlignVCenter, gainText);
}

QString DiversityScope::buildTextRow() const
{
    QString row;
    row += m_mode.isEmpty() ? QStringLiteral("mode –") : QStringLiteral("mode %1").arg(m_mode);
    row += QStringLiteral("  talk ") + (m_talking ? QStringLiteral("●") : QStringLiteral("○"));
    row += QStringLiteral(" mod ")
        + (m_haveTalkMod ? QString::number(m_talkMod, 'f', 2) : QStringLiteral("–"));
    row += QStringLiteral("  rn ") + (m_haveRnSource ? m_rnSource : QStringLiteral("–"));
    row += QStringLiteral(" coh ")
        + (m_haveNoiseCoh ? QString::number(m_noiseCoh, 'f', 2) : QStringLiteral("–"));
    row += QStringLiteral("  upd %1").arg(m_updates);
    row += QStringLiteral("  ") + (m_aligned ? tr("aligned") : tr("not aligned"));
    row += QStringLiteral(" lag %1 pk %2").arg(m_lagSamples).arg(m_corrPeak, 0, 'f', 2);
    row += QStringLiteral("  nb ")
        + (m_haveNb ? QStringLiteral("%1%").arg(m_nbBlankedPct, 0, 'f', 1) : QStringLiteral("–"));
    row += QStringLiteral("  mem %1").arg(m_memoryCount);
    row += QStringLiteral("  cap ")
        + (m_haveCapture ? (m_captureActive ? QStringLiteral("●") : QStringLiteral("○"))
                         : QStringLiteral("–"));
    return row;
}

void DiversityScope::paintTextRow(QPainter& p, const QRectF& rectArea) const
{
    auto& tm = ThemeManager::instance();
    QFont mono(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSizeF(8.5);
    p.setFont(mono);
    p.setPen(tm.color(this, QStringLiteral("color.text.secondary")));

    const QFontMetricsF fm(mono);
    const QString elided = fm.elidedText(buildTextRow(), Qt::ElideRight, rectArea.width() - 6.0);
    p.drawText(rectArea.adjusted(4.0, 0, -2.0, 0), Qt::AlignLeft | Qt::AlignVCenter, elided);
}

} // namespace AetherSDR
