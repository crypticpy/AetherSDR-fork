// The START page itself, and the seam that feeds it. See
// DiversitySessionPage.h for the design.
//
// Two things live in this file, for the reason DiversityWindowPanels.cpp
// holds four of DiversityWindow's own builders: the page, and the four
// DiversityWindow members that own the session model. DiversityWindow.cpp is
// at AGENTS.md's file-size budget and this is the seam's natural home --
// every one of those four members is about the model this page draws.

#include "gui/DiversitySessionPage.h"

#include "core/ThemeManager.h"
#include "gui/DiversityNextStrip.h"
#include "gui/DiversityWindow.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

const char* kHeaderStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }";

const char* kOffersStyle =
    "QWidget#diversityWindowSessionOffers { background: {{color.background.1}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px; }";

const char* kDigLineStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }";

constexpr int kOffersHeight = 76;
constexpr int kOfferButtonHeight = 20;

} // namespace

// --------------------------------------------------------------------------
// The page
// --------------------------------------------------------------------------

DiversitySessionPage::DiversitySessionPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowSessionPage"));
    setAccessibleName(tr("Session"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* header = new QLabel(
        tr("The order, and why. A step lower down never undoes one above it."), this);
    header->setObjectName(QStringLiteral("diversityWindowSessionHeader"));
    header->setAccessibleName(tr("Session order"));
    header->setToolTip(
        tr("RECEIVER and SITE NOISE last the whole session; BAND resets when "
           "you change band; STATION resets when the talker does. LISTEN is "
           "where the four of them were leading."));
    header->setAccessibleDescription(header->toolTip());
    header->setWordWrap(false);
    header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    header->setMinimumWidth(0);
    header->setFixedHeight(18);
    ThemeManager::instance().applyStyleSheet(header, QString::fromLatin1(kHeaderStyle));
    root->addWidget(header);

    for (int i = 0; i < DiversitySessionModel::StepCount; ++i) {
        m_cards[i] = new DiversitySessionCard(i + 1, this);
        connect(m_cards[i], &DiversitySessionCard::cureActivated, this,
                &DiversitySessionPage::cureActivated);
        root->addWidget(m_cards[i]);
    }

    // The two things that are NOT steps: the run you can spend a minute on,
    // and the one button that puts the receiver in the state the first card
    // is asking for. Offers, not chores -- so they are below the checklist
    // rather than in it (R2.1: "Not steps: the two offers").
    m_offers = new QWidget(this);
    m_offers->setObjectName(QStringLiteral("diversityWindowSessionOffers"));
    m_offers->setAttribute(Qt::WA_StyledBackground, true);
    m_offers->setFixedHeight(kOffersHeight);
    ThemeManager::instance().applyStyleSheet(m_offers, QString::fromLatin1(kOffersStyle));

    auto* offersRoot = new QVBoxLayout(m_offers);
    offersRoot->setContentsMargins(10, 6, 10, 6);
    offersRoot->setSpacing(4);

    auto* offersRow = new QHBoxLayout;
    offersRow->setContentsMargins(0, 0, 0, 0);
    offersRow->setSpacing(6);

    QLabel* caption = DiversityWidgets::makeCaption(tr("OFFERS"), m_offers);
    caption->setObjectName(QStringLiteral("diversityWindowSessionOffersCaption"));
    caption->setAccessibleName(tr("Offers"));
    offersRow->addWidget(caption);

    m_quickStart = new QPushButton(tr("QUICK START"), m_offers);
    m_quickStart->setObjectName(QStringLiteral("diversityWindowQuickStartButton"));
    m_quickStart->setAccessibleName(tr("Quick start"));
    m_quickStart->setToolTip(
        tr("Puts the pair where the first card is asking for it: TRACK, the "
           "combined output in your ears, and AUTO CLEAN on. Three writes, "
           "in that order. It changes nothing else."));
    m_quickStart->setAccessibleDescription(m_quickStart->toolTip());
    m_quickStart->setCursor(Qt::PointingHandCursor);
    m_quickStart->setFixedHeight(kOfferButtonHeight);
    applyToggleButtonStyle(m_quickStart);
    connect(m_quickStart, &QPushButton::clicked, this,
            &DiversitySessionPage::quickStartRequested);
    offersRow->addWidget(m_quickStart);

    offersRow->addStretch(1);

    QLabel* digCaption = DiversityWidgets::makeCaption(tr("DIG OUT"), m_offers);
    digCaption->setObjectName(QStringLiteral("diversityWindowSessionDigCaption"));
    digCaption->setAccessibleName(tr("Dig out"));
    digCaption->setToolTip(
        tr("Let the gate spend a minute, three or five trying one knob of the "
           "chain at a time on whoever is talking, keeping only what "
           "measurably helped. Never a step -- it is an offer, and STOP puts "
           "everything back."));
    digCaption->setAccessibleDescription(digCaption->toolTip());
    offersRow->addWidget(digCaption);
    offersRoot->addLayout(offersRow);

    m_digLine = new QLabel(m_offers);
    m_digLine->setObjectName(QStringLiteral("diversityWindowSessionDigLine"));
    m_digLine->setAccessibleName(tr("Dig state"));
    m_digLine->setWordWrap(false);
    m_digLine->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_digLine->setMinimumWidth(0);
    m_digLine->setFixedHeight(14);
    ThemeManager::instance().applyStyleSheet(m_digLine,
                                             QString::fromLatin1(kDigLineStyle));
    offersRoot->addWidget(m_digLine);

    root->addWidget(m_offers);
    // Everything above is fixed height, so the surplus goes here rather than
    // stretching a card out of its 86 px.
    root->addStretch(1);

    setDigLine(QString());
}

void DiversitySessionPage::setSteps(const QVector<DiversitySessionModel::Step>& steps,
                                    int next)
{
    Q_UNUSED(next); // the tone the model already put on each step says it
    for (int i = 0; i < DiversitySessionModel::StepCount && i < steps.size(); ++i) {
        if (m_cards[i])
            m_cards[i]->setStep(steps.at(i));
    }
}

void DiversitySessionPage::setDigLine(const QString& text)
{
    if (!m_digLine)
        return;
    // The gate has nothing to say about a dig -- no run, or a gate too old to
    // offer one. The offer itself is still on the row above; this line is
    // only ever about a run that happened.
    const QString shown = text.isEmpty() ? tr("no run yet") : text;
    m_digLine->setText(shown);
    m_digLine->setToolTip(shown);
    m_digLine->setAccessibleDescription(shown);
}

void DiversitySessionPage::setDigDurations(QWidget* durations)
{
    if (!durations || !m_offers)
        return;
    auto* offersRoot = qobject_cast<QVBoxLayout*>(m_offers->layout());
    if (!offersRoot)
        return;
    auto* offersRow = qobject_cast<QHBoxLayout*>(offersRoot->itemAt(0)->layout());
    if (!offersRow)
        return;
    durations->setParent(m_offers);
    offersRow->addWidget(durations);
}

// --------------------------------------------------------------------------
// The window's half of the seam
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildStartPage()
{
    m_startPage = new DiversitySessionPage;
    connect(m_startPage, &DiversitySessionPage::cureActivated, this,
            &DiversityWindow::onSessionCure);
    connect(m_startPage, &DiversitySessionPage::quickStartRequested, this, [this] {
        // Three writes in the model's own order. Nothing is composed here:
        // the queries are the gate's own, and QUICK START's whole contract is
        // that it sends those three and nothing else.
        for (const QString& query : m_session.quickStartQueries())
            emit requestSet(QUrlQuery(query));
    });

    // The three DIG durations. Built by the window because they write, given
    // a home here because they are an offer -- see DiversityWindowFilter.cpp.
    m_startPage->setDigDurations(buildDigDurations());

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("diversityWindowStartScroll"));
    scroll->setWidget(m_startPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

// Re-derives every card and the footer from the payloads this window has
// already polled. Called from every apply*() that touches one of them --
// there is no cache inside the model, so the cards and the sentence beside
// them can never disagree about what the gate last said.
void DiversityWindow::refreshSession()
{
    if (!m_startPage && !m_nextStrip)
        return;
    m_session.setNowSecs(QDateTime::currentSecsSinceEpoch());
    m_session.apply(m_lastDiversity, m_lastFilter, m_lastDig, m_lastBeacons,
                    QJsonObject(), m_tunedHz);

    const QVector<DiversitySessionModel::Step> steps = m_session.steps();
    const int next = m_session.nextStep();
    if (m_startPage) {
        m_startPage->setSteps(steps, next);
        m_startPage->setDigLine(m_session.digSummary());
    }
    if (m_nextStrip) {
        const bool haveNext = next >= 0 && next < steps.size();
        const DiversitySessionModel::Step shown =
            haveNext ? steps.at(next) : DiversitySessionModel::Step{};
        const QString listen =
            steps.size() > DiversitySessionModel::StepListen
                ? steps.at(DiversitySessionModel::StepListen).state
                : QString();
        m_nextStrip->setNext(shown, haveNext, listen, m_session.allChoresDone());
    }
}

// A card's cure, or the footer's one button. Both carry a StepId rather than
// a Cure, so the query that goes on the wire is re-read from the model here
// rather than taken from a label that may be a poll out of date.
void DiversityWindow::onSessionCure(int stepId)
{
    const QVector<DiversitySessionModel::Step> steps = m_session.steps();
    if (stepId < 0 || stepId >= steps.size())
        return;
    const DiversitySessionModel::Step& step = steps.at(stepId);
    const DiversitySessionModel::Cure& cure = step.cure;
    if (cure.kind.isEmpty())
        return;

    if (cure.kind == QLatin1String("align")) {
        showPage(DiversitySessionModel::PageSlice);
        startRealign();
        return;
    }
    if (cure.kind == QLatin1String("set")) {
        emit requestSet(QUrlQuery(cure.query));
        return;
    }
    if (cure.kind == QLatin1String("dig")) {
        emit requestDig(QUrlQuery(cure.query));
        return;
    }
    // "page" -- and anything a newer model grows that this build does not
    // know. Going to the page the step is about is the answer that is never
    // wrong; starting something we did not recognise would be.
    showPage(step.page);
}

// Which noise-finding kinds the operator has dismissed on the SITE page.
// Nothing in this package connects to it: the DISMISS control is another
// package's, and the two are wired together outside both.
void DiversityWindow::setDismissedNoiseKinds(const QSet<QString>& kinds)
{
    m_session.setDismissedKinds(kinds);
    refreshSession();
}

} // namespace AetherSDR
