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
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QUrlQuery;

namespace AetherSDR {

class RadioModel;

// Paints /diversity/map's coherence bars + source brackets. Defined in its
// own DiversityMapStrip.{h,cpp}: nothing outside this applet consumes it, so
// only the .cpp that builds one includes that header, the way the
// device-control widgets already keep out of THIS header's include surface
// (they are built as plain QWidget* and typed only where the .cpp needs to).
class DiversityMapStrip;

// Read-only weight/SNR/status visualisation, replacing the numbers that used
// to live in the ever-changing status line -- see its own header comment.
// Same include-surface discipline as DiversityMapStrip above.
class DiversityScope;

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

signals:
    void gatePresenceChanged(bool present);

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    // Watches m_diversityCompareButton for FocusOut/Hide -- the two ways the
    // "Hear A only" hold can end besides its own released() signal (see
    // restoreDiversityCompareMode()'s header comment).
    bool eventFilter(QObject* obj, QEvent* event) override;

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
    // m_failures/setPresent(false).
    void pollDiversity();
    void applyDiversity(const QJsonObject& d, bool isJson);
    void applyDiversitySources(const QJsonArray& sources);
    void sendDiversitySet(const QUrlQuery& query);
    void sendDiversityAlign();

    // "Hear A only" compare hold — see the header comment on
    // m_diversityCompareButton. onDiversityComparePressed() records the mode
    // to return to and sends mode=off; restoreDiversityCompareMode() sends
    // that mode back and is safe to call from anywhere (released(), the
    // eventFilter's FocusOut/Hide, setRadioAddress() and setPresent(false))
    // because m_diversityCompareDown makes every call after the first a
    // no-op — the gate must never be left stuck in "off".
    void onDiversityComparePressed();
    void restoreDiversityCompareMode();

    // v2 additions — same non-critical-to-presence contract as pollDiversity()
    // above: nothing here ever touches m_failures or setPresent().
    void pollDiversityMap();          // /diversity/map, throttled off applyDiversity()
    void sendDiversityCapture();
    void sendDiversityMemoryClear();

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

    // Diversity — RSPduo dual-tuner combining. Hidden until /diversity reports
    // "available": true.
    QWidget*        m_diversityBox{nullptr};
    QComboBox*      m_diversityMode{nullptr};
    QSlider*        m_diversityPhase{nullptr};
    QLabel*         m_diversityPhaseValue{nullptr};
    QDoubleSpinBox* m_diversityRatio{nullptr};
    QComboBox*      m_diversitySource{nullptr};
    QPushButton*    m_diversityRealign{nullptr};
    QLabel*         m_diversityStatusLine{nullptr};
    QTimer*         m_diversityPhaseDebounce{nullptr};   // ~150ms so a drag sends once
    QTimer*         m_diversityRatioDebounce{nullptr};

    // Read-only weight/SNR/status visualisation — see DiversityScope's own
    // header comment for why the numbers that used to grow the status line
    // now live here instead.
    DiversityScope* m_diversityScope{nullptr};

    // "Hear A only" — a press-and-hold A/B compare that never leaves the
    // gate stuck off: m_diversityCompareDown is true only between a
    // confirmed press and the (exactly one) restore that follows it, so
    // every one of the four ways the hold can end (released(), FocusOut,
    // Hide, setPresent(false)/a radio-address change) can call
    // restoreDiversityCompareMode() unconditionally and only the first one
    // actually sends anything.
    QPushButton*    m_diversityCompareButton{nullptr};
    QString         m_diversityCompareResumeMode;
    bool            m_diversityCompareDown{false};

    // Diversity v2 — noise blanker, pan select, the noise map, the source
    // list + null-selected, memory, and a one-shot capture. Every field here
    // is optional on the wire (an older gate lacks all of it), so each is
    // only touched in applyDiversity() when its key is actually present —
    // absence leaves the widget at the default set in the constructor.
    QCheckBox*      m_diversityNbCheck{nullptr};
    QDoubleSpinBox* m_diversityNbSpin{nullptr};
    QTimer*         m_diversityNbDebounce{nullptr};
    QComboBox*      m_diversityPanCombo{nullptr};
    DiversityMapStrip* m_diversityMapStrip{nullptr};
    bool            m_mapFetched{false};
    int             m_pollsSinceMap{0};
    QListWidget*    m_diversitySourcesList{nullptr};
    QPushButton*    m_diversityNullSourceButton{nullptr};
    QLabel*         m_diversityMemoryLabel{nullptr};
    QPushButton*    m_diversityMemoryClearButton{nullptr};
    QSpinBox*       m_diversityCaptureSpin{nullptr};
    QPushButton*    m_diversityCaptureButton{nullptr};
    QLabel*         m_diversityCaptureLabel{nullptr};
    QString         m_lastDiversityMode;      // clears m_captureLocalResult on a mode change
    // Set by sendDiversityCapture()'s own reply (an error, or a body that
    // failed to parse); while set, applyDiversity()'s capture.active==false
    // branch must not overwrite the label with the poll's (possibly stale)
    // "path" — see the header comment on sendDiversityCapture().
    bool            m_captureLocalResult{false};
};

} // namespace AetherSDR
