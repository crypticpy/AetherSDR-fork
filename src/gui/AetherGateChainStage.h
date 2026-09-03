#pragma once

// One row of the gate's chain[] -- the data, and the tile that draws it.
//
// Split out of AetherGateChainStrip.{h,cpp} for the reason every other file in
// this window's family exists: AGENTS.md asks for files under 800 lines, and
// the strip's own job (parse an array, lay out N tiles, keep one selected) is a
// different story from a tile's (four widgets, one control, nothing optimistic).
//
// THE CONTRACT, restated in C++. `chain` is authored by the gate, in signal
// order, and this struct is a transcription of it rather than an
// interpretation:
//
//   {"id", "name", "kind", "fixed", "enabled", "detail", "value",
//    "options": [...], "measured": {"in_db", "out_db"},
//    "action": {"label", "route", "query"} | "why": "..."}
//
// `kind` is toggle | select | value | fixed. A `fixed` row renders with no
// control, DIMMED, and with its `why` printed on the tile itself rather than
// hidden on a hover: "gate does not offer this yet" is the answer to the
// operator's first question about the RF roof, and an answer nobody can see is
// not an answer (design §0.3 items 3 and 6). `action.query` ending in "="
// means the app appends the chosen value; anything else is sent verbatim,
// because the gate wrote the query for the action the button is about to
// perform (a NB row that is off carries "nb=on", the same row when on carries
// "nb=off"). That is the shape already in production for the noise profile's
// kinds[] rows, and this window inherits it rather than inventing a second one.
//
// NOTHING HERE IS OPTIMISTIC. A toggle click does not flip the switch and a
// select does not move: both send one write and put the control straight back
// where the gate last said it was. The tile changes when -- and only when --
// a gate answer says so. What the click DOES do is disable the control until
// that answer arrives, so one press cannot become three writes while the gate
// is thinking (design §0.3 item 5).
//
// Real widgets, not a painted list: the automation bridge and a screen reader
// both address this window by objectName, and a custom-painted strip would be
// invisible to both.

#include <QFrame>
#include <QList>
#include <QString>
#include <QUrlQuery>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace AetherSDR {

// One entry on a select row: what the operator reads, what goes on the wire,
// the group header it sits under ("" for an ungrouped list), and whether the
// gate will accept it. The group is how the roofing rows say "12 kHz, the one
// an FTdx101MP calls its widest" without the app pretending the gate sent a
// radio name; `enabled` is how a width the gate did NOT list stays visible and
// unpickable rather than vanishing -- an operator who knows his K3 has a
// 250 Hz filter should be told this receiver cannot make one, not left
// wondering where it went (design §0.3 item 6).
struct ChainOption {
    QString label;
    QString value;
    QString group;
    bool    enabled{true};
};

struct ChainStage {
    QString id;
    QString name;
    QString kind;            // toggle | select | value | fixed
    QString detail;          // the gate's own headline value
    QString why;             // why a fixed row cannot be acted on
    QString tip;             // the sentence on the hover and in the detail pane
    bool    fixed{false};
    bool    enabled{false};
    QString value;           // wire form of the value in force, on a select
    QList<ChainOption> options;
    bool    freeEntryHz{false};   // the digital roof's own 100 Hz - 25 kHz entry
    QString actionLabel;
    QString actionRoute;
    QString actionQuery;
    bool    hasIn{false};
    double  inDb{0.0};
    bool    hasOut{false};
    double  outDb{0.0};

    bool actionable() const { return !fixed && !actionRoute.isEmpty(); }

    // The query to send. `appended` is the chosen value on a query that ends
    // in "="; it is ignored on a verbatim one.
    QUrlQuery queryFor(const QString& appended = QString()) const;

    // Everything about this row that changes what WIDGETS it needs, as one
    // string. Equal fingerprints mean the strip updates in place instead of
    // rebuilding -- a rebuild every 500 ms poll would eat the operator's
    // half-typed frequency and close an open combo.
    QString shape() const;

