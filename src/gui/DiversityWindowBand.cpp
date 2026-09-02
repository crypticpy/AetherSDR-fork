// The Diversity window's BAND page: everything that is about the SPAN rather
// than about the slice.
//
// The rest of the window (the SLICE page) answers "what is the combiner doing
// with what I am tuned to". It cannot answer the question that comes first --
// "where should I be tuned?" -- because every readout on it is about one
// channel. The gate has the whole 125 kHz of both loops in front of it, so the
// BAND page asks it two questions the slice page cannot:
//
//   * /diversity/spatial -- per-bin phase, coherence and level, painted as a
//     scrolling waterfall where colour is DIRECTION. Two stations from
//     different places are different colours; one local noise source is a
//     single flat colour across everything it touches.
//   * /diversity/finder  -- the last ten minutes of voice-shaped energy on
//     both loops, ranked, with the diversity gain the pair can earn on each.
//
// Both are click-to-tune, which is the only thing that makes them useful: the
// gate has no tune route of its own (docs/DIVERSITY.md, "Limits and known
// gaps"), so the offer has to live here and the tune has to leave through
// AetherSDR's own slice.
//
// Defined in its own file rather than in DiversityWindow.cpp for the reason
// DiversityWindowPanels.cpp and DiversityWindowEvents.cpp exist: those members
// belong to DiversityWindow, and DiversityWindow.cpp is at the file-size
// budget AGENTS.md asks for.

#include "gui/DiversityWindow.h"

#include "core/ThemeManager.h"
#include "gui/DiversityFinderPanel.h"
#include "gui/DiversitySpatialWaterfall.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The page tabs. QToolButton rather than QPushButton so they read as a view
// switch (a place you are) rather than as a command (a thing you do), which is
// the same distinction the chain row's checkable MODE/HEAR buttons draw
// against REALIGN and CAPTURE beside them.
const char* kPageButtonStyle =
    "QToolButton { background: {{color.toggle.background}};"
    " color: {{color.toggle.foreground}};"
    " border: 1px solid {{color.toggle.border}}; border-top-left-radius: 3px;"
    " border-top-right-radius: 3px; border-bottom-left-radius: 0px;"
    " border-bottom-right-radius: 0px; padding: 3px 12px; font-size: 11px;"
    " font-weight: bold; }"
    "QToolButton:hover { background: {{color.background.2}}; }"
    "QToolButton:checked { background: {{color.toggle.accent.background.checked}};"
    " color: {{color.toggle.accent.foreground.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }";

constexpr int kPageButtonHeight = 26;

} // namespace

void DiversityWindow::buildPageSwitch(QWidget* row)
{
    auto* layout = qobject_cast<QHBoxLayout*>(row->layout());
    if (!layout)
        return;

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    const auto makeButton = [&](const QString& text, const QString& objectName,
                                const QString& accessibleName, const QString& tip) {
        auto* button = new QToolButton(row);
        button->setObjectName(objectName);
        button->setText(text);
        button->setAccessibleName(accessibleName);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFixedHeight(kPageButtonHeight);
        button->setFocusPolicy(Qt::StrongFocus);
        ThemeManager::instance().applyStyleSheet(button,
                                                 QString::fromLatin1(kPageButtonStyle));
        group->addButton(button);
        layout->addWidget(button);
        return button;
    };

    m_pageSliceButton = makeButton(
        tr("SLICE"), QStringLiteral("diversityWindowPageSlice"),
        tr("Show the slice page"),
        tr("What the combiner is doing with the frequency you are tuned to: "
           "the weight, the two loops, the remembered talkers and what just "
           "happened."));
    m_pageBandButton = makeButton(
        tr("BAND"), QStringLiteral("diversityWindowPageBand"),
        tr("Show the band page"),
        tr("The gate's whole span: a waterfall coloured by the direction each "
           "signal arrives from, and the conversations the gate has found on "
           "it in the last ten minutes. Click anything on this page to tune "
           "there."));
    m_pageSliceButton->setChecked(true);

    connect(m_pageSliceButton, &QToolButton::clicked, this, [this] { showPage(false); });
    connect(m_pageBandButton, &QToolButton::clicked, this, [this] { showPage(true); });
    layout->addSpacing(10);
}

