#include "gui/AetherGateChainWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/DiversityWindowPanels.h"

#include <QFontMetrics>
#include <QFrame>
#include <QJsonObject>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The frame. The minimum is what five tiles across need before the strip
// starts wrapping into a sixth row; the initial size is chosen so that NOTHING
// scrolls when the window first opens -- the operator should not have to
// discover a scrollbar to find the second half of his own receiver.
constexpr int kMinWidth = 900;
constexpr int kMinHeight = 560;
constexpr int kInitialWidth = 1120;
constexpr int kInitialHeight = 820;

// The detail area's sentence field. Wide enough for a whole explanation and
// fixed, because a label that grew with its text would move the control under
// it every time the selection changed.
constexpr int kTipWidth = 700;
constexpr int kDetailTextWidth = 420;

const char* kWindowStyle =
    "QWidget { background: {{color.background.0}}; color: {{color.text.primary}}; }"
    "QFrame#stripGroupBox { border: 1px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QScrollArea { background: transparent; border: none; }";

const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }"
    "QLabel[live=\"false\"] { color: {{color.text.disabled}}; }";

QString emDash()
{
    return QStringLiteral("—");
}

void setElided(QLabel* label, const QString& text, int width)
{
    const QFontMetrics fm(label->font());
    label->setText(fm.elidedText(text, Qt::ElideRight, width));
    label->setToolTip(text);
    label->setAccessibleDescription(text);
}

// A /filter status, as opposed to a device object, an error body or an empty
// reply. The write door is shared with routes that answer with something else
// entirely (a chain row may carry "route": "/device/set"), and a body that is
// not a filter status must leave the strip alone rather than blank it.
bool looksLikeFilterStatus(const QJsonObject& obj)
{
    return obj.contains(QStringLiteral("chain"))
           || obj.contains(QStringLiteral("roofing"))
           || obj.contains(QStringLiteral("agc"))
           || obj.contains(QStringLiteral("low_hz"));
}

} // namespace

AetherGateChainWindow::AetherGateChainWindow(QWidget* parent)
    : PersistentDialog(tr("Chain"), QStringLiteral("AetherGateChainWindowGeometry"),
                       parent, /*toolWindow=*/true)
{
    setObjectName(QStringLiteral("gateChainWindow"));
    setAccessibleName(tr("Filter chain window"));
    // Closing this must never be able to end the application, however the
    // platform decides to count top-level windows.
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(kMinWidth, kMinHeight);
    resize(kInitialWidth, kInitialHeight);
    ThemeManager::instance().applyStyleSheet(this, QString::fromLatin1(kWindowStyle));

    auto* root = new QVBoxLayout(bodyWidget());
    root->setContentsMargins(8, 4, 8, 8);
    root->setSpacing(8);

    auto* caption = DiversityWidgets::makeCaption(tr("FILTER CHAIN"), bodyWidget());
    caption->setObjectName(QStringLiteral("gateChainCaption"));
    caption->setToolTip(tr("Every stage between the antenna and your ears, in the "
                           "order the signal goes through them. Nothing on this "
                           "strip reorders: it is a block diagram, not a rack."));
    caption->setAccessibleDescription(caption->toolTip());
    root->addWidget(caption);

    m_source = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainSourceLabel"),
        tr("13 stages · assembled by the app from this gate's filter status"),
        tr("Whether the rows came from the gate's own chain[] array -- in which "
           "case a stage the app has never heard of still renders -- or from the "
           "app's built-in fallback, which is everything a gate with no chain[] "
           "can honestly describe."),
        bodyWidget());
    root->addWidget(m_source);

    // Everything below the caption scrolls, so the window can be dragged
    // smaller than its natural content height without a tile becoming
    // unreachable. At the initial size nothing scrolls.
    auto* host = new QWidget;
    host->setObjectName(QStringLiteral("gateChainScrollHost"));
    auto* hostBox = new QVBoxLayout(host);
    hostBox->setContentsMargins(0, 0, 0, 0);
    hostBox->setSpacing(8);

    m_strip = new AetherGateChainStrip(host);
    connect(m_strip, &AetherGateChainStrip::stageSelected, this,
            &AetherGateChainWindow::showStage);
    connect(m_strip, &AetherGateChainStrip::requestWrite, this,
            &AetherGateChainWindow::requestWrite);
    hostBox->addWidget(m_strip);

    QVBoxLayout* detailBody = nullptr;
    QFrame* detailFrame = DiversityWidgets::makeGroupBox(
        tr("THIS STAGE"), QStringLiteral("gateChainDetail"), detailBody, host);
    auto* pane = new QWidget(detailFrame);
    pane->setObjectName(QStringLiteral("gateChainDetailPane"));
    pane->setAccessibleName(tr("The selected stage"));
    auto* paneBox = new QVBoxLayout(pane);
    paneBox->setContentsMargins(0, 0, 0, 0);
    paneBox->setSpacing(5);

    m_detailName = DiversityWidgets::makeCaption(emDash(), pane);
    m_detailName->setObjectName(QStringLiteral("gateChainDetailName"));
    m_detailName->setAccessibleName(tr("Selected stage"));
    paneBox->addWidget(m_detailName);

    m_detailText = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailText"),
        QStringLiteral("med · 5000/5000/5000 ms · AGC-T 60 · -99.9 dB"),
        tr("What this stage is doing right now, in the gate's own words."), pane);
    m_detailText->setAccessibleName(tr("Selected stage detail"));
    paneBox->addWidget(m_detailText);

    m_detailLevels = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailLevels"), chainLevelWorstCase(),
        tr("What the gate measured going into this stage and coming out of it. An "
           "em dash is a leg the gate does not measure, never a zero."),
        pane);
    m_detailLevels->setAccessibleName(tr("Selected stage levels"));
    paneBox->addWidget(m_detailLevels);

    m_detailTip = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailTip"), QString(),
        tr("What this stage is, and what a radio's manual would call it."), pane);
    m_detailTip->setAccessibleName(tr("Selected stage explanation"));
    m_detailTip->setFixedWidth(kTipWidth);
    paneBox->addWidget(m_detailTip);

    m_detailControlBox = new QVBoxLayout;
    m_detailControlBox->setContentsMargins(0, 0, 0, 0);
    m_detailControlBox->setSpacing(4);
    paneBox->addLayout(m_detailControlBox);

    detailBody->addWidget(pane);
    hostBox->addWidget(detailFrame);
    hostBox->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("gateChainScroll"));
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);

    m_status = new QLabel(tr("gate not answering"), bodyWidget());
    m_status->setObjectName(QStringLiteral("gateChainStatusLabel"));
    m_status->setAccessibleName(tr("Gate status"));
    m_status->setToolTip(tr("Whether the Aether-gate bridge is answering, and what "
                            "it said about the last write. A stage that refuses a "
                            "value says so here rather than moving on screen."));
    m_status->setAccessibleDescription(m_status->toolTip());
    ThemeManager::instance().applyStyleSheet(m_status, QString::fromLatin1(kStatusStyle));
    root->addWidget(m_status);

    setStatus(tr("gate not answering"), false);
    setElided(m_detailTip, tr("Pick a stage on the strip above."), kTipWidth);
    setElided(m_detailText, emDash(), kDetailTextWidth);
    setElided(m_detailLevels, emDash(), kDetailTextWidth);
}

