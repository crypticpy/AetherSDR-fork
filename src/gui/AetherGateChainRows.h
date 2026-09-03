#pragma once

// /filter, turned into rows.
//
// Two functions and nothing else, split out of AetherGateChainStrip.{h,cpp}
// so the strip can be about LAYOUT and this can be about the contract:
//
//   * chainFromFilter() -- the gate's own `chain` array when there is one.
//     Rows the app has never heard of render from their own name, detail and
//     action, so a gate that grows a stage does not need an app release. That
//     is the entire point of the array being gate-authored.
//
//   * a built-in 13-row FALLBACK for a gate with no `chain` key. Assembled
//     from the flat low_hz/high_hz/shape/notches/anf/contour/apf/auto/auto_eq/
//     nb/agc/talker/roofing keys only, so the window is useful against an
//     older gate rather than only against the current one. Rows the flat keys
//     cannot answer -- ALIGN, COMBINER, SUB-BAND, POST-FILTER, all of which
//     live in /diversity -- are deliberately absent rather than invented.
//
// Nothing here sorts or reorders. The gate's order IS signal order, and a
// window that rearranged the stages would be describing a receiver nobody
// owns. What the app does do is GROUP them into four columns for reading;
// that table is chainStageGroup() in AetherGateChainModes.h.

#include "gui/AetherGateChainStage.h"

#include <QList>

class QJsonObject;

namespace AetherSDR {

// The rows one /filter answer describes. `fromGate` (optional) reports
// whether they came from the gate's own chain[] or from the built-in
// fallback.
QList<ChainStage> chainFromFilter(const QJsonObject& filter, bool* fromGate = nullptr);

// The 13 rows a chain-less /filter can honestly describe, exported for the
// test that pins them.
QList<ChainStage> chainFallback(const QJsonObject& filter);

// "12 kHz", "2.8 kHz", "300 Hz", "1.536 MHz". The unit follows the number
// rather than a fixed choice, because a roofing menu that reads
// "0.3 kHz / 0.6 kHz / 1.2 kHz" is not the menu on anybody's front panel.
QString chainFormatWidth(double hz);

} // namespace AetherSDR
