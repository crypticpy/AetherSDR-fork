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

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<float> m_coherence;
    QVector<std::pair<double, double>> m_sources;
    double m_startHz{0.0};
    double m_stepHz{0.0};
};

} // namespace AetherSDR
