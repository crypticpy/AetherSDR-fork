#pragma once

// MODE, and the two sets behind it.
//
// The operator's words (design §0.3 item 2): "turn on everything related to
// voice in one place", and "the CW-only filters live somewhere else". So the
// CHAIN window has a mode at the top -- PHONE, CW, DATA/OTHER -- and the mode
// does exactly two things:
//
//   * it says which stages are FOR that mode. The rest are not hidden and not
//     disabled; they are collected into a "not for this mode" group that is
//     collapsed by default. An APF is not broken on phone, it is simply not
//     what you reach for, and a window that pretended it did not exist would
//     be lying about the receiver.
//   * it offers ONE button that applies that mode's set: an ordered list of
//     writes, sent one at a time, each waited for. Not a preset object on the
//     gate, not a bulk route -- the same /filter/set the tiles use, in an
//     order chosen so that no write can be undone by the one after it.
//
// Everything the sets can ask for is a parameter the gate accepts TODAY
// (aether_gate/core/engine.py `_filter_kwargs` + `_FILTER_FLAGS`, and
// core/filter.py SliceFilter.set()). Nothing here anticipates a gate feature:
// a set that quietly did nothing on half its lines would be worse than no set
// at all.
//
// IT IS ALSO THE WORDS FILE. Three more tables live here, for the same
// reason the mode table does -- they are prose with a lookup key, not widget
// code, and the window is easier to read when the sentences are somewhere
// else:
//
//   * chainStageGroup() -- which of the four groups a stage is drawn in.
//     FRONT END, PAIR, PASSBAND, OUT: the window is a left-to-right block
//     diagram and the group is the column.
//   * chainPrimaryParts() -- the ONE short line a card shows. The gate's
//     `detail` is a whole sentence ("track - phi 157.3 deg - -4.6 dB - SNR a
//     15.2 / b 12.9 -> 16.4 dB"); a card that printed it would elide it, and
//     an elided measurement is not a measurement. So each stage names which
//     of the detail's own fields it keeps and how short each one may be, and
//     the card joins what fits. The whole sentence stays on the hover and in
//     the inspector.
//   * chainSoundSentence() / chainOffSentence() -- what the stage does to
//     the SOUND, and what you would hear without it. The inspector is for an
//     operator at 4 AM, not for whoever wrote the route.
//
// This file is separate from the window for the reason every other file in
// this family is separate: AGENTS.md asks for files under 800 lines, and a
// table of presets with a sentence per line is a document, not a widget.

#include "gui/AetherGateChainStage.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

class QTimer;

