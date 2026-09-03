// The Diversity window's FILTER page: what the PAIR itself does to the audio
// before the receiver's own filter chain ever sees it -- and, at the bottom of
// the file, the FLOW row's dig controls and the cadence their status is polled
// at, which are here because they are about the same chain.
//
// Every other stage a slice filter offers -- roofing, the blanker, shape,
// notch, the automatic notcher, contour, the audio peaking filter, auto EQ,
// per-talker recall, AGC -- exists for a single antenna and has nothing to do
// with having two. Those all moved to the gate's own CHAIN window (see
// AetherGateChainWindow), a page of its own opened from the applet, so they
// are drawn once rather than once per place a receiver's filter chain shows
// up. This page is the button that opens it, and the two stages that only
// exist because there is a SECOND loop to combine: the coherence post-filter
// and the sub-band MRC weighting, both /diversity/set keys read back off the
// same status object MODE/HEAR/PAN already use (DiversityWindowChain.cpp).
//
// What lives HERE is only the seam: how the page is wired into the window's
// page stack, and how /diversity's "post" and "mrc" objects reach it.
// DiversityFilterControls.h/.cpp own the widgets and the wire values; see
// that file for why POST-FILTER and MRC write immediately rather than
// through a hold, the way this page's controls used to for /filter/set.

#include "gui/DiversityWindow.h"

#include "gui/DiversityFilterControls.h"
#include "gui/DiversityFlowStrip.h"
#include "gui/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
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
// The window's half: one more page in the stack
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildFilterPage()
{
    m_filter = new DiversityFilterControls;
    // Signal-to-signal, exactly like every other write this window makes: the
    // page does not know there is a gate, only that it has asked for
    // something.
    connect(m_filter, &DiversityFilterControls::requestSet, this,
            &DiversityWindow::requestSet);
    connect(m_filter, &DiversityFilterControls::requestOpenChain, this,
            &DiversityWindow::requestOpenChain);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("diversityWindowFilterScroll"));
    scroll->setWidget(m_filter);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

bool DiversityWindow::filterPageVisible() const
{
    return m_pages && m_pageFilterButton && m_pageFilterButton->isChecked();
}

void DiversityWindow::applyFilter(const QJsonObject& filter)
{
    // The FLOW strip's last step states the passband in force, so it is fed
    // wherever a /filter answer arrives -- including the reply to a write made
    // from a different page -- rather than only while FILTER is up. Nothing
    // else on this page reads /filter any more: POST-FILTER and MRC are
    // /diversity fields, applied from applyDiversity() instead (see
    // DiversityWindow.cpp).
    if (m_flow)
        m_flow->applyFilter(filter);
}

// --------------------------------------------------------------------------
// The dig's own buttons
// --------------------------------------------------------------------------
//
// Three small buttons at the right-hand end of one line. Deliberately NOT a
// row of pills of their own: the complaint that moved this whole strip off a
// pill row was that a second row of lit boxes reads as navigation, and three
// more boxes on a second line would have been exactly that again. On the end
// of the line they read as what they are -- the control belonging to the last
// word of the checklist.
//
// A QStackedWidget rather than show/hide, because the strip is the last row
// above the status bar and the three states have to occupy the same space: a
// stack is as wide and as tall as its widest page in every one of them.
//
// They live on the window rather than on DiversityFlowStrip for the reason
// every other write in this window does: the strip is a derivation of polled
// state and owns no transport, and these three buttons are nothing but writes.

