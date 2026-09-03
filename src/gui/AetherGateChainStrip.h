#pragma once

// The block diagram: the gate's chain[], read left to right in four labelled
// groups, with an arrow between them so it reads as a chain and not as a wall
// of tiles.
//
//   FRONT END   what the antenna and the receiver do before any of this
//               reaches the filters. Nothing in it is switched from this
//               window, so it is ONE compact card with a line per item and a
//               single "set on the setup page" hint -- not seven tiles that
//               all say the same dead sentence.
//   PAIR        align, blanker, combiner, sub-band null, post-filter.
//   PASSBAND    the filter you actually tune, in two sub-columns.
//   OUT         detector, AGC, and the point where the audio leaves the gate.
//
// Nothing here SORTS. The gate's order is signal order and the cards keep it
// inside their own group; the group is the app's reading of the same order,
// which is what turns a list into a diagram. The table is chainStageGroup()
// in AetherGateChainModes.h, and a stage the app has never heard of lands
// beside its neighbour rather than in a bucket of its own.
//
// What the MODE does to it: the stages that are for the mode stay in their
// group; the rest drop into a "not for this mode" fold under the diagram that
// is collapsed by default. Nothing is hidden and nothing is disabled -- an
// APF still works on phone, it is simply not what you reach for.
//
// KEYBOARD. The strip takes focus, the arrow keys walk the stages that are on
// the diagram in signal order, and space presses the selected card's switch.
// The walk never steps into the collapsed fold: a selection nobody can see is
// not a selection.
//
// The /filter-to-rows translation is AetherGateChainRows.h. This file is the
// layout and nothing else.

#include "gui/AetherGateChainModes.h"
#include "gui/AetherGateChainRows.h"
#include "gui/AetherGateChainStage.h"

#include <QList>
#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace AetherSDR {

class AetherGateChainStrip : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateChainStrip(QWidget* parent = nullptr);

    // One poll's worth of rows. Rebuilds the cards only when the row SHAPES
    // changed (a stage added, a kind changed, an option list changed);
    // otherwise every card is updated in place, because a rebuild twice a
    // second would eat a half-typed frequency and close an open menu.
    void setStages(const QList<ChainStage>& stages);

    // FRONT END only: the dBm scale's calibration caveat -- "calibrated for
    // LNA 0, now 1" -- shown only while the guard has moved the LNA state
    // away from the one the scale was trimmed against. `text` is ignored
    // when `show` is false.
    void setFrontendCalNote(bool show, const QString& text);

    // Which stages stay on the diagram and which drop into the fold.
    void setMode(ChainMode mode);
    ChainMode mode() const { return m_mode; }

    // Drops every card -- the connection went away, and last minute's numbers
    // must not sit there looking live.
    void clear();

    int tileCount() const { return int(m_tiles.size()); }
    const QList<ChainStage>& stages() const { return m_stages; }
    AetherGateChainTile* tileAt(int index) const;
    AetherGateChainTile* tile(const QString& id) const;

    // The cards on the diagram proper, in signal order -- what the arrow keys
    // walk.
    QList<AetherGateChainTile*> tilesInMode() const;

    // The stage the inspector is showing. Empty when the diagram is empty.
    QString selectedId() const { return m_selected; }
    void selectStage(const QString& id);

signals:
    void stageSelected(QString id);
    void requestWrite(QString route, QUrlQuery query);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    // One labelled column of the diagram.
    struct Column {
        QWidget*     host{nullptr};
        QWidget*     body{nullptr};
        QGridLayout* grid{nullptr};
        QLabel*      hint{nullptr};   // FRONT END only
        QLabel*      calNote{nullptr};   // FRONT END only, the B23 cal caveat
        int          count{0};
    };

    void buildColumns(QVBoxLayout* root);
    void rebuild();
    void relayout();
    void moveSelection(int delta);
    Column& columnFor(ChainGroup group);

    Column                     m_columns[4];
    QGridLayout*               m_foldGrid{nullptr};
    QWidget*                   m_fold{nullptr};
    QPushButton*               m_foldToggle{nullptr};
    QList<ChainStage>          m_stages;
    QList<AetherGateChainTile*> m_tiles;
    QString                    m_shape;      // rebuild-only-on-change fingerprint
    QString                    m_selected;
    ChainMode                  m_mode{ChainMode::Phone};
};

} // namespace AetherSDR
