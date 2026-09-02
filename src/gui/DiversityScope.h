#pragma once

// DiversityScope -- a read-only visualisation of one /diversity poll, built to
// answer the operator complaint that drove this file: individual widgets
// (the phase slider, the ratio spin, the growing status line) were being
// rewritten out from under the operator's cursor on every poll, which reads
// as the UI "glitching out", and gave no way to tell whether Track mode was
// actually doing anything.
//
// A second complaint (the sidebar's 250px inner width) narrowed the layout
// further: this widget owns none of the churn above AND must never force a
// horizontal scrollbar or mid-word truncation at that width. It never
// receives focus, is never written TO by the operator, and repaints in
// place -- nothing here can grow or shrink the row it sits in, so a poll
// landing mid-glance never reflows anything around it.
// AetherGateApplet::applyDiversity() feeds it every poll (and every write's
// read-back) through setState(); setPresent(false) and an "available":
// false payload both route to clear().
//
// Two regions, painted top-to-bottom, all inside this widget's own rect:
//   - top: a polar plot of the live weight (radius = ratio_db over its
//     -20..+20 dB range, angle = phase_deg) in a fixed square on the left --
//     the current weight as a filled dot, a fading trail of the last ~30
//     samples, and memory entries as small hollow markers, so a Track solve
//     that keeps landing near the same spot is visibly converging rather
//     than invisibly "doing something" -- and, to its right, three
//     fixed-scale SNR bars (A / B / OUT, -10..+30 dB) with a fixed-width
//     numeric readout beside each, plus a "gain +X.X dB" line (out - max(a,
//     b)), coloured in the accent token only when that gain is worth having
//     (>= +1 dB).
//   - bottom: two short, fixed-field text lines (talk indicator / update
//     count / memory count, then the noise-null source / coherence / noise
//     blanker percentage) in the widget's normal small font -- elided only
//     as a last resort, since at the sidebar's default width they are sized
//     to fit without it. mode/lag/corr-peak/aligned/capture all moved out of
//     this widget: the mode combo, the status label and the capture label
//     elsewhere in the applet already show them, so repeating them here was
//     redundant width this widget cannot spare.

#include <QWidget>
#include <QString>
#include <QVector>

#include <limits>

class QJsonObject;
class QPaintEvent;
class QPainter;
class QRectF;

namespace AetherSDR {

class DiversityScope : public QWidget {
    Q_OBJECT
public:
    explicit DiversityScope(QWidget* parent = nullptr);

    // Feeds one /diversity payload in. Every field is read defensively --
    // exactly the same "each v2 key is independently optional, and a missing
    // or malformed one leaves the affected part of the paint at its last (or
    // default) state rather than being invented" contract
    // AetherGateApplet::applyDiversity() already keeps for the widgets it
    // owns directly. Never crashes on a payload with nulls, missing keys, or
    // an empty object.
    void setState(const QJsonObject& diversity);

    // Clears the trail, memory markers, bars and text lines -- called when
    // the gate is no longer present, or /diversity stops reporting
    // available.
    void clear();

    // out - max(a, b) in dB from the last setState() with all three SNR legs
    // present; NaN when any leg was null/missing. Exposed because the scope
    // is otherwise a black box from outside (raw QPainter, no child widgets)
    // -- tests have no other way to check what the gain readout would show.
    double lastGainDb() const { return m_gainDb; }

    // True when the last paint had to elide either of the two bottom text
    // lines to fit the widget's actual width. Test-only escape hatch: the
    // 250px-sidebar requirement is "no mid-word truncation at the default
    // font", which is a claim about real QFontMetrics against a real width,
    // not something a unit test can otherwise observe from a raw QPainter
    // paint with no child widgets.
    bool bottomLinesElided() const { return m_line1Elided || m_line2Elided; }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    struct WeightSample {
        double phaseDeg;
        double ratioDb;
    };

    void paintWeightPlot(QPainter& p, const QRectF& rectArea) const;
    void paintSnrBars(QPainter& p, const QRectF& rectArea) const;
    void paintTextLines(QPainter& p, const QRectF& rectArea);
    QString buildTopLine() const;
    QString buildBottomLine() const;

    QVector<WeightSample> m_trail;      // ring buffer, oldest first, capped
    QVector<WeightSample> m_memory;

    bool   m_haveWeight{false};
    double m_phaseDeg{0.0};
    double m_ratioDb{0.0};

    bool   m_snrAValid{false};
    bool   m_snrBValid{false};
    bool   m_snrOutValid{false};
    double m_snrA{0.0};
    double m_snrB{0.0};
    double m_snrOut{0.0};
    double m_gainDb{std::numeric_limits<double>::quiet_NaN()};   // NaN until all three legs valid

    bool    m_talking{false};
    bool    m_haveRnSource{false};
    bool    m_haveNoiseCoh{false};
    double  m_noiseCoh{0.0};
    QString m_rnSource;
    int     m_updates{0};
    bool    m_haveNb{false};
    double  m_nbBlankedPct{0.0};
    int     m_memoryCount{0};

    // Set by the most recent paintTextLines() -- see bottomLinesElided().
    bool m_line1Elided{false};
    bool m_line2Elided{false};
};

} // namespace AetherSDR