QWidget* DiversityWindow::buildDigControls()
{
    m_digStack = new QStackedWidget(this);
    m_digStack->setObjectName(QStringLiteral("diversityWindowFlowDigControls"));
    m_digStack->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    const auto makeRow = [this](const char* objectName) {
        auto* row = new QWidget(m_digStack);
        row->setObjectName(QString::fromLatin1(objectName));
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        return row;
    };
    // One button, wired straight to the gate's own query. Nothing between the
    // click and the wire composes anything: the key and the value are what the
    // route documents, so a gate that grows a fourth duration needs no build.
    const auto makeButton = [this](QWidget* row, const QString& text,
                                   const QString& objectName, const QString& accessible,
                                   const QString& tip, const QString& key,
                                   const QString& value) {
        auto* button = new QPushButton(text, row);
        button->setObjectName(objectName);
        button->setAccessibleName(accessible);
        button->setToolTip(tip);
        button->setAccessibleDescription(tip);
        button->setFixedHeight(kDigButtonHeight);
        applyToggleButtonStyle(button);
        connect(button, &QPushButton::clicked, this, [this, key, value] {
            QUrlQuery q;
            q.addQueryItem(key, value);
            emit requestDig(q);
        });
        row->layout()->addWidget(button);
        return button;
    };

    QWidget* offer = makeRow("diversityWindowFlowDigOffer");
    const QString seconds = QStringLiteral("seconds");
    makeButton(offer, tr("1 MIN"), QStringLiteral("diversityWindowFlowDig60"),
               tr("Dig for one minute"),
               tr("One minute of trying knobs. Long enough for the two or three "
                  "changes that usually matter, short enough to do mid-over."),
               seconds, QStringLiteral("60"));
    makeButton(offer, tr("3 MIN"), QStringLiteral("diversityWindowFlowDig180"),
               tr("Dig for three minutes"),
               tr("Three minutes. The default: enough trials to get past the "
                  "first change that helped and find out whether a second one "
                  "helps on top of it."),
               seconds, QStringLiteral("180"));
    makeButton(offer, tr("5 MIN"), QStringLiteral("diversityWindowFlowDig300"),
               tr("Dig for five minutes"),
               tr("Five minutes, for a weak signal that needs a long baseline "
                  "before a half-decibel means anything. Stop it at any time -- "
                  "what it has already kept, it keeps."),
               seconds, QStringLiteral("300"));
    m_digStack->addWidget(offer);

    QWidget* running = makeRow("diversityWindowFlowDigRunning");
    makeButton(running, tr("STOP"), QStringLiteral("diversityWindowFlowDigStop"),
               tr("Stop digging"),
               tr("End the run now and put the chain back exactly as you had "
                  "it. Nothing the dig found is kept."),
               QStringLiteral("cancel"), QStringLiteral("1"));
    m_digStack->addWidget(running);

    QWidget* verdict = makeRow("diversityWindowFlowDigVerdict");
    const QString word = QStringLiteral("verdict");
    makeButton(verdict, tr("BETTER"), QStringLiteral("diversityWindowFlowDigBetter"),
               tr("It sounds better"),
               tr("Keep the changes and tell the gate they worked. It files the "
                  "run so the next dig on this kind of signal starts from what "
                  "helped last time."),
               word, QStringLiteral("better"));
    makeButton(verdict, tr("WORSE"), QStringLiteral("diversityWindowFlowDigWorse"),
               tr("It sounds worse"),
               tr("Put the chain back on your own settings and tell the gate "
                  "the run was wrong. The measurement said it helped and your "
                  "ears say it did not -- yours win, and the gate learns that."),
               word, QStringLiteral("worse"));
    makeButton(verdict, tr("KEEP"), QStringLiteral("diversityWindowFlowDigKeep"),
               tr("Keep it without judging"),
               tr("Leave the changes in force without saying whether they "
                  "sounded better. For a run you did not get to listen to."),
               word, QStringLiteral("keep"));
    m_digStack->addWidget(verdict);

    m_digStack->setCurrentIndex(0);
    m_digStack->hide();
    return m_digStack;
}

// Which of the three faces the control wears, and whether it is there at all.
// Every fact it switches on is one the FLOW strip has already read off the
// same payload -- asked for rather than parsed twice, so the word on the line
// and the button beside it can never disagree.
void DiversityWindow::updateDigControls()
{
    if (!m_digStack || !m_flow)
        return;
    m_digStack->setVisible(m_flow->digAvailable());
    if (!m_flow->digAvailable())
        return;
    if (m_flow->digRunning())
        m_digStack->setCurrentIndex(1);
    else
        m_digStack->setCurrentIndex(m_flow->digAwaitingVerdict() ? 2 : 0);
}


// --------------------------------------------------------------------------
// The dig's seam: one payload in, one cadence out
// --------------------------------------------------------------------------

void DiversityWindow::applyDig(const QJsonObject& dig)
{
    if (m_flow)
        m_flow->applyDig(dig);
    m_digPrimed = true;
    updateDigControls();
    updateDigPoll();
}

// 1 Hz while there is something moving, 10 s otherwise, and nothing at all
// while the window is hidden or the gate is not answering. The window owns
// this rather than the poller because it is the only object that knows whether
// a run is still going -- and unlike every other route here the dig is NOT
// gated on a page, because a run started from the FLOW strip goes on wherever
// the operator navigates to and the strip is at the foot of every page.
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
    const bool busy = m_flow && (m_flow->digRunning() || m_flow->digAwaitingVerdict());
    const int want = (busy || !m_digPrimed) ? kDigBusyMs : kDigIdleMs;
    if (m_digTimer->isActive() && m_digTimer->interval() == want)
        return;
    m_digTimer->start(want);
}

void DiversityWindow::clearFilterReadouts()
{
    if (m_filter)
        m_filter->clear();
}

} // namespace AetherSDR
