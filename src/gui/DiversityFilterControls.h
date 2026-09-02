#pragma once

// DiversityFilterControls -- the whole body of the Diversity window's FILTER
// page: the caption over the response curve, the curve itself, the one status
// line, and the four columns of controls under it.
//
// A widget of its own rather than four more builder members on DiversityWindow
// for the reason DiversityWindowPanels.cpp and DiversityWindowBand.cpp exist,
// and then one better one. The size argument is the same: DiversityWindow.cpp
// is at the file-size budget AGENTS.md asks for and this page is the largest
// of the four. The better reason is that this page, unlike the other three,
// keeps NO window state at all -- it is a pure function of one /filter status
// object plus whichever control the operator currently has hold of. Making
// that a class boundary rather than thirty more m_filter* members on
// DiversityWindow is what keeps "the gate is the source of truth" checkable:
// there is exactly one method that writes to these widgets, and exactly one
// signal by which they write to the gate.
//
// THE FIGHT BETWEEN A POLL AND A HAND. /filter is polled twice a second, so
// every control on this page is being overwritten every 500 ms. That is the
// right default -- the gate owns the filter, the auto-width tracker can move
// the edges without being asked, and a page that quietly disagreed with the
// radio would be worse than no page. But a spin box the operator is halfway
// through typing into, and a passband edge they are dragging, are the two
// places where "the gate is authoritative" produces nonsense. Both are
// excluded by the same rule and nothing else is: a widget with focus, or a
// drag in progress, is not written from a status object.
//
// Every write is immediate -- there is no Apply button and no debounce, because
// every control here is either a discrete choice (a shape, an AGC mode, a
// checkbox) or a spin box committed on editingFinished. The reply to a write
// is the same status object a poll returns, so a set and the read-back after it
// are one request, and the page is showing the gate's answer rather than its
// own optimism within a single round trip.

#include <QString>
#include <QStringList>
#include <QUrlQuery>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QJsonArray;
class QJsonObject;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace AetherSDR {

class DiversityFilterPanel;

class DiversityFilterControls : public QWidget {
    Q_OBJECT
public:
    explicit DiversityFilterControls(QWidget* parent = nullptr);

    // One /filter answer, or the identical object a /filter/set or
    // /filter/notch write replies with. Three shapes arrive here and they mean
    // three different things:
    //
    //   * an EMPTY object -- the request failed or the gate has no such route.
    //     Nothing is changed: a dropped poll must not empty a page that was
    //     showing a working filter a moment ago.
    //   * {"error": "..."} -- the gate refused a value. The text goes on the
    //     status line for a few seconds and no control moves; the next poll
    //     puts the refused control back where the gate actually has it.
    //   * a status object -- the page is redrawn from it.
    void applyStatus(const QJsonObject& filter);

    // Gate gone. Same as an "available": false payload, said by the window.
    void clear();

signals:
    // -> GET <path>?<query>, where path is "/filter/set", "/filter/notch", or
    // "/filter" itself for a plain re-read. Routed through DiversityWindow and
    // AetherGateDiversityPanel to DiversityBandPoller, which owns this page's
    // transport the way it owns the BAND and SITE pages'.
    void requestFilter(QString path, QUrlQuery query);

private:
    // The five whole-filter presets under the four columns. Defined in
    // DiversityWindowFilter.cpp beside applyStatus() rather than here, for the
    // file-size reason the rest of this class is already split for.
    QWidget* buildPresetStrip();
    QWidget* buildWidthColumn();
    QWidget* buildNotchColumn();
    QWidget* buildToneColumn();
    QWidget* buildAgcColumn();

    // The two doors every control leaves by. `set` is one or more keys on
    // /filter/set; `notch` is /filter/notch, whose three verbs (add, clear one,
    // clear all) are its own route rather than more keys on set.
    void set(const QString& key, const QString& value);
    void set(const QUrlQuery& query);
    void notch(const QString& key, const QString& value);

