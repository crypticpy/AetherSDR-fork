#include "gui/AetherGateChainNow.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

// See AetherGateChainNow.h for the ladder's own contract. This file is
// exactly two things: the eight-case ladder itself (refresh()) and the
// small local formatters it needs that nothing else in this window's
// family already exports.

namespace AetherSDR {

namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("AetherGateChainNow", text);
}

// "+1.8", "-0.9" -- one decimal, always signed. AetherGateChainAuto.cpp
// keeps its own copy of this same three-line formatter rather than
// exporting it; this is a second private copy for the same reason.
QString signedDb(double db)
{
    return (db >= 0.0 ? QStringLiteral("+") : QString()) + QString::number(db, 'f', 1);
}

// "just now" / "N min" / "N h" / "N d" -- design §3.4's four age bands, in
// miniature. A shared DiversityAge.h unit is being written in parallel to
// replace every private copy of this table (including this one); this file
// must not create or include it, so this stays a short local duplicate,
// the same way AetherGateChainAuto.cpp's signedDb() and AetherGateChainTile
// .cpp's suffixFor() are each kept local to one file rather than shared for
// three lines of code.
QString ageFromSecs(qint64 secs)
{
    if (secs < 0)
        secs = 0;
    if (secs < 60)
        return tr_("just now");
    if (secs < 3600)
        return tr_("%1 min").arg(secs / 60);
    if (secs < 86400)
        return tr_("%1 h").arg(secs / 3600);
    return tr_("%1 d").arg(secs / 86400);
}

// The chain row's own NAME for a governor tool, upper-cased the way every
// card on this diagram already reads. "dig" has no row (chainAutoRowIdForTool
// answers "" for it) but the ladder still names it in a sentence, so it gets
// a word here even though there is nothing to light.
QString toolRowName(const QString& tool)
{
    if (tool == QLatin1String("guard"))
        return QStringLiteral("GUARD");
    if (tool == QLatin1String("nb"))
        return QStringLiteral("NB");
    if (tool == QLatin1String("mode"))
        return QStringLiteral("COMBINER");
    if (tool == QLatin1String("squeeze"))
        return QStringLiteral("SQUEEZE");
    if (tool == QLatin1String("dig"))
        return QStringLiteral("DIG");
    return tool.toUpper();
}

// The tool behind the newest events[] entry that ended in "error", or the
// pending tool if nothing has -- governor.error is one string with no tool
// field of its own, so this is the app's own reading of which row it is
// about.
QString erroringRowId(const ChainAutoGovernor& gov)
{
    for (int i = gov.events.size() - 1; i >= 0; --i) {
        if (gov.events.at(i).result == QLatin1String("error"))
            return chainAutoRowIdForTool(gov.events.at(i).tool);
    }
    if (gov.hasPending)
        return chainAutoRowIdForTool(gov.pending.tool);
    return QString();
}

// The strip line and the two buttons. Tokens only -- color.accent.bright and
// color.accent are the same pair AetherGateChainWindowTabs.cpp's own
// kSetButtonStyle already wears for a button of this kind, and
// color.text.primary/secondary are the same pair every other readout in
// this window already uses.
const char* const kLineStyle =
    "QLabel { color: {{color.text.primary}}; font-size: 11px; font-weight: bold;"
    " background: transparent; }";

const char* const kButtonStyle =
    "QPushButton { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold;"
    " padding: 3px 10px; border: 1px solid {{color.accent}}; border-radius: 4px;"
    " background: transparent; }"
    "QPushButton:hover { background: {{color.background.1}}; }"
    "QPushButton:pressed { background: {{color.background.3}}; }";

const char* const kHistoryStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; background: transparent; }";

} // namespace

