#include "gui/DiversityTimeline.h"

#include "core/ThemeManager.h"

#include <QBrush>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSizePolicy>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// The window's whole point is that it is FIXED: two minutes, always, so the
// slope of a trace means the same thing on every glance.
constexpr qint64 kWindowMs = 120000;

// The same -10..+30 dB frame DiversityScope's bars and DiversitySnrMeter use.
// Three views of the same three numbers that disagreed about what "half way"
// meant would be worse than one view.
constexpr double kSnrLoDb = -10.0;
constexpr double kSnrHiDb = 30.0;

constexpr int kHeight = 120;

// Bottom furniture, from the baseline up: the time-axis text, the talk band,
// and the thin steady-QRM band above it.
constexpr double kAxisH  = 13.0;
constexpr double kTalkH  = 9.0;
constexpr double kQrmH   = 4.0;
constexpr double kLeftGutter = 30.0;   // room for the "+20"/"+10"/"0" labels
constexpr double kRightPad   = 4.0;

// Talker colours cycle through four tokens that already exist rather than
// inventing a palette: the point is only "this over is a different station
// from that one", which four distinguishable tones carry fine.
const char* const kTalkerTokens[] = {
    "color.accent",
    "color.accent.bright",
    "color.accent.warning",
    "color.spectrum.trace",
};
constexpr int kTalkerTokenCount = int(sizeof(kTalkerTokens) / sizeof(kTalkerTokens[0]));

} // namespace