QWidget* DiversityWindow::buildBandPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("diversityWindowBandPage"));
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    QVBoxLayout* waterfallBody = nullptr;
    QFrame* waterfallFrame = DiversityWidgets::makeGroupBox(
        tr("SPATIAL WATERFALL"), QStringLiteral("diversityWindowSpatialBox"),
        waterfallBody, page);
    waterfallFrame->setToolTip(
        tr("Every bin of the gate's span, one row per poll, coloured by where "
           "the signal came from rather than by how strong it is. Two stations "
           "in different directions cannot share a colour; a local noise "
           "source paints one flat colour across everything it touches; sky "
           "noise has no direction at all and goes grey."));

    m_waterfall = new DiversitySpatialWaterfall(waterfallFrame);
    connect(m_waterfall, &DiversitySpatialWaterfall::tuneRequested, this,
            &DiversityWindow::tuneTo);
    waterfallBody->addWidget(m_waterfall, 1);

    // Plain label, explicit spacing, NO word wrap: a wrapping label is
    // height-for-width, which makes the layout above it height-for-width too
    // and puts a scrollbar on a page that fits.
    QLabel* legend = DiversityWidgets::makeFieldLabel(
        tr("hue: arrival phase · saturation: coherence · brightness: level"),
        waterfallFrame);
    legend->setObjectName(QStringLiteral("diversityWindowSpatialLegend"));
    legend->setAccessibleName(tr("Spatial waterfall legend"));
    waterfallBody->addWidget(legend);
    root->addWidget(waterfallFrame, 1);

    QVBoxLayout* finderBody = nullptr;
    QFrame* finderFrame = DiversityWidgets::makeGroupBox(
        tr("FINDER"), QStringLiteral("diversityWindowFinderBox"), finderBody, page);
    finderFrame->setToolTip(
        tr("Conversations the gate has found on the span in the last ten "
           "minutes, best first. The score combines how voice-shaped the "
           "energy is, how strong it is and how long it has been going; the "
           "gain column is what the second loop can actually buy you there, "
           "which is often nothing and says so."));

    m_finder = new DiversityFinderPanel(finderFrame);
    connect(m_finder, &DiversityFinderPanel::tuneRequested, this,
            &DiversityWindow::tuneTo);
    finderBody->addWidget(m_finder, 1);
    root->addWidget(finderFrame, 1);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("diversityWindowBandScroll"));
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

// The two show/hide overrides live here rather than beside closeEvent() in
// DiversityWindow.cpp because the only thing they do is the BAND page's
// business. Both fire AFTER isVisible() has flipped, which is the state
// AetherGateDiversityPanel::wantsBandPoll() reads.
void DiversityWindow::showEvent(QShowEvent* event)
{
    PersistentDialog::showEvent(event);
    emit bandPageChanged(bandPageVisible());
}

void DiversityWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    emit bandPageChanged(false);
}

bool DiversityWindow::bandPageVisible() const
{
    return m_pages && m_pageBandButton && m_pageBandButton->isChecked();
}

void DiversityWindow::showPage(bool band)
{
    if (!m_pages)
        return;
    {
        const QSignalBlocker blockSlice(m_pageSliceButton);
        const QSignalBlocker blockBand(m_pageBandButton);
        m_pageSliceButton->setChecked(!band);
        m_pageBandButton->setChecked(band);
    }
    m_pages->setCurrentIndex(band ? 1 : 0);
    // The applet polls /diversity/spatial and /diversity/finder only while
    // this page is on screen -- a page nobody is looking at costs no requests.
    emit bandPageChanged(band && isVisible());
}

void DiversityWindow::tuneTo(double hz)
{
    if (hz <= 0.0)
        return;
    emit requestTune(hz);

    // Arriving on a conversation with the combiner parked in off/manual/null
    // would mean the first over is heard on one loop, or through a weight
    // solved for somewhere else entirely. Track is the mode that starts
    // solving for whoever is talking, so ask for it -- but only when it is not
    // already what the gate reported, so a tune does not write a mode on every
    // click.
    const QAbstractButton* checked = m_modeGroup ? m_modeGroup->checkedButton() : nullptr;
    const QString mode = checked ? checked->property("diversityValue").toString() : QString();
    if (mode != QLatin1String("track")) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), QStringLiteral("track"));
        emit requestSet(q);
    }

    addEventLines({tr("tune → %1 kHz").arg(hz / 1e3, 0, 'f', 2)});
}

void DiversityWindow::applySpatial(const QJsonObject& spatial)
{
    if (m_waterfall)
        m_waterfall->setSpatial(spatial);
}

void DiversityWindow::applyFinder(const QJsonObject& finder)
{
    if (m_finder)
        m_finder->applyFinder(finder);
}

void DiversityWindow::clearBandReadouts()
{
    if (m_waterfall)
        m_waterfall->clear();
    if (m_finder)
        m_finder->clear();
}

} // namespace AetherSDR