AetherGateChainNow::AetherGateChainNow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gateChainNowStrip"));
    setAccessibleName(tr("What is worth changing right now"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);

    auto* lineRow = new QWidget(this);
    lineRow->setObjectName(QStringLiteral("gateChainNowLineRow"));
    auto* lineBox = new QHBoxLayout(lineRow);
    lineBox->setContentsMargins(0, 0, 0, 0);
    lineBox->setSpacing(6);

    auto* caption = DiversityWidgets::makeCaption(tr("NOW"), lineRow);
    caption->setObjectName(QStringLiteral("gateChainNowCaption"));
    lineBox->addWidget(caption);

    m_line = new QLabel(lineRow);
    m_line->setObjectName(QStringLiteral("gateChainNowLine"));
    m_line->setAccessibleName(tr("What NOW recommends"));
    m_line->setWordWrap(false);
    // Ignored/minimumWidth(0), the same treatment the banner this replaces
    // used: the governor's own `why` has no true worst case, so a long one
    // clips instead of pushing this window's minimum width past the 1120 it
    // opens at.
    m_line->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_line->setMinimumWidth(0);
    m_line->setToolTip(
        tr("The one thing worth changing right now, and why the receiver thinks so."));
    // A fixed sentence, set once -- the same convention DiversityWindowPanels
    // .cpp's makeReadoutLine() already uses for every other live readout in
    // this app: the accessible description explains what the line IS, not
    // what it currently says (the live text reaches a screen reader through
    // the label's own text() already).
    m_line->setAccessibleDescription(
        tr("The chain's own governor, read as one recommendation: what it is doing or would do "
           "next, which stage it is about, and what it measured. The stage it names is lit on "
           "the diagram below."));
    ThemeManager::instance().applyStyleSheet(m_line, QString::fromLatin1(kLineStyle));
    lineBox->addWidget(m_line, 1);
    root->addWidget(lineRow);

    auto* buttonRow = new QWidget(this);
    buttonRow->setObjectName(QStringLiteral("gateChainNowButtonRow"));
    auto* buttonBox = new QHBoxLayout(buttonRow);
    buttonBox->setContentsMargins(0, 0, 0, 0);
    buttonBox->setSpacing(6);

    m_action = new QPushButton(buttonRow);
    m_action->setObjectName(QStringLiteral("gateChainNowAction"));
    m_action->setCursor(Qt::PointingHandCursor);
    m_action->setFixedHeight(22);
    ThemeManager::instance().applyStyleSheet(m_action, QString::fromLatin1(kButtonStyle));
    connect(m_action, &QPushButton::clicked, this, &AetherGateChainNow::onActionClicked);
    buttonBox->addWidget(m_action);

    m_history = new QPushButton(QStringLiteral("HISTORY ▾"), buttonRow);
    m_history->setObjectName(QStringLiteral("gateChainNowHistory"));
    m_history->setAccessibleName(tr("AUTO CLEAN's history"));
    m_history->setCursor(Qt::PointingHandCursor);
    m_history->setFixedHeight(22);
    m_history->setToolTip(
        tr("Shows the last eight moves AUTO CLEAN made, and what each one was worth."));
    m_history->setAccessibleDescription(
        tr("One line per governor event, newest first, with the time of day it happened and "
           "the decibels it gained or lost, plus any tool currently backing off and until "
           "when."));
    ThemeManager::instance().applyStyleSheet(m_history, QString::fromLatin1(kButtonStyle));
    connect(m_history, &QPushButton::clicked, this, &AetherGateChainNow::onHistoryClicked);
    buttonBox->addWidget(m_history);
    buttonBox->addStretch(1);
    root->addWidget(buttonRow);

    m_historyPanel = new QLabel(this);
    m_historyPanel->setObjectName(QStringLiteral("gateChainNowHistoryPanel"));
    m_historyPanel->setAccessibleName(tr("AUTO CLEAN's recent moves"));
    m_historyPanel->setWordWrap(false);
    m_historyPanel->setVisible(false);
    ThemeManager::instance().applyStyleSheet(m_historyPanel, QString::fromLatin1(kHistoryStyle));
    root->addWidget(m_historyPanel);

    setVisible(false);
}

