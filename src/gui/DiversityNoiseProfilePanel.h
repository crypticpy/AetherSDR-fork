#pragma once

// The SITE page's NOISE PROFILE: "what KIND of noise is this, and where in the
// house is it coming from?"
//
// Every other noise readout in the Diversity window is a number about how much
// noise there is. None of them says what it IS. The gate's own profiler answers
// that from the shape of the noise rather than its size, and this panel is the
// view of that answer:
//
//   * a mains verdict -- is there a hum comb locked to 50 or 60 Hz, how loud,
//     and how many harmonics of it. A 100 or 120 Hz comb is a rectifier: a
//     switching supply, an LED driver, a dimmer, a charger. That is a thing you
//     can walk around the house and unplug, which no dB figure ever told
//     anybody.
//   * the impulse rate and size -- fences, vehicle ignition, arcing insulators,
//     power-line telecoms. A rate is what distinguishes an electric fence (one
//     or two a second, like a metronome) from an arcing joint (a continuous
//     rasp).
//   * the strongest lines that are NOT mains harmonics -- a single tone at some
//     odd frequency is usually one device's own oscillator.
//   * a two-minute strip of both, because "is it getting worse?" and "did
//     unplugging that help?" are questions no instantaneous readout answers.
//
// It also carries the one line about the per-bin refinement (`subband`), which
// belongs here for the same reason the rest does: it is a fact about how the
// combiner is treating THIS site's noise, not about the station you are on.
//
// Nothing here is invented. `noise_profile` is null until the gate has aligned
// the tuners and measured, every field inside it is independently optional, and
// a field that did not arrive renders as an em dash rather than as a zero that
// would read as a measurement.
//
// It owns no transport and no timer: DiversityWindow feeds it one
// applyProfile() per /diversity poll.

#include <QVector>
#include <QWidget>

class QJsonValue;
class QLabel;
class QPaintEvent;

namespace AetherSDR {

// pushSample() is deliberately not one of check_a11y.py's watched setter names:
// the strip is a glance-view of the two numbers the labels beside it already
// state in words, so it carries nothing a screen reader cannot reach -- the
// same "decorative vs. data-carrying" split DiversityActivityStrip takes.
class DiversityNoiseHistoryStrip : public QWidget {
    Q_OBJECT
public:
    explicit DiversityNoiseHistoryStrip(QWidget* parent = nullptr);

    // One poll's worth of history. A leg the gate did not report is pushed as
    // invalid rather than as zero: a gap in the line is honest, a run of zeros
    // is a claim that the hum stopped.
    void pushSample(double impulsesPerS, bool haveImpulses, double humDb, bool haveHum);
    void clearHistory();

    int sampleCount() const { return int(m_samples.size()); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Sample {
        float impulses{0.0f};
        float hum{0.0f};
        bool  haveImpulses{false};
        bool  haveHum{false};
    };
    QVector<Sample> m_samples;
};

class DiversityNoiseProfilePanel : public QWidget {
    Q_OBJECT
public:
    explicit DiversityNoiseProfilePanel(QWidget* parent = nullptr);

    // One /diversity status object's "noise_profile" value. A null (or absent,
    // or malformed) value is "the gate has not profiled yet" -- said in words,
    // with every readout dashed, never as a profile of silence.
    void applyProfile(const QJsonValue& profile);

    // The same status object's "subband" value: the per-bin refinement of the
    // tracked weight. A gate too old to have the key gets a dash.
    void applySubband(const QJsonValue& subband);

    // Gate gone, or diversity no longer available.
    void clear();

private:
    QLabel* m_verdict{nullptr};
    QLabel* m_impulses{nullptr};
    QLabel* m_periodic{nullptr};
    QLabel* m_seconds{nullptr};
    QLabel* m_subband{nullptr};
    DiversityNoiseHistoryStrip* m_strip{nullptr};
};

} // namespace AetherSDR
