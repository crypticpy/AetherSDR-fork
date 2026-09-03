#include "gui/DiversitySpatialLegend.h"

#include "core/ThemeManager.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QRect>
#include <QSizePolicy>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// The hue bar itself. Thin: it is a scale, and a scale that competes with the
// picture it explains has stopped being a legend.
constexpr int kBarHeight = 9;

// One line of 9px tick text under the bar.
constexpr int kTickTextHeight = 12;

// The "no direction" swatch, square-ish at the bar's height.
constexpr int kSwatchWidth = 16;

// Between the bar and the grey swatch, and between the swatch and its words.
constexpr int kGap = 10;
constexpr int kSwatchTextGap = 5;

// Small enough to read as a caption rather than a control, and the same 9px
// the frequency axis under the waterfall uses.
constexpr int kFontPixelSize = 9;

// Stops around the hue circle. Twelve segments is smooth to the eye at this
// height and costs nothing; the last stop stays just short of 1.0 because hue
// 1.0 and hue 0.0 are the same red and a gradient that ends where it began
// would look like a seam.
constexpr int kGradientStops = 12;

// The value and saturation the bar draws its hues at: full colour, i.e. what
// the waterfall paints a strong, fully coherent bin. The bar is the top of the
// scale, not an average of it.
constexpr double kBarSaturation = 1.0;
constexpr double kBarValue = 1.0;

// The grey the waterfall paints where coherence is below the gate. Mid-value
// so it reads as "grey" rather than as "dark" -- the waterfall's own greys run
// the whole brightness range, and it is the ABSENCE OF HUE that is being
// named here, not any one level.
constexpr double kIncoherentValue = 0.55;

// The four numbers on the bar, as fractions along it. -180 and +180 are the
// same colour and both are printed: the operator reading a streak off the bar
// needs to see that the scale wraps, and one end labelled would look like a
// range that stops.
struct Tick {
    double      at;
    const char* label;
};
const Tick kTicks[] = {
    {0.0, QT_TR_NOOP("−180°")},
    {0.25, QT_TR_NOOP("−90°")},
    {0.5, QT_TR_NOOP("0°")},
    {0.75, QT_TR_NOOP("+90°")},
    {1.0, QT_TR_NOOP("+180°")},
};

} // namespace

DiversitySpatialLegend::DiversitySpatialLegend(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowSpatialLegend"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kBarHeight + kTickTextHeight);
    setAccessibleName(tr("spatial waterfall colour key"));
    // The whole legend in words, because the one operator who most needs a
    // colour key may be the one who cannot see the key.
    setToolTip(tr("The waterfall's colour is the phase between the two loops: "
                  "the same colour twice on the span is one signal arriving "
                  "from one direction, two colours are two directions. Where "
                  "the loops do not agree well enough to call it a direction "
                  "the bin is left grey — that is sky noise, which arrives "
                  "from everywhere at once."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these -- declare the tokens actually read so an
    // Inspect-mode click surfaces them, and repaint on a live theme switch.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.text.secondary"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

QSize DiversitySpatialLegend::sizeHint() const
{
    // Wide enough that the five tick labels do not collide; the page gives it
    // the waterfall's full width in practice.
    return QSize(300, kBarHeight + kTickTextHeight);
}

void DiversitySpatialLegend::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QFont f = font();
    f.setPixelSize(kFontPixelSize);
    p.setFont(f);
    const QFontMetrics fm(f);

    const QColor secondary =
        ThemeManager::instance().color(this, QStringLiteral("color.text.secondary"));

    // Right-hand block first: it is fixed width, and whatever is left over is
    // the bar's. A narrow window shrinks the scale, never the sentence that
    // says what grey means.
    const QString greyText = tr("grey = incoherent");
    const int greyTextW = fm.horizontalAdvance(greyText);
    const int rightW = kSwatchWidth + kSwatchTextGap + greyTextW;
    const int barW = std::max(60, width() - rightW - kGap);

    const QRect bar(0, 0, barW, kBarHeight);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int i = 0; i <= kGradientStops; ++i) {
        const double t = double(i) / double(kGradientStops);
        g.setColorAt(t, QColor::fromHsvF(std::min(t, 0.9999), kBarSaturation, kBarValue));
    }
    p.fillRect(bar, g);

    p.setPen(secondary);
    for (const Tick& tick : kTicks) {
        const QString label = tr(tick.label);
        const int x = int(std::lround(tick.at * barW));
        const int w = fm.horizontalAdvance(label);
        // The end labels hang inside the bar rather than off it: a legend that
        // overflows its own row would be clipped by the layout.
        const int left = std::clamp(x - w / 2, 0, std::max(0, barW - w));
        p.drawText(QRect(left, kBarHeight, w, kTickTextHeight),
                   Qt::AlignHCenter | Qt::AlignVCenter, label);
    }

    const QRect swatch(barW + kGap, 0, kSwatchWidth, kBarHeight);
    p.fillRect(swatch, QColor::fromHsvF(0.0, 0.0, kIncoherentValue));
    p.drawText(QRect(swatch.right() + kSwatchTextGap, 0,
                     std::max(0, width() - swatch.right() - kSwatchTextGap),
                     kBarHeight + kTickTextHeight),
               Qt::AlignLeft | Qt::AlignVCenter, greyText);
}

} // namespace AetherSDR
