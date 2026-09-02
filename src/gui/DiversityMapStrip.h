#pragma once

// DiversityMapStrip — paints /diversity/map's coherence array as a strip of
// vertical bars, with bracket marks under whichever "sources" fed it: the
// glance the numeric gateDiversitySourcesList rows in AetherGateApplet can't
// give — which patch of the passband is actually coherent enough to null
// against right now.
//
// Self-contained and consumed only by AetherGateApplet, which forward-declares
// it in its own header and includes this one only from the .cpp that builds
// it.
//
// setMap() is not on check_a11y.py's watched-setter list (setLevel/setDbm/
// setFrequency/update*), so the custom paintEvent is exempt from its
// QAccessibleInterface-companion check; the strip is a secondary, glanceable
// view of data gateDiversitySourcesList already exposes to a screen reader as
// text, so its own accessible name is enough (docs/a11y.md's "decorative vs.
// data-carrying" distinction).

#include <QWidget>
#include <QVector>

#include <utility>

class QJsonObject;

namespace AetherSDR {

class DiversityMapStrip : public QWidget {
    Q_OBJECT
public:
    explicit DiversityMapStrip(QWidget* parent = nullptr);

    // An {"error": ...} reply, a non-JSON body, or a route that never
    // answered all mean "nothing to draw" -- an empty strip, not a stale one
    // still showing the last good map. Hides the widget itself when there is
    // nothing to draw, so a v1 gate (no /diversity/map) does not reserve a
    // blank row in the form layout.
    void setMap(const QJsonObject& map);

    // Bar height, in px. 24 (the constructor default) is the sidebar's row;
    // DiversityWindow gives it a much taller one, where the same coherence
    // array is the panel's main noise readout rather than a glance strip.
    // Height only -- setMap() and the paint are identical at either size.
    void setStripHeight(int px);

    // Window mode. The sidebar's strip is a GLANCE -- 24px, no axis, and the
    // numeric sources list right under it carries the frequencies. In the
    // window the same array is the noise panel's main readout, where a bar
    // with no frequency under it is a picture of nothing in particular: axis
    // mode adds MHz labels at the two edges and the centre, and draws the
    // receiver's own passband over the strip so "the coherent patch" and "the
    // bit I am listening to" can be compared by eye rather than by arithmetic.
    //
    // Off by default, so the sidebar's rendering and geometry are untouched.
    void setAxisMode(bool on);
    bool axisMode() const { return m_axisMode; }

    // /diversity/map's "passband_hz": [lo, hi] absolute Hz, or absent on a
    // gate that does not report one. Exposed because the strip is a raw
    // QPainter paint with no children -- a test has no other way to check
    // that a missing key drew no marker rather than a marker at zero.
    bool   hasPassband() const { return m_havePassband; }
    double passbandLoHz() const { return m_passbandLoHz; }
    double passbandHiHz() const { return m_passbandHiHz; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    // Recomputes the widget's fixed height from the bar height and whether
    // the axis row is reserved.
    void applyHeight();

    QVector<float> m_coherence;
    QVector<std::pair<double, double>> m_sources;
    double m_startHz{0.0};
    double m_stepHz{0.0};
    bool   m_havePassband{false};
    double m_passbandLoHz{0.0};
    double m_passbandHiHz{0.0};
    int    m_barHeight{24};
    bool   m_axisMode{false};
};

} // namespace AetherSDR
