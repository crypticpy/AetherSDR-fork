#pragma once

// DiversityWindow -- the RSPduo dual-tuner combiner as a piece of station
// equipment rather than a sidebar strip.
//
// AetherGateDiversityPanel has to live inside a ~250px applet column, and
// every design decision in it is a concession to that: a 176px scope, a
// four-row sources list, phase/ratio on a slider and a spinbox, four
// collapsible blocks so the whole section fits at all. This window is the
// same state with the width to show it properly -- a large scope, real
// per-antenna meters, the remembered-station list as a table, and the
// alignment/capture numbers as fixed-width fields you can read at a glance
// from across the shack.
//
// It owns NO network transport and NO state of its own. Every payload
// arrives through applyDiversity()/applyMap()/applyCaptureResult() exactly as
// the sidebar panel receives it, and every write leaves as one of the same
// five request signals the panel emits -- DiversityWindow::createFor() wires
// them straight through to the panel's own signals, so AetherGateApplet keeps
// being the one place a gate request is built. That is also why a change made
// here shows up in the sidebar and vice versa: both are views of the same
// polled state, neither echoes locally.
//
// Layout, top to bottom:
//   * chain row  -- MODE (off/manual/null/track), HEAR (combined/A/B), the
//                   hold-to-compare "Hear A only", REALIGN, and CAPTURE.
//   * scope row  -- DiversityScope in large mode; the only row that stretches.
//   * ANTENNAS / NOISE      -- meters + manual weight, blanker + noise map.
//   * STATIONS  / ALIGNMENT & CAPTURE.
//   * status strip mirroring the applet's own presence line.
//
// Nothing in it moves or resizes on a poll: every numeric readout has a fixed
// field width, the stations table has fixed column widths and a fixed height,
// and no widget is shown or hidden by data.

#include "gui/PersistentDialog.h"

#include <QString>
#include <QStringList>
#include <QUrlQuery>

class QButtonGroup;
class QCloseEvent;
class QJsonArray;
class QJsonObject;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace AetherSDR {

class AetherGateDiversityPanel;
class ClientCompKnob;
class DiversityMapStrip;
class DiversityScope;
class DiversitySnrMeter;

class DiversityWindow : public PersistentDialog {
    Q_OBJECT
public:
    explicit DiversityWindow(QWidget* parent = nullptr);

    // Builds a window for `panel` and connects its five request signals
    // straight through to the panel's own identically-named ones, so a write
    // made here takes exactly the route a write made in the sidebar does.
    // The window is parented to the panel: it is a top-level either way (a
    // QDialog), but the parent keeps it in front of the main window and gets
    // it destroyed with the applet.
    static DiversityWindow* createFor(AetherGateDiversityPanel* panel);

    // Same three payload entry points AetherGateDiversityPanel has, with the
    // same contract: every field is independently optional, a missing or
    // malformed one leaves its widget alone, and isJson == false or
    // "available": false clears everything.
    void applyDiversity(const QJsonObject& diversity, bool isJson);
    void applyMap(const QJsonObject& map);
    void applyCaptureResult(bool ok, const QString& pathOrError);

    // Mirrors AetherGateApplet's own presence flag: false clears every
    // readout and greys the status strip, but leaves the window open -- the
    // operator opened it, and a dropped poll is not a reason to take it away.
    void setPresent(bool present);

signals:
    void requestSet(QUrlQuery query);
    void requestCompareRestore(QUrlQuery query);
    void requestAlign();
    void requestCapture(int seconds);
    void requestMemoryClear();

protected:
    // Persists DiversityWindowVisible; PersistentDialog's own override saves
    // the geometry.
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildChainRow();
    QWidget* buildAntennasPanel();
    QWidget* buildNoisePanel();
    QWidget* buildStationsPanel();
    QWidget* buildAlignmentPanel();

    // One exclusive row of checkable buttons (MODE, HEAR, PAN). `values` are
    // the wire values; `key` is the /diversity/set query key each button
    // writes. Returns the group so applyDiversity() can check a button back
    // without re-emitting.
    QButtonGroup* addButtonRow(QWidget* row, const QString& caption, const QString& key,
                               const QString& objectPrefix, const QStringList& labels,
                               const QStringList& values);
    // Checks the button carrying `value` without emitting a write.
    static void checkValue(QButtonGroup* group, const QString& value);

    void applyMemory(const QJsonArray& memory);
    void clearReadouts();
    // True while the operator is holding/editing `knob` or its debounce has a
    // write pending -- a poll must not move it out from under either.
    static bool knobBusy(const ClientCompKnob* knob, const QTimer* debounce);

    // --- chain row --------------------------------------------------------
    QButtonGroup* m_modeGroup{nullptr};
    QButtonGroup* m_hearGroup{nullptr};
    QPushButton*  m_compareButton{nullptr};
    QPushButton*  m_realignButton{nullptr};
    QPushButton*  m_captureButton{nullptr};
    QSpinBox*     m_captureSpin{nullptr};
    QString       m_compareResumeMode;
    bool          m_compareDown{false};

    // --- scope ------------------------------------------------------------
    DiversityScope* m_scope{nullptr};

    // --- antennas ---------------------------------------------------------
    DiversitySnrMeter* m_meterA{nullptr};
    DiversitySnrMeter* m_meterB{nullptr};
    DiversitySnrMeter* m_meterOut{nullptr};
    QLabel*            m_manualCaption{nullptr};
    ClientCompKnob*    m_phaseKnob{nullptr};
    ClientCompKnob*    m_ratioKnob{nullptr};
    QTimer*            m_phaseDebounce{nullptr};
    QTimer*            m_ratioDebounce{nullptr};

    // --- noise ------------------------------------------------------------
    QPushButton*       m_nbButton{nullptr};
    ClientCompKnob*    m_nbKnob{nullptr};
    QTimer*            m_nbDebounce{nullptr};
    QButtonGroup*      m_panGroup{nullptr};
    DiversityMapStrip* m_mapStrip{nullptr};
    QListWidget*       m_sourcesList{nullptr};
    QPushButton*       m_nullSourceButton{nullptr};
    QLabel*            m_noiseStatus{nullptr};

    // --- stations ---------------------------------------------------------
    QTableWidget* m_stations{nullptr};
    // Last rendered table content, one "col|col|col|col" per row -- the table
    // is rebuilt only when this changes, so an unchanged memory list does not
    // drop the operator's selection or scroll position on every poll.
    QStringList   m_stationRows;
    QLabel*       m_stationsCount{nullptr};
    QPushButton*  m_memoryClearButton{nullptr};

    // --- alignment & capture ----------------------------------------------
    QLabel* m_alignedValue{nullptr};
    QLabel* m_lagValue{nullptr};
    QLabel* m_peakValue{nullptr};
    QLabel* m_realigningValue{nullptr};
    QLabel* m_captureResult{nullptr};

    QLabel* m_statusStrip{nullptr};

    bool m_present{false};
    // Set by applyCaptureResult(false, ...) -- while set, a poll's own
    // (possibly stale) capture.path must not overwrite the error this
    // request just reported. Same guard the sidebar panel keeps.
    bool m_captureLocalResult{false};
};

} // namespace AetherSDR
