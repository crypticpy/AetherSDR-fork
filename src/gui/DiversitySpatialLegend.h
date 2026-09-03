#pragma once

// DiversitySpatialLegend -- the key to the picture above it.
//
// The spatial waterfall's colours are a measurement, not decoration: the hue
// IS the inter-loop phase and the greyness IS the absence of one. That only
// helps an operator who knows it, and the sentence the page used to carry --
// "hue: arrival phase · saturation: coherence · brightness: level" -- is a
// description of the code, not something anyone can read a colour against. A
// station streak that is orange means nothing until orange has a number.
//
// So this draws the scale itself: the hue circle from -180 to +180 degrees
// with ticks, and beside it the grey the waterfall paints where there is no
// direction to report. Two swatches and four numbers, in the same row height
// the caption occupied, so nothing below it moves.
//
// It is a picture of a fixed mapping and holds no state -- no payload reaches
// it and no signal leaves it. It repaints on a theme change only because the
// text and the grey come from tokens.
//
// Raw QPainter with no child widgets, and no setLevel/setDbm/setFrequency/
// update* setter, so check_a11y.py does not require a QAccessibleInterface
// companion -- the same exemption the waterfall it labels already takes. The
// widget carries its whole meaning as a tooltip for anyone who cannot see the
// colours at all.

#include <QWidget>

class QPaintEvent;

namespace AetherSDR {

class DiversitySpatialLegend : public QWidget {
    Q_OBJECT
public:
    explicit DiversitySpatialLegend(QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
};

} // namespace AetherSDR