    // Builds one exclusive row of checkable buttons writing `key`, and returns
    // the group so applyStatus() can check one back without re-emitting.
    QButtonGroup* buildValueButtons(QVBoxLayout* body, const QString& key,
                                    const QString& objectPrefix,
                                    const QStringList& labels,
                                    const QStringList& values, const QString& tip);
    // A labelled spin box that writes `key` on editingFinished. Registered in
    // m_controls so setControlsEnabled() reaches it.
    QSpinBox* buildSpin(const QString& objectName, const QString& key, int lo, int hi,
                        const QString& suffix, const QString& accessibleName,
                        const QString& tip);
    QCheckBox* buildCheck(const QString& objectName, const QString& key,
                          const QString& text, const QString& tip);

    // Writes a widget from the gate WITHOUT emitting -- and without touching it
    // at all while it has focus. See the header comment.
    static void writeSpin(QSpinBox* spin, int value);
    static void writeCheck(QCheckBox* check, bool on);
    static void checkValue(QButtonGroup* group, const QString& value);

    void setControlsEnabled(bool on);
    // `text` sits on the status line for a few seconds and then the line goes
    // back to whatever is permanently true (nothing, or the not-available
    // sentence).
    void showTransient(const QString& text);
    void applyNotchTable(const QJsonArray& notches);

    DiversityFilterPanel* m_panel{nullptr};
    QLabel*               m_caption{nullptr};
    // The one line between the curve and the columns: the whole state of the
    // filter as a sentence, so the answer to "what is switched on?" does not
    // require reading four columns of controls. Everything on it is also
    // somewhere below it -- that is the point, not a duplication bug.
    QLabel*               m_forceLine{nullptr};
    QLabel*               m_status{nullptr};
    QTimer*               m_statusTimer{nullptr};
    QString               m_baseStatus;

    // --- WIDTH -----------------------------------------------------------
    QButtonGroup* m_shapeGroup{nullptr};
    QSpinBox*     m_lowSpin{nullptr};
    QSpinBox*     m_highSpin{nullptr};
    QPushButton*  m_autoButton{nullptr};
    QLabel*       m_autoLine{nullptr};
    QLabel*       m_roofingLine{nullptr};

    // --- NOTCH -----------------------------------------------------------
    QCheckBox*    m_anfCheck{nullptr};
    QLabel*       m_anfLine{nullptr};
    QTableWidget* m_notchTable{nullptr};
    QSpinBox*     m_notchHzSpin{nullptr};
    QSpinBox*     m_notchWidthSpin{nullptr};
    QPushButton*  m_notchAddButton{nullptr};
    QPushButton*  m_notchClearAllButton{nullptr};

    // --- TONE ------------------------------------------------------------
    QCheckBox* m_contourCheck{nullptr};
    QSpinBox*  m_contourHzSpin{nullptr};
    QSpinBox*  m_contourDbSpin{nullptr};
    QSpinBox*  m_contourWidthSpin{nullptr};
    QCheckBox* m_apfCheck{nullptr};
    QSpinBox*  m_apfHzSpin{nullptr};
    QSpinBox*  m_apfWidthSpin{nullptr};
    QCheckBox* m_autoEqCheck{nullptr};
    QLabel*    m_tiltLine{nullptr};

    // --- AGC & NB --------------------------------------------------------
    QButtonGroup* m_agcGroup{nullptr};
    QSpinBox*     m_attackSpin{nullptr};
    QSpinBox*     m_decaySpin{nullptr};
    QSpinBox*     m_hangSpin{nullptr};
    QLabel*       m_gainLine{nullptr};
    QCheckBox*    m_nbCheck{nullptr};
    QSpinBox*     m_nbSpin{nullptr};
    QLabel*       m_blankedLine{nullptr};

    // Every interactive child, so "the gate has no filter for this mode" can
    // grey the lot in one call rather than by naming thirty members.
    QVector<QWidget*> m_controls;
    // Last rendered notch rows, one packed string each. The table is rebuilt
    // only when this changes, so a 2 Hz poll that reports the same notches back
    // does not destroy and rebuild four buttons twice a second.
    QStringList m_notchRows;
    // Last edges the gate reported. A drag reports both handles; this is how
    // the page works out which one actually moved, so a low drag writes low=
    // alone and leaves an auto-owned high= where it is.
    int  m_lowHz{0};
    int  m_highHz{0};
    bool m_available{false};
};

} // namespace AetherSDR
