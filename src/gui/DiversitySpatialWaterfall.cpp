#include "gui/DiversitySpatialWaterfall.h"

#include "core/ThemeManager.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizePolicy>
#include <QStringList>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace AetherSDR {

namespace {

// How much history the picture keeps. At the 4 Hz the gate is polled for this
// route that is about 75 seconds -- long enough for an over to be a streak
// rather than a dot, short enough that the memmove per append is nothing.
constexpr int kHistoryRows = 300;

// /diversity/spatial is gate-reported and not bounded by us on the wire -- cap
// defensively so a buggy (or malicious) gate cannot make this widget allocate
// or paint proportional to an arbitrarily large array. Same guard
// DiversityMapStrip keeps on its own coherence array.
constexpr int kMaxPoints = 4096;

// Brightness window, in dB below the brightest bin of the SAME row. Relative
// to the row rather than to an absolute dBFS floor so the picture survives a
// gain change: what it claims is "loud FOR THIS ROW", which is the claim a
// waterfall can actually support.
constexpr double kLevelWindowDb = 50.0;

// The frequency axis under the picture: one line of small text.
constexpr int kAxisHeight = 15;

// The page gives this the full width; the height is the operator's, but never
// so short that a streak cannot be told from a dot.
constexpr int kMinHeight = 260;

// Number of labelled ticks on the frequency axis, ends included.
constexpr int kAxisTicks = 5;

// The passband bracket is a MARKER over live data, not a surface of its own.
constexpr double kPassbandFillAlpha = 0.14;

// A row whose level leg the gate did not send still carries phase and
// coherence, and blacking it out would hide the two numbers we DO have. It is
// drawn at full value with the level tooltip saying "—" -- the honest split
// between "not measured" (the readout) and "not drawable" (the pixel).
constexpr double kValueWithoutLevel = 0.9;

// Reads one gate array into `out`, clamped to kMaxPoints. Returns false when
// the key is absent or is not an array: a leg nobody sent is not a leg of
// zeros.
bool readFloats(const QJsonObject& obj, const char* key, QVector<float>* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isArray())
        return false;
    const QJsonArray arr = v.toArray();
    out->clear();
    out->reserve(std::min(int(arr.size()), kMaxPoints));
    for (const QJsonValue& item : arr) {
        if (out->size() >= kMaxPoints)
            break;
        out->push_back(item.isDouble() ? float(item.toDouble())
                                       : std::numeric_limits<float>::quiet_NaN());
    }
    return true;
}

QString emDash()
{
    return QStringLiteral("—");
}

} // namespace

