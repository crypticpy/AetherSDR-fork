#pragma once

// AetherGateDeviceStrip -- what the gate has plugged in, and the way out of
// diversity.
//
// Two questions an operator asks in the first second, and until now the applet
// answered neither. "gate connected - streaming" says a gate is there; it does
// not say whether the thing behind it is the RSPduo running two tuners, the
// RSPdx on one, or an RTL stick. And once diversity was running there was no
// control in the app that could stop it: the only way back to a single tuner
// was the gate's own web panel, or a restart.
//
// So: one line naming the device, and -- only on a device that can actually do
// it -- a DIVERSITY switch and an A/B tuner selector beside it.
//
// Everything here follows the gate's answer. A click emits a write and then
// re-asserts the state the last /status reported, so the switch shows what the
// receiver IS doing rather than what it was just asked to do; the next poll
// (1 Hz) is what moves it. The strip owns no transport at all -- its writes
// leave as requestDiversitySet() and go out through the applet's one
// /diversity/set path, the same one every other diversity write uses.

#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QJsonValue;
class QLabel;
class QPushButton;

namespace AetherSDR {

class AetherGateDeviceStrip : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateDeviceStrip(QWidget* parent = nullptr);

    // GET /status's "device": {driver, model, serial, hardware_key, tuners,
    // diversity: {capable, running, mode, tuner}, label}.
    //
    // OPTIONAL by contract: a gate older than the field sends no "device" at
    // all, and so does a gate that has not opened a device yet. Either way the
    // line reads "device: -" and the controls go away -- a dash is honest,
    // where a serial guessed from the radio name would not be (the gate's
    // --serial is operator-settable, which is why presence is probed rather
    // than sniffed; see AetherGateApplet.h).
    void applyDevice(const QJsonValue& device);

signals:
    // -> GET /diversity/set?<query>, routed through AetherGateApplet's own
    // onDiversityRequestSet() exactly like AetherGateDiversityPanel's writes.
    // The reply IS the read-back, so the applet feeds it back to the diversity
    // panel and the next /status poll re-renders this strip.
    void requestDiversitySet(QUrlQuery query);

private:
    // mode= plus the source/pan pair that goes with it. "combined" is both
    // tuners summed (diversity proper); "a"/"b" is one tuner feeding both the
    // audio and the panadapter, which is what leaving diversity means.
    void sendSet(const QString& mode, const QString& feed);
    // Redraw from the last device object and nothing else.
    void render();
    // The tuner an "off" write should land on: the selected one, else A.
    QString feedTuner() const;

    QLabel*      m_label{nullptr};
    QPushButton* m_diversity{nullptr};
    QPushButton* m_tunerA{nullptr};
    QPushButton* m_tunerB{nullptr};

    // Last answer from the gate, reduced to what this strip draws.
    QString m_text;                 // the gate's own label line, or empty
    bool    m_capable{false};       // device.diversity.capable
    bool    m_running{false};       // device.diversity.running
    QString m_tuner;                // device.diversity.tuner: "a", "b", "both"
};

} // namespace AetherSDR
