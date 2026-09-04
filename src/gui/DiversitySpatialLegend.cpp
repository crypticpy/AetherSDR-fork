#include "gui/DiversitySpatialLegend.h"

#include "core/ThemeManager.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPair>
#include <QRect>
#include <QSizePolicy>
#include <QStringList>
#include <QVector>

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

// The brightness ramp is wider than a swatch because it has to READ as a ramp:
// a 16px gradient is a smudge.
constexpr int kRampWidth = 34;

// Between blocks, and between a swatch and the words it belongs to.
constexpr int kGap = 10;
constexpr int kSwatchTextGap = 5;

// Below this the bar is no longer a scale, so a block of words is dropped
// instead of squeezing it further.
constexpr int kMinBarWidth = 60;

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

// The brightness ramp is drawn at ONE hue -- the hue the bar carries at 0
// degrees -- on purpose: the thing being said is "the same colour dark and
// light is the same direction weak and strong", which a black-to-white ramp
// would not say, and which a grey ramp would confuse with the grey below.
constexpr double kRampHue = 0.5;
constexpr double kRampLowValue = 0.10;

// The grey the waterfall paints where coherence is below the gate. Mid-value
// so it reads as "grey" rather than as "dark" -- the waterfall's own greys run
// the whole brightness range, and it is the ABSENCE OF HUE that is being
// named here, not any one level.
constexpr double kIncoherentValue = 0.55;

// A mark in the bottom of the bar itself, not under it: the eye reads a colour
// off a tick that touches the colour, not off a number in the row below.
constexpr int kTickMarkHeight = 3;

// The scale on the phase axis: labelled every 90 degrees, marked every 45.
// -180 and +180 are the same colour and both are printed -- the operator
// reading a streak off the bar needs to see that the scale WRAPS, and one end
// labelled would look like a range that stops.
struct Tick {
    double      at;
    const char* label;   // null: a mark with no number, so the scale is finer
};                       // than the four numbers it has room for
const Tick kTicks[] = {
    {0.0, QT_TR_NOOP("−180°")},
    {0.125, nullptr},
    {0.25, QT_TR_NOOP("−90°")},
    {0.375, nullptr},
    {0.5, QT_TR_NOOP("0°")},
    {0.625, nullptr},
    {0.75, QT_TR_NOOP("+90°")},
    {0.875, nullptr},
    {1.0, QT_TR_NOOP("+180°")},
};
constexpr int kTickCount = int(sizeof(kTicks) / sizeof(kTicks[0]));

