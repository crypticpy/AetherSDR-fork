#pragma once

#include <QUrlQuery>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QJsonArray;
class QJsonObject;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace AetherSDR {

// Same include-surface discipline AetherGateApplet.h already keeps for
// these two: nothing outside the .cpp that builds one needs the full type.
class DiversityMapStrip;
class DiversityScope;

// AetherGateDiversityPanel — the RSPduo dual-tuner-combining section of
// AetherGateApplet, split into its own widget because AetherGateApplet.cpp
// had grown past ~1600 lines carrying this section alone (AGENTS.md: "files
// must not grow like this").
//
// This widget owns every diversity CONTROL and its presentation: the
// mode/phase/ratio/source combos, the read-only DiversityScope, the
// noise-blanker/pan/map/sources/memory/capture rows, and the four
// collapsible section headers (Combine/Listen/Noise/Memory & capture) the
// operator asked for ("the UIUX could be nicer ... maybe there should be
// some subsections"). It owns NO network transport: AetherGateApplet keeps
// the QNetworkAccessManager (and stays the only file the network-timeout
// ratchet in tools/check_network_timeouts.py has to reason about for the
// gate section). Every write this panel wants to make is a signal;
// AetherGateApplet turns each into the matching GET and feeds the read-back
// back in through applyDiversity().
//
// objectNames and accessible names are unchanged from before the
// extraction — tests/aether_gate_applet_test.cpp finds every one of them by
// name via AetherGateApplet::findChild(), which is recursive, so moving the
// widgets one level deeper under this panel does not change what a test can
// find.
class AetherGateDiversityPanel : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateDiversityPanel(QWidget* parent = nullptr);

    // Applied on every /diversity poll AND every write's read-back — same
    // contract AetherGateApplet::applyDiversity() always had. isJson==false,
    // or an "available": false payload, both mean "nothing to show": the
    // panel hides itself and clears the scope. Every v2 field is
    // independently optional on the wire; a missing or malformed one leaves
    // its widget at whatever it already showed rather than inventing a
    // value.
    void applyDiversity(const QJsonObject& diversity, bool isJson);

    // Feeds one /diversity/map answer into the map strip. An empty/error
    // object clears it — DiversityMapStrip's own setMap() decides what
    // "nothing to draw" looks like.
    void applyMap(const QJsonObject& map);

    // A capture request's own reply: ok==true with a (possibly empty) file
    // path on success, ok==false with the text to show instead (a network
    // failure, a non-JSON body, or the gate's own {"error": ...}). Mirrors
    // the old sendDiversityCapture()'s three failure branches plus its
    // success path, now split so only the network part stays in the applet.
    void applyCaptureResult(bool ok, const QString& pathOrError);

    // present/absent — mirrors AetherGateApplet::setPresent(): false resets
    // every row/label/widget that must not outlive a reconnect to a
    // different (or older) gate at the same address, and unconditionally
    // ends the "Hear A only" hold (restoreCompareHold() below) so the gate
    // is never left stuck in "off" just because nobody released the button.
    void setPresent(bool present);

    // Ends the "Hear A only" compare hold if (and only if) it is currently
    // down — idempotent, so every one of the paths that can end it (the
    // button's own released(), FocusOut/Hide via this widget's eventFilter,
    // AetherGateApplet::setRadioAddress() before it reassigns the address,
    // and setPresent(false) above) can call this unconditionally without
    // coordinating with each other. Emits requestCompareRestore() with the
    // mode to resume when there is one to send.
    void restoreCompareHold();

