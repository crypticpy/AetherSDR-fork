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
// THE SHAPE, verbatim from aether_gate/core/governor.py's status() -- the
// same block rides on /diversity under "governor" and on /filter under the
// same key (see docs/DIVERSITY.md "AUTO CLEAN: the chain decides"):
//
//   {"auto", "state" (idle|measuring|applying|settling|backoff), "why",
//    "settle_s", "margin_db", "spread_db", "objective_source" ("snr" or
//    "none" -- there is no talker to score against right now, so a kept
//    move was scored by its tool's own proxy instead),
//    "holding": [{"tool", "params", "kind", "why", "since", "delta_db"}],
//    "pending": one row of the same shape, or null,
//    "events": [{"t", "wall", "tool", "kind", "params", "undo", "why",
//                "before", "result": pending|kept|undone|released|error,
//                "delta_db"}],
//    "ruled_out": [{"tool", "why"}] -- this tick's rules that found nothing
//    to do, in the order they ran; the same list `why` is already the
//    joined sentence of when the governor is otherwise idle (core/
//    governor.py's _why_idle()), kept here raw so a state other than idle
//    can still say what else it is not doing,
//    "backoff": [{"kind", "tool", "until", "until_wall"}],
//    "available", "error"}
//
// TWO CLOCKS. `t` (governor events) and `until` (backoff) are the gate's own
// monotonic clock -- fine for `until - now` maths, meaningless run through
// QDateTime. `wall` and `until_wall` are the real epoch-seconds companions
// a gate now sends alongside them, and are what every formatter below prints
// the time of day from; a gate too old to send the `_wall` field falls back
// to the monotonic one rather than blanking the line, which reads as a
// wrong clock rather than no clock -- the same trade-off this file already
// made everywhere else a gate-side field can simply be missing.
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

class QPushButton;
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
    // Optional, present only on a held "dig" (DIG has no row of its own -- see
    // chainAutoRowIdForTool() -- so this is how its own progress reaches the
    // AUTO CLEAN card's inspector at all). Read out of params.step: every
    // other tool's hold is fully described by kind/why, but DIG is a whole A/B
    // session and "why" alone cannot say which knob it is on right now.
    QString step;
    // Optional -- B26's scorer field, shared with ChainAutoEvent below. See
    // that struct's own comment.
    QString scorer;
};

// One row of governor.events[].
struct ChainAutoEvent {
    double  t{0.0};
    // `wall` -- the same event's real epoch-seconds stamp. Absent on an
    // older gate (hasWall false); eventLine() falls back to `t` then, which
    // is the gate's monotonic clock and prints a meaningless time of day,
    // but a wrong time reads no worse than the pre-existing behaviour did.
    bool    hasWall{false};
    double  wall{0.0};
    QString tool;
    QString kind;
    QString why;
    QString result;   // pending | kept | undone | released | error
    bool    hasDelta{false};
    double  deltaDb{0.0};
    // Optional -- which objective the gate scored this move against ("snr",
    // "depth", ...). Absent on an older gate; when present it is appended to
    // the event line as "scored by <scorer>".
    QString scorer;
};

// One row of governor.backoff[].
struct ChainAutoBackoff {
    QString kind;
    QString tool;
    double  until{0.0};
    // `until_wall` -- the same deadline as real epoch seconds. Absent on an
    // older gate (hasUntilWall false); backoffLine() falls back to `until`.
    bool    hasUntilWall{false};
    double  untilWall{0.0};
};

// One row of governor.ruled_out[]: a rule that ran this tick and found
// nothing to do, and why not.
struct ChainAutoRuledOut {
    QString tool;
    QString why;
};

