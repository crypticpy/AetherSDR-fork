#include "gui/DiversityFilterPanel.h"

#include "core/ThemeManager.h"

#include <QFont>
#include <QPainter>
#include <QStringList>
#include <QVarLengthArray>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// The picture AND THE AXIS IT IS DRAWN ON, split from DiversityFilterPanel.cpp
// for the reason every other file in this window's family is split: AGENTS.md
// asks for files under 800 lines, and the widget's two jobs are genuinely two
// stories. The other file holds what the gate said and what the pointer is
// doing; this one holds how that is drawn, which is where the operator's "a
// little laggy" was fixed -- and, since every mark's position is xForHz() of
// something, the axis those calls read.
//
// The three rules it implements are stated in DiversityFilterPanel.h. In short:
// a poll that is not news never gets here at all; everything that changes only
// when the FILTER changes is in one cached transparent pixmap; and a drag
// repaints a handle's own column rather than the widget.

namespace AetherSDR {

namespace {

// The Hz axis wants labels a person reads, not a span divided by six: 250,
// 500, 1000, 2000 rather than 233, 466, 933. One-two-five times a power of
// ten is the step every instrument in this application ticks on.
double niceStepHz(double span, int wanted)
{
    if (span <= 0.0 || wanted <= 0)
        return 0.0;
    const double raw = span / double(wanted);
    const double decade = std::pow(10.0, std::floor(std::log10(raw)));
    for (double mult : {1.0, 2.0, 5.0}) {
        if (mult * decade >= raw)
            return mult * decade;
    }
    return 10.0 * decade;
}

} // namespace

// --------------------------------------------------------------------------
// The axis
// --------------------------------------------------------------------------

QRectF DiversityFilterPanel::plotRect() const
{
    return QRectF(kLeftGutter, kTopMargin,
                  std::max(1, width() - kLeftGutter - kRightMargin),
                  std::max(1, height() - kTopMargin - kBottomGutter));
}

// The window onto the gate's own array: the passband, plus the greater of six
// tenths of its width and 250 Hz either side, clipped to the array's own two
// ends. On SSB that arithmetic reaches both ends and the picture is the whole
// band, unchanged; on a 250 Hz CW filter it is a 750 Hz span, and the filter
// an operator opened this tab to look at is a third of the plot instead of
// seven per cent of it.
//
// NEVER CALLED FROM A DRAG. Re-spanning per pixel would slide the axis under
// the pointer that is holding a handle: the handle is drawn at xForHz(edge),
// so moving the edge and the axis together makes the handle chase the pointer
// and never catch it. moveEdge()'s partial path returns before this; the
// release and the arrow keys call it, and so does every applyStatus().
bool DiversityFilterPanel::updateSpan()
{
    const double wasMin = m_viewMinHz;
    const double wasMax = m_viewMaxHz;
    if (m_hz.isEmpty() || m_maxHz <= m_minHz) {
        m_viewMinHz = m_minHz;
        m_viewMaxHz = m_maxHz;
        return m_viewMinHz != wasMin || m_viewMaxHz != wasMax;
    }
    const double lo = double(std::min(m_lowHz, m_highHz));
    const double hi = double(std::max(m_lowHz, m_highHz));
    const double margin =
        std::max(kSpanMarginMinHz, (hi - lo) * kSpanMarginFraction);
    m_viewMinHz = std::max(m_minHz, lo - margin);
    m_viewMaxHz = std::min(m_maxHz, hi + margin);
    // A passband the gate has not filled in yet (both edges 0) would leave a
    // zero-width axis and every xForHz() pinned to the left gutter. The whole
    // array is the honest fallback -- it is what was drawn before there was a
    // span at all.
    if (m_viewMaxHz - m_viewMinHz < kSpanMarginMinHz) {
        m_viewMinHz = m_minHz;
        m_viewMaxHz = m_maxHz;
    }
    return m_viewMinHz != wasMin || m_viewMaxHz != wasMax;
}

double DiversityFilterPanel::xForHz(double hz) const
{
    const QRectF r = plotRect();
    if (m_viewMaxHz <= m_viewMinHz)
        return r.left();
    const double t = (hz - m_viewMinHz) / (m_viewMaxHz - m_viewMinHz);
    return r.left() + std::clamp(t, 0.0, 1.0) * r.width();
}

double DiversityFilterPanel::hzForX(double x) const
{
    const QRectF r = plotRect();
    if (m_viewMaxHz <= m_viewMinHz || r.width() <= 0.0)
        return 0.0;
    const double t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return m_viewMinHz + t * (m_viewMaxHz - m_viewMinHz);
}

double DiversityFilterPanel::yForDb(double db) const
{
    const QRectF r = plotRect();
    const double t = (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb);
    return r.top() + t * r.height();
}

double DiversityFilterPanel::spectrumAxisDbAt(int index) const
{
    if (index < 0 || index >= int(m_specAxisDb.size()))
        return std::numeric_limits<double>::quiet_NaN();
    return m_specAxisDb[index];
}

double DiversityFilterPanel::localAxisDbAt(int index) const
{
    if (index < 0 || index >= int(m_localAxisDb.size()))
        return std::numeric_limits<double>::quiet_NaN();
    return m_localAxisDb[index];
}

// How far the arriving spectrum stands over the GATE'S OWN FLOOR at the
// pointer -- "+34.0 dB over floor", a measurement, rather than the axis
// coordinate the corner used to read, which said only where the pointer was.
//
// The bin is found by arithmetic, not by walking 128 of them on every mouse
// move: the gate's own spectrum axis is np.linspace(), so the grid is uniform
// and the nearest bin is one round(). Nearest, not interpolated: a number
// between two bins is one nobody measured.
double DiversityFilterPanel::cursorDbOverFloor() const
{
    if (std::isnan(m_cursorHz) || m_specDb.size() < 2 || std::isnan(m_specFloorDb))
        return std::numeric_limits<double>::quiet_NaN();
    const double first = m_specHz.first();
    const double step = (m_specHz.last() - first) / double(m_specHz.size() - 1);
    if (step <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    const int at = std::clamp(int(std::lround((m_cursorHz - first) / step)), 0,
                              int(m_specDb.size()) - 1);
    return m_specDb[at] - m_specFloorDb;
}

// --------------------------------------------------------------------------
// The local trace
// --------------------------------------------------------------------------

// The FFT of what this application is actually playing, reduced here, once
// per tick, to what is drawable: at most kLocalTracePoints across the span the
// axis currently shows. Doing it at paint time instead would be this same
// arithmetic over 1 025 bins per frame, and the trace's whole point is that it
// arrives at 25 Hz.
//
// Its dB is pinned to the same kFloorAxisDb tick the gate's own area is, using
// ITS OWN floor. The two paths have different, unknowable gains -- the gate's
// numbers are dB below its own peak, these are dBFS after a client volume
// control -- so laying one on the other absolutely would be an invented
// measurement. Over their own floors they are honestly comparable, and that is
// the comparison the picture is for: if the curve says 40 dB down at 3 kHz and
// this trace is not, the chain is not doing what the curve claims.
void DiversityFilterPanel::setLocalSpectrum(const std::vector<float>& binsDb,
                                            double sampleRate)
{
    const int bins = int(binsDb.size());
    if (bins < 2 || sampleRate <= 0.0 || m_viewMaxHz <= m_viewMinHz) {
        clearLocalSpectrum();
        return;
    }
    // Bin i is i * sampleRate / (2 * (bins - 1)) Hz -- the analyzer answers
    // 0 Hz..Nyquist inclusive, so the last bin is Nyquist and the spacing is
    // half the rate over the bins between them.
    const double perBin = sampleRate / (2.0 * double(bins - 1));
    const int firstBin = std::max(0, int(std::floor(m_viewMinHz / perBin)));
    const int lastBin = std::min(bins - 1, int(std::ceil(m_viewMaxHz / perBin)));
    if (lastBin - firstBin < 2) {
        clearLocalSpectrum();
        return;
    }
    const int count = lastBin - firstBin + 1;
    const int stride = std::max(1, count / kLocalTracePoints);

    // The trace's own floor: the 20th percentile of what is in the span (the
    // gate's and the waterfall's convention), so a hiss and a station are
    // told apart by height rather than by level -- and a passband that is
    // mostly one strong carrier does not put the floor on the carrier, the
    // way a median would.
    std::vector<float> sorted;
    sorted.reserve(size_t(count));
    for (int i = firstBin; i <= lastBin; ++i)
        sorted.push_back(binsDb[size_t(i)]);
    const size_t at = sorted.size() / 5;
    std::nth_element(sorted.begin(), sorted.begin() + at, sorted.end());
    const double floorDb = double(sorted[at]);

    m_localHz.clear();
    m_localAxisDb.clear();
    m_localHz.reserve(count / stride + 1);
    m_localAxisDb.reserve(count / stride + 1);
    for (int i = firstBin; i <= lastBin; i += stride) {
        m_localHz.append(double(i) * perBin);
        m_localAxisDb.append(std::clamp(kFloorAxisDb + (double(binsDb[size_t(i)]) - floorDb),
                                        kBottomDb, kTopDb));
    }
    update();
}

// --------------------------------------------------------------------------
// The key to the marks
// --------------------------------------------------------------------------

// One swatch: the token's own colour, then the word. Rich text and not a
// painted strip, so the key is selectable, translatable and readable by a
// screen reader -- a colour key is the one thing on this tab whose whole
// content is words.
//
// The colour goes in by NAME rather than by token, because a QLabel's style
// sheet cannot colour a run of text inside itself. The page that shows this
// declares every token below on the label (ThemeManager::declareWidgetTokens)
// so Inspect mode still finds them.
static QString legendSwatch(const QColor& colour, const QString& word)
{
    return QStringLiteral("<span style=\"color:%1\">&#9632;</span>&nbsp;%2")
        .arg(colour.name(), word.toHtmlEscaped());
}

QString DiversityFilterPanel::legendHtml() const
{
    if (!m_available)
        return QString();
    auto& tm = ThemeManager::instance();
    QStringList marks;
    const auto add = [&](const char* token, const QString& word) {
        marks << legendSwatch(tm.color(this, QString::fromLatin1(token)), word);
    };
    // In the order the eye meets them: the two things always drawn, then the
    // families that are only there when something put them there.
    add("color.accent", tr("edges"));
    add("color.spectrum.trace", tr("response"));
    if (!m_specDb.isEmpty())
        add("color.spectrum.average", tr("arriving"));
    if (!m_localAxisDb.isEmpty())
        add("color.accent.success", tr("hearing"));
    if (!m_notches.isEmpty())
        add("color.accent.warning", tr("notch"));
    if (!m_anf.isEmpty())
        add("color.accent.dim", tr("auto-notch"));
    if (m_squeezeActive)
        add("color.accent.danger", tr("SQUEEZE"));
    if (!std::isnan(m_autoLowHz) || !std::isnan(m_autoHighHz))
        add("color.text.secondary", tr("AUTO width"));
    if (m_roofAvailable)
        add("color.text.label", tr("roof"));
    if (!std::isnan(m_contourHz) || !std::isnan(m_apfHz))
        add("color.accent.bright", tr("contour/APF"));
    return marks.join(QStringLiteral("&nbsp;&nbsp;&nbsp;"));
}

void DiversityFilterPanel::clearLocalSpectrum()
{
    if (m_localAxisDb.isEmpty())
        return;
    m_localHz.clear();
    m_localAxisDb.clear();
    update();
}


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

    if (!m_available || m_viewMaxHz <= m_viewMinHz) {
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
    // read; with it the height is decibels over the floor (20th percentile).
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

    // The Hz axis. Three labels -- both ends and the middle -- were enough
    // when the axis was always 0..3000 and everyone knew it; on a span that
    // now moves with the passband they are not, because the reader has to work
    // out what the axis IS before reading anything off it. So the gutter is
    // ticked on round numbers across the drawn span, at whatever one-two-five
    // step gives about six of them, each labelled under its own tick.
    p.setPen(secondary);
    const double step = niceStepHz(m_viewMaxHz - m_viewMinHz, 6);
    if (step > 0.0) {
        const double firstTick = std::ceil(m_viewMinHz / step) * step;
        for (double hz = firstTick; hz <= m_viewMaxHz + 0.5; hz += step) {
            const double x = xForHz(hz);
            p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 3));
            p.drawText(QRectF(x - 30, r.bottom() + 2, 60, kBottomGutter - 2),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       QString::number(qint64(std::llround(hz))));
        }
    }
    // The unit, once, at the right-hand end -- naming what the ticks are
    // without repeating "Hz" six times across the gutter.
    p.drawText(QRectF(r.right() - 24, r.bottom() + 2, 24, kBottomGutter - 2),
               Qt::AlignRight | Qt::AlignVCenter, tr("Hz"));
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

