#pragma once

// DiversitySessionModel -- the Diversity window's answer to "where do I
// start?", Phase 3a rework. See ~/.claude/plans/diversity-phase3a-workflow-
// plan.md "Revision 2" for the design this implements (R2.1 the step model,
// R2.2 the decision rules).
//
// THE THREE LOOPS, not five flat steps. The gate resets different things on
// different events (traced in R2.0), and the workflow follows that trace
// rather than a single top-to-bottom checklist:
//
//   SESSION  (resets on a gate restart)      1 RECEIVER  2 SITE NOISE
//   BAND     (resets on an amateur band change)           3 BAND
//   STATION  (resets on a talker change or a big retune)   4 STATION
//   —        (never resets)                                 5 LISTEN
//
// A step lower in that table never resets a step above it: retuning inside a
// band touches only STATION, changing band touches BAND and STATION, and
// only a gate restart touches SESSION. nextStep() walks RECEIVER, SITE
// NOISE, BAND, STATION in that fixed order and returns the first one not
// done; LISTEN is the answer once all four are.
//
// This is a pure derivation over five JSON payloads the window already
// polls (/diversity, /filter, /diversity/dig, /diversity/beacons,
// /diversity/compass) plus the tuned frequency and an injectable clock. No
// widgets, no transport, no timers, and nothing is cached between polls: a
// step's done-ness is recomputed from scratch every apply(), which is what
// keeps "done" and "the state sentence beside it" from ever disagreeing
// (see DiversityFlowStrip.cpp's header comment for the same rule stated
// about the model this one replaces).
//
// WHAT APPLY() CANNOT SEE. Deliberately narrow: no /diversity/finder (the
// FINDER candidate count some R2.2 examples quote, "FINDER has 6", is
// therefore not reproduced here -- that number belongs to whichever package
// owns the BAND page's own poller) and no /device (the FRONT END guard's
// headroom number lives there, per AetherGateChainAuto.h's own note on
// where "guard" is synthesised from; see headroomClear() below for the TODO
// this leaves).

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace AetherSDR {

class DiversitySessionModel {
public:
    // The five steps, in nextStep()'s walk order. LISTEN is the destination:
    // never a "chore", never resets, and is what nextStep() answers once the
    // four ahead of it are done.
    enum StepId {
        StepReceiver = 0,
        StepSiteNoise,
        StepBand,
        StepStation,
        StepListen,
        StepCount
    };

    // Which of the three loops (plus the destination, which is not a loop
    // that resets at all) a step belongs to. Read by callers that want to
    // group cards, and by the reset-rule tests.
    enum Loop {
        LoopSession = 0,
        LoopBand,
        LoopStation,
        LoopDestination
    };

    // The window's five tabs, START first (R2 §3: "New persisted key
    // DiversityWindowPage ... No stored value -> START"). WP-B renumbers its
    // own QStackedWidget to match; this model never opens a page, it only
    // names one for a cure to land on.
    enum Page {
        PageStart = 0,
        PageSlice,
        PageBand,
        PageSite,
        PageFilter
    };

    // The one-click fix for the step's first undone tick, or an empty Cure
    // (kind.isEmpty()) when there is nothing to press: the step is done, the
    // gate is gone, or AUTO CLEAN is off (R2.1: "Manual operators ... NEXT
    // becomes a status line, never a nudge -- no button").
    //
    //   kind "align"  -- GET /diversity/align, query always empty.
    //   kind "set"    -- GET /diversity/set?<query>.
    //   kind "dig"    -- GET /diversity/dig?<query> (starts a run).
    //   kind "page"   -- no write at all; only Step::page matters. Used by
    //                    every cure that R2.1 says "does not start anything"
    //                    -- BAND's GO goes to SITE without tuning away, and
    //                    STATION's GO goes to BAND without picking a voice.
    struct Cure {
        QString label;
        QString kind;
        QString query;
    };

    // One card. `tone` is one of four exact tokens -- "lit" (this is
    // nextStep(), AUTO CLEAN on: the only step with a live cure), "state"
    // (this is nextStep(), AUTO CLEAN off: shown, not nudged), "plain" (done,
    // behind you) or "dim" (ahead, not yet next). Every field is drawn in
    // every state per R2.2's "All five cards are drawn in full ... only the
    // tone, the state line and the button change" -- gives/when never
    // disappear once a step is done.
    struct Step {
        int id{StepReceiver};
        int loop{LoopSession};
        QString title;
        QString gives[2];
        QString when;
        QString state;
        QString tone;
        bool done{false};
        int page{PageStart};
        Cure cure;
    };

    DiversitySessionModel();

    // Injectable clock, for the beacon-freshness test (30 min, R2.1) to be
    // deterministic. Must be called before apply() the first time; apply()
    // itself never reads the wall clock.
    void setNowSecs(qint64 nowSecs);

    // Which noise-finding kinds the operator has dismissed (AppSettings is
    // the caller's problem -- R2.1: "Every noise finding gets DISMISS ...
    // persisted per finding kind ... until the finding's dB changes by more
    // than 3 dB". This model only ever sees the current set, verbatim).
    void setDismissedKinds(const QSet<QString>& kinds);

    // The five payloads this model reads, exactly as polled, plus the
    // tuned frequency in Hz. Recomputes every step from scratch; nothing
    // here is cached across calls except the retune bookkeeping the reset
    // rules need (see the .cpp's "reset rules" section).
    void apply(const QJsonObject& diversity, const QJsonObject& filter,
               const QJsonObject& dig, const QJsonObject& beacons,
               const QJsonObject& compass, double tunedHz);

    // The five cards, in StepId order, drawn as of the last apply().
    QVector<Step> steps() const;

    // The first undone of RECEIVER, SITE NOISE, BAND, STATION; StepListen
    // once all four are done; -1 when the gate is gone (nothing is next
    // because nothing can be answered -- R2.2's "gate gone | none").
    int nextStep() const;

    // False once apply() has read a diversity payload that is empty or
    // whose own "available" is false. Every card dashes and offers no cure
    // in that state (R2.2's gateGone row).
    bool gateAvailable() const { return m_available; }

    // True once RECEIVER, SITE NOISE, BAND and STATION are all done -- what
    // the NEXT strip's collapse decision (R2.1's 22 px dot-and-word) reads;
    // rendering the collapse itself is the window's job, not this model's.
    bool allChoresDone() const;

    // "mode=track", "source=combined", "auto=on", in that exact order --
    // QUICK START's whole contract (R2.1). Static: does not depend on
    // anything apply() has read, because QUICK START's job is to reach the
    // same state regardless of where the gate currently is.
    QStringList quickStartQueries() const;

    // The DIG offer's own state sentence, quoting only the gate's fields
    // (never recomputing gain_db from objective_before/after -- the same
    // rule DiversityFlowStrip::digState() keeps). DIG is not a step (R2.1:
    // "Not steps: the two offers"), so it has no Step of its own; this is
    // the whole of what the model says about it.
    QString digSummary() const;

private:
    Step buildReceiver() const;
    Step buildSiteNoise() const;
    Step buildBand() const;
    Step buildStation() const;
    Step buildListen() const;

    void applyToneAndCure(Step& step, int next) const;

    // Amateur-band identity for a frequency, and the beacon frequency (0 if
    // none) that band carries. A private table rather than BandPlanManager:
    // that class needs a QObject, resource-loaded plans and a search range
    // you already have to know the band to construct (contiguousRegionsFor
    // Band(lowMhz, highMhz)) -- there is no bare "which band is this Hz in"
    // call on it to reuse headlessly. Same trade DiversityBeaconPanel.cpp's
    // own kBandNames table already made (see its comment); this is a fourth
    // copy of the same small idea, now for the four owning files' worth of
    // Diversity* code that need it.
    static int bandIndexForHz(double hz);
    static double beaconHzForBandIndex(int index);

    bool m_haveNow{false};
    qint64 m_nowSecs{0};

    QSet<QString> m_dismissed;

    bool m_available{false};
    QJsonObject m_diversity;
    QJsonObject m_filter;
    QJsonObject m_dig;
    QJsonObject m_beacons;
    QJsonObject m_compass;
    double m_tunedHz{0.0};

    // --- reset-rule bookkeeping (R2.1: "a step lower never resets a step
    // above it") ---------------------------------------------------------
    // The gate does not clear `talker` on retune today (R2.0's own trace:
    // "talking + active talker: nothing; VAD only"), so a talker id that
    // matched before a big retune can still read as a match afterwards --
    // a false positive, not a fact. apply() compares this call's tuned
    // frequency against the last one: a band change, or a same-band move
    // bigger than the current filter's passband, marks whichever talker id
    // the payload names right then as SUSPECT. STATION reads done=false
    // for that id until the gate names a different one (a real
    // re-confirmation) or none at all -- BAND and the SESSION loop read
    // neither of these fields, which is what keeps the reset scoped to
    // STATION alone.
    bool m_haveLastTuned{false};
    double m_lastTunedHz{0.0};
    bool m_stationSuppressed{false};
    int m_suppressedTalkerId{-1};
};

// --- text, kept in DiversitySessionText.cpp -------------------------------
//
// Free functions rather than members, and split into their own translation
// unit, for the same reason DiversityFlowStripAuto.cpp/…Dig.cpp are split
// out of DiversityFlowStrip.cpp (see that header): every "gives"/"when"
// line is somewhere a reviewer can audit them all at once, in one file,
// without reading a single line of derivation logic.

// One step's fixed prose: the two "gives" lines and the one "when" line.
// stepId is one of DiversitySessionModel's five StepId values; anything
// else returns an empty SessionCopy rather than guessing.
struct SessionCopy {
    QString title;
    QString gives[2];
    QString when;
};

SessionCopy sessionStepCopy(int stepId);

// "track follows the talker -- null steers away from the noise", the
// operator's own words (Phase 3a plan, R2.1), appended to RECEIVER's state
// whenever mode is off so the choice between the two cures TRACK and NULL
// is explained rather than merely offered.
QString sessionPairModeExplain();

// Every fixed literal line this model ever prints: the fifteen lines
// sessionStepCopy() returns across the five steps, plus sessionPair
// ModeExplain(). What diversity_session_model_test's
// everyCopyLineFitsNinetyTwoChars walks.
QStringList sessionAllFixedCopyLines();

} // namespace AetherSDR