void AetherGateChainWindow::applyFilter(const QJsonObject& filter)
{
    if (filter.isEmpty())
        return;
    const QString error = filter.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        // The gate refused the write. Say so and change nothing: the strip is
        // showing what the receiver IS doing, and a refused value never
        // happened.
        setStatus(tr("gate refused: %1").arg(error), true);
        return;
    }
    if (!looksLikeFilterStatus(filter))
        return;

    bool fromGate = false;
    const QList<ChainStage> stages = chainFromFilter(filter, &fromGate);
    m_fromGate = fromGate;
    m_strip->setStages(stages);
    setElided(m_source,
              fromGate ? tr("%1 stages · authored by the gate").arg(stages.size())
                       : tr("%1 stages · assembled by the app from this gate's "
                            "filter status")
                             .arg(stages.size()),
              kTipWidth);
    showStage(m_strip->selectedId());
    setStatus(tr("gate answering"), true);
}

void AetherGateChainWindow::setPresent(bool present)
{
    if (m_present == present)
        return;
    m_present = present;
    if (present) {
        setStatus(tr("gate answering"), true);
        return;
    }
    m_strip->clear();
    m_fromGate = false;
    setElided(m_source, emDash(), kTipWidth);
    showStage(QString());
    setStatus(tr("gate not answering"), false);
}

void AetherGateChainWindow::showStage(const QString& id)
{
    const AetherGateChainTile* tile = id.isEmpty() ? nullptr : m_strip->tile(id);
    if (m_detailControl) {
        m_detailControl->deleteLater();
        m_detailControl = nullptr;
    }
    if (!tile) {
        m_detailName->setText(emDash());
        setElided(m_detailText, emDash(), kDetailTextWidth);
        setElided(m_detailLevels, emDash(), kDetailTextWidth);
        setElided(m_detailTip, tr("Pick a stage on the strip above."), kTipWidth);
        return;
    }

    const ChainStage& stage = tile->stage();
    m_detailName->setText(stage.name);
    m_detailName->setAccessibleDescription(stage.name);
    setElided(m_detailText, stage.detail.isEmpty() ? emDash() : stage.detail,
              kDetailTextWidth);
    setElided(m_detailLevels, chainLevelText(stage), kDetailTextWidth);
    const QString tip = stage.tip.isEmpty() ? stage.why : stage.tip;
    setElided(m_detailTip, tip.isEmpty() ? emDash() : tip, kTipWidth);

    m_detailControl = new AetherGateChainControl(stage, QStringLiteral("gateChainDetail"),
                                                 /*large=*/true, nullptr);
    connect(m_detailControl, &AetherGateChainControl::requestWrite, this,
            &AetherGateChainWindow::requestWrite);
    m_detailControlBox->addWidget(m_detailControl);
}

void AetherGateChainWindow::setStatus(const QString& text, bool live)
{
    m_status->setText(text);
    DiversityWidgets::setLive(m_status, live);
}

} // namespace AetherSDR
