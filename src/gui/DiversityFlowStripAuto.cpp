// B25 AUTO CLEAN's own banner on the FLOW strip -- the second of the three
// surfaces docs/DIVERSITY.md's "AUTO CLEAN: the chain decides" asks for a
// switch on (the sidebar's copy is gui/AetherGateDiversityPanel.cpp; the
// CHAIN window's read-only header is gui/AetherGateChainWindowTabs.cpp). Its
// own unit rather than folded into DiversityFlowStrip.cpp: that file sits
// right at AGENTS.md's 800-line budget already.
//
// The operator's own words: "Auto clean should be an option somewhere that
// we can turn on and off but it should be really visible when we turn that
// on." A checkable QPushButton is both halves of that at once -- pressed IS
// on, and the text carries the state+why the moment it is. Off, it collapses
// to the bare label per the header's own contract on chainAutoIndicatorLine().
//
// Not optimistic: a click here never toggles the button itself. It emits
// requestAutoCleanToggle(), the window turns that into GET
// /diversity/set?auto=on|off, and the next /diversity poll's governor block
// is what actually moves this widget -- exactly the discipline every write
// in this window keeps.

#include "gui/DiversityFlowStrip.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>

namespace AetherSDR {

void DiversityFlowStrip::updateAutoCleanBanner()
{
    if (!m_autoCleanButton) {
        m_autoCleanButton = new QPushButton(tr("AUTO CLEAN"), this);
        m_autoCleanButton->setObjectName(
            QStringLiteral("diversityWindowFlowAutoCleanButton"));
        m_autoCleanButton->setAccessibleName(tr("AUTO CLEAN switch"));
        m_autoCleanButton->setToolTip(
            tr("The chain's own governor: it measures the noise profile and "
               "moves one tool at a time, putting a move back if the audio "
               "got worse. Off by default; off, it holds nothing."));
        m_autoCleanButton->setCheckable(true);
        m_autoCleanButton->setCursor(Qt::PointingHandCursor);
        applyToggleButtonStyle(m_autoCleanButton, ToggleTribe::Warning);
        // The gate's own `why` is an arbitrary sentence -- unlike every other
        // fixed-shape readout in this window, this one has no true worst
        // case. Ignored, the same treatment m_line already carries, so a
        // long one clips instead of dragging the window's minimum width
        // past the 1120 it opens at.
        //
        // Unlike m_line, this widget sits in the same QHBoxLayout WITHOUT a
        // stretch factor, next to m_line's stretch of 1 -- a bare
        // setMinimumWidth(0) here made it a zero-width, invisible button
        // whenever that sibling was present, because a stretch-0 Ignored
        // item's whole contribution collapses to its minimum once any
        // sibling declares stretch. Floor it at the compact label's own
        // width instead, so it always renders as a real button; the long
        // indicator sentence still clips, just inside that floor rather
        // than inside a nonexistent one.
        m_autoCleanButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_autoCleanButton->setMinimumWidth(
            m_autoCleanButton->fontMetrics().horizontalAdvance(tr("AUTO CLEAN ON")) + 32);
        connect(m_autoCleanButton, &QPushButton::clicked, this, [this](bool checked) {
            emit requestAutoCleanToggle(checked);
        });
        // Right after the FLOW caption, ahead of the checklist itself -- the
        // "really visible" the operator asked for, on the one line every
        // page of this window keeps on screen.
        if (auto* box = qobject_cast<QHBoxLayout*>(layout()))
            box->insertWidget(1, m_autoCleanButton);
    }

    const QString indicator = chainAutoIndicatorLine(m_governor);
    m_autoCleanButton->setVisible(m_governor.available);
    const QSignalBlocker block(m_autoCleanButton);
    m_autoCleanButton->setChecked(m_governor.available && m_governor.autoOn);
    m_autoCleanButton->setText(indicator.isEmpty() ? tr("AUTO CLEAN") : indicator);
}

} // namespace AetherSDR
