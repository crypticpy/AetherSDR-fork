#pragma once

// DiversityFlowStrip -- the Diversity window's answer to "where do I start?".
//
// Every other readout in this window answers a question the operator has to
// already know to ask. The four pages, the two rows of pair controls and the
// forty-odd numbers under them are all true and none of them says which one
// matters FIRST. The operator who sat in front of v2 said exactly that: "where
// do I start, what is the flow, is there an order of operations". There is one,
// it has always been implicit in the physics, and this strip is it written down
// where it can be read:
//
//   1 ALIGN   Nothing can be combined until the two tuners' sample streams are
//             lined up. Every other number on every page is meaningless while
//             they are not, so this is first and there is no arguing with it.
//   2 MODE    A weight has to be being solved for. OFF is loop A on its own --
//             an ordinary single-tuner receiver with a second antenna wasted.
//   3 HEAR    ...and the combined output has to be what reaches the ears. A
//             perfectly solved weight with HEAR on A is silent work.
//   4 NOISE   Only now is it worth acting on what the site is doing to you.
//             The gate profiles the noise floor by itself and nominates one
//             control per finding; before the tuners are aligned it has
//             nothing to profile with.
//   5 FILTER  Last, because the filter is about how the station SOUNDS, and
//             the four steps above decide whether there is anything to sound
//             like. It is also the only step that is never "not done" -- there
//             is always a filter in force -- so it is the strip's last stop
//             rather than a thing to finish.
//
// The state beside each step is the gate's own, quoted back: no number here is
// recomputed and nothing is set optimistically when a step is clicked. The
// step the operator should do NEXT is the first one in order that is not done,
// and it is the one drawn lit; the ones before it are plain and the ones after
// it are dimmed. That single rule is the whole widget -- everything else is
// wording.
//
// WHY ONE LINE AT THE BOTTOM RATHER THAN A ROW OF PILLS AT THE TOP. The first
// build of this drew the five steps as buttons in a strip under the four page
// tabs, and the operator read the strip as a second tab bar: "the tabs change
// what I'm seeing, but there's these flows that look like actual tabs for what
// I am seeing... it currently looks like the tab you're supposed to be on, but
// the tabs are at the top". Five lit rounded boxes in a row directly beneath
// four lit rounded boxes in a row is a navigation control, whatever the words
// on it say. So the flow is now ONE checklist line at the foot of the window,
// immediately above the gate status strip, where nothing in the layout can be
// mistaken for navigation -- and it is written the way a checklist is read:
// "✓ align lag −63 · ✓ mode track · ● hear · A only → hear OUT · ○ noise ·
// ○ filter", with only the next step clickable.
//
// AND IT IS ABOUT THE PAGE YOU ARE ON. The second half of the same complaint
// was that the flow should be "relevant to whatever tab you're currently on".
// setCurrentPage() is how: the steps that belong to the page in front of the
// operator are drawn in full, everything else goes dim. The next step is never
// hidden by that rule -- when it belongs to another page its link says which
// one, so the one thing to do next is always readable from every page.
//
// It keeps no transport and no timers. Two payloads reach it, both of which the
// window already receives for other reasons (/diversity every poll, /filter
// whenever the FILTER page is up or a filter write is answered), and one signal
// leaves it: stepActivated(), which the window turns into a page switch and,
// where the step has a one-click cure, the write that applies it.

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QJsonArray;
class QJsonObject;
class QLabel;

namespace AetherSDR {

class DiversityFlowStrip : public QWidget {
    Q_OBJECT
public:
    // The five steps, in the order the strip draws them and in the order
    // "which one is next" walks. The values are also what stepActivated()
    // carries, so a slot can switch on them rather than on a magic index.
    enum Step {
        StepAlign = 0,
        StepMode,
        StepHear,
        StepNoise,
        StepFilter,
        StepCount
    };

    // The window's four pages, in the order QStackedWidget holds them, which
    // is also the order the tab row draws them. Only three of them own a step
    // -- BAND owns none, and is the page on which every step is somewhere
    // else.
    enum Page {
        PageSlice = 0,
        PageBand,
        PageSite,
        PageFilter
    };

    explicit DiversityFlowStrip(QWidget* parent = nullptr);

