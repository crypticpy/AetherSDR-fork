#pragma once

// The Diversity window's footer line -- ONE step, ONE button. Phase 3a WP-B;
// replaces DiversityFlowStrip, whose five-glyph checklist is now the START
// page (DiversitySessionPage) with room for the WHY beside each step.
//
// WHY ONE STEP. The FLOW strip said all five at once, on a line that had to
// fit inside 1120 px, so every state string was clipped down to a couple of
// words and the checklist read as a second tab bar under the real one -- the
// exact confusion it was built to end. A footer that quotes only the step the
// operator is actually on has room for the gate's whole state sentence, and
// the one button beside it is unambiguous rather than one of five.
//
// WHAT IT DERIVES AND WHAT IT IS TOLD. The step, its state and its cure are
// handed in whole from DiversitySessionModel via setNext(): this widget makes
// no judgement about which step is next and never re-parses /diversity for
// one. Two things it DOES read off a payload, because nothing else on the
// footer can:
//
//   * the governor block, for the AUTO CLEAN switch (moved here verbatim from
//     DiversityFlowStripAuto.cpp, object name and all -- the operator was
//     explicit that the switch carries the two words and nothing else);
//   * /diversity/dig, for the run's own clock, quoted after the step because
//     a run is the thing happening NOW on whatever page the operator
//     wandered to. The STOP button itself is the window's (setDigControls()
//     below just gives it a home at the end of this row) -- there used to be
//     a second one here too, and a run out live showed both at once.
//
// COLLAPSE. Once RECEIVER, SITE NOISE, BAND and STATION are all done there is
// no next step to nudge about, so the line collapses to the one fact worth a
// row of the window at that point -- who is talking and what the pair is
// buying on them. A click toggles it and the choice persists under the
// AppSettings key DiversityNextStripCollapsed.
//
// NO WORD WRAP. One line, 22 px, wordWrap false, Ignored horizontally. See
// DiversitySessionPage.h's own note: this window has grown a scrollbar twice
// from a height-for-width label.

#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "gui/AetherGateChainAuto.h"
#include "gui/DiversitySessionModel.h"

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace AetherSDR {

class DiversityNextStrip : public QWidget {
    Q_OBJECT
public:
    explicit DiversityNextStrip(QWidget* parent = nullptr);

    // The one step to quote, whether there is one at all (nextStep() answers
    // -1 across a dead gate), the LISTEN step's state for the collapsed line,
    // and whether the four chores are behind us. All four straight off
    // DiversitySessionModel; nothing here is re-derived.
    void setNext(const DiversitySessionModel::Step& next, bool haveNext,
                 const QString& listenState, bool allDone);

    // The governor block off the /diversity status object -- the AUTO CLEAN
    // switch's only input. `available` false clears it, the same way a
    // dropped poll clears every other readout in this window.
    void applyDiversity(const QJsonObject& d, bool available);

    // One /diversity/dig status object. Drives the STOP button, the run's
    // clock on the line, and the three predicates the window's own dig stack
    // and poll cadence read back off this widget.
    void applyDig(const QJsonObject& dig);

    void clear();

    bool digAvailable() const { return m_digAvailable; }
    bool digRunning() const { return m_digRunning; }
    // A finished run nobody has judged. Cancelled and errored runs are NOT
    // that: the chain is already back on the operator's own settings, so
    // there is nothing to be a verdict about. Carried over verbatim from
    // DiversityFlowStrip::digAwaitingVerdict().
    bool digAwaitingVerdict() const;

    // The line as a screen reader would hear it -- no markup. What the tests
    // assert on; a rich-text QLabel has no other readable trace.
    QString lineText() const { return m_plain; }

    // True while the strip is showing the one-line listening summary rather
    // than a step. Only ever true once every chore is done.
    bool collapsed() const;

    // The window's own dig buttons (STOP, and the three verdict words). They
    // WRITE, so the window builds them -- see DiversityWindowFilter.cpp --
    // and this strip only gives them a home at the end of its row.
    void setDigControls(QWidget* controls);

signals:
    // The cure button was pressed, carrying the StepId it was drawn for. The
    // window re-reads the model rather than trusting a label a poll old.
    void cureActivated(int stepId);
    void requestAutoCleanToggle(bool on);

private:
    void rebuild();
    void updateAutoCleanButton();
    QString digTail() const;

    QHBoxLayout* m_row{nullptr};
    QLabel*      m_line{nullptr};
    QPushButton* m_button{nullptr};
    QPushButton* m_autoCleanButton{nullptr};
    QString      m_plain;

    // --- the step, as handed in ------------------------------------------
    bool    m_haveNext{false};
    int     m_nextId{DiversitySessionModel::StepReceiver};
    QString m_title;
    QString m_state;
    QString m_cureLabel;
    QString m_listenState;
    bool    m_allDone{false};

    // The operator's own choice, persisted. Only consulted once every chore
    // is done -- there is nothing to collapse before that.
    bool m_collapsePref{true};

    ChainAutoGovernor m_governor;

    bool    m_digAvailable{false};
    bool    m_digRunning{false};
    QString m_digPhase;
    QString m_digVerdict;
    QString m_digError;
    bool    m_digCancelled{false};
    double  m_digGainDb{0.0};
    double  m_digElapsedS{0.0};
    double  m_digSeconds{0.0};
};

} // namespace AetherSDR
