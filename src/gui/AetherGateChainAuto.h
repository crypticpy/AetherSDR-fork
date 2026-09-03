#pragma once

// B25 AUTO CLEAN -- the governor's own status, turned into what the CHAIN
// window shows for it: a one-line "held by AUTO" note on the stage card
// whose tool the governor currently holds or is trying, and the AUTO CLEAN
// card's own inspector (its state and why, then its recent moves).
//
// Split into its own unit rather than folded into AetherGateChainWindow.cpp
// (744 lines) or AetherGateChainStage.cpp (796 lines) because AGENTS.md's
// 800-line budget leaves neither room for this. Everything here is pure
// functions over a QJsonObject plus one small applier that walks the
// window's own stage map (AetherGateChainStrip::tile()); the window and the
// stage carry only the hooks that call into it.
//
// THE SHAPE, verbatim from aether_gate/adapters/diversity_governor.py's
// status() -- the same block rides on /diversity under "governor" and on
// /filter under the same key (see docs/DIVERSITY.md "AUTO CLEAN: the chain
// decides"):
//
//   {"auto", "state" (idle|measuring|applying|settling|backoff), "why",
//    "settle_s", "margin_db", "spread_db",
//    "holding": [{"tool", "params", "kind", "why", "since", "delta_db"}],
//    "pending": one row of the same shape, or null,
//    "events": [{"t", "tool", "kind", "params", "undo", "why", "before",
//                "result": pending|kept|undone|released|error, "delta_db"}],
//    "backoff": [{"kind", "tool", "until"}],
//    "available", "error"}
//
// TOOL -> CHAIN ROW. The governor names a TOOL (guard, nb, mode, squeeze,
// dig); the card it decorates is named by ROW ID, and the two are not
// always the same string. Four of the five tools have a row: guard is the
// FRONT END card's GUARD row, which this app synthesises from GET /device's
// "frontend" key under the id "frontend_guard" (AetherGateChainRows.cpp) --
// there is no "guard" id in the gate's own chain[]. nb, combiner (mode's
// row) and squeeze keep the ids the gate already authors those rows with.
// `dig` has no row of its own -- its own A/B rule is shown on the AUTO
// CLEAN card alone, per docs/DIVERSITY.md -- so chainAutoRowIdForTool()
// answers an empty string for it.

#include <QList>
#include <QString>
#include <QStringList>

class QJsonObject;

namespace AetherSDR {

class AetherGateChainStrip;

// One row of governor.holding[], or governor.pending (the same shape).
struct ChainAutoHeld {
    QString tool;
    QString kind;
    QString why;
    double  since{0.0};
    bool    hasDelta{false};
    double  deltaDb{0.0};
};

// One row of governor.events[].
struct ChainAutoEvent {
    double  t{0.0};
    QString tool;
    QString kind;
    QString why;
    QString result;   // pending | kept | undone | released | error
    bool    hasDelta{false};
    double  deltaDb{0.0};
};

// One row of governor.backoff[].
struct ChainAutoBackoff {
    QString kind;
    QString tool;
    double  until{0.0};
};

// The whole block, transcribed rather than interpreted -- the same contract
// every other /filter shape in this window's family keeps.
struct ChainAutoGovernor {
    bool    available{false};
    bool    autoOn{false};
    QString state;
    QString why;
    double  settleS{0.0};
    double  marginDb{0.0};
    double  spreadDb{0.0};
    QList<ChainAutoHeld>    holding;
    bool                    hasPending{false};
    ChainAutoHeld           pending;
    QList<ChainAutoEvent>   events;     // gate order: oldest first
    QList<ChainAutoBackoff> backoff;
    QString error;
};

// filter.value("governor"). Missing, JSON null, or anything that is not an
// object all come back default-constructed (`available` false) -- absent,
// not dashed, the same rule chainFrontendFromDevice() keeps for B23.
ChainAutoGovernor chainAutoParseGovernor(const QJsonObject& filter);

// The chain row id AUTO holds this tool through, or an empty string for a
// tool with no row of its own (today, only "dig").
QString chainAutoRowIdForTool(const QString& tool);

// The one-line note for the card whose id is `stageId`: "AUTO · <kind> ·
// <why>[, <±d.d> dB]" for a tool AUTO is holding there, "AUTO · trying ·
// <why>" for one it is still measuring, or empty when AUTO holds nothing on
// this row (including every row while the governor block never arrived).
QString chainAutoNoteForStage(const QString& stageId, const ChainAutoGovernor& gov);

// Sets, or clears, every stage card's "held by AUTO" note from one governor
// read. Builds the note QLabel itself, lazily, on the tile the first time a
// row has something to say -- AetherGateChainStage.cpp carries no
// AUTO-specific code at all; this is the whole of the "small applier ...
// over the window's stage map."
void chainAutoApplyNotes(AetherGateChainStrip* strip, const ChainAutoGovernor& gov);

// The AUTO CLEAN inspector's event history, newest first: one line per event
// ("12:41:07 · squeeze · carrier · kept +1.8 dB · <why>"), then one line per
// active backoff ("backing off: mains/squeeze until 12:46"). Capped at
// `maxLines` between the two lists together, because the CHAIN window's
// inspector has no scroll of its own and nothing in this window scrolls at
// the initial size.
QStringList chainAutoEventLines(const ChainAutoGovernor& gov, int maxLines = 8);

// "state · why", for the inspector's line under the AUTO CLEAN card's own
// detail. Empty when the governor block never arrived.
QString chainAutoStateLine(const ChainAutoGovernor& gov);

} // namespace AetherSDR