namespace AetherSDR {

// The three ways an operator listens. DATA/OTHER is deliberately one bucket:
// the gate has no per-mode DSP of its own, so splitting RTTY from FT8 would be
// three empty pages instead of one honest one.
enum class ChainMode { Phone, Cw, Data };

// "phone" | "cw" | "data" -- the objectName suffix and the settings key.
QString chainModeId(ChainMode mode);

// "PHONE" | "CW" | "DATA" -- what the operator reads. Three words, not
// "DATA/OTHER": a segmented control is read at a glance and a slash in the
// middle of one segment is a word nobody parses at 4 AM.
QString chainModeLabel(ChainMode mode);

// The paragraph in the mode button's accessible description. Longer than the
// caption, and one of the few places in this window that is allowed more
// than one clause -- a screen reader gets it, the hover does not.
QString chainModeTip(ChainMode mode);

// The one line on the mode button's hover: what switching to `mode` does to
// the diagram, <=90 characters. chainModeTip() above is the long form.
QString chainModeShortTip(ChainMode mode);

// The ONE plain line under the set button, in terms of what the set does to
// the sound: "opens the passband to 350-2750, soft edges, ...". Never a
// paragraph, and it never mentions the control port.
QString chainModeSound(ChainMode mode);

// Whether a stage belongs to `mode`. A stage id the app has never heard of --
// a row a newer gate authored -- is relevant in EVERY mode: the app must not
// collapse a stage it cannot reason about into "not for this mode".
bool chainStageInMode(const QString& id, ChainMode mode);

// "SET UP FOR PHONE" | "SET UP FOR CW" | "SET UP FOR DATA". The label names
// the mode, so an operator on PHONE can see that CW has a set of its own
// without having to switch modes to find out.
QString chainSetLabel(ChainMode mode);

// The label while that set is running.
QString chainSetBusyLabel();

// ---------------------------------------------------------------------------
// The four groups
// ---------------------------------------------------------------------------
//
// The window is a block diagram read left to right: what the antenna and the
// receiver do to the signal before the gate sees it (FRONT END, one summary
// card because none of it is switched from here), what the two loops do
// together (PAIR), the filter you actually tune (PASSBAND), and what leaves
// for your ears (OUT).
enum class ChainGroup { FrontEnd, Pair, Passband, Out };

// "frontend" | "pair" | "passband" | "out" -- the objectName suffix.
QString chainGroupId(ChainGroup group);

// "FRONT END" | "PAIR" | "PASSBAND" | "OUT" -- what the operator reads.
QString chainGroupLabel(ChainGroup group);

// The one sentence on the group caption's hover.
QString chainGroupTip(ChainGroup group);

// Which group a stage is drawn in. A stage the app has never heard of -- a
// row a newer gate authored -- lands in `previous`, the group of the stage
// before it: the array is in signal order, so a new stage belongs beside its
// neighbours and never in a bucket of its own.
ChainGroup chainStageGroup(const QString& id, ChainGroup previous);

// ---------------------------------------------------------------------------
// The one line a card shows
// ---------------------------------------------------------------------------

// The short parts of `stage`'s measured line, most important first. The card
// joins them with " - " and drops from the END until what is left fits its
// fixed width, so the line is never elided and never clipped.
QStringList chainPrimaryParts(const ChainStage& stage);

// The widest line chainPrimaryParts() can produce for a stage this app knows
// about. The card's width is measured from this and from nothing else.
QString chainPrimaryWorstCase();

// ---------------------------------------------------------------------------
// What the inspector says
// ---------------------------------------------------------------------------

// One sentence: what this stage does to the SOUND. Empty for a stage the app
// has never heard of, in which case the inspector falls back to the row's own
// words.
QString chainSoundSentence(const QString& id);

// One line: what you would hear with this stage off. Empty for a stage that
// cannot be switched -- an "off:" line under a row nothing can turn off is a
// promise the window cannot keep.
QString chainOffSentence(const QString& id);

// One line of a set: the route, the query, and the reason the line is there.
// `why` is not decoration -- it is what the window shows while the set runs,
// so an operator watching thirteen writes go past can see what each one was
// for.
struct ChainPresetWrite {
    QString route;
    QString query;
    QString why;
};

// The mode's set, in the order it must be sent. Empty for a mode that has no
// set (DATA/OTHER: the gate has no data-specific stage, and inventing one
// would be the app claiming a capability the receiver does not have).
QList<ChainPresetWrite> chainPreset(ChainMode mode);

// Walks one set, one write at a time.
//
// "Each waited for" is the whole point: the writes are ordered (AUTO WIDTH off
// before the edges are placed, or the fit would move them straight back), and
// firing thirteen GETs into a threaded HTTP server at once would let the gate
// apply them in any order it liked. So a step is sent, and the next one waits
// for a /filter body to come back -- which is the write's own reply, because
// the gate answers /filter/set with the status.
//
// The guard timer is the only thing here that is not driven by the gate: a
// body that never arrives (the gate went away mid-set) must not leave the set
// half-applied AND stuck. It gives up on that step and stops, rather than
// racing ahead through writes nobody is answering.
class AetherGateChainPreset : public QObject {
    Q_OBJECT
public:
    explicit AetherGateChainPreset(QObject* parent = nullptr);

    // Starts (or restarts) a set. An empty list is a no-op.
    void start(const QList<ChainPresetWrite>& writes, const QString& name);

    // One /filter body arrived. Advances the set if a step is waiting on one.
    void noteFilterBody();

    // The gate refused a step. The set stops there rather than sending the
    // rest into a receiver that has already said no.
    void noteError(const QString& error);

    void abort();

    bool running() const { return m_index >= 0; }

signals:
    void requestWrite(QString route, QUrlQuery query);
    // done/total and the line's own sentence, for the window's status line.
    void progress(QString name, int done, int total, QString why);
    // `ok` is false when the set stopped early -- a refusal or a silent gate.
    void finished(QString name, bool ok, QString reason);

private:
    void sendStep();

    QList<ChainPresetWrite> m_writes;
    QString m_name;
    int     m_index{-1};       // -1 == not running
    QTimer* m_guard{nullptr};
};

} // namespace AetherSDR