    // Just the part a write can move: the switch position and the value in
    // force. This is what "did the gate confirm my write?" is asked of, so it
    // must NOT include the detail sentence -- blanked_pct and the AGC's gain
    // change on their own every poll, and a confirmation test that counted
    // those would call every poll a confirmation.
    QString settingKey() const;
};

// The control a row carries: a switch for toggle, a combo (plus, on the digital
// roof, a free entry in Hz) for select, nothing at all for fixed. Built once
// per tile and once more, larger, in the window's detail area -- the same class
// both times, so the two can never disagree about what a row can do.
class AetherGateChainControl : public QWidget {
    Q_OBJECT
public:
    // `prefix` is the objectName stem ("gateChain" on the strip,
    // "gateChainDetail" in the detail area); `large` picks the taller metrics
    // the detail area uses.
    AetherGateChainControl(const ChainStage& stage, const QString& prefix, bool large,
                           QWidget* parent = nullptr);

    // New numbers for the same row shape. Never touches a control the operator
    // is editing beyond putting it back where the gate says it is.
    void setStage(const ChainStage& stage);

    // A write for this stage is on the wire: the control greys until an answer
    // comes back. One press must not be able to become three.
    void setBusy(bool busy);

    // Presses the control's switch as though the operator had -- the strip's
    // space bar. Does nothing on a row that has no switch.
    void activateSwitch();

signals:
    void requestWrite(QString route, QUrlQuery query);

private:
    void buildToggle(const QString& prefix, bool large);
    void buildSelect(const QString& prefix, bool large);
    void syncToGate();
    void applyBusy();

    ChainStage   m_stage;
    QPushButton* m_toggle{nullptr};
    QComboBox*   m_select{nullptr};
    QLineEdit*   m_free{nullptr};
    bool         m_busy{false};
    // The value in force when it is NOT on the gate's own option list, kept so
    // the row it was shown on is replaced rather than stacked up poll on poll.
    QString      m_inserted;
};

// One block of the diagram. Name in the Diversity window's field-label style,
// the gate's headline value in its value style, the reason a fixed stage
// cannot move, an optional in/out level readout, and the control.
class AetherGateChainTile : public QFrame {
    Q_OBJECT
public:
    AetherGateChainTile(const ChainStage& stage, QWidget* parent = nullptr);

    void setStage(const ChainStage& stage);
    const ChainStage& stage() const { return m_stage; }
    QString id() const { return m_stage.id; }
    void setSelected(bool on);
    bool isSelected() const { return m_selected; }

    // A write for this stage is in flight.
    void setBusy(bool busy);

    // What the gate said when it refused this stage's last write. Printed on
    // the tile, where the operator is looking, and cleared by the next answer
    // that moves the row.
    void setError(const QString& error);
    QString error() const { return m_error; }

    void activateSwitch();

signals:
    void clicked(QString id);
    void requestWrite(QString route, QUrlQuery query);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void refreshUnderline();

    ChainStage   m_stage;
    QLabel*      m_name{nullptr};
    QLabel*      m_value{nullptr};
    QLabel*      m_under{nullptr};     // the `why`, or the gate's refusal
    QLabel*      m_levels{nullptr};
    AetherGateChainControl* m_control{nullptr};
    QString      m_error;
    bool         m_selected{false};
};

// "in -97.4 · out -101.2 dB", with an em dash for a leg the gate did not
// measure. Shared with the detail area, which shows the same sentence larger.
QString chainLevelText(const ChainStage& stage);

// The widest string chainLevelText() can ever return, for the fixed field
// width every readout in this window is built to.
QString chainLevelWorstCase();

// The tile's own frame, in one place: the strip's no-scroll arithmetic and the
// window's initial size are both computed from these.
constexpr int kChainTileWidth = 220;
constexpr int kChainTileHeight = 84;

} // namespace AetherSDR
