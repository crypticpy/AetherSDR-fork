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

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/DiversityFinderPanel.h"
#include "gui/DiversitySpatialLegend.h"
#include "gui/DiversitySpatialWaterfall.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonObject>
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
    " border-bottom-right-radius: 0px; padding: 3px 6px; font-size: 11px;"
    " font-weight: bold; }"
    "QToolButton:hover { background: {{color.background.2}}; }"
    "QToolButton:checked { background: {{color.toggle.accent.background.checked}};"
    " color: {{color.toggle.accent.foreground.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }";

constexpr int kPageButtonHeight = 26;

// A floor, not the label's own width. Five tabs at their natural width would
// set the window's minimum width on their own, and the window has to stay
// narrow enough to open at 1120 with nothing on any page behind a scrollbar --
// which is what tests/diversity_site_test.cpp and tests/diversity_filter_test
// .cpp both assert. Below this the labels elide; above it, which is every size
// the window is ever actually at, they are their full selves.
constexpr int kPageButtonMinWidth = 48;

// The AppSettings key the page you were last on lives under. A number, and
// out-of-range means START -- a stored value from a build with a different
// page order must not be able to open a page that is not there.
const char* kPageKey = "DiversityWindowPage";

} // namespace

void DiversityWindow::buildPageSwitch(QWidget* row)
{
    auto* layout = qobject_cast<QHBoxLayout*>(row->layout());
    if (!layout)
        return;

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    // The five tabs sit against each other rather than at the row's 6 px
    // spacing: they are one control -- a place you are -- and a gap between
    // them would read as five separate commands.
    auto* tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 0, 0, 0);
    tabs->setSpacing(0);
    layout->addLayout(tabs);

    // Two strings per tab, not one. The tooltip is one line, because a
    // tooltip that is a paragraph is a tooltip nobody finishes reading and
    // this row has five of them; the paragraph itself is still there for a
    // screen reader, and for the HELP button beside them to have been worth
    // building.
    const auto makeButton = [&](const QString& text, const QString& objectName,
                                const QString& accessibleName, const QString& tip,
                                const QString& description) {
        auto* button = new QToolButton(row);
        button->setObjectName(objectName);
        button->setText(text);
        button->setAccessibleName(accessibleName);
        button->setToolTip(tip);
        button->setAccessibleDescription(description);
        button->setCheckable(true);
        button->setFixedHeight(kPageButtonHeight);
        button->setMinimumWidth(kPageButtonMinWidth);
        button->setFocusPolicy(Qt::StrongFocus);
        ThemeManager::instance().applyStyleSheet(button,
                                                 QString::fromLatin1(kPageButtonStyle));
        group->addButton(button);
        tabs->addWidget(button);
        return button;
    };

    m_pageStartButton = makeButton(
        tr("START"), QStringLiteral("diversityWindowPageStart"),
        tr("Show the start page"),
        tr("The four things to get right, in order, and why each one matters."),
        tr("The session itself: align the pair and hear its output, read what "
           "noise this address makes, measure the band on beacons, then let "
           "the gate learn the station you are listening to. Each step says "
           "what it buys you and when it has to be done again."));
    m_pageSliceButton = makeButton(
        tr("SLICE"), QStringLiteral("diversityWindowPageSlice"),
        tr("Show the slice page"),
        tr("What the combiner is doing with the frequency you are tuned to."),
        tr("What the combiner is doing with the frequency you are tuned to: "
           "the weight, the two loops, the remembered talkers and what just "
           "happened."));
    m_pageBandButton = makeButton(
        tr("BAND"), QStringLiteral("diversityWindowPageBand"),
        tr("Show the band page"),
        tr("The whole span: direction-coloured waterfall, and what is on it."),
        tr("The gate's whole span: a waterfall coloured by the direction each "
           "signal arrives from, and the conversations the gate has found on "
           "it in the last ten minutes. Click anything on this page to tune "
           "there."));
    m_pageSiteButton = makeButton(
        tr("SITE"), QStringLiteral("diversityWindowPageSite"),
        tr("Show the site page"),
        tr("Your station: what noise this address makes, and what beacons say."),
        tr("Your station rather than the band: what kind of noise this address "
           "makes -- mains hum, impulses, single lines -- and what the world's "
           "beacon network measures your antennas to be worth."));
    m_pageFilterButton = makeButton(
        tr("FILTER"), QStringLiteral("diversityWindowPageFilter"),
        tr("Show the filter page"),
        tr("The slice filter drawn, with every notch, contour and AGC setting."),
        tr("The slice filter itself, drawn: the response curve with your "
           "passband over it and the edges draggable on it, plus every notch, "
           "contour, AGC and blanker setting the gate has. This is the page "
           "for \"why does this sound like that\", which is almost never the "
           "combiner."));
    m_pageStartButton->setChecked(true);

    connect(m_pageStartButton, &QToolButton::clicked, this, [this] { showPage(0); });
    connect(m_pageSliceButton, &QToolButton::clicked, this, [this] { showPage(1); });
    connect(m_pageBandButton, &QToolButton::clicked, this, [this] { showPage(2); });
    connect(m_pageSiteButton, &QToolButton::clicked, this, [this] { showPage(3); });
    connect(m_pageFilterButton, &QToolButton::clicked, this, [this] { showPage(4); });
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

    // The key, drawn rather than described. The sentence that used to sit here
    // -- "hue: arrival phase · saturation: coherence · brightness: level" --
    // named the mapping without giving a single colour a number, so a streak
    // on the picture still could not be read off it. Fixed height and no word
    // wrap, for the same reason the label had them: a height-for-width widget
    // here makes the whole page height-for-width and puts a scrollbar on
    // something that fits.
    waterfallBody->addWidget(new DiversitySpatialLegend(waterfallFrame));
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
    // The page you were last on, once per window. Restored here rather than
    // in the constructor because the window is built long before it is shown
    // and a page switch is what tells the applet which routes to poll -- the
    // signal below has to be the one that carries the restored page.
    if (!m_pageRestored) {
        m_pageRestored = true;
        bool ok = false;
        const int page = AppSettings::instance()
                             .value(QLatin1String(kPageKey), QString())
                             .toString()
                             .toInt(&ok);
        // No stored value, or one this build has no page for: START. It is
        // where a session starts and it is never wrong to be sent there.
        showPage(ok ? page : 0);
    }
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

