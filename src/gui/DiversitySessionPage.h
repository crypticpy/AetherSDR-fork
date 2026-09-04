#pragma once

// The Diversity window's START page -- the whole session workflow, written
// out, with the WHY beside every step. Phase 3a WP-B; the model behind it is
// DiversitySessionModel (WP-A), the footer that quotes one line of it is
// DiversityNextStrip.
//
// WHY A PAGE AND NOT A LINE. The FLOW strip this replaces had one line at the
// foot of the window for five steps, and one line can say WHICH step is next
// but never WHY that order is the order. The operator's own words were "the UI
// must say the order -- beacons? noise profile? filtering? watch the slice --
// and WHY". A card per step, 86 px each, has room for the two "gives" lines
// and the one "when" line the model already carries, and those three lines are
// drawn in every state: a step that is behind you still says what it bought
// you, which is what makes this a station display rather than a wizard that
// blanks each page as you leave it.
//
// NOTHING HERE WRITES. Every card's cure leaves as cureActivated(stepId), and
// the OFFERS row's QUICK START leaves as quickStartRequested(); the window
// turns both into the same requestSet/requestAlign/requestDig/showPage doors
// every other control in the window already leaves through. The 1/3/5 MIN DIG
// buttons are not built here at all -- they WRITE, so the window builds them
// (DiversityWindowFilter.cpp's buildDigDurations()) and hands the widget in
// through setDigDurations(), exactly the way the dig stack has always been
// owned by the window rather than by the strip that describes it.
//
// NO WORD WRAP, ANYWHERE. Every label on this page is a fixed "\n"-joined
// string with wordWrap false and an Ignored horizontal size policy. A
// height-for-width label inside a QScrollArea makes the scroll area size to
// sizeHint() rather than minimumSizeHint(), and a scrollbar appears at
// 1120x860 on a page that fits. That has cost this window a scrollbar twice.

#include <QString>
#include <QVector>
#include <QWidget>

#include "gui/DiversitySessionModel.h"

class QLabel;
class QPushButton;

namespace AetherSDR {

// One step's card: the title, the gate's own state sentence, the one-click
// cure, and the three fixed lines of copy under them. Declared here and
// defined in DiversitySessionCard.cpp -- the same split DiversityFlowStrip
// kept for its own two extra units, for the same 800-line reason.
class DiversitySessionCard : public QWidget {
    Q_OBJECT
public:
    // `index` is 1..5, the number the operator reads on the card and the
    // number in its objectName ("diversityWindowSessionCard1").
    DiversitySessionCard(int index, QWidget* parent = nullptr);

    // One Step, exactly as the model built it. The three copy lines are set
    // once (they never change) and the state, tone and cure every call.
    void setStep(const DiversitySessionModel::Step& step);

    // The tone the card was last drawn in -- "lit", "state", "plain" or
    // "dim". A painted colour has no other readable trace; this is what the
    // page test asserts "the lit card is the first not done" on.
    QString tone() const { return m_tone; }

signals:
    // The cure button was pressed. Carries the StepId the card is showing,
    // not the cure itself: the window re-reads the model rather than trusting
    // a label that may be a poll out of date.
    void cureActivated(int stepId);

private:
    void applyTone();

    int      m_index{1};
    int      m_stepId{0};
    QString  m_tone;
    QLabel*  m_title{nullptr};
    QLabel*  m_state{nullptr};
    QLabel*  m_body{nullptr};
    QPushButton* m_cure{nullptr};
};

class DiversitySessionPage : public QWidget {
    Q_OBJECT
public:
    explicit DiversitySessionPage(QWidget* parent = nullptr);

    // The five cards, in StepId order, plus which one nextStep() named. Both
    // come straight off DiversitySessionModel::steps()/nextStep().
    void setSteps(const QVector<DiversitySessionModel::Step>& steps, int next);

    // The OFFERS row's one derived line: DiversitySessionModel::digSummary(),
    // or the fixed copy when there is no run to talk about.
    void setDigLine(const QString& text);

    // The window's own 1/3/5 MIN buttons (they write, so it builds them).
    // Inserted into the OFFERS row after the DIG OUT caption.
    void setDigDurations(QWidget* durations);

signals:
    void cureActivated(int stepId);
    void quickStartRequested();

private:
    DiversitySessionCard* m_cards[DiversitySessionModel::StepCount]{};
    QLabel*  m_digLine{nullptr};
    QWidget* m_offers{nullptr};
    QPushButton* m_quickStart{nullptr};
};

} // namespace AetherSDR
