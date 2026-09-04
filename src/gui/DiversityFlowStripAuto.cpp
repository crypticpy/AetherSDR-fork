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
// on, and the face reads "AUTO CLEAN ON" the moment it is; the state and why
// ride in the tooltip and accessible description instead (the operator's own
// follow-up: no long status message on the face, no paragraph in the
// tooltip). Off, it collapses to the bare label per the header's own
// contract on chainAutoIndicatorLine().
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
        m_autoCleanButton->setCheckable(true);
        m_autoCleanButton->setCursor(Qt::PointingHandCursor);
        // The same emphasised ON tone the sidebar's switch wears (U1: not
        // the warning gold, which read as an alarm).
        applyToggleButtonStyle(m_autoCleanButton);
        // Unlike m_line, this widget sits in the same QHBoxLayout WITHOUT a
        // stretch factor, next to m_line's stretch of 1 -- a bare
        // setMinimumWidth(0) here made it a zero-width, invisible button
        // whenever that sibling was present, because a stretch-0 Ignored
        // item's whole contribution collapses to its minimum once any
        // sibling declares stretch. Floor it at the face text's own width
        // instead, so it always renders as a real button -- the face is a
        // fixed short string now ("AUTO CLEAN" or "AUTO CLEAN ON", see
        // chainAutoSetButtonIndicator()), not a state word or sentence, so
        // the floor only has to fit that.
        m_autoCleanButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_autoCleanButton->setMinimumWidth(
            m_autoCleanButton->fontMetrics().horizontalAdvance(
                QStringLiteral("AUTO CLEAN ON")) + 40);
        connect(m_autoCleanButton, &QPushButton::clicked, this, [this](bool checked) {
            emit requestAutoCleanToggle(checked);
        });
        // Right after the FLOW caption, ahead of the checklist itself -- the
        // "really visible" the operator asked for, on the one line every
        // page of this window keeps on screen.
        if (auto* box = qobject_cast<QHBoxLayout*>(layout()))
            box->insertWidget(1, m_autoCleanButton);
    }

    m_autoCleanButton->setVisible(m_governor.available);
    const QSignalBlocker block(m_autoCleanButton);
    m_autoCleanButton->setChecked(m_governor.available && m_governor.autoOn);
    chainAutoSetButtonIndicator(m_autoCleanButton, chainAutoIndicatorLine(m_governor),
                                chainAutoStateWord(m_governor));
}

} // namespace AetherSDR
