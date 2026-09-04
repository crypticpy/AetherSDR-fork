// The Diversity window's /filter seam and the dig's own buttons: two things
// that are about the same chain and nothing else in this window is.
//
// There used to be a FILTER page above them here. It is retired. Every stage
// switch it carried -- the coherence post-filter and the sub-band MRC
// weighting, the two the PAIR itself owns -- is a chain row like any other
// now, drawn by the gate's own CHAIN window (AetherGateChainWindow) along
// with roofing, the blanker, shape, notch, the automatic notcher, contour,
// the audio peaking filter, auto EQ, per-talker recall and AGC. One window
// draws the chain once rather than once per place a receiver's filter chain
// shows up, and OPEN CHAIN moved to the pair row (DiversityWindowChain.cpp)
// so it is one press from every page rather than a page of its own.
//
// What is left in this file is the seam: the one method that takes a /filter
// answer for the START page's STATION step, and the dig -- its duration
// buttons, its STOP/verdict controls, and the cadence its status is polled
// at.

#include "gui/DiversityWindow.h"

#include "gui/DiversityNextStrip.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrlQuery>

namespace AetherSDR {

namespace {

// The same height every other small button in this window is (the SITE page's
// SET/FORGET, the beacon check row).
constexpr int kDigButtonHeight = 20;

// While a run is going, or while one is waiting to be judged, the dig moves
// once a second and is worth asking about once a second. Otherwise it is a
// fact that changes only when the operator presses something, and ten seconds
// is enough to notice a run somebody started from the other view.
constexpr int kDigBusyMs = 1000;
constexpr int kDigIdleMs = 10000;

} // namespace

// --------------------------------------------------------------------------
// One /filter answer, for the one card that is about a filter
// --------------------------------------------------------------------------

void DiversityWindow::applyFilter(const QJsonObject& filter)
{
    // The STATION step is about whose filter is in force, so the session model
    // is fed wherever a /filter answer arrives -- including the reply to a
    // write made from the CHAIN window -- rather than only while some page is
    // up. Nothing in this window DRAWS a filter.
    //
    // An empty answer or an error is not a filter: it leaves the last one
    // standing, the same guard the strip this replaced kept, because "the
    // request failed" is not the same fact as "there is no filter".
    if (filter.isEmpty() || filter.contains(QStringLiteral("error")))
        return;
    m_lastFilter = filter;
    refreshSession();
}

// --------------------------------------------------------------------------
// The dig's own buttons
// --------------------------------------------------------------------------
//
// Two homes, because the dig is two different things at two different moments.
//
//   * The three DURATIONS are an OFFER -- something you may spend a minute on,
//     never something the checklist is waiting for. They live on the START
//     page's OFFERS row, beside QUICK START, under the object names they have
//     always had (buildDigDurations()).
//   * STOP, and the three verdict words a finished run asks for, are about a
//     run that is happening NOW. They live on the NEXT strip, at the end of
//     the one line that is on screen whatever page the operator wandered to
//     (buildDigControls()).
//
// A QStackedWidget rather than show/hide for the second group, because the
// strip is the last row above the status bar and the two states have to
// occupy the same space: a stack is as wide and as tall as its widest page in
// every one of them.
//
// They live on the window rather than on the strip that describes them, for
// the reason every other write in this window does: the strip is a derivation
// of polled state and owns no transport, and these buttons are nothing but
// writes.

namespace {

// One small button on a dig row. The caller wires the click, because what
// goes on the wire is the window's business and this is a widget.
// `tip` is the one-line hover text (what the button is and what it is for);
// `why` is the longer story, kept as the accessible description.
QPushButton* makeDigButton(QWidget* row, const QString& text, const QString& objectName,
                           const QString& accessible, const QString& tip, const QString& why)
{
    auto* button = new QPushButton(text, row);
    button->setObjectName(objectName);
    button->setAccessibleName(accessible);
    button->setToolTip(tip);
    button->setAccessibleDescription(why);
    button->setFixedHeight(kDigButtonHeight);
    applyToggleButtonStyle(button);
    row->layout()->addWidget(button);
    return button;
}

QWidget* makeDigRow(QWidget* parent, const char* objectName)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QString::fromLatin1(objectName));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    return row;
}

} // namespace

QWidget* DiversityWindow::buildDigDurations()
{
    QWidget* offer = makeDigRow(this, "diversityWindowFlowDigOffer");
    const QString seconds = QStringLiteral("seconds");
    // One button, wired straight to the gate's own query. Nothing between the
    // click and the wire composes anything: the key and the value are what the
    // route documents, so a gate that grows a fourth duration needs no build.
    const auto wire = [this, seconds](QPushButton* button, const QString& value) {
        connect(button, &QPushButton::clicked, this, [this, seconds, value] {
            QUrlQuery q;
            q.addQueryItem(seconds, value);
            emit requestDig(q);
        });
    };
    wire(makeDigButton(offer, tr("1 MIN"), QStringLiteral("diversityWindowFlowDig60"),
                       tr("Dig for one minute"),
                       tr("A one-minute dig - a quick pass over the knobs that usually matter."),
                       tr("One minute of trying knobs. Long enough for the two or three "
                          "changes that usually matter, short enough to do mid-over.")),
         QStringLiteral("60"));
    wire(makeDigButton(offer, tr("3 MIN"), QStringLiteral("diversityWindowFlowDig180"),
                       tr("Dig for three minutes"),
                       tr("A three-minute dig, the default - finds a second change on top of the first."),
                       tr("Three minutes. The default: enough trials to get past the "
                          "first change that helped and find out whether a second one "
                          "helps on top of it.")),
         QStringLiteral("180"));
    wire(makeDigButton(offer, tr("5 MIN"), QStringLiteral("diversityWindowFlowDig300"),
                       tr("Dig for five minutes"),
                       tr("A five-minute dig for a weak signal that needs a long baseline."),
                       tr("Five minutes, for a weak signal that needs a long baseline "
                          "before a half-decibel means anything. Stop it at any time -- "
                          "what it has already kept, it keeps.")),
         QStringLiteral("300"));
    return offer;
}

