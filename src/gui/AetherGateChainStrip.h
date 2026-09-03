#pragma once

// The block-diagram strip: one tile per row of the gate's chain[], in the
// gate's own order, which IS signal order. Nothing here sorts, groups or
// reorders -- a strip that rearranged the stages would be describing a
// receiver nobody owns.
//
// It also carries the two functions that turn a /filter payload into rows:
//
//   * chainFromFilter() -- the gate's `chain` array when there is one. Rows
//     the app has never heard of render from their own name/detail/action, so
//     a gate that grows a stage does not need an app release. That is the
//     entire point of the array being gate-authored.
//
//   * a built-in 13-row FALLBACK for a gate with no `chain` key -- which is
//     every gate shipping today (GET /filter on the live gate, 2026-09-03,
//     returns low_hz/high_hz/shape/notches/anf/contour/apf/auto/auto_eq/nb/
//     agc/talker/roofing and no chain). The fallback is assembled from those
//     flat keys only, so the window is useful against the gate on the bench
//     rather than against the gate in the design document. Rows the flat keys
//     cannot answer -- ALIGN, COMBINER, SUB-BAND, POST-FILTER, all of which
//     live in /diversity -- are deliberately absent rather than invented; they
//     arrive the day the gate authors its own chain.
//
// The strip wraps into rows of kColumns tiles. A 13-tile diagram laid out in
// one line would be 2.7 metres of horizontal scrolling, and a scrollbar the
// operator has to discover to see the second half of his own receiver is the
// failure this window exists to avoid. Reading order (left to right, then
// down) is still signal order.

#include "gui/AetherGateChainStage.h"

#include <QList>
#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QGridLayout;
class QJsonObject;

namespace AetherSDR {

// The rows one /filter answer describes. `fromGate` (optional) reports whether
// they came from the gate's own chain[] or from the built-in fallback.
QList<ChainStage> chainFromFilter(const QJsonObject& filter, bool* fromGate = nullptr);

// The 13 rows a chain-less /filter can honestly describe, exported for the
// test that pins them and for the window's own "this gate predates chain[]"
// line.
QList<ChainStage> chainFallback(const QJsonObject& filter);

class AetherGateChainStrip : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateChainStrip(QWidget* parent = nullptr);

    // One poll's worth of rows. Rebuilds the tiles only when the row SHAPES
    // changed (a stage added, a kind changed, an option list changed);
    // otherwise every tile is updated in place, because a rebuild twice a
    // second would eat a half-typed frequency and close an open combo.
    void setStages(const QList<ChainStage>& stages);

    // Drops every tile -- the gate stopped answering, and last minute's
    // numbers must not sit there looking live.
    void clear();

    int tileCount() const { return int(m_tiles.size()); }
    const QList<ChainStage>& stages() const { return m_stages; }
    AetherGateChainTile* tileAt(int index) const;
    AetherGateChainTile* tile(const QString& id) const;

    // The stage the detail area is showing. Empty when the strip is empty.
    QString selectedId() const { return m_selected; }
    void selectStage(const QString& id);

signals:
    void stageSelected(QString id);
    void requestWrite(QString route, QUrlQuery query);

private:
    void rebuild();

    QGridLayout*               m_grid{nullptr};
    QList<ChainStage>          m_stages;
    QList<AetherGateChainTile*> m_tiles;
    QString                    m_shape;      // rebuild-only-on-change fingerprint
    QString                    m_selected;
};

} // namespace AetherSDR
