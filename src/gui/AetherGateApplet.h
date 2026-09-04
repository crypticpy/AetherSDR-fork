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
class SliceModel;

// The RSPduo dual-tuner-combining section -- everything diversity-related
// used to live directly in this applet; it moved to its own widget when
// AetherGateApplet.cpp outgrew ~1600 lines (see AetherGateDiversityPanel's
// own header comment). This applet still owns the network transport: every
// write the panel wants to make arrives here as a signal and leaves as a GET.
class AetherGateDiversityPanel;
class AudioEngine;

// The line under the connection status: what the gate has plugged in, and --
// on a device that can combine two tuners -- the switch that stops it and the
// A/B selector that says which tuner is left feeding the receiver. Its writes
// come back here as requestDiversitySet() and leave through
// onDiversityRequestSet(), so the applet still owns every socket in the
// section. See AetherGateDeviceStrip.h.
class AetherGateDeviceStrip;

// The BAND page's own transport: /diversity/spatial at 4 Hz and
// /diversity/finder at 1 Hz, off a timer of their own because neither cadence
// fits the 1 Hz one /status and /diversity share. Constructed with THIS
// applet's QNetworkAccessManager and owned by it, so the section still has one
// transport; see DiversityBandPoller.h for why it is a separate file.
class DiversityBandPoller;

// CHAIN -- the filter chain as a block diagram, opened from the button beside
// the Diversity one. Owned here rather than by the diversity panel because the
// chain is a receiver feature and not a two-tuner one: it works on any device
// the gate fronts. It owns no transport either -- /filter reaches it through
// the same DiversityBandPoller the FILTER page uses, and its writes come back
// as onChainRequestWrite(). See AetherGateChainWindow.h.
class AetherGateChainWindow;

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
//
// The class is defined across three translation units, the way DiversityWindow
// is across DiversityWindowBand.cpp and its siblings -- AetherGateApplet.cpp
// had outgrown the file-size budget AGENTS.md asks for:
//
//   * AetherGateApplet.cpp          -- construction, the presence state
//                                      machine, /status and /resolution.
//   * AetherGateAppletControls.cpp  -- /device: the ArgInfo-typed control rows
//                                      and the /device/set write behind them.
//   * AetherGateAppletDiversity.cpp -- /diversity and its routes, the CHAIN
//                                      window, and the one request that never
//                                      reaches the gate (the BAND page's tune).
//
// AetherGateAppletShared.h carries the two file-local helpers more than one of
// the three needs.
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

    // The CHAIN window, or null until the button has been pressed once. Built
    // lazily for the same reason the Diversity window is.
    AetherGateChainWindow* chainWindow() const;

    // The RX audio the operator hears, for the CHAIN window's VISUAL tab to
    // analyse locally. Held here because that window is built lazily; a null
    // engine (tests) simply leaves the local trace off.
    void setAudioEngine(AudioEngine* audio);

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
    // A write whose answer is not read -- see the definition.
    void sendFireAndForget(const QString& path, const QUrlQuery& query,
                           int timeoutMs);
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
    // One shape for the three diversity writes whose reply IS the read-back:
    // build base + `path` + `query`, GET it, and hand the answer to the panel.
    // `requirePresent` is false for exactly one caller — see
    // onDiversityRequestCompareRestore().
    void sendDiversityWrite(const QString& path, const QUrlQuery& query,
                            bool requirePresent);
    void onDiversityRequestSet(QUrlQuery query);
    void onDiversityRequestCompareRestore(QUrlQuery query);
    void onDiversityRequestAlign();
    void onDiversityRequestCapture(int seconds);
    void onDiversityRequestMemoryClear();
    void onDiversityRequestMemoryName(int id, QString name);
    // The one diversity request that does NOT go to the gate: the gate has no
    // tune route, so a click on the BAND page tunes AetherSDR's own active
    // slice. `hz` is absolute.
    // The slice a diversity write is about: the active one, or the first if
    // none is marked active. Null with no radio model or no slices.
    SliceModel* activeSlice() const;
    void onDiversityRequestTune(double hz);

    // CHAIN -- built on first use and then kept; every write it makes is one
    // GET on the gate's own route, sent through the filter poller so the reply
    // IS the read-back the window redraws from.
    void toggleChainWindow();
    void onChainRequestWrite(QString route, QUrlQuery query);
    // Starts or stops the band poller from the panel's wantsBandPoll(), and
    // (separately) from m_diversityAvailable -- see DiversityBandPoller::
    // setBandAvailable()'s own comment for why the two are not the same gate.
    void updateBandPoll();

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

    // The device line and its two diversity controls, fed from /status's
    // optional "device" object.
    AetherGateDeviceStrip* m_deviceStrip{nullptr};

    // CHAIN -- the door and the window behind it.
    QPushButton*                   m_openChainButton{nullptr};
    QPointer<AetherGateChainWindow> m_chainWindow;
    AudioEngine* m_audio{nullptr};

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
    // Reset whenever m_diversityPanel->wantsMapPoll() goes false (the
    // pop-out Diversity window closed, which is the only view of the map)
    // so the next time it goes true the map is fetched immediately rather
    // than waiting out a stale count.
    bool m_mapFetched{false};
    int  m_pollsSinceMap{0};
    // /diversity/spatial + /diversity/finder. Runs at 4 Hz while the window
    // is open on its BAND page (see updateBandPoll()) and in the background
    // at 1 Hz whenever m_diversityAvailable holds, whatever page (or no
    // window at all) is on screen.
    DiversityBandPoller* m_bandPoller{nullptr};
    // Set from every /diversity poll's own top-level "available" flag --
    // whether the gate currently reports a dual-tuner pair, not whether the
    // JSON parsed or the panel is showing anything. Feeds
    // DiversityBandPoller::setBandAvailable() through updateBandPoll() so the
    // BAND page's history keeps filling while nobody is looking at it.
    bool m_diversityAvailable{false};
};

} // namespace AetherSDR