QWidget* DiversityWindow::buildDigControls()
{
    m_digStack = new QStackedWidget(this);
    m_digStack->setObjectName(QStringLiteral("diversityWindowFlowDigControls"));
    m_digStack->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Page 0 is deliberately empty: with the durations moved to START there is
    // nothing for the footer to offer while no run is out. An empty page
    // rather than hiding the stack, so the footer is exactly as tall in all
    // three states -- it is the last row above the status strip and nothing
    // down there may move when a payload lands.
    m_digStack->addWidget(makeDigRow(m_digStack, "diversityWindowFlowDigIdle"));

    QWidget* running = makeDigRow(m_digStack, "diversityWindowFlowDigRunning");
    connect(makeDigButton(running, tr("STOP"), QStringLiteral("diversityWindowFlowDigStop"),
                          tr("Stop digging"),
                          tr("Stop the dig now and put the chain back as you had it."),
                          tr("End the run now and put the chain back exactly as you had "
                             "it. Nothing the dig found is kept.")),
            &QPushButton::clicked, this, [this] {
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("cancel"), QStringLiteral("1"));
                emit requestDig(q);
            });
    m_digStack->addWidget(running);

    QWidget* verdict = makeDigRow(m_digStack, "diversityWindowFlowDigVerdict");
    const QString word = QStringLiteral("verdict");
    const auto wireVerdict = [this, word](QPushButton* button, const QString& value) {
        connect(button, &QPushButton::clicked, this, [this, word, value] {
            QUrlQuery q;
            q.addQueryItem(word, value);
            emit requestDig(q);
        });
    };
    wireVerdict(makeDigButton(verdict, tr("BETTER"),
                              QStringLiteral("diversityWindowFlowDigBetter"),
                              tr("It sounds better"),
                              tr("Keep the dig's changes and file the run as one that helped."),
                              tr("Keep the changes and tell the gate they worked. It files "
                                 "the run so the next dig on this kind of signal starts "
                                 "from what helped last time.")),
                QStringLiteral("better"));
    wireVerdict(makeDigButton(verdict, tr("WORSE"),
                              QStringLiteral("diversityWindowFlowDigWorse"),
                              tr("It sounds worse"),
                              tr("Put your own settings back and file the run as wrong."),
                              tr("Put the chain back on your own settings and tell the gate "
                                 "the run was wrong. The measurement said it helped and "
                                 "your ears say it did not -- yours win, and the gate "
                                 "learns that.")),
                QStringLiteral("worse"));
    wireVerdict(makeDigButton(verdict, tr("KEEP"),
                              QStringLiteral("diversityWindowFlowDigKeep"),
                              tr("Keep it without judging"),
                              tr("Keep the changes without a verdict - for a run you did not hear."),
                              tr("Leave the changes in force without saying whether they "
                                 "sounded better. For a run you did not get to listen to.")),
                QStringLiteral("keep"));
    m_digStack->addWidget(verdict);

    m_digStack->setCurrentIndex(0);
    m_digStack->hide();
    return m_digStack;
}

// Which of the three faces the control wears, and whether it is there at all.
// Every fact it switches on is one the NEXT strip has already read off the
// same payload -- asked for rather than parsed twice, so the word on the line
// and the button beside it can never disagree.
void DiversityWindow::updateDigControls()
{
    if (!m_digStack || !m_nextStrip)
        return;
    m_digStack->setVisible(m_nextStrip->digAvailable());
    if (!m_nextStrip->digAvailable())
        return;
    if (m_nextStrip->digRunning())
        m_digStack->setCurrentIndex(1);
    else
        m_digStack->setCurrentIndex(m_nextStrip->digAwaitingVerdict() ? 2 : 0);
}



// --------------------------------------------------------------------------
// The dig's seam: one payload in, one cadence out
// --------------------------------------------------------------------------

void DiversityWindow::applyDig(const QJsonObject& dig)
{
    if (m_nextStrip)
        m_nextStrip->applyDig(dig);
    m_lastDig = dig;
    refreshSession();
    m_digPrimed = true;
    updateDigControls();
    updateDigPoll();
}

// 1 Hz while there is something moving, 10 s otherwise, and nothing at all
// while the window is hidden or the gate is not answering. The window owns
// this rather than the poller because it is the only object that knows whether
// a run is still going -- and unlike every other route here the dig is NOT
// gated on a page, because a run started from the START page goes on wherever
// the operator navigates to and the NEXT strip is at the foot of every page.
void DiversityWindow::updateDigPoll()
{
    if (!m_digTimer)
        return;
    if (!isVisible() || !m_present) {
        m_digTimer->stop();
        // A window opened again later starts from knowing nothing, so it asks
        // at the fast cadence again rather than sitting blank for ten seconds.
        m_digPrimed = false;
        return;
    }
    const bool busy =
        m_nextStrip && (m_nextStrip->digRunning() || m_nextStrip->digAwaitingVerdict());
    const int want = (busy || !m_digPrimed) ? kDigBusyMs : kDigIdleMs;
    if (m_digTimer->isActive() && m_digTimer->interval() == want)
        return;
    m_digTimer->start(want);
}

} // namespace AetherSDR
