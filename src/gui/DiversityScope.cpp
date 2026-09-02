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

// The polar plot's fixed square side, and the vertical budget (two text
// lines + padding) it is sized to leave room for -- both from the 250px
// sidebar redesign's own numbers.
constexpr double kWeightPlotSide = 120.0;
constexpr double kWeightPlotReserve = 56.0;

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
    setFixedHeight(176);
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

    m_talking = d.value(QStringLiteral("talking")).toBool();

    const QJsonValue rn = d.value(QStringLiteral("rn_source"));
    m_haveRnSource = d.contains(QStringLiteral("rn_source")) && !rn.isNull() && !rn.isUndefined();
    if (m_haveRnSource)
        m_rnSource = rn.toString();

    // Inter-antenna coherence of the noise: near 0 means isotropic noise
    // (nothing to null), near 1 means one dominant local source.
    m_haveNoiseCoh = jsonNumber(d, "noise_coherence", &m_noiseCoh);

    m_updates = d.value(QStringLiteral("updates")).toInt();

    // Same isObject() guard applyDiversity() uses for "nb" -- a malformed
    // (non-object) value must not read as "0% blanked" via toObject()'s
    // silent {}.
    m_haveNb = d.contains(QStringLiteral("nb")) && d.value(QStringLiteral("nb")).isObject();
    if (m_haveNb) {
        const QJsonObject nb = d.value(QStringLiteral("nb")).toObject();
        m_nbBlankedPct = nb.value(QStringLiteral("blanked_pct")).toDouble();
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
    m_talking = false;
    m_haveRnSource = false;
    m_rnSource.clear();
    m_haveNoiseCoh = false;
    m_noiseCoh = 0.0;
    m_updates = 0;
    m_haveNb = false;
    m_nbBlankedPct = 0.0;
    m_memoryCount = 0;
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
    const double textRowsH = 32.0;   // two fixed-field lines, see paintTextLines()
    const double topH = std::max(0.0, h - textRowsH);

    const double squareSide =
        std::clamp(std::min(kWeightPlotSide, h - kWeightPlotReserve), 0.0, topH);
    const double squareY = (topH - squareSide) / 2.0;
    const QRectF weightRect(0, squareY, squareSide, squareSide);
    const QRectF barsRect(squareSide + 6.0, 0, std::max(0.0, w - squareSide - 6.0), topH);
    const QRectF textRect(0, topH, w, textRowsH);

    paintWeightPlot(p, weightRect);
    paintSnrBars(p, barsRect);
    paintTextLines(p, textRect);
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

    const double barH = 12.0;
    const double gap = 5.0;
    const double labelW = 22.0;
    const double valueW = 34.0;   // fixed-width numeric field, right of the bar
    const double top = rectArea.top() + 3.0;

    const auto drawBar = [&](int row, const QString& label, bool valid, double value,
                              const QColor& fillColor) {
        const double y = top + row * (barH + gap);
        p.setPen(secondary);
        p.drawText(QRectF(rectArea.left(), y, labelW, barH),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
        const double trackX = rectArea.left() + labelW;
        const double trackW = std::max(0.0, rectArea.width() - labelW - valueW);
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRect(QRectF(trackX, y, trackW, barH));
        if (valid) {
            const double t = std::clamp((value - kSnrLoDb) / (kSnrHiDb - kSnrLoDb), 0.0, 1.0);
            p.setBrush(fillColor);
            p.drawRect(QRectF(trackX, y, trackW * t, barH));
        }
        // Fixed-width value field: right-aligned in its own reserved
        // rectangle so a digit-count change never moves anything beside it.
        const QString valueText =
            valid ? QString::asprintf("%+5.1f", value) : QStringLiteral("    –");
        p.setPen(secondary);
        p.drawText(QRectF(trackX + trackW, y, valueW, barH),
                   Qt::AlignRight | Qt::AlignVCenter, valueText);
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

// "talk ●   moves 7   mem 6" -- what used to be scattered across the status
// line's ever-changing tail (talking, updates, memory count), now a single
// fixed-order line. "mem" rather than "remembered": it is the word that
// still fits without eliding at the 250px sidebar's default font once the
// three fields (talk marker, an updates count that can run to 4+ digits, a
// memory count of the same width) are all present at once.
QString DiversityScope::buildTopLine() const
{
    return QStringLiteral("talk %1   moves %2   mem %3")
        .arg(m_talking ? QStringLiteral("●") : QStringLiteral("○"))
        .arg(m_updates)
        .arg(m_memoryCount);
}

// "noise guard · coh 0.12   nb 0.1%" -- rn_source is gate-reported free text;
// "guard" and "inband" are the two values the gate is documented to send, so
// those map to the short words the design calls for and anything else (an
// older/newer gate's own wording) is shown verbatim rather than swallowed.
QString DiversityScope::buildBottomLine() const
{
    QString noiseWord;
    if (!m_haveRnSource)
        noiseWord = QStringLiteral("noise –");
    else if (m_rnSource == QLatin1String("guard"))
        noiseWord = QStringLiteral("noise guard");
    else if (m_rnSource == QLatin1String("inband"))
        noiseWord = QStringLiteral("noise in-band");
    else
        noiseWord = QStringLiteral("noise %1").arg(m_rnSource);

    const QString cohText =
        m_haveNoiseCoh ? QString::number(m_noiseCoh, 'f', 2) : QStringLiteral("–");
    const QString nbText = m_haveNb
        ? QStringLiteral("%1%").arg(m_nbBlankedPct, 0, 'f', 1)
        : QStringLiteral("–");

    return QStringLiteral("%1 · coh %2   nb %3").arg(noiseWord, cohText, nbText);
}

void DiversityScope::paintTextLines(QPainter& p, const QRectF& rectArea)
{
    auto& tm = ThemeManager::instance();
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(small);
    p.setPen(tm.color(this, QStringLiteral("color.text.secondary")));

    const QFontMetricsF fm(small);
    const double lineH = rectArea.height() / 2.0;
    const double availW = std::max(0.0, rectArea.width() - 6.0);

    const QString line1 = buildTopLine();
    const QString line2 = buildBottomLine();
    const QString elided1 = fm.elidedText(line1, Qt::ElideRight, availW);
    const QString elided2 = fm.elidedText(line2, Qt::ElideRight, availW);
    m_line1Elided = (elided1 != line1);
    m_line2Elided = (elided2 != line2);

    p.drawText(QRectF(rectArea.left() + 4.0, rectArea.top(), availW, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, elided1);
    p.drawText(QRectF(rectArea.left() + 4.0, rectArea.top() + lineH, availW, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, elided2);
}

} // namespace AetherSDR
