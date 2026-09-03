// B25 DIG STOP, drawn directly on the FLOW strip's own line rather than only
// in the window's three-button stack (DiversityWindowFilter.cpp's
// buildDigControls(), which keeps its own STOP under the object name
// "diversityWindowFlowDigStop"). That stack sits BESIDE this strip and is
// easy to miss while reading the checklist itself; this is the same write,
// on the line that says a dig is running at all. A deliberately redundant
// control, not a replacement -- the existing stack is untouched.
//
// Its own unit for the same reason DiversityFlowStripAuto.cpp is one:
// DiversityFlowStrip.cpp is already at AGENTS.md's 800-line budget.

#include "gui/DiversityFlowStrip.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QPushButton>

namespace AetherSDR {

void DiversityFlowStrip::updateDigStopButton()
{
    if (!m_digStopButton) {
        m_digStopButton = new QPushButton(tr("STOP"), this);
        m_digStopButton->setObjectName(
            QStringLiteral("diversityWindowFlowStripDigStopButton"));
        m_digStopButton->setAccessibleName(tr("Stop the running dig"));
        m_digStopButton->setToolTip(
            tr("End the run now and put the chain back exactly as you had "
               "it. Nothing the dig found is kept."));
        m_digStopButton->setCursor(Qt::PointingHandCursor);
        // Same helper the window's own STOP already wears (default Accent
        // tribe) -- no new colour, no new setStyleSheet call site.
        applyToggleButtonStyle(m_digStopButton);
        connect(m_digStopButton, &QPushButton::clicked, this,
                &DiversityFlowStrip::requestDigCancel);
        // At the end of the line -- the DIG step's own state is the last
        // thing the checklist says, and this is its control.
        if (auto* box = qobject_cast<QHBoxLayout*>(layout()))
            box->addWidget(m_digStopButton);
    }
    m_digStopButton->setVisible(m_digRunning);
}

} // namespace AetherSDR