// The whole block, transcribed rather than interpreted -- the same contract
// every other /filter shape in this window's family keeps.
struct ChainAutoGovernor {
    bool    available{false};
    bool    autoOn{false};
    QString state;
    QString why;        // the sentence: the card, the inspector, hover
    QString label;      // `state_label`: the few plain words a switch shows
    double  settleS{0.0};
    double  marginDb{0.0};
    double  spreadDb{0.0};
    // "snr", or "none" while there is no talker to score a move against (a
    // proxy-scored move is still kept, per core/governor.py's own docstring
    // -- this just says which basis was in force). Empty on an older gate.
    QString objectiveSource;
    QList<ChainAutoHeld>       holding;
    bool                       hasPending{false};
    ChainAutoHeld              pending;
    QList<ChainAutoEvent>      events;     // gate order: oldest first
    QList<ChainAutoRuledOut>   ruledOut;   // this tick's rules, in gate order
    QList<ChainAutoBackoff>    backoff;
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
// ("12:41:07 · squeeze · carrier · kept +1.8 dB · <why>", the time from
// `wall` where the gate sends it), then one line per active backoff
// ("backing off: mains/squeeze until 12:46:00" once the gate sends
// `until_wall`, "...until 12:46" -- no seconds -- on one that does not).
// Capped at `maxLines` between the two lists together, because the CHAIN
// window's inspector has no scroll of its own and nothing in this window
// scrolls at the initial size.
QStringList chainAutoEventLines(const ChainAutoGovernor& gov, int maxLines = 8);

// "state · why[ · dig line][ · held back: ...][ · objective: source][ · ERROR: ...]",
// for the inspector's line under the AUTO CLEAN card's own detail. Empty
// when the governor block never arrived. A held or just-finished dig is
// folded in (see chainAutoDigLine()) since DIG has no card of its own to
// carry it; "held back" is `ruled_out` joined the same way the gate's own
// `why` already is while idle (core/governor.py's _why_idle()), omitted
// when it would only repeat `why` verbatim; "objective" names which basis
// (`objective_source`) the governor is currently scoring by; a non-empty
// `error` is prefixed ahead of everything else so it reads as the warning
// it is. The whole line reaches the widget through setElided() at the call
// site, which is where the H1 90-char tooltip cap and the uncapped
// accessibleDescription both come from -- nothing here truncates.
QString chainAutoStateLine(const ChainAutoGovernor& gov);

// DIG has no chain row of its own (chainAutoRowIdForTool() answers "" for
// it), so its own note appears nowhere chainAutoApplyNotes() decorates -- this
// is the one place it is said at all, and chainAutoStateLine() folds it in.
// While AUTO is holding or trying a dig: "AUTO · weak · <why>", with
// " · dig running <step>" appended when the gate sends one and
// " · scored by <scorer>" appended when it sends that too. Once the dig has
// concluded -- the newest events[] entry for tool "dig" -- "dig done, kept
// +x dB" (kept), "dig done, nothing kept" (undone), or empty for a result
// this has nothing new to say about (released, error: the generic event
// line already covers those). Empty when AUTO has never touched a dig.
QString chainAutoDigLine(const ChainAutoGovernor& gov);

// The governor's own few plain words -- `state_label` ("listening",
// "trying a null on the mains hum", "kept", "put back", "holding the
// blanker"), or the raw state on a gate too old to send one.
QString chainAutoStateWord(const ChainAutoGovernor& gov);

// B25's own AUTO CLEAN ON indicator, what a switch's face shows while the
// governor is on: the bare "AUTO CLEAN ON" and nothing more -- the operator's
// own words ("it shouldn't have a big-ass long status message") rule out the
// state word here too, not just the sentence. Empty when the governor block
// never arrived or auto is off -- the caller's job to collapse to
// "AUTO CLEAN" alone (or hide entirely) in that case, since that decision is
// surface-specific (a switch collapses to the bare label; a read-only header
// just disappears).
QString chainAutoIndicatorLine(const ChainAutoGovernor& gov);

// The whole line for the one surface with room for it -- the CHAIN window's
// read-only header: "AUTO CLEAN ON · <state_label> · <why>". Empty on the
// same terms. Not used by the switches any more (see
// chainAutoSetButtonIndicator()); built independently of
// chainAutoIndicatorLine() above so the header keeps the label even though
// the switches' own face text no longer carries it.
QString chainAutoIndicatorSentence(const ChainAutoGovernor& gov);

// Puts the indicator on a switch: the face shows `indicator` verbatim (empty
// collapses it to the bare "AUTO CLEAN"), the accessible description carries
// `stateWord` folded in -- "AUTO CLEAN ON · <stateWord>", short, for screen
// readers -- and the tooltip is one fixed short sentence per on/off state.
// The operator's own words: no status message on the face, no paragraph in
// the tooltip.
void chainAutoSetButtonIndicator(QPushButton* button, const QString& indicator,
                                 const QString& stateWord);

// True when the governor itself started the dig currently running or just
// finished: a pending write for tool "dig", a held "dig", or -- once neither
// of those is still true because the run has already landed in events[] --
// the newest events[] entry for tool "dig" whose result is still "pending".
// False (an operator's own DIG button) whenever none of those hold, which is
// also the answer for a gate too old to send a governor block at all.
bool chainAutoDigStartedByAuto(const ChainAutoGovernor& gov);

} // namespace AetherSDR
