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

// Where the brightness scale is pinned, as percentiles of THIS row's own
// levels. A row's 20th percentile IS its noise floor -- four bins in five are
// louder than it -- and its 98th is the loudest thing that is not one outlying
// bin. Pinning to min/max instead let a single strong carrier set the top of
// the scale and squashed everything else into the bottom fifth of the range,
// which is what the operator saw as "a big blurry mess": everything mid-grey,
// nothing readable.
constexpr double kLevelLowPercentile = 0.20;
constexpr double kLevelHighPercentile = 0.98;

// A row whose 20th and 98th percentiles are the same number (a flat span, or a
// gate sending one repeated value) would divide by zero. Give it a nominal
// window so it paints flat rather than black.
constexpr double kMinLevelWindowDb = 1.0;

// Coherence gate for the hue. Below kCoherenceFloor the phase between the
// loops is not a direction, it is two noise samples that happened to line up
// for one poll, and painting it a confident colour is the picture telling a
// lie. From there the colour comes UP to full over kCoherenceFull, so the
// transition reads as "this is starting to look like something" rather than a
// hard edge. Grey below the floor is the honest statement.
constexpr double kCoherenceFloor = 0.5;
constexpr double kCoherenceFull = 0.9;

// The frequency axis under the picture: one line of small text.
constexpr int kAxisHeight = 15;

// The page gives this the full width; the height is the operator's, but never
// so short that a streak cannot be told from a dot.
constexpr int kMinHeight = 260;

// The frequency scale. Labels land on ROUND frequencies and no closer together
// than this, so the numbers read as a scale (…3830, 3840…) rather than as
// wherever an even division of the span happened to fall (…3828.3, 3859.6…);
// between them, unlabelled marks divide each step into kMinorPerMajor, which
// is how a bin gets read to a kilohertz without a number every 40 pixels.
constexpr double kMinLabelSpacingPx = 68.0;
constexpr int    kMinorPerMajor = 5;
constexpr double kMajorTickHeight = 3.0;
constexpr double kMinorTickHeight = 2.0;

// Never walk more ticks than could conceivably be on screen, whatever span a
// gate claims.
constexpr int kMaxTicks = 512;

// The grid over the picture is a hint for the eye to carry a frequency up from
// the axis, not furniture: any heavier and it is a cage over the measurement.
constexpr double kGridAlpha = 0.16;

// The passband bracket is a MARKER over live data, not a surface of its own.
constexpr double kPassbandFillAlpha = 0.14;

// The hover crosshair has to be findable without hiding the pixel it is
// pointing at.
constexpr double kCrosshairAlpha = 0.55;

// A row whose level leg the gate did not send still carries phase and
// coherence, and blacking it out would hide the two numbers we DO have. It is
// drawn at full value with the level readout saying "—" -- the honest split
// between "not measured" (the readout) and "not drawable" (the pixel).
constexpr double kValueWithoutLevel = 0.9;

// One percentile of an already-sorted, non-empty array. Nearest-rank rather
// than interpolated: with a few hundred bins the difference is invisible, and
// the rank is the number the comment above can honestly claim.
float percentileOf(const QVector<float>& sorted, double p)
{
    const int last = sorted.size() - 1;
    const int idx = std::clamp(int(std::llround(p * last)), 0, last);
    return sorted[idx];
}

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

