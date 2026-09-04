#pragma once

// NOW -- the CHAIN window's own "one thing worth changing right now" strip.
//
// Design (diversity-redesign-3d-plan.md) §2.3. Sits above the CHAIN/VISUAL
// tabs -- AetherGateChainWindowTabs.cpp puts it there -- so the governor's
// recommendation is visible on both tabs at once, and it replaces the
// read-only AUTO CLEAN banner this window used to show under the header.
// Everything it draws comes from three inputs, none of which it fetches
// itself:
//
//   * the auto_clean chain row -- lifted out of chainFromFilter()'s own row
//     list by AetherGateChainRows.cpp, because AUTO CLEAN itself no longer
//     draws on the diagram (chainStageGroup() has no entry for it and never
//     will -- see AetherGateChainModes.cpp's kGroupTable);
//   * the governor block, already parsed by AetherGateChainAuto.h;
//   * ChainFrontendStatus, the B23 linearity guard's own status.
//
// THE LADDER. refresh() runs an eight-case ladder (§2.3's own table); one
// case wins, and that case decides the line, which button (if any) shows
// beside HISTORY, and which chain row the diagram lights -- a 2 px left
// edge in the same accent the selected border already uses (see
// AetherGateChainTile.cpp's own lit style, appended there rather than in
// the shared AetherGateChainStagePrivate.h). The ladder lives in the app,
// not the gate, on purpose: no `governor.recommend` key is asked for, the
// same trade chainAutoStateLine() already makes for `state_label`.
//
// ONE BUTTON, FOUR FACES. TURN GUARD ON, TRY AGAIN, HAND IT BACK, and
// turning AUTO CLEAN on are the same QPushButton with its text, route,
// query and tooltip swapped per case rather than four separate widgets --
// only one of them is ever live at a time, and an operator addressing this
// window by objectName wants one stable name for "the thing NOW wants me to
// press", not four names that come and go.
//
// HISTORY is a plain in-place disclosure: click it and the last eight
// governor lines (chainAutoEventLines(), reused rather than reimplemented)
// appear under the buttons row; click again and they are gone.
//
// WRITES go out through the same requestWrite(route, query) signal every
// other control in this window's family uses -- AetherGateChainWindowTabs.cpp
// wires it to the window's own onWriteRequested() the way the strip, the
// visual tab and HEAR RAW already are.

#include "gui/AetherGateChainAuto.h"
#include "gui/AetherGateChainRows.h"
#include "gui/AetherGateChainStage.h"

#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QLabel;
class QPushButton;

namespace AetherSDR {

class AetherGateChainNow : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateChainNow(QWidget* parent = nullptr);

    // One /filter poll's worth of inputs, on the same 500 ms timer the old
    // banner re-read its own state off -- AetherGateChainWindowTabs.cpp
    // calls this regardless of whether the underlying body changed, so a
    // held tool's age keeps counting up between polls that answer
    // byte-identical bodies.
    void refresh(const ChainStage& autoCleanRow, const ChainAutoGovernor& gov,
                const ChainFrontendStatus& fe);

signals:
    // The chain row id the diagram should light, or empty to light nothing.
    // Emitted on every refresh() regardless of whether it changed --
    // AetherGateChainWindowTabs.cpp's own apply is a property compare, so a
    // repeat is a no-op.
    void stageLit(QString id);
    void requestWrite(QString route, QUrlQuery query);

private:
    void onActionClicked();
    void onHistoryClicked();
    void updateHistoryPanel();

    QLabel*      m_line{nullptr};
    QPushButton* m_action{nullptr};
    QPushButton* m_history{nullptr};
    QLabel*      m_historyPanel{nullptr};

    QString m_actionRoute;
    QString m_actionQuery;
    bool    m_historyOpen{false};
    // Kept only so HISTORY can be opened/closed between refresh() calls
    // without needing the caller to hand the governor block in again.
    ChainAutoGovernor m_governor;
};

} // namespace AetherSDR