// The eight-case ladder, design §2.3's own table, in the order it is
// written there: FRONT END clipping outranks everything (an operator who
// cannot hear the audio does not care what AUTO CLEAN is holding), then a
// stopped governor, then what it is actively doing (pending, holding), then
// whether it is even on, then whether it is idle with something to say
// about why, then plain listening, then -- last -- no governor at all.
void AetherGateChainNow::refresh(const ChainStage& autoCleanRow, const ChainAutoGovernor& gov,
                                 const ChainFrontendStatus& fe)
{
    // Accepted per this widget's own contract (design §2.3's three inputs)
    // even though no case below reads it: a gate old enough to send
    // chain[0]==auto_clean but not the governor block that rides beside it
    // is not a shape any real gate sends (AetherGateChainAuto.h's own header
    // comment -- the same block rides on /diversity and on /filter under the
    // same key), so !governor.available already covers "nothing to show"
    // whether or not the row itself is present.
    Q_UNUSED(autoCleanRow)
    m_governor = gov;

    QString line;
    QString actionText;
    QString actionTip;
    QString actionAd;
    QString litId;
    bool showAction = false;
    bool visible = true;

    const bool clipping =
        fe.available && (fe.clips1s > 0 || (fe.hasHeadroom && fe.headroomDb < 3.0));

    if (clipping) {
        // Case 1 -- FRONT END is clipping.
        line = tr_("FRONT END is clipping — the audio will break up");
        actionText = tr_("TURN GUARD ON");
        actionTip = tr_("Switches the front-end guard on so the ADC stops clipping the audio.");
        actionAd = tr_("GUARD steps the LNA state down whenever a sample sits within 3 dB of "
                       "full scale, and back up 30 seconds after the danger clears, never "
                       "below the floor set beside it on the FRONT END card.");
        m_actionRoute = QStringLiteral("/frontend/set");
        m_actionQuery = QStringLiteral("guard=on");
        litId = QStringLiteral("frontend_guard");
        showAction = true;
    } else if (!gov.available) {
        // Case 8 -- no governor at all.
        visible = false;
    } else if (!gov.error.isEmpty()) {
        // Case 2 -- the governor stopped on a refusal.
        line = tr_("AUTO CLEAN stopped: %1").arg(gov.error);
        actionText = tr_("TRY AGAIN");
        actionTip = tr_("Restarts AUTO CLEAN after the receiver refused its last move.");
        actionAd = tr_("Sends auto=on again. The refusal it hit is printed on the stage that "
                       "asked for it.");
        m_actionRoute = QStringLiteral("/diversity/set");
        m_actionQuery = QStringLiteral("auto=on");
        litId = erroringRowId(gov);
        showAction = true;
    } else if (gov.hasPending) {
        // Case 3 -- trying something.
        line = tr_("AUTO CLEAN is trying %1 on %2")
                   .arg(gov.pending.kind, toolRowName(gov.pending.tool));
        actionText = tr_("HAND IT BACK");
        actionTip = tr_("Stops AUTO CLEAN and leaves the chain exactly where it is now.");
        actionAd = tr_("Turns AUTO CLEAN off without reverting anything it is holding, so "
                       "whatever it found stays in circuit and the chain is yours again.");
        m_actionRoute = QStringLiteral("/diversity/set");
        m_actionQuery = QStringLiteral("auto=off");
        litId = chainAutoRowIdForTool(gov.pending.tool);
        showAction = true;
    } else if (!gov.holding.isEmpty()) {
        // Case 4 -- holding something.
        const ChainAutoHeld& h = gov.holding.first();
        QString text =
            tr_("AUTO CLEAN is holding %1 on %2").arg(h.kind, toolRowName(h.tool));
        if (h.hasDelta)
            text += tr_(", %1 dB").arg(signedDb(h.deltaDb));
        // The age reads from since_wall only: `since` is the governor's
        // uptime, and read as epoch it drew a held null as "20671 d" old on
        // the live window. A gate too old to send since_wall gets no age
        // clause rather than a wrong one.
        if (h.hasSinceWall) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            text += tr_(", %1").arg(ageFromSecs(now - qint64(h.sinceWall)));
        }
        line = text;
        actionText = tr_("HAND IT BACK");
        actionTip = tr_("Stops AUTO CLEAN and leaves the chain exactly where it is now.");
        actionAd = tr_("Turns AUTO CLEAN off without reverting anything it is holding, so "
                       "whatever it found stays in circuit and the chain is yours again.");
        m_actionRoute = QStringLiteral("/diversity/set");
        m_actionQuery = QStringLiteral("auto=off");
        litId = chainAutoRowIdForTool(h.tool);
        showAction = true;
    } else if (!gov.autoOn) {
        // Case 5 -- off.
        line = tr_("AUTO CLEAN is off — it will try tools for you and keep only what "
                   "helps");
        actionText = tr_("AUTO CLEAN ON");
        actionTip = tr_("Lets the receiver try a tool on the noise and keep it only if it "
                        "measures better.");
        actionAd = tr_("AUTO CLEAN measures the combined signal-to-noise, tries one tool at a "
                       "time — a null, the blanker, a notch, a dig — and undoes "
                       "anything that did not improve the number. It never moves a control "
                       "you set by hand without saying so here.");
        m_actionRoute = QStringLiteral("/diversity/set");
        m_actionQuery = QStringLiteral("auto=on");
        showAction = true;
    } else if (!gov.ruledOut.isEmpty()) {
        // Case 6 -- idle, and it says why not.
        QStringList reasons;
        for (int i = 0; i < gov.ruledOut.size() && i < 2; ++i)
            reasons << gov.ruledOut.at(i).why;
        line = tr_("Nothing to change: %1").arg(reasons.join(QStringLiteral(" · ")));
    } else {
        // Case 7 -- idle, listening.
        line = tr_("AUTO CLEAN ON · listening");
    }

    setVisible(visible);
    if (!visible) {
        m_historyOpen = false;
        m_historyPanel->setVisible(false);
        emit stageLit(QString());
        return;
    }

    m_line->setText(line);

    m_action->setVisible(showAction);
    if (showAction) {
        m_action->setText(actionText);
        m_action->setAccessibleName(actionText);
        m_action->setToolTip(actionTip);
        m_action->setAccessibleDescription(actionAd);
    }

    updateHistoryPanel();
    emit stageLit(litId);
}

void AetherGateChainNow::onActionClicked()
{
    if (m_actionRoute.isEmpty())
        return;
    emit requestWrite(m_actionRoute, QUrlQuery(m_actionQuery));
}

void AetherGateChainNow::onHistoryClicked()
{
    m_historyOpen = !m_historyOpen;
    updateHistoryPanel();
}

void AetherGateChainNow::updateHistoryPanel()
{
    const QStringList lines = chainAutoEventLines(m_governor, 8);
    m_historyPanel->setText(lines.join(QLatin1Char('\n')));
    m_historyPanel->setVisible(m_historyOpen && !lines.isEmpty());
}

} // namespace AetherSDR
