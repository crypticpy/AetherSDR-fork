#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QEvent;
class QJsonArray;
class QJsonObject;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSpinBox;
class QTimer;
class QUrlQuery;

namespace AetherSDR {

class RadioModel;

// The RSPduo dual-tuner-combining section -- everything diversity-related
// used to live directly in this applet; it moved to its own widget when
// AetherGateApplet.cpp outgrew ~1600 lines (see AetherGateDiversityPanel's
// own header comment). This applet still owns the network transport: every
// write the panel wants to make arrives here as a signal and leaves as a GET.
class AetherGateDiversityPanel;

// GATE — Aether-gate device controls.
//
// Aether-gate presents non-Flex hardware (an SDRplay RSP, an HPSDR, an Icom) to
// AetherSDR as a FLEX-6600, which means everything reaches us as Flex wire text.
// That works for the things Flex has verbs for. It has none for an RSPdx's
// antenna port, bias-T, MW/DAB notches, HDR mode or AGC setpoint, so those
// controls could previously only be reached by opening the gate's own web panel
// in a browser. This applet brings them into the app.
//
// Deliberately NOT a fixed set of widgets: the gate reports what the attached
// device actually offers (it asks the driver — listAntennas/getSettingInfo) and
// the controls are built from that answer. An RSP1a, an RSPdx and an RTL stick
// share almost no settings, and hardcoding one device's list is the same
// mistake as hardcoding its sample rates.
//
// Presence is detected by PROBING the gate's control port rather than by
// sniffing the radio serial: the serial is operator-settable (--serial), so a
// renamed gate would vanish from the UI. A gate is whatever answers /status
// with a JSON object; a port that answers anything else is not one.
class AetherGateApplet : public QWidget {
    Q_OBJECT
public:
    // `net` is the transport, or nullptr for a manager of the applet's own.
    // Injectable so the presence state machine can be driven socket-free
    // (tests/aether_gate_applet_test.cpp answers from canned replies).
    explicit AetherGateApplet(QWidget* parent = nullptr,
                              QNetworkAccessManager* net = nullptr);

    void setRadioModel(RadioModel* model);

    // The radio's address, or empty while disconnected. The gate answers at the
    // radio's address: a new address starts a fresh probe, an empty one drops
    // presence at once — the GATE button must not linger for a radio that is
    // gone. setRadioModel() feeds this from the model; it is public so the
    // state machine can be driven without one.
    void setRadioAddress(const QString& ip);

    // True while the gate's control port is answering. AppletPanel uses this
    // to keep the button hidden for operators on a real Flex.
    bool gatePresent() const { return m_present; }

    // Test/introspection accessor — the diversity section's widgets are all
    // found by objectName through AetherGateDiversityPanel's own objectName
    // ("gateDiversityBox"), which findChild() reaches recursively either
    // way; this exists for the handful of behaviours (wantsMapPoll()) that
    // are not a widget property.
    AetherGateDiversityPanel* diversityPanel() const { return m_diversityPanel; }

signals:
    void gatePresenceChanged(bool present);

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private slots:
    void poll();                                  // /status — cheap, on the timer

private:
    QString baseUrl() const;
    void scheduleTimer();                         // cadence from visible/present/address
    void refreshDeviceControls();                 // /device — only when it can have changed
    void applyStatus(const QJsonObject& root, bool isJson);
    void applyDeviceControls(const QJsonObject& dev, bool isJson);
    void buildDeviceControls(const QJsonObject& dev);
    void sendResolution();
    void sendDeviceSet(const QUrlQuery& query);
    void get(const QString& path,
             void (AetherGateApplet::*handler)(const QJsonObject&, bool));
    void setPresent(bool present);

    // Diversity — polled off the same timer as /status (never a second one:
    // see poll()), but NOT through get(): an old gate with no /diversity route
    // 404s, and that says nothing about whether the GATE itself answered, so a
    // diversity failure only hides the section rather than counting toward
    // m_failures/setPresent(false). The JSON itself is parsed here and handed
    // to m_diversityPanel; every write the panel wants to make comes back as
    // one of the on*() slots below, which is where the actual GET lives.
    void pollDiversity();

    // v2 additions — same non-critical-to-presence contract as pollDiversity()
    // above: nothing here ever touches m_failures or setPresent().
    void pollDiversityMap();          // /diversity/map, throttled off m_diversityPanel->wantsMapPoll()

    // AetherGateDiversityPanel's signals, turned into the matching GET. See
    // AetherGateDiversityPanel.h's own comment on each signal for the exact
    // route and guard.
    void onDiversityRequestSet(QUrlQuery query);
    void onDiversityRequestCompareRestore(QUrlQuery query);
    void onDiversityRequestAlign();
    void onDiversityRequestCapture(int seconds);
    void onDiversityRequestMemoryClear();

    QPointer<RadioModel>   m_model;
    QNetworkAccessManager* m_net{nullptr};
    QTimer*                m_timer{nullptr};
    bool                   m_present{false};
    int                    m_failures{0};
    QString                m_ip;                  // the address we are asking
    bool                   m_deviceFetched{false};
    int                    m_pollsSinceDevice{0};

    // Header
    QLabel* m_status{nullptr};

    // Resolution — hidden when the gate predates the "res" status field.
    QWidget*   m_resBox{nullptr};
    QComboBox* m_span{nullptr};
    QComboBox* m_bins{nullptr};
    QLabel*    m_binWidth{nullptr};

    // Device controls, rebuilt from whatever the gate reports.
    QWidget*     m_deviceBox{nullptr};
    QFormLayout* m_deviceForm{nullptr};
    QLabel*      m_deviceHint{nullptr};           // shown when the gate has no /device
    QComboBox*   m_antenna{nullptr};
    QHash<QString, QWidget*> m_settingWidgets;    // Soapy setting key -> control
    QString      m_controlsFingerprint;           // rebuild only when the SET changes

    // Diversity — RSPduo dual-tuner combining. See AetherGateDiversityPanel's
    // own header comment; this applet only feeds it JSON and turns its
    // signals into GETs.
    AetherGateDiversityPanel* m_diversityPanel{nullptr};
    // /diversity/map cadence — network-polling state, so it stays here
    // rather than on the panel (which owns presentation, not the transport).
    // Reset whenever m_diversityPanel->wantsMapPoll() goes false (panel
    // hidden, or the Noise section collapsed) so the next time it goes true
    // the map is fetched immediately rather than waiting out a stale count.
    bool m_mapFetched{false};
    int  m_pollsSinceMap{0};
};

} // namespace AetherSDR
