#pragma once

// DiversityTimeline -- the last two minutes of the combiner, as a strip.
//
// DiversityScope answers "what is the weight doing right now"; the SNR meters
// answer "how loud is each loop right now". Neither answers the question the
// operator actually asks between overs -- "is this thing HELPING?" -- because
// that is a comparison over time, not a value at an instant. This widget is
// that comparison: A, B and OUT on the same fixed -10..+30 dB scale the scope
// and the meters use, scrolling right-to-left over a fixed 120 s window, with
// a band along the bottom saying who was talking while each stretch of trace
// was drawn.
//
// It holds NO timer and asks nothing of the clock: the window feeds it one
// addSample() per /diversity poll with the timestamp it already has, which is
// also what lets a test lay down a controlled 120 s of history in a loop
// instead of waiting two minutes for it. Samples older than the window are
// dropped on the way in, so the buffer is bounded by the poll rate rather
// than by how long the window has been open.
//
// Nothing about a poll can resize it: the height is fixed, the dB scale is
// fixed, and the time axis is fixed at "-120 s ... now" whether there are two
// samples in it or two hundred. A null SNR leg is a GAP in that leg's trace
// rather than a point at zero -- the same "a missing measurement is not a
// measurement of zero" rule the meters and the scope keep (Principle XI).

#include <QVector>
#include <QWidget>

#include <cstdint>

class QPaintEvent;

namespace AetherSDR {

class DiversityTimeline : public QWidget {
    Q_OBJECT
public:
    // One poll's worth of what the strip draws. Every leg is independently
    // optional in exactly the way /diversity's own fields are.
    struct Sample {
        bool   haveA{false};
        double a{0.0};
        bool   haveB{false};
        double b{0.0};
        bool   haveOut{false};
        double out{0.0};
        // The remembered talker whose weight was live at this sample, if the
        // gate reported one. haveTalker == false is "nobody was talking",
        // which is a fact worth drawing (the band goes to the neutral tone),
        // not an absence.
        bool   haveTalker{false};
        int    talkerId{0};
        bool   steadyQrm{false};
    };

    explicit DiversityTimeline(QWidget* parent = nullptr);

    // `ms` is QDateTime::currentMSecsSinceEpoch() as the CALLER saw it --
    // passed in rather than read here so a test can lay down a controlled
    // history. Samples further than the 120 s window behind `ms` are dropped
    // here, so the buffer never grows without bound.
    void addSample(qint64 ms, const Sample& sample);

    // Empties the strip -- gate gone, or diversity no longer available. A
    // dead gate's last two minutes must not keep scrolling as if they were
    // still arriving.
    void clear();

    // Test/introspection accessors: the widget is a raw QPainter paint with
    // no child widgets, so there is otherwise no way to observe what it holds.
    int sampleCount() const { return int(m_samples.size()); }
    qint64 windowMs() const;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Entry {
        qint64 ms;
        Sample s;
    };

    QVector<Entry> m_samples;
    qint64         m_nowMs{0};
};

} // namespace AetherSDR