    // True when the panel itself is visible AND the Noise section (which
    // holds the map strip) is expanded. AetherGateApplet gates its
    // /diversity/map poll on this so a collapsed Noise block, same as a
    // hidden panel, costs no polling.
    bool wantsMapPoll() const;

signals:
    // -> GET /diversity/set, guarded by the applet's own presence check —
    // same contract the old sendDiversitySet() enforced internally.
    void requestSet(QUrlQuery query);
    // -> GET /diversity/set for the compare-hold's forced resume. NOT
    // gated on presence — see restoreCompareHold()'s comment on why the old
    // restoreDiversityCompareMode() bypassed that guard on purpose.
    void requestCompareRestore(QUrlQuery query);
    // -> GET /diversity/align
    void requestAlign();
    // -> GET /diversity/capture?seconds=<seconds>
    void requestCapture(int seconds);
    // -> GET /diversity/memory/clear
    void requestMemoryClear();

protected:
    // Watches m_compareButton for FocusOut/Hide — see restoreCompareHold()'s
    // comment.
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // Builds one caption's QToolButton header wired to show/hide `content`
    // and persist its own open state in AppSettings, then adds both to
    // `root`. Returns the header so callers that need to read it back
    // (Noise, for wantsMapPoll()) can keep a pointer.
    QToolButton* addCollapsibleSection(QVBoxLayout* root, const QString& caption,
                                        const QString& objectNameSuffix,
                                        const QString& settingsKey, bool defaultExpanded,
                                        QWidget* content);
    void applySources(const QJsonArray& sources);
    void setCaptureResultLabel(const QString& path);

    // --- Combine ----------------------------------------------------------
    QComboBox*      m_mode{nullptr};
    QSlider*         m_phase{nullptr};
    QLabel*          m_phaseValue{nullptr};
    QDoubleSpinBox*  m_ratio{nullptr};
    QTimer*          m_phaseDebounce{nullptr};   // ~150ms so a drag sends once
    QTimer*          m_ratioDebounce{nullptr};
    DiversityScope*  m_scope{nullptr};

    // --- Listen -------------------------------------------------------------
    QComboBox*   m_source{nullptr};
    QPushButton* m_realign{nullptr};
    QLabel*      m_statusLine{nullptr};

    // "Hear A only" — a press-and-hold A/B compare that never leaves the
    // gate stuck off: m_compareDown is true only between a confirmed press
    // and the (exactly one) restore that follows it — see
    // restoreCompareHold()'s comment.
    QPushButton* m_compareButton{nullptr};
    QString      m_compareResumeMode;
    bool         m_compareDown{false};

    // --- Noise --------------------------------------------------------------
    QCheckBox*         m_nbCheck{nullptr};
    QDoubleSpinBox*    m_nbSpin{nullptr};
    QTimer*            m_nbDebounce{nullptr};
    QComboBox*         m_panCombo{nullptr};
    DiversityMapStrip* m_mapStrip{nullptr};
    QListWidget*       m_sourcesList{nullptr};
    QPushButton*       m_nullSourceButton{nullptr};
    QToolButton*       m_noiseHeader{nullptr};   // wantsMapPoll() reads its checked state

    // --- Memory & capture -----------------------------------------------
    QLabel*      m_memoryLabel{nullptr};
    QPushButton* m_memoryClearButton{nullptr};
    QSpinBox*    m_captureSpin{nullptr};
    QPushButton* m_captureButton{nullptr};
    QLabel*      m_captureLabel{nullptr};

    QString m_lastMode;             // clears m_captureLocalResult on a mode change
    // Set by applyCaptureResult(false, ...); while set, applyDiversity()'s
    // capture.active==false branch must not overwrite the label with the
    // poll's (possibly stale) "path" — see applyCaptureResult()'s header
    // comment.
    bool m_captureLocalResult{false};

    // Guards onComparePressed()/the capture button's click handler — both
    // used to be gated on AetherGateApplet::m_present directly; this panel
    // is fed the same flag through setPresent() since it now owns both
    // handlers. present==true implies the applet's baseUrl() is non-empty
    // (setPresent(true) only ever follows a real reply), so this one flag
    // is the same guard the applet's combined "baseUrl().isEmpty() ||
    // !m_present" check used to be.
    bool m_present{false};
};

} // namespace AetherSDR
