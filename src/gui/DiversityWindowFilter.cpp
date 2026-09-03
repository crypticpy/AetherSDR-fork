// The Diversity window's FILTER page: what the PAIR itself does to the audio
// before the receiver's own filter chain ever sees it.
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

#include <QFrame>
#include <QJsonObject>
#include <QScrollArea>
#include <QToolButton>

namespace AetherSDR {

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

void DiversityWindow::clearFilterReadouts()
{
    if (m_filter)
        m_filter->clear();
}

} // namespace AetherSDR
