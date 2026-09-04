#include "gui/AetherGateChainWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/DiversityWindowPanels.h"

#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

// WHAT THIS DOES -- the pane along the bottom of the CHAIN tab. Split from
// AetherGateChainWindow.cpp (792 lines, over AGENTS.md's 800-line budget
// once W4's own additions landed) the same way AetherGateChainWindowTabs.cpp
// already splits buildTabs() out of the window: one class, a second
// translation unit, the boundary drawn at "everything that only touches the
// selected-stage pane" rather than at any particular member.
//
// buildInspector() builds it once; showStage() is the only thing that ever
// changes what it shows, driven by AetherGateChainStrip's own selection
// signal (wired in AetherGateChainWindowTabs.cpp) and by every place in
// AetherGateChainWindow.cpp that rebuilds the diagram out from under the
// current selection.
//
// Five fixed lines and one control, in the order an operator asks the
// questions (design §2.4): what is this, what does it do to the sound, what
// is it doing now, can I change it, what would I hear without it -- plus the
// levels the gate measured, shown only when it measured either leg. Nothing
// here repeats a card verbatim: the card has the short form, this has the
// whole of it. AUTO CLEAN's own state and event history left this pane for
// the NOW strip's HISTORY disclosure (AetherGateChainNow.cpp) -- there is no
// card on the diagram to select that would show them here any more.

namespace AetherSDR {

namespace {

// Mirrors AetherGateChainWindow.cpp's own kTipWidth/kDetailTextWidth: both
// files size the same fixed-width pane fields, and a magic 1020 duplicated
// once is not worth a shared header symbol -- the same trade
// AetherGateChainWindowTabs.cpp already makes for kModes/kModeCount/kTipWidth.
constexpr int kTipWidth = 1020;
constexpr int kDetailTextWidth = 1020;

// The one line that says what you would hear WITHOUT the selected stage. Dim,
// because it describes something that is not happening.
const char* kOffStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px;"
    " background: transparent; }";

// The receiver's own words when it refuses. The only warning-coloured thing in
// the pane, and hidden entirely when there is nothing to refuse.
const char* kNoteStyle =
    "QLabel { color: {{color.accent.warning}}; font-size: 11px;"
    " background: transparent; }";

QString emDash()
{
    return QStringLiteral("—");
}

void setElided(QLabel* label, const QString& text, int width)
{
    const QFontMetrics fm(label->font());
    label->setText(fm.elidedText(text, Qt::ElideRight, width));
    // H1's 90-char tooltip rule; the full sentence still reaches a screen
    // reader below.
    label->setToolTip(text.length() > 90 ? text.left(87) + QStringLiteral("…") : text);
    label->setAccessibleDescription(text);
}

} // namespace

// The bottom pane. It answers four questions in the order an operator asks
// them: what is this, what is it doing, what would I hear without it, and
// can I change it. It never repeats the card's one line verbatim -- the card has
// the short form, this has the whole of it.
void AetherGateChainWindow::buildInspector(QVBoxLayout* hostBox, QWidget* host)
{
    QVBoxLayout* detailBody = nullptr;
    // The box's own caption carries the name (design §2.4 item 1): it is
    // already styled in {{color.accent.bright}} by makeGroupBox(), the same
    // token the selected tile's own frame edge carries, so the pane and the
    // card read as the same stage without a second, redundant title line
    // inside the body. "WHAT THIS DOES" is the default with nothing picked;
    // showStage() below rewrites it to "<NAME> -- what it does" per selection.
    QFrame* detailFrame = DiversityWidgets::makeGroupBox(
        tr("WHAT THIS DOES"), QStringLiteral("gateChainDetail"), detailBody, host);
    m_detailCaption = detailFrame->findChild<QLabel*>(QStringLiteral("gateChainDetailCaption"));
    auto* pane = new QWidget(detailFrame);
    pane->setObjectName(QStringLiteral("gateChainDetailPane"));
    pane->setAccessibleName(tr("The selected stage"));
    auto* paneBox = new QVBoxLayout(pane);
    paneBox->setContentsMargins(0, 0, 0, 0);
    paneBox->setSpacing(4);

    // 1. What it is, in terms of the sound.
    m_detailTip = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailTip"), QString(),
        tr("What this stage does to what you hear."), pane);
    m_detailTip->setAccessibleName(tr("What this stage does"));
    m_detailTip->setFixedWidth(kTipWidth);
    paneBox->addWidget(m_detailTip);