    // One /diversity status object, exactly as DiversityWindow::applyDiversity
    // received it. `available` is that method's own available flag: false means
    // the gate answered but has no diversity, which is not the same fact as a
    // dead gate and is not this strip's business to distinguish -- both clear
    // the four steps that come off this payload.
    void applyDiversity(const QJsonObject& d, bool available);

    // One /filter answer. An empty object (a dropped poll) and an
    // {"error": ...} refusal both leave the step where it is: the FILTER step
    // states what is IN FORCE, and a failed request did not change that.
    void applyFilter(const QJsonObject& filter);

    // /diversity's memory[], for the name behind the talker id /filter reports.
    // A separate call rather than a read out of applyDiversity() because the
    // FILTER step is the one step fed from BOTH payloads, and the two arrive on
    // different polls: the strip has to be able to hold a name across a tick
    // where only one of them landed.
    void setTalkerNames(const QJsonArray& memory);

    // Gate gone. Every step goes to a dash and ALIGN becomes next again --
    // when the gate comes back the order starts from the top, because after a
    // reconnect the tuners genuinely may not be aligned.
    void clear();

    // The step the operator should do next: the first one in order that is not
    // done, or StepFilter when the first four are. Read by tests; the strip
    // itself uses it to decide which step is the link.
    int nextStep() const { return m_next; }

    // How one step is drawn: "lit" (the next step, and the only clickable
    // one), "normal" (a step that belongs to the page in front of the
    // operator) or "dim". A painted colour inside rich text has no other
    // readable trace -- DiversitySnrMeter::shownDb() exists for the same
    // reason -- and this is what a test asserts the page relevance on.
    QString stepTone(int step) const;

    // Which page a step is about. Anything outside the five steps is PageBand,
    // which is the page that owns no step.
    static int stepPage(int step);

public slots:
    // The page the window just switched to, as a Page. Connected to the
    // stack's own currentChanged rather than to the tab buttons, so a page
    // switch made from anywhere -- a tab, a FLOW click, a SITE row's button --
    // reaches the line.
    void setCurrentPage(int page);

signals:
    // A step was clicked. The window owns what that means -- see
    // DiversityWindowChain.cpp -- because the cure for a step is a page switch
    // plus, sometimes, a gate write, and this widget knows about neither.
    void stepActivated(int step);

private:
    // One step's derived state: the words after the "·" and whether the
    // operator still has something to do about it.
    struct State {
        QString text;
        bool    done{false};
    };

    void rebuild();

    // The tab's own word for a page, for the "→ SITE" a next step wears when
    // it is not on the page in front of the operator. Empty for BAND, which no
    // step is ever on.
    static QString pageWord(int page);

    State alignState() const;
    State modeState() const;
    State hearState() const;
    State noiseState() const;
    State filterState() const;

    QLabel*             m_caption{nullptr};
    QLabel*             m_line{nullptr};
    QStringList         m_labels;
    QVector<QString>    m_tones;
    int                 m_next{StepAlign};
    int                 m_page{PageSlice};

    // --- last /diversity ---------------------------------------------------
    bool    m_available{false};
    bool    m_aligned{false};
    bool    m_realigning{false};
    bool    m_haveLag{false};
    double  m_lagSamples{0.0};
    QString m_mode;
    QString m_source;
    // The noise profile, reduced to the three facts the step needs: whether the
    // gate has profiled at all, how many findings still have an unused action,
    // and the kinds of the ones already being acted on.
    bool        m_haveProfile{false};
    int         m_offered{0};
    QStringList m_activeKinds;

    // --- last /filter ------------------------------------------------------
    bool    m_haveFilter{false};
    bool    m_filterAvailable{false};
    QString m_filterEdges;
    QString m_filterShape;
    bool    m_filterAuto{false};
    // Whose filter is in force. The id is /filter's; the name is /diversity's,
    // and a talker the combiner has no label for is quoted as its number
    // rather than left out -- "somebody's own filter" is the fact, and the
    // number is how the TALKERS table names them too.
    bool    m_talkerOn{false};
    bool    m_haveTalkerId{false};
    int     m_talkerId{0};
    QHash<int, QString> m_talkerNames;
    // The automatic contour, reduced to the three facts the step states: is it
    // fitting, has it fitted anything yet, and where it put the bell.
    bool    m_autoContour{false};
    bool    m_haveContour{false};
    double  m_contourHz{0.0};
    double  m_contourDb{0.0};
};

} // namespace AetherSDR
