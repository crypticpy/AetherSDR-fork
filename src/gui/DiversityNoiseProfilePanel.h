#pragma once

// The SITE page's NOISE PROFILE: "what KIND of noise is this, where in the
// house is it coming from, and what can this receiver do about it?"
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
// THE ROWS. Those four sentences say what is there. The gate's `kinds` array
// says what to DO about each one, and that is what the table under them is:
// one row per finding, each carrying the gate's own verdict (the label and the
// detail it wrote), the window it was measured over, its size in decibels, and
// ONE button -- the action the gate itself nominated. A row with no action
// carries the gate's reason instead, on the disabled button's hover, because
// "there is nothing to do about this" is a finding too and an operator who is
// not told it will go looking for the control that is missing.
//
// Nothing here invents an action. The route and the query string are the gate's
// own, sent back verbatim (docs/DIVERSITY.md, "Acting on the noise profile"):
// this panel never composes a set of its own from a kind name, so a gate that
// grows a new kind gets a working button in this window without a new build.
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
// It owns no transport and no timer beyond the one that expires a refusal
// message: DiversityWindow feeds it one applyProfile() per /diversity poll, and
// every action leaves as actionRequested().

#include <QString>
#include <QStringList>
#include <QUrlQuery>
#include <QVector>
#include <QWidget>

class QJsonObject;
class QJsonValue;
class QLabel;
class QPaintEvent;
class QTableWidget;
class QTimer;

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

    // The reply to one of this panel's own action buttons. Only an
    // {"error": ...} body means anything here -- the row's own state comes back
    // on the next /diversity poll, which is a quarter of a second away and is
    // the gate's answer rather than our optimism. Ignored entirely unless this
    // panel is the one that asked, so the beacon panel's refusals do not land
    // on the noise panel's status line.
    void applyActionReply(const QJsonObject& reply);

    // Gate gone, or diversity no longer available.
    void clear();

    QTableWidget* kindsTable() const { return m_kinds; }

signals:
    // -> GET <route>?<query> on the gate, taken verbatim from the row's own
    // "action" object. The two routes the gate uses today are /diversity/set
    // and /filter/notch; nothing here depends on which, which is the point.
    void actionRequested(QString route, QUrlQuery query);

private:
    // Rebuilds the kinds table from one "kinds" array. Only on change: at 1 Hz
    // an unchanged list would otherwise destroy and re-create one button per
    // row every second, and the button under the pointer would be a different
    // object on every frame.
    void applyKinds(const QJsonValue& kinds);
    void showTransient(const QString& text);

    QLabel* m_verdict{nullptr};
    QLabel* m_impulses{nullptr};
    QLabel* m_periodic{nullptr};
    QLabel* m_seconds{nullptr};
    QLabel* m_subband{nullptr};
    QLabel* m_status{nullptr};
    QTimer* m_statusTimer{nullptr};
    QTableWidget* m_kinds{nullptr};
    DiversityNoiseHistoryStrip* m_strip{nullptr};

    // Last rendered rows, one packed string each -- see applyKinds().
    QStringList m_kindRows;
    // Set from the moment an action button is pressed until its reply lands,
    // so a refusal is shown by the panel that caused it and by no other.
    bool m_actionPending{false};
};

} // namespace AetherSDR