    // ROOFING · DIGITAL PEAK OFFSET (A1): the digital roof's own band, drawn
    // faint and under everything else that marks a frequency -- see
    // DiversityFilterPanelRoof.cpp.
    paintRoofBand(p, r);

    // The tones the automatic notcher found, dashed: they are not filters the
    // operator placed and must not read as if they were. Their own token
    // rather than the spectrum's: color.spectrum.average is the arriving band
    // AND the SQUEEZE bracket, and three families sharing one colour is a
    // legend that cannot tell them apart. color.accent.dim is already in the
    // theme and used nowhere on this widget.
    p.setPen(QPen(tm.color(this, QStringLiteral("color.accent.dim")), 1,
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

    // SQUEEZE: the operator's own null or notch (or comb), drawn over the
    // manual notches and under the contour/APF ticks -- see
    // DiversityFilterPanelSqueeze.cpp for the Hz-axis conversion this needs
    // and the reason it needs one at all.
    paintSqueeze(p, r);

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
    const QSize want = (size() * dpr).expandedTo(QSize(1, 1));
    // The pixmap is only REALLOCATED when its size actually changes. A rebuild
    // is not a resize -- a notch moving rebuilds the layer at exactly the same
    // size -- and freeing and re-taking a full-window pixmap for that is a
    // graphics allocation per gesture for no change of geometry.
    if (m_layer.size() != want) {
        m_layer = QPixmap(want);
        m_layer.setDevicePixelRatio(dpr);
    }
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
    // The rim is the polygon MINUS its two baseline corners, drawn straight
    // out of the polygon's own storage. mid() would deep-copy 128 (or 4 096)
    // points into a fresh QPolygonF on every frame to draw the same line.
    p.drawPolyline(m_specArea.constData() + 1, int(m_specAxisDb.size()));
    p.setRenderHint(QPainter::Antialiasing, false);
}

// The second trace, over the gate's area and under the cached layer: what
// this application is actually playing, floor-pinned the same way (see
// setLocalSpectrum). A line and no fill -- the filled area is the band
// ARRIVING, and two filled areas over each other would be a picture in which
// neither can be read.
void DiversityFilterPanel::paintLocalSpectrum(QPainter& p, const QRectF& r)
{
    Q_UNUSED(r);
    if (m_localAxisDb.size() < 2)
        return;
    QVarLengthArray<QPointF, kLocalTracePoints> line;
    line.reserve(int(m_localAxisDb.size()));
    for (int i = 0; i < int(m_localAxisDb.size()); ++i)
        line.append(QPointF(xForHz(m_localHz[i]), yForDb(m_localAxisDb[i])));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ThemeManager::instance().color(
                      this, QStringLiteral("color.accent.success")),
                  1));
    p.drawPolyline(line.constData(), line.size());
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
    // The source rectangle in DEVICE pixels, rounded rather than truncated.
    // int() on a fractional device pixel ratio (1.5 on a scaled external
    // display, 2.0 only on the built-in panel) walks the source left and up by
    // up to a pixel while the target does not, which is the faint seam that
    // appeared down the left of a partial repaint during a drag.
    const QRectF source(target.x() * dpr, target.y() * dpr, target.width() * dpr,
                        target.height() * dpr);
    p.setClipRect(target);
    p.fillRect(target, ThemeManager::instance().color(
                           this, QStringLiteral("color.background.spectrum")));
    const QRectF r = plotRect();
    const bool drawable = m_available && m_viewMaxHz > m_viewMinHz;
    if (drawable) {
        paintSpectrum(p, r);
        paintLocalSpectrum(p, r);
    }
    p.drawPixmap(QRectF(target), m_layer, source);
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

    // ROOFING · DIGITAL PEAK OFFSET (A1): the roof's own handle, live like
    // the two above -- see DiversityFilterPanelRoof.cpp.
    paintRoofHandle(p, r);

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