    // 2. What it is doing now -- the card's line, spelled out whole.
    m_detailText = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailText"), QString(),
        tr("What this stage is set to, in full."), pane);
    m_detailText->setAccessibleDescription(
        tr("What this stage is set to right now, in full; the card shows the short form."));
    m_detailText->setAccessibleName(tr("Selected stage now"));
    m_detailText->setFixedWidth(kDetailTextWidth);
    paneBox->addWidget(m_detailText);

    // 3. What you would hear without it.
    m_detailOff = new QLabel(pane);
    m_detailOff->setObjectName(QStringLiteral("gateChainDetailOff"));
    m_detailOff->setAccessibleName(tr("With this stage off"));
    m_detailOff->setWordWrap(false);
    m_detailOff->setFixedWidth(kDetailTextWidth);
    ThemeManager::instance().applyStyleSheet(m_detailOff, QString::fromLatin1(kOffStyle));
    paneBox->addWidget(m_detailOff);

    m_detailLevels = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailLevels"), chainLevelWorstCase(),
        tr("What the receiver measured in and out of this stage."), pane);
    m_detailLevels->setAccessibleDescription(
        tr("What the receiver measured in and out; hidden when it measured neither leg."));
    m_detailLevels->setAccessibleName(tr("Selected stage levels"));
    paneBox->addWidget(m_detailLevels);

    // The receiver's own words when it says no. Hidden until there are any.
    m_detailNote = new QLabel(pane);
    m_detailNote->setObjectName(QStringLiteral("gateChainDetailNote"));
    m_detailNote->setAccessibleName(tr("What the receiver said"));
    m_detailNote->setWordWrap(false);
    m_detailNote->setFixedWidth(kDetailTextWidth);
    m_detailNote->setVisible(false);
    ThemeManager::instance().applyStyleSheet(m_detailNote,
                                             QString::fromLatin1(kNoteStyle));
    paneBox->addWidget(m_detailNote);

    // 6. The control, at full size, last (design §2.4): read what the stage
    // is and does before the switch that changes it.
    m_detailControlBox = new QVBoxLayout;
    m_detailControlBox->setContentsMargins(0, 0, 0, 0);
    m_detailControlBox->setSpacing(4);
    paneBox->addLayout(m_detailControlBox);

    detailBody->addWidget(pane);
    hostBox->addWidget(detailFrame);
}

// The pane, in the order the questions get asked: what is this, what is it
// doing, can I change it, what would I hear without it. The "doing" line is
// the WHOLE of what the card shortened, which is why the two never read as a
// repetition.
void AetherGateChainWindow::showStage(const QString& id)
{
    const AetherGateChainTile* tile = id.isEmpty() ? nullptr : m_strip->tile(id);
    if (m_detailControl) {
        m_detailControl->deleteLater();
        m_detailControl = nullptr;
    }
    if (!tile) {
        setElided(m_detailCaption, tr("WHAT THIS DOES"), kDetailTextWidth);
        setElided(m_detailTip, tr("Click a stage."), kTipWidth);
        setElided(m_detailText, QString(), kDetailTextWidth);
        m_detailLevels->setVisible(false);
        m_detailOff->setVisible(false);
        return;
    }

    const ChainStage& stage = tile->stage();

    // The box's own title (design §2.4 item 1). See buildInspector()'s
    // comment for why this alone is enough to share the tile's accent
    // without a second title line in the body.
    setElided(m_detailCaption, tr("%1 — what it does").arg(stage.name), kDetailTextWidth);

    // What it does to the sound. The app's own sentence when it knows the
    // stage; the row's own words when a newer receiver sent one the app has
    // never heard of.
    QString sound = chainSoundSentence(stage.id);
    if (sound.isEmpty())
        sound = stage.tip.isEmpty() ? stage.why : stage.tip;
    setElided(m_detailTip, sound.isEmpty() ? emDash() : sound, kTipWidth);

    // What it is doing NOW. The card shows the short form of this line; here
    // it is whole, prefixed so the two cannot be mistaken for each other.
    // GUARD is the one row where "now" is not the card's own detail line --
    // the card says on/off and the floor, the pane says the last thing
    // the guard actually DID, which is a sentence built from /device's own
    // events[] rather than anything chainFromFilter() ever produces.
    QString now = stage.detail;
    if (stage.id == QLatin1String("frontend_guard")) {
        const QString eventSentence = chainFrontendEventSentence(m_frontend);
        if (!eventSentence.isEmpty())
            now = eventSentence;
    }
    if (now.isEmpty())
        now = emDash();
    setElided(m_detailText, tr("now: %1").arg(now), kDetailTextWidth);

    m_detailControl = new AetherGateChainControl(stage, QStringLiteral("gateChainDetail"),
                                                 /*large=*/true, nullptr);
    connect(m_detailControl, &AetherGateChainControl::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    auto it = m_pending.constFind(stage.id);
    m_detailControl->setBusy(it != m_pending.constEnd() && !it->confirmed);
    m_detailControlBox->addWidget(m_detailControl, 0, Qt::AlignLeft);

    // One dim line, in exactly one of three forms (design §2.4 item 4):
    // what you would hear with a switchable stage off, the reason a stage
    // nothing here can change is fixed, or GUARD's own calibration caveat.
    // Never two at once -- for any one stage only one of the three is ever
    // true, and this is their one source: a single `aside`, overwritten,
    // never appended to.
    QString aside;
    if (stage.actionable()) {
        // chainOffSentence() already returns "off: ..."; "with it " in front
        // of that is the whole of the sentence design §2.4 item 4 asks for.
        const QString off = chainOffSentence(stage.id);
        if (!off.isEmpty())
            aside = tr("with it %1").arg(off);
    } else {
        aside = stage.why;
    }
    // GUARD's own caveat -- a guard-moved LNA state breaks the gate's dBm
    // calibration -- belongs here rather than an "off" sentence nothing
    // asked for.
    if (stage.id == QLatin1String("frontend_guard") && !m_frontend.dbmCalibrated)
        aside = chainFrontendCalNoteText(m_frontend);
    m_detailOff->setVisible(!aside.isEmpty());
    if (!aside.isEmpty())
        setElided(m_detailOff, aside, kDetailTextWidth);

    // The levels line only exists when the gate measured at least one leg
    // (design §2.4 item 5) -- a dashed "in — · out — dB" said nothing an
    // operator could act on, so a stage that measures neither leg gets no
    // line here at all rather than two dashes.
    const bool hasLevels = stage.hasIn || stage.hasOut;
    m_detailLevels->setVisible(hasLevels);
    if (hasLevels)
        setElided(m_detailLevels, chainLevelText(stage), kDetailTextWidth);
}

} // namespace AetherSDR
