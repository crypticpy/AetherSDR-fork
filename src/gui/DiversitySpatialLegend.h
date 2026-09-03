#pragma once

// DiversitySpatialLegend -- the key to the picture above it.
//
// The spatial waterfall's colours are a measurement, not decoration: the hue
// IS the inter-loop phase, the brightness IS the level and the greyness IS the
// absence of a direction. That only helps an operator who knows it, and the
// sentence the page used to carry -- "hue: arrival phase · saturation:
// coherence · brightness: level" -- is a description of the code, not
// something anyone can read a colour against. A station streak that is orange
// means nothing until orange has a number.
//
// So this draws the scale itself AND names it in words, because a colour bar
// with no sentence beside it is still a puzzle: the operator's verdict on the
// bar-only version was "the colours don't mean anything to anybody just
// looking at it". Left to right:
//
//   colour = arrival phase   [hue bar, marked every 45° and numbered every
//                            90° from −180 to +180]
//   [dark-to-light ramp] bright = stronger
//   [grey swatch] grey = noise, no direction
//
// All of it in the row height the caption occupied, so nothing below it moves;
// when the window is too narrow for all three, a block is dropped rather than
// overlapped -- the scale and the sentence about grey are the two that
// survive longest.
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