// Clear air either side of a tick label before the next one may be drawn.
constexpr int kLabelGap = 4;

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
    setToolTip(tr("Colour is direction (phase), brightness is strength, "
                  "grey is noise."));
    setAccessibleDescription(
        tr("Colour is the phase between the two loops, −180° to +180° "
           "once around the colour circle: the same colour twice on the "
           "span is one signal arriving from one direction, two colours "
           "are two directions. Brightness is how strong the bin is, "
           "against that row's own noise floor. Where the loops do not "
           "agree well enough to call it a direction the bin is left "
           "grey — that is noise, which arrives from everywhere at once "
           "and has no one direction to colour."));

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
    // Wide enough for all three blocks of words plus a bar that is still a
    // scale; the page gives it the waterfall's full width in practice, and
    // paintEvent drops a block rather than overlap if it ever gets less.
    return QSize(460, kBarHeight + kTickTextHeight);
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

    // Every block of words is fixed width and the SCALE is what stretches: a
    // narrow window shrinks the bar, never the sentences that say what the bar
    // and the greys are.
    const QString hueText = tr("colour = arrival phase");
    const QString brightText = tr("bright = stronger");
    const QString greyText = tr("grey = noise, no direction");
    const int hueW = fm.horizontalAdvance(hueText);
    const int brightTextW = fm.horizontalAdvance(brightText);
    const int brightW = kRampWidth + kSwatchTextGap + brightTextW;
    const int greyW = kSwatchWidth + kSwatchTextGap + fm.horizontalAdvance(greyText);

    // Everything; then without the brightness block; then without the leading
    // caption. Whatever the width, what survives longest is the scale and the
    // sentence about grey -- the two that decide whether a pixel can be read
    // at all.
    bool showBright = true;
    bool showHueWords = true;
    int barW = width() - (hueW + kGap) - (brightW + kGap) - (greyW + kGap);
    if (barW < kMinBarWidth) {
        showBright = false;
        barW = width() - (hueW + kGap) - (greyW + kGap);
    }
    if (barW < kMinBarWidth) {
        showHueWords = false;
        barW = width() - (greyW + kGap);
    }
    barW = std::max(kMinBarWidth, barW);

    p.setPen(secondary);
    int x = 0;
    if (showHueWords) {
        p.drawText(QRect(0, 0, hueW, height()), Qt::AlignLeft | Qt::AlignVCenter, hueText);
        x = hueW + kGap;
    }

    const QRect bar(x, 0, barW, kBarHeight);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int i = 0; i <= kGradientStops; ++i) {
        const double t = double(i) / double(kGradientStops);
        g.setColorAt(t, QColor::fromHsvF(std::min(t, 0.9999), kBarSaturation, kBarValue));
    }
    p.fillRect(bar, g);

    const auto tickX = [&](const Tick& tick) {
        return std::min(bar.left() + int(std::lround(tick.at * barW)), bar.right());
    };
    for (const Tick& tick : kTicks)
        p.drawLine(tickX(tick), kBarHeight - kTickMarkHeight, tickX(tick), kBarHeight - 1);

    // A label only where it has the room to be read: a bar too narrow for five
    // numbers prints "−18090°", which is worse than printing three. The ENDS
    // are offered the room first -- −180 and +180 are the same colour and
    // printing both is how the scale says it wraps -- and the marks are all
    // still there under the numbers that did fit.
    QVector<QPair<int, int>> placed;
    const auto tryLabel = [&](const Tick& tick) {
        if (tick.label == nullptr)
            return;
        const QString label = tr(tick.label);
        const int w = fm.horizontalAdvance(label);
        // The end labels hang inside the bar rather than off it: a legend that
        // overflows its own row would be clipped by the layout.
        const int left = std::clamp(tickX(tick) - w / 2, bar.left(),
                                    std::max(bar.left(), bar.right() + 1 - w));
        for (const QPair<int, int>& taken : placed) {
            if (left < taken.second + kLabelGap && taken.first < left + w + kLabelGap)
                return;
        }
        placed.append({left, left + w});
        p.drawText(QRect(left, kBarHeight, w, kTickTextHeight),
                   Qt::AlignHCenter | Qt::AlignVCenter, label);
    };
    tryLabel(kTicks[0]);
    tryLabel(kTicks[kTickCount - 1]);
    for (const Tick& tick : kTicks)
        tryLabel(tick);

    x = bar.right() + 1 + kGap;
    if (showBright) {
        const QRect ramp(x, 0, kRampWidth, kBarHeight);
        QLinearGradient rg(ramp.topLeft(), ramp.topRight());
        rg.setColorAt(0.0, QColor::fromHsvF(kRampHue, kBarSaturation, kRampLowValue));
        rg.setColorAt(1.0, QColor::fromHsvF(kRampHue, kBarSaturation, kBarValue));
        p.fillRect(ramp, rg);
        p.drawText(QRect(ramp.right() + kSwatchTextGap, 0, brightTextW, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, brightText);
        x = ramp.right() + 1 + kSwatchTextGap + brightTextW + kGap;
    }

    const QRect swatch(x, 0, kSwatchWidth, kBarHeight);
    p.fillRect(swatch, QColor::fromHsvF(0.0, 0.0, kIncoherentValue));
    p.drawText(QRect(swatch.right() + kSwatchTextGap, 0,
                     std::max(0, width() - swatch.right() - kSwatchTextGap), height()),
               Qt::AlignLeft | Qt::AlignVCenter, greyText);
}

} // namespace AetherSDR