DiversitySpatialWaterfall::DiversitySpatialWaterfall(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowSpatialWaterfall"));
    setMinimumHeight(kMinHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Spatial waterfall"));
    setAccessibleDescription(
        tr("The gate's whole span over time. Colour is the direction a signal "
           "arrives from (the phase between the two loops), how saturated it "
           "is says how coherent the two loops are there, and how bright it is "
           "says how strong it is. Click a column to tune to it."));
    setToolTip(tr("Every bin of the gate's span, one row per poll. Two "
                  "stations from different directions are different colours; "
                  "one local noise source is a single flat colour across "
                  "everything it touches; sky noise has no direction and goes "
                  "grey. Click a column to tune there."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these -- declare the tokens actually read so an
    // Inspect-mode click surfaces them, and repaint on a live theme switch
    // (the pattern DiversityMapStrip and DiversityTimeline already follow).
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.text.secondary"),
        QStringLiteral("color.accent"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

int DiversitySpatialWaterfall::waterfallHeight() const
{
    return std::max(1, height() - kAxisHeight);
}

void DiversitySpatialWaterfall::resetHistory(int points)
{
    m_points = std::clamp(points, 0, kMaxPoints);
    m_rows = 0;
    if (m_points <= 0) {
        m_history = QImage();
        return;
    }
    m_history = QImage(m_points, kHistoryRows, QImage::Format_RGB32);
    m_history.fill(Qt::black);
}

void DiversitySpatialWaterfall::clear()
{
    m_available = false;
    m_startHz = 0.0;
    m_stepHz = 0.0;
    m_havePassband = false;
    m_passbandLoHz = 0.0;
    m_passbandHiHz = 0.0;
    m_phaseDeg.clear();
    m_coherence.clear();
    m_levelDb.clear();
    m_haveLevel.clear();
    resetHistory(0);
    update();
}

void DiversitySpatialWaterfall::setSpatial(const QJsonObject& spatial)
{
    m_available = spatial.value(QStringLiteral("available")).toBool()
                  && !spatial.contains(QStringLiteral("error"));
    if (!m_available) {
        // No row, but no amnesia either: the history already drawn is still
        // true, it is just not being added to.
        update();
        return;
    }

    QVector<float> phase;
    QVector<float> coherence;
    QVector<float> level;
    const bool havePhase = readFloats(spatial, "phase_deg", &phase);
    const bool haveCoherence = readFloats(spatial, "coherence", &coherence);
    const bool haveLevel = readFloats(spatial, "level_db", &level);

    // "points" is the gate's own count; the arrays are the truth. Take the
    // shortest non-empty one so a truncated leg cannot walk off the end.
    int n = spatial.value(QStringLiteral("points")).toInt(0);
    n = std::clamp(n, 0, kMaxPoints);
    for (const QVector<float>* leg : {&phase, &coherence, &level}) {
        if (!leg->isEmpty())
            n = (n > 0) ? std::min(n, int(leg->size())) : int(leg->size());
    }
    if (n <= 0) {
        m_available = false;
        update();
        return;
    }

    m_startHz = spatial.value(QStringLiteral("start_hz")).toDouble();
    m_stepHz = spatial.value(QStringLiteral("step_hz")).toDouble();

    // "passband_hz": [lo, hi] absolute Hz, null while the gate has none, and
    // missing entirely on a gate that predates the key. All three mean "draw
    // no bracket" -- never a bracket at 0 Hz.
    m_havePassband = false;
    const QJsonValue pb = spatial.value(QStringLiteral("passband_hz"));
    if (pb.isArray()) {
        const QJsonArray pba = pb.toArray();
        if (pba.size() == 2 && pba[0].isDouble() && pba[1].isDouble()
            && pba[1].toDouble() > pba[0].toDouble()) {
            m_havePassband = true;
            m_passbandLoHz = pba[0].toDouble();
            m_passbandHiHz = pba[1].toDouble();
        }
    }

    if (m_points != n)
        resetHistory(n);
    if (m_history.isNull())
        return;

    // Brightness is relative to the brightest bin of THIS row -- see
    // kLevelWindowDb.
    double peakDb = 0.0;
    bool havePeak = false;
    if (haveLevel) {
        for (int i = 0; i < n && i < level.size(); ++i) {
            if (std::isnan(level[i]))
                continue;
            if (!havePeak || level[i] > peakDb) {
                peakDb = level[i];
                havePeak = true;
            }
        }
    }

    // Scroll the picture down by one row and paint the new one at the top.
    uchar* base = m_history.bits();
    const qsizetype bpl = m_history.bytesPerLine();
    std::memmove(base + bpl, base, size_t(bpl * (kHistoryRows - 1)));
    auto* row = reinterpret_cast<QRgb*>(m_history.scanLine(0));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    m_phaseDeg.fill(nan, n);
    m_coherence.fill(nan, n);
    m_levelDb.fill(nan, n);
    m_haveLevel.fill(false, n);

    for (int i = 0; i < n; ++i) {
        const bool okPhase = havePhase && i < phase.size() && !std::isnan(phase[i]);
        const bool okCoh = haveCoherence && i < coherence.size() && !std::isnan(coherence[i]);
        const bool okLevel = haveLevel && i < level.size() && !std::isnan(level[i]);

        if (okPhase)
            m_phaseDeg[i] = phase[i];
        if (okCoh)
            m_coherence[i] = coherence[i];
        if (okLevel) {
            m_levelDb[i] = level[i];
            m_haveLevel[i] = true;
        }

        // A bin with no phase has no direction to colour: hue 0 at zero
        // saturation is grey, which is the same thing the incoherent case
        // says, and both are true.
        double hue = 0.0;
        if (okPhase) {
            const double wrapped = std::fmod(std::fmod(double(phase[i]) + 180.0, 360.0) + 360.0,
                                             360.0);
            hue = std::clamp(wrapped / 360.0, 0.0, 0.9999);
        }
        const double sat = (okPhase && okCoh) ? std::clamp(double(coherence[i]), 0.0, 1.0) : 0.0;
        double value = kValueWithoutLevel;
        if (okLevel && havePeak) {
            value = std::clamp((double(level[i]) - (peakDb - kLevelWindowDb)) / kLevelWindowDb,
                               0.0, 1.0);
        }
        row[i] = QColor::fromHsvF(hue, sat, value).rgb();
    }

    m_rows = std::min(m_rows + 1, kHistoryRows);
    update();
}

QColor DiversitySpatialWaterfall::newestColour(int column) const
{
    if (m_rows <= 0 || column < 0 || column >= m_points || m_history.isNull())
        return {};
    return QColor(m_history.pixel(column, 0));
}

double DiversitySpatialWaterfall::columnHz(int column) const
{
    if (m_stepHz <= 0.0 || column < 0 || column >= m_points)
        return 0.0;
    return m_startHz + (double(column) + 0.5) * m_stepHz;
}

int DiversitySpatialWaterfall::columnAt(int x) const
{
    if (m_points <= 0 || width() <= 0)
        return -1;
    const int col = int(double(x) * double(m_points) / double(width()));
    return std::clamp(col, 0, m_points - 1);
}

void DiversitySpatialWaterfall::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton || ev->position().y() > waterfallHeight()) {
        QWidget::mousePressEvent(ev);
        return;
    }
    const int col = columnAt(int(ev->position().x()));
    const double hz = columnHz(col);
    if (hz <= 0.0) {
        QWidget::mousePressEvent(ev);
        return;
    }
    ev->accept();
    emit tuneRequested(hz);
}

bool DiversitySpatialWaterfall::event(QEvent* ev)
{
    if (ev->type() != QEvent::ToolTip || m_rows <= 0)
        return QWidget::event(ev);

    auto* help = static_cast<QHelpEvent*>(ev);
    const int col = columnAt(help->pos().x());
    const double hz = columnHz(col);
    if (col < 0 || hz <= 0.0)
        return QWidget::event(ev);

    const auto number = [](const QVector<float>& v, int i, int decimals) {
        if (i < 0 || i >= v.size() || std::isnan(v[i]))
            return emDash();
        return QString::number(double(v[i]), 'f', decimals);
    };
    QToolTip::showText(
        help->globalPos(),
        tr("%1 kHz\nphase %2°\ncoherence %3\nlevel %4 dB")
            .arg(QString::number(hz / 1e3, 'f', 2), number(m_phaseDeg, col, 0),
                 number(m_coherence, col, 2), number(m_levelDb, col, 1)),
        this);
    return true;
}

void DiversitySpatialWaterfall::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    const int wfH = waterfallHeight();
    const QRect wf(0, 0, width(), wfH);
    p.fillRect(wf, tm.color(this, QStringLiteral("color.background.spectrum")));

    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));

    if (m_rows <= 0 || m_history.isNull()) {
        p.setPen(secondary);
        p.drawText(wf, Qt::AlignCenter, tr("waiting for the gate"));
        return;
    }

    // Only the rows that have actually been filled: the rest of the image is
    // history nobody has lived through yet, and drawing it would be a picture
    // of nothing presented as measurement.
    const int drawnH = std::max(1, int(std::lround(double(wfH) * double(m_rows)
                                                   / double(kHistoryRows))));
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(QRect(0, 0, width(), drawnH), m_history, QRect(0, 0, m_points, m_rows));

    if (m_havePassband && m_stepHz > 0.0) {
        const double span = m_stepHz * double(m_points);
        const double xLo = std::clamp((m_passbandLoHz - m_startHz) / span * width(),
                                      0.0, double(width()));
        const double xHi = std::clamp((m_passbandHiHz - m_startHz) / span * width(),
                                      0.0, double(width()));
        const QColor accent = tm.color(this, QStringLiteral("color.accent"));
        QColor fill = accent;
        fill.setAlphaF(kPassbandFillAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRect(QRectF(xLo, 0.0, std::max(1.0, xHi - xLo), double(wfH)));
        // A bracket, not a box: the two verticals plus a lip top and bottom,
        // so the picture underneath is never fenced off by a full outline.
        p.setPen(QPen(accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(xLo, 0.0), QPointF(xLo, double(wfH)));
        p.drawLine(QPointF(xHi, 0.0), QPointF(xHi, double(wfH)));
        p.drawLine(QPointF(xLo, 1.0), QPointF(xHi, 1.0));
        p.drawLine(QPointF(xLo, double(wfH) - 1.0), QPointF(xHi, double(wfH) - 1.0));
    }

    if (m_stepHz <= 0.0)
        return;

    QFont axisFont = font();
    axisFont.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(axisFont);
    const QFontMetricsF fm(axisFont);
    p.setPen(secondary);
    const double y = double(height()) - 3.0;
    const double span = m_stepHz * double(m_points);
    for (int i = 0; i < kAxisTicks; ++i) {
        const double t = double(i) / double(kAxisTicks - 1);
        const QString text = QString::number((m_startHz + span * t) / 1e3, 'f', 1);
        double x = t * double(width());
        if (i == 0)
            x = 0.0;
        else if (i == kAxisTicks - 1)
            x = double(width()) - fm.horizontalAdvance(text);
        else
            x -= fm.horizontalAdvance(text) / 2.0;
        p.drawText(QPointF(x, y), text);
    }
}

} // namespace AetherSDR