// The smallest 1/2/5-times-a-power-of-ten step at or above `raw`. Round steps
// are the whole point: they are what put a label on 3840.0 instead of 3838.7.
double niceStep(double raw)
{
    if (!(raw > 0.0) || !std::isfinite(raw))
        return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    if (norm <= 1.0)
        return mag;
    if (norm <= 2.0)
        return 2.0 * mag;
    if (norm <= 5.0)
        return 5.0 * mag;
    return 10.0 * mag;
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
    // Readout on the MOVE, not after the pointer has been held still long
    // enough for a tooltip: the operator sweeping the span is reading numbers
    // off it, and a number that only arrives if you stop is a number nobody
    // sees.
    setMouseTracking(true);
    setAccessibleName(tr("Spatial waterfall"));
    setToolTip(tr("One row per poll; colour is direction, brightness is "
                  "level. Click to tune."));
    setAccessibleDescription(
        tr("The gate's whole span over time. Colour is the direction a signal "
           "arrives from (the phase between the two loops), how saturated it "
           "is says how coherent the two loops are there, and how bright it is "
           "says how strong it is. Move the pointer over it for the frequency, "
           "phase, coherence and level of one bin; click a column to tune to "
           "it. Two stations from different directions are different colours; "
           "one local noise source is a single flat colour across everything "
           "it touches; sky noise has no direction and goes grey."));

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

int DiversitySpatialWaterfall::drawnHeight() const
{
    if (m_rows <= 0)
        return 0;
    return std::max(1, int(std::lround(double(waterfallHeight()) * double(m_rows)
                                       / double(kHistoryRows))));
}

void DiversitySpatialWaterfall::resetHistory(int points)
{
    m_points = std::clamp(points, 0, kMaxPoints);
    m_rows = 0;
    m_head = 0;
    m_hoverColumn = -1;
    m_hoverRow = -1;
    if (m_points <= 0) {
        m_history = QImage();
        m_histPhaseDeg.clear();
        m_histCoherence.clear();
        m_histLevelDb.clear();
        return;
    }
    m_history = QImage(m_points, kHistoryRows, QImage::Format_RGB32);
    m_history.fill(Qt::black);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (QVector<float>* ring : {&m_histPhaseDeg, &m_histCoherence, &m_histLevelDb})
        ring->fill(nan, kHistoryRows * m_points);
}

void DiversitySpatialWaterfall::clear()
{
    m_available = false;
    m_startHz = 0.0;
    m_stepHz = 0.0;
    m_havePassband = false;
    m_passbandLoHz = 0.0;
    m_passbandHiHz = 0.0;
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

    // Brightness is stretched between two percentiles of THIS row -- see
    // kLevelLowPercentile.
    double lowDb = 0.0;
    double highDb = 0.0;
    bool haveWindow = false;
    if (haveLevel) {
        QVector<float> sorted;
        sorted.reserve(std::min(n, int(level.size())));
        for (int i = 0; i < n && i < level.size(); ++i) {
            if (!std::isnan(level[i]))
                sorted << level[i];
        }
        if (!sorted.isEmpty()) {
            std::sort(sorted.begin(), sorted.end());
            lowDb = percentileOf(sorted, kLevelLowPercentile);
            highDb = percentileOf(sorted, kLevelHighPercentile);
            if (highDb - lowDb < kMinLevelWindowDb)
                highDb = lowDb + kMinLevelWindowDb;
            haveWindow = true;
        }
    }

    // Scroll the picture down by one row and paint the new one at the top.
    uchar* base = m_history.bits();
    const qsizetype bpl = m_history.bytesPerLine();
    std::memmove(base + bpl, base, size_t(bpl * (kHistoryRows - 1)));
    auto* row = reinterpret_cast<QRgb*>(m_history.scanLine(0));

    // The numbers do not move at all: the newest row is wherever m_head now
    // points, and row r of the picture is r slots along from it.
    m_head = (m_head + kHistoryRows - 1) % kHistoryRows;
    const int slot = m_head * m_points;
    float* phaseRing = m_histPhaseDeg.data() + slot;
    float* cohRing = m_histCoherence.data() + slot;
    float* levelRing = m_histLevelDb.data() + slot;
    const float nan = std::numeric_limits<float>::quiet_NaN();

    for (int i = 0; i < n; ++i) {
        const bool okPhase = havePhase && i < phase.size() && !std::isnan(phase[i]);
        const bool okCoh = haveCoherence && i < coherence.size() && !std::isnan(coherence[i]);
        const bool okLevel = haveLevel && i < level.size() && !std::isnan(level[i]);

        phaseRing[i] = okPhase ? phase[i] : nan;
        cohRing[i] = okCoh ? coherence[i] : nan;
        levelRing[i] = okLevel ? level[i] : nan;

        // A bin with no phase has no direction to colour: hue 0 at zero
        // saturation is grey, which is the same thing the incoherent case
        // says, and both are true.
        double hue = 0.0;
        if (okPhase) {
            const double wrapped = std::fmod(std::fmod(double(phase[i]) + 180.0, 360.0) + 360.0,
                                             360.0);
            hue = std::clamp(wrapped / 360.0, 0.0, 0.9999);
        }
        // Colour only where there is a direction to be had. Saturation used to
        // BE the coherence, which meant a bin at 0.3 -- noise -- still came out
        // a third of the way to a confident hue, and a whole span of those is a
        // wash of pastel with the real signals lost in it.
        double sat = 0.0;
        if (okPhase && okCoh) {
            sat = std::clamp((double(coherence[i]) - kCoherenceFloor)
                                 / (kCoherenceFull - kCoherenceFloor),
                             0.0, 1.0);
        }
        double value = kValueWithoutLevel;
        if (okLevel && haveWindow) {
            const double t = std::clamp((double(level[i]) - lowDb) / (highDb - lowDb),
                                        0.0, 1.0);
            // Smoothstep, not the straight ramp: it pushes the floor darker and
            // the loud end brighter, so a signal separates from the noise around
            // it instead of being three shades of the same grey.
            value = t * t * (3.0 - 2.0 * t);
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

float DiversitySpatialWaterfall::sampleAt(const QVector<float>& ring, int row, int column) const
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (row < 0 || row >= m_rows || column < 0 || column >= m_points)
        return nan;
    const qsizetype idx = qsizetype((m_head + row) % kHistoryRows) * m_points + column;
    return (idx >= 0 && idx < ring.size()) ? ring[idx] : nan;
}

int DiversitySpatialWaterfall::columnAt(int x) const
{
    if (m_points <= 0 || width() <= 0)
        return -1;
    const int col = int(double(x) * double(m_points) / double(width()));
    return std::clamp(col, 0, m_points - 1);
}

int DiversitySpatialWaterfall::rowAt(int y) const
{
    const int drawn = drawnHeight();
    if (drawn <= 0 || y < 0 || y >= drawn)
        return -1;
    return std::clamp(int(double(y) * double(m_rows) / double(drawn)), 0, m_rows - 1);
}

QString DiversitySpatialWaterfall::readoutAt(int x, int y) const
{
    const int col = columnAt(x);
    const int row = rowAt(y);
    const double hz = columnHz(col);
    if (col < 0 || row < 0 || hz <= 0.0)
        return {};

    const float phase = sampleAt(m_histPhaseDeg, row, col);
    const float coh = sampleAt(m_histCoherence, row, col);
    const float level = sampleAt(m_histLevelDb, row, col);
    // A leg the gate did not send is a dash on its own, not a dash wearing a
    // unit: "phase —" is missing, "phase —°" is a measurement of nothing.
    const QString phaseText =
        std::isnan(phase) ? emDash()
                          : tr("%1°").arg(QString::number(double(phase), 'f', 0));
    const QString cohText =
        std::isnan(coh) ? emDash() : QString::number(double(coh), 'f', 2);
    const QString levelText =
        std::isnan(level) ? emDash()
                          : tr("%1 dB").arg(QString::number(double(level), 'f', 1));
    return tr("%1 kHz · phase %2 · coherence %3 · level %4")
        .arg(QString::number(hz / 1e3, 'f', 2), phaseText, cohText, levelText);
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

void DiversitySpatialWaterfall::mouseMoveEvent(QMouseEvent* ev)
{
    const int x = int(ev->position().x());
    const int y = int(ev->position().y());
    const QString text = readoutAt(x, y);
    const int col = text.isEmpty() ? -1 : columnAt(x);
    const int row = text.isEmpty() ? -1 : rowAt(y);
    if (col != m_hoverColumn || row != m_hoverRow) {
        m_hoverColumn = col;
        m_hoverRow = row;
        update();
    }
    // A tooltip rather than a line painted in the corner of the picture: the
    // corner of the picture is measurement too, and this one follows the
    // pointer instead of covering a bin the operator is trying to look at.
    if (text.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(ev->globalPosition().toPoint(), text, this);
    QWidget::mouseMoveEvent(ev);
}

void DiversitySpatialWaterfall::leaveEvent(QEvent* ev)
{
    if (m_hoverColumn >= 0 || m_hoverRow >= 0) {
        m_hoverColumn = -1;
        m_hoverRow = -1;
        update();
    }
    QToolTip::hideText();
    QWidget::leaveEvent(ev);
}

bool DiversitySpatialWaterfall::event(QEvent* ev)
{
    // The pointer resting still still gets the same sentence the move gives --
    // and where there is no measurement under it, the widget's own tooltip
    // saying what the picture IS is the better answer.
    if (ev->type() != QEvent::ToolTip)
        return QWidget::event(ev);
    auto* help = static_cast<QHelpEvent*>(ev);
    const QString text = readoutAt(help->pos().x(), help->pos().y());
    if (text.isEmpty())
        return QWidget::event(ev);
    QToolTip::showText(help->globalPos(), text, this);
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
    const int drawnH = drawnHeight();
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

    if (m_hoverColumn >= 0 && m_hoverColumn < m_points) {
        QColor cross = secondary;
        cross.setAlphaF(kCrosshairAlpha);
        p.setPen(QPen(cross, 1));
        const double x = (double(m_hoverColumn) + 0.5) * double(width()) / double(m_points);
        p.drawLine(QPointF(x, 0.0), QPointF(x, double(wfH)));
        if (m_hoverRow >= 0 && m_rows > 0) {
            // Which ROW the numbers came from, because a readout that could
            // have come from any of three hundred is not a readout.
            const double y = (double(m_hoverRow) + 0.5) * double(drawnH) / double(m_rows);
            p.drawLine(QPointF(0.0, y), QPointF(double(width()), y));
        }
    }

    if (m_stepHz <= 0.0)
        return;

    QFont axisFont = font();
    axisFont.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(axisFont);
    const QFontMetricsF fm(axisFont);

    // Round steps, chosen from the width rather than fixed in number: a wide
    // window gets more numbers and a narrow one fewer, and neither gets two
    // labels on top of each other.
    const double span = m_stepHz * double(m_points);
    const double major = niceStep(span * kMinLabelSpacingPx / std::max(1.0, double(width())));
    const double minor = major / double(kMinorPerMajor);
    const int decimals = major >= 1000.0 ? 0 : (major >= 100.0 ? 1 : (major >= 10.0 ? 2 : 3));

    QColor grid = secondary;
    grid.setAlphaF(kGridAlpha);
    const double baseline = double(height()) - 2.0;

    const long long first = (long long)std::ceil(m_startHz / minor);
    const long long last = (long long)std::floor((m_startHz + span) / minor);
    for (long long k = first; k <= last && k - first < kMaxTicks; ++k) {
        const double hz = double(k) * minor;
        const double x = (hz - m_startHz) / span * double(width());
        // minor is major/kMinorPerMajor and both are anchored at 0 Hz, so
        // every kMinorPerMajor-th tick IS a labelled one.
        const bool isMajor = (k % kMinorPerMajor) == 0;
        if (isMajor) {
            p.setPen(grid);
            p.drawLine(QPointF(x, 0.0), QPointF(x, double(wfH)));
        }
        p.setPen(secondary);
        p.drawLine(QPointF(x, double(wfH)),
                   QPointF(x, double(wfH) + (isMajor ? kMajorTickHeight : kMinorTickHeight)));
        if (!isMajor)
            continue;
        const QString text = QString::number(hz / 1e3, 'f', decimals);
        const double left = x - fm.horizontalAdvance(text) / 2.0;
        // A number that would hang off either end is not drawn at all: the
        // tick is still there, and half a frequency is worse than none.
        if (left < 0.0 || left + fm.horizontalAdvance(text) > double(width()))
            continue;
        p.drawText(QPointF(left, baseline), text);
    }
}

} // namespace AetherSDR