void DiversityWindow::showPage(int page)
{
    if (!m_pages)
        return;
    if (page < 0 || page >= m_pages->count())
        page = 0;
    {
        const QSignalBlocker blockStart(m_pageStartButton);
        const QSignalBlocker blockSlice(m_pageSliceButton);
        const QSignalBlocker blockBand(m_pageBandButton);
        const QSignalBlocker blockSite(m_pageSiteButton);
        const QSignalBlocker blockFilter(m_pageFilterButton);
        m_pageStartButton->setChecked(page == 0);
        m_pageSliceButton->setChecked(page == 1);
        m_pageBandButton->setChecked(page == 2);
        m_pageSiteButton->setChecked(page == 3);
        m_pageFilterButton->setChecked(page == 4);
    }
    m_pages->setCurrentIndex(page);
    retargetPageHelp(page);
    // Where you were is where you come back to. Written on every switch
    // rather than on close, so a session that ends in a crash still leaves
    // the right page behind.
    AppSettings::instance().setValue(QLatin1String(kPageKey), QString::number(page));
    // The applet polls /diversity/spatial and /diversity/finder only while
    // BAND is on screen, /diversity/beacons only while SITE is, and /filter
    // only while FILTER is -- a page nobody is looking at costs no requests.
    // One signal for every page switch: the handler reads bandPageVisible(),
    // sitePageVisible() and filterPageVisible() back off the window.
    emit bandPageChanged(bandPageVisible() && isVisible());
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