DiversityTimeline::DiversityTimeline(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setObjectName(QStringLiteral("diversityWindowTimeline"));
    setAccessibleName(tr("Diversity timeline"));
    setToolTip(tr("Two-minute A/B/combined SNR trend - check the combiner really "
                  "beats both loops."));
    setAccessibleDescription(
        tr("The last two minutes. Each line is one leg's "
           "signal-to-noise: A and B are the two loops, the bright one "
           "is what you are hearing. When the bright line sits above "
           "both others, the combiner is buying you something; when it "
           "tracks the better loop exactly, it is not. The coloured "
           "band along the bottom is who was talking, one colour per "
           "remembered talker, and the thin band above it marks the "
           "stretches where a steady carrier was being nulled. Read-only."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these: declare the tokens actually read so
    // Inspect mode surfaces them, and repaint on a live theme switch -- the
    // pattern DiversityScope and DiversityMapStrip already follow.
    auto& tm = ThemeManager::instance();
    QStringList tokens{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.spectrum.grid"),
        QStringLiteral("color.text.secondary"),
        QStringLiteral("color.text.primary"),
        QStringLiteral("color.spectrum.average"),
        QStringLiteral("color.accent"),
        QStringLiteral("color.accent.warning"),
    };
    for (const char* token : kTalkerTokens)
        tokens << QString::fromLatin1(token);
    tokens.removeDuplicates();
    tm.declareWidgetTokens(this, tokens);
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

qint64 DiversityTimeline::windowMs() const
{
    return kWindowMs;
}

void DiversityTimeline::addSample(qint64 ms, const Sample& sample)
{
    // A clock that went backwards (an NTP step, a test replaying history)
    // must not leave the buffer holding "future" samples the paint would
    // draw off the right edge -- drop anything ahead of the newest stamp.
    while (!m_samples.isEmpty() && m_samples.back().ms > ms)
        m_samples.pop_back();

    m_samples.push_back({ms, sample});
    m_nowMs = ms;

    const qint64 cutoff = ms - kWindowMs;
    int drop = 0;
    while (drop < m_samples.size() && m_samples[drop].ms < cutoff)
        ++drop;
    if (drop > 0)
        m_samples.remove(0, drop);

    update();
}

void DiversityTimeline::clear()
{
    m_samples.clear();
    m_nowMs = 0;
    update();
}

void DiversityTimeline::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));

    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));
    const QColor grid = tm.color(this, QStringLiteral("color.spectrum.grid"));

    const double left = kLeftGutter;
    const double right = std::max(left + 1.0, double(width()) - kRightPad);
    const double plotTop = 3.0;
    const double plotBot = std::max(plotTop + 1.0,
                                    double(height()) - kAxisH - kTalkH - kQrmH - 3.0);
    const double plotH = plotBot - plotTop;
    const double plotW = right - left;

    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(small);
    const QFontMetricsF fm(small);

    const auto yFor = [&](double db) {
        const double t = std::clamp((db - kSnrLoDb) / (kSnrHiDb - kSnrLoDb), 0.0, 1.0);
        return plotBot - t * plotH;
    };
    // Fixed frame: the time axis always spans exactly the window, whether the
    // buffer holds two samples or two hundred, so a trace's slope means the
    // same thing on every glance.
    const auto xFor = [&](qint64 ms) {
        const double age = double(m_nowMs - ms);
        const double t = std::clamp(1.0 - age / double(kWindowMs), 0.0, 1.0);
        return left + t * plotW;
    };

    // Horizontal grid at 0 / +10 / +20 dB, labelled in the gutter.
    for (const double db : {0.0, 10.0, 20.0}) {
        const double y = yFor(db);
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(left, y), QPointF(right, y));
        p.setPen(secondary);
        const QString label = db > 0.0 ? QStringLiteral("+%1").arg(int(db))
                                       : QStringLiteral("0");
        p.drawText(QPointF(left - 4.0 - fm.horizontalAdvance(label), y + fm.ascent() / 2.0 - 1.0),
                   label);
    }

    // One trace per leg. A null leg breaks the polyline rather than dropping
    // to zero -- Principle XI: nothing measured is not a measurement of nought.
    const auto drawTrace = [&](const QColor& colour, double widthPx,
                               bool (*have)(const Sample&), double (*value)(const Sample&)) {
        p.setPen(QPen(colour, widthPx));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        bool open = false;
        for (const Entry& e : m_samples) {
            if (!have(e.s)) {
                open = false;
                continue;
            }
            const QPointF pt(xFor(e.ms), yFor(value(e.s)));
            if (!open) {
                path.moveTo(pt);
                open = true;
            } else {
                path.lineTo(pt);
            }
        }
        if (!path.isEmpty())
            p.drawPath(path);
    };

    // A and B are the two grey text tokens rather than secondary and
    // spectrum.average: those two resolve to the SAME grey in the default
    // theme, which would have drawn the two loops in one colour and made the
    // key a lie.
    const QColor legA = tm.color(this, QStringLiteral("color.text.primary"));
    const QColor legB = secondary;
    drawTrace(legA, 1.2,
              [](const Sample& s) { return s.haveA; },
              [](const Sample& s) { return s.a; });
    drawTrace(legB, 1.2,
              [](const Sample& s) { return s.haveB; },
              [](const Sample& s) { return s.b; });
    drawTrace(tm.color(this, QStringLiteral("color.accent")), 2.0,
              [](const Sample& s) { return s.haveOut; },
              [](const Sample& s) { return s.out; });

    // Leg key, top-right of the plot, so the three lines do not need a
    // separate legend widget to be readable.
    {
        QFont keyFont = small;
        keyFont.setBold(true);
        p.setFont(keyFont);
        const QFontMetricsF kfm(keyFont);
        struct Key { const char* text; QColor colour; };
        const Key keys[] = {
            {"A", legA},
            {"B", legB},
            {"OUT", tm.color(this, QStringLiteral("color.accent"))},
        };
        double x = right;
        for (int i = 2; i >= 0; --i) {
            const QString text = QString::fromLatin1(keys[i].text);
            x -= kfm.horizontalAdvance(text);
            p.setPen(keys[i].colour);
            p.drawText(QPointF(x, plotTop + kfm.ascent()), text);
            x -= 8.0;
        }
        p.setFont(small);
    }

    // Talk band: one filled cell per sample interval, coloured by who was
    // talking. Cells are drawn a hair wide so a 1 Hz poll leaves no seams.
    const double talkTop = plotBot + kQrmH + 2.0;
    const double qrmTop = plotBot + 1.0;
    const double stepW = m_samples.size() > 1
        ? std::max(1.0, plotW / double(kWindowMs) * 1000.0)
        : 2.0;
    QColor idle = secondary;
    idle.setAlphaF(0.25);
    QBrush qrmBrush(tm.color(this, QStringLiteral("color.accent.warning")),
                    Qt::BDiagPattern);
    // Whole pixels and no antialiasing for the bands: a run of adjacent cells
    // at fractional coordinates gets a half-covered edge pixel each, which
    // reads as a dotted line rather than the solid band it is.
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int i = 0; i < m_samples.size(); ++i) {
        const Entry& e = m_samples[i];
        const double x0 = std::floor(xFor(e.ms));
        const double x1 = (i + 1 < m_samples.size())
            ? std::floor(std::min(right, xFor(m_samples[i + 1].ms)))
            : std::floor(std::min(right, x0 + stepW));
        const double w = std::max(1.0, x1 - x0);
        QColor c = idle;
        if (e.s.haveTalker) {
            const int slot = ((e.s.talkerId % kTalkerTokenCount) + kTalkerTokenCount)
                             % kTalkerTokenCount;
            c = tm.color(this, QString::fromLatin1(kTalkerTokens[slot]));
        }
        p.fillRect(QRectF(x0, talkTop, w, kTalkH - 1.0), c);
        if (e.s.steadyQrm) {
            // Hatched, not solid: the talker cycle already uses the warning
            // token for every fourth station, and two solid amber bars one
            // above the other would read as one thing. The texture says
            // "different kind of fact" without inventing a colour.
            p.fillRect(QRectF(x0, qrmTop, w, kQrmH - 1.0), qrmBrush);
        }
    }

    // Time axis. Fixed text, fixed positions -- it says what the window is,
    // not what happens to be in it.
    p.setPen(secondary);
    const double axisY = double(height()) - 2.0;
    p.drawText(QPointF(left, axisY), tr("-120 s"));
    const QString nowText = tr("now");
    p.drawText(QPointF(right - fm.horizontalAdvance(nowText), axisY), nowText);
    const QString midText = tr("-60 s");
    p.drawText(QPointF(left + plotW / 2.0 - fm.horizontalAdvance(midText) / 2.0, axisY),
               midText);

    p.setPen(QPen(grid, 1));
    p.drawLine(QPointF(left, plotTop), QPointF(left, plotBot));
    p.drawLine(QPointF(left, plotBot), QPointF(right, plotBot));
}

} // namespace AetherSDR
