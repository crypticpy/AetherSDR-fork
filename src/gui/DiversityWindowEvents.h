#pragma once

// DiversityEventLog -- poll-to-poll transitions of the combiner, as sentences.
//
// Every other readout in DiversityWindow answers "what is true now". None of
// them answers "what just happened", which is the question an operator who
// looked away for thirty seconds actually has: did somebody come up on
// frequency, did Track re-solve, did the gate blink out. Deriving that means
// remembering the last poll and diffing it, and diffing state is exactly the
// kind of logic that rots when it lives inside a widget: it becomes reachable
// only by building a window, showing it, and feeding it fake HTTP.
//
// So it lives here, as a plain class over a plain struct. No QWidget, no
// network, no clock -- the caller stamps the lines. That makes the whole
// derivation testable by calling apply() twice and comparing QStringLists,
// which is the only reason the rules below can be trusted to be right.
//
// The rules, in the order apply() checks them:
//
//   * presence is a barrier. A gate that just went away (or came back)
//     produces exactly one line and NOTHING else that tick: every other field
//     is either stale or freshly-invented across a gap, and "mode -> off,
//     talker ended, memory emptied" is a description of the poll failing, not
//     of anything the operator did or heard.
//   * a talker change emits the end of the old over before the start of the
//     new one, so the log reads in the order the air did.
//   * a field the gate does not report at all (an older gate, a null) never
//     produces a line. Absent is not a transition.

#include <QString>
#include <QStringList>
#include <QVector>

namespace AetherSDR {

// One poll's worth of the state the log watches. Filled by DiversityWindow
// from the /diversity payload; every "have" flag is the gate's own
// "independently optional" contract carried through rather than flattened
// into a default.
struct DiversitySnapshot {
    bool    present{false};        // the gate answered at all
    bool    available{false};      // ...and reported diversity available

    bool    haveTalker{false};     // "talker": {...} rather than null
    int     talkerId{0};
    double  talkerSinceS{0.0};
    QString talkerName;            // memory[] entry's "name", may be empty
    bool    haveTalkerWeight{false};
    double  talkerPhaseDeg{0.0};
    double  talkerRatioDb{0.0};

    QVector<int> memoryIds;        // ids present in "memory", in gate order

    // The gate's running count of times it decided the voice it was hearing
    // was NOT the one it had been hearing, and split the over into two
    // talkers. A count rather than a flag because the split is instantaneous:
    // a poll can miss the moment but cannot miss the increment.
    bool    haveVoiceSplits{false};
    int     voiceSplits{0};

    bool    haveFocus{false};      // "focus": {...} rather than null
    int     focusId{0};
    QString focusName;
    bool    focusNulling{false};   // the current over is being nulled, not steered

    bool    haveSteadyQrm{false};
    bool    steadyQrm{false};

    QString mode;                  // "off"/"manual"/"null"/"track"
    QString hear;                  // "combined"/"a"/"b"

    bool    aligned{false};
    bool    realigning{false};
    bool    haveLag{false};
    double  lagSamples{0.0};
};

class DiversityEventLog {
public:
    // Folds one poll in and returns the lines it produced, oldest first.
    // The first call after construction or reset() only seeds: a window
    // opened onto a gate that has been running for an hour must not spray
    // "started"/"remembered"/"aligned" for state that was already true.
    QStringList apply(const DiversitySnapshot& snapshot);

    // Forgets the previous poll, so the next apply() seeds again.
    void reset();

    // Lines the log cannot derive because they are not poll state -- the
    // operator's own writes. Here rather than at the call site so every
    // sentence in the list is written in one place and reads as one voice.
    static QString memoryClearedLine();
    static QString captureSavedLine(const QString& basename);

    // `#2 "Bob"` when the gate has a name for it, `#2` when it does not. A
    // talker with no name is not "unnamed" or "(none)": it is just a number,
    // and saying so in fewer words is the honest form. Shared with the
    // window's own LOCKED/nulling state line for the reason shortDuration is
    // shared with the talkers table -- one station, one spelling.
    static QString talkerTag(int id, const QString& name);

    // "12 s" / "3 m" / "2 h" -- a duration an operator reads without
    // counting digits. Shared with the talkers table's Heard/First columns so
    // the same number never appears two ways in one window.
    static QString shortDuration(double seconds);

private:
    bool              m_have{false};
    DiversitySnapshot m_prev;
};

} // namespace AetherSDR
