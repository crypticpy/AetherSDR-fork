#include "gui/AetherGateChainWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The frame. The minimum is what four tiles across need before the strip
// starts wrapping into another row; the initial size is chosen so that NOTHING
// scrolls when the window first opens -- the operator should not have to
// discover a scrollbar to find the second half of his own receiver.
constexpr int kMinWidth = 960;
constexpr int kMinHeight = 560;
constexpr int kInitialWidth = 1120;
constexpr int kInitialHeight = 760;

// The detail area's sentence field. Wide enough for a whole explanation and
// fixed, because a label that grew with its text would move the control under
// it every time the selection changed.
constexpr int kTipWidth = 700;
constexpr int kDetailTextWidth = 420;

// How long a write's settling window stays open. The poller bounds every
// request at 2 s (DiversityBandPoller.cpp kTransferTimeoutMs), so past that
// the write's own answer is not late, it is not coming -- and a stage that
// stayed frozen after that would be the app deciding it knew better than the
// gate.
constexpr int kSettleMs = 2500;

const char* kWindowStyle =
    "QWidget { background: {{color.background.0}}; color: {{color.text.primary}}; }"
    "QFrame#stripGroupBox { border: 1px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QScrollArea { background: transparent; border: none; }";

const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }"
    "QLabel[live=\"false\"] { color: {{color.text.disabled}}; }";

// "SELECTED: SHAPE", in the same token as the selected tile's 2 px frame. The
// shared colour is the whole point (design §0.3 item 7): the pane and the tile
// say they are about the same stage without either of them having to spell it
// out.
const char* kSelectedTitleStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold;"
    " background: transparent; }";

// The set button. Deliberately the same shape as the two doors on the applet,
// because it is the same kind of thing: one press, a lot happens.
const char* kSetButtonStyle =
    "QPushButton { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "padding: 4px 10px; border: 1px solid {{color.accent}}; border-radius: 4px; "
    "background: transparent; }"
    "QPushButton:hover { background: {{color.background.1}}; }"
    "QPushButton:pressed { background: {{color.background.3}}; }"
    "QPushButton:disabled { color: {{color.text.disabled}};"
    " border: 1px solid {{color.background.1}}; }";

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

const ChainMode kModes[] = {ChainMode::Phone, ChainMode::Cw, ChainMode::Data};
constexpr int kModeCount = 3;

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
    root->setSpacing(6);

    auto* caption = DiversityWidgets::makeCaption(tr("FILTER CHAIN"), bodyWidget());
    caption->setObjectName(QStringLiteral("gateChainCaption"));
    caption->setToolTip(tr("Every stage between the antenna and your ears, in the "
                           "order the signal goes through them. Nothing on this "
                           "strip reorders: it is a block diagram, not a rack."));
    caption->setAccessibleDescription(caption->toolTip());
    root->addWidget(caption);

    buildModeRow(root);

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
    m_strip->setMode(m_mode);
    connect(m_strip, &AetherGateChainStrip::stageSelected, this,
            &AetherGateChainWindow::showStage);
    connect(m_strip, &AetherGateChainStrip::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    hostBox->addWidget(m_strip);

    QVBoxLayout* detailBody = nullptr;
    QFrame* detailFrame = DiversityWidgets::makeGroupBox(
        tr("THIS STAGE"), QStringLiteral("gateChainDetail"), detailBody, host);
    auto* pane = new QWidget(detailFrame);
    pane->setObjectName(QStringLiteral("gateChainDetailPane"));
    pane->setAccessibleName(tr("The selected stage"));
    auto* paneBox = new QVBoxLayout(pane);
    paneBox->setContentsMargins(0, 0, 0, 0);
    paneBox->setSpacing(4);

    m_detailName = new QLabel(emDash(), pane);
    m_detailName->setObjectName(QStringLiteral("gateChainDetailName"));
    m_detailName->setAccessibleName(tr("Selected stage"));
    m_detailName->setToolTip(tr("The stage the strip has selected. Its tile carries "
                                "a frame in this same colour."));
    m_detailName->setAccessibleDescription(m_detailName->toolTip());
    ThemeManager::instance().applyStyleSheet(m_detailName,
                                             QString::fromLatin1(kSelectedTitleStyle));
    paneBox->addWidget(m_detailName);

    // The first line under the title is what the stage IS (design §0.3 item 7),
    // then its control, then what the gate measured through it.
    m_detailTip = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailTip"), QString(),
        tr("What this stage is, and what a radio's manual would call it."), pane);
    m_detailTip->setAccessibleName(tr("Selected stage explanation"));
    m_detailTip->setFixedWidth(kTipWidth);
    paneBox->addWidget(m_detailTip);

    m_detailText = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailText"),
        QStringLiteral("med · 5000/5000/5000 ms · AGC-T 60 · -99.9 dB"),
        tr("What this stage is doing right now, in the gate's own words."), pane);
    m_detailText->setAccessibleName(tr("Selected stage detail"));
    paneBox->addWidget(m_detailText);

    m_detailControlBox = new QVBoxLayout;
    m_detailControlBox->setContentsMargins(0, 0, 0, 0);
    m_detailControlBox->setSpacing(4);
    paneBox->addLayout(m_detailControlBox);

    m_detailLevels = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailLevels"), chainLevelWorstCase(),
        tr("What the gate measured going into this stage and coming out of it. An "
           "em dash is a leg the gate does not measure, never a zero."),
        pane);
    m_detailLevels->setAccessibleName(tr("Selected stage levels"));
    paneBox->addWidget(m_detailLevels);

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
                            "value says so here AND on its own tile."));
    m_status->setAccessibleDescription(m_status->toolTip());
    ThemeManager::instance().applyStyleSheet(m_status, QString::fromLatin1(kStatusStyle));
    root->addWidget(m_status);

    setStatus(tr("gate not answering"), false);
    setElided(m_detailTip, tr("Pick a stage on the strip above."), kTipWidth);
    setElided(m_detailText, emDash(), kDetailTextWidth);
    setElided(m_detailLevels, emDash(), kDetailTextWidth);
}

// PHONE · CW · DATA/OTHER, and the one set each mode offers. Three mode
// buttons and three set buttons, all built here and all named, because the
// automation bridge addresses them by objectName and a button that appeared
// only in one mode would be a name that sometimes does not exist.
void AetherGateChainWindow::buildModeRow(QVBoxLayout* root)
{
    auto* row = new QWidget(bodyWidget());
    row->setObjectName(QStringLiteral("gateChainModeRow"));
    row->setAccessibleName(tr("Listening mode"));
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);

    auto* caption = DiversityWidgets::makeCaption(tr("MODE"), row);
    caption->setObjectName(QStringLiteral("gateChainModeCaption"));
    box->addWidget(caption);

    for (ChainMode mode : kModes) {
        auto* button = new QPushButton(chainModeLabel(mode), row);
        button->setObjectName(QStringLiteral("gateChainMode_") + chainModeId(mode));
        button->setAccessibleName(tr("Listen in %1").arg(chainModeLabel(mode)));
        button->setToolTip(chainModeTip(mode));
        button->setAccessibleDescription(button->toolTip());
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(24);
        applyToggleButtonStyle(button);
        connect(button, &QPushButton::clicked, this, [this, mode] { setMode(mode); });
        m_modeButtons.append(button);
        box->addWidget(button);
    }

    box->addSpacing(10);

    for (ChainMode mode : kModes) {
        auto* button = new QPushButton(chainSetLabel(mode), row);
        button->setObjectName(QStringLiteral("gateChainSetButton_") + chainModeId(mode));
        button->setAccessibleName(tr("Apply the %1").arg(chainSetLabel(mode)));
        const QList<ChainPresetWrite> writes = chainPreset(mode);
        const QString tip =
            writes.isEmpty()
                ? tr("No set for this mode: the gate has no data-specific stage, "
                     "and a button that wrote nothing would be a lie.")
                : tr("%1 writes to /filter/set, in order, each one waited for "
                     "before the next goes out. Every line is a parameter this "
                     "gate accepts today; nothing here is a bulk route.")
                      .arg(writes.size());
        button->setToolTip(tip);
        button->setAccessibleDescription(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(24);
        button->setEnabled(!writes.isEmpty());
        ThemeManager::instance().applyStyleSheet(button,
                                                 QString::fromLatin1(kSetButtonStyle));
        connect(button, &QPushButton::clicked, this, [this, mode] {
            m_preset->start(chainPreset(mode), chainSetLabel(mode));
        });
        m_setButtons.append(button);
        box->addWidget(button);
    }

    box->addStretch(1);
    root->addWidget(row);

    m_modeTip = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainModeTipLabel"), QString(),
        tr("What this mode puts on the strip, and what its set does."), bodyWidget());
    m_modeTip->setAccessibleName(tr("Mode explanation"));
    m_modeTip->setFixedWidth(kTipWidth);
    root->addWidget(m_modeTip);

    m_preset = new AetherGateChainPreset(this);
    connect(m_preset, &AetherGateChainPreset::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    connect(m_preset, &AetherGateChainPreset::progress, this,
            [this](const QString& name, int done, int total, const QString& why) {
                setStatus(tr("%1: %2 of %3 — %4").arg(name).arg(done).arg(total).arg(why),
                          true);
            });
    connect(m_preset, &AetherGateChainPreset::finished, this,
            [this](const QString& name, bool ok, const QString& reason) {
                setStatus(ok ? tr("%1 applied").arg(name)
                             : tr("%1 stopped: %2").arg(name, reason),
                          ok);
            });

    setMode(m_mode);
}

void AetherGateChainWindow::setMode(ChainMode mode)
{
    m_mode = mode;
    for (int i = 0; i < m_modeButtons.size() && i < kModeCount; ++i)
        m_modeButtons.at(i)->setChecked(kModes[i] == mode);
    for (int i = 0; i < m_setButtons.size() && i < kModeCount; ++i)
        m_setButtons.at(i)->setVisible(kModes[i] == mode);
    if (m_modeTip)
        setElided(m_modeTip, chainModeTip(mode), kTipWidth);
    if (m_strip)
        m_strip->setMode(mode);
    // A set that was mid-flight belongs to the mode it was started from.
    if (m_preset && m_preset->running())
        m_preset->abort();
}

// Every write in this window goes through here so that exactly one place
// records what was on screen when it left. Without that record a stale poll --
// one the gate answered from a status it read BEFORE the write applied -- is
// indistinguishable from news.
void AetherGateChainWindow::onWriteRequested(const QString& route, const QUrlQuery& query)
{
    m_lastWriteStage.clear();
    // Which stage asked? The one whose control carries this route and query.
    // A refusal stays on its tile until the operator tries THAT stage again --
    // clearing it on the next poll would be a 500 ms flash of the one sentence
    // that says why nothing happened.
    for (const ChainStage& stage : m_strip->stages()) {
        if (!stage.actionable() || stage.actionRoute != route)
            continue;
        const QString key = stage.actionQuery;
        const QString sent = query.toString();
        if (sent == key || (key.endsWith(QLatin1Char('=')) && sent.startsWith(key))) {
            m_lastWriteStage = stage.id;
            PendingWrite pending;
            pending.before = stage.settingKey();
            pending.age.start();
            m_pending.insert(stage.id, pending);
            if (AetherGateChainTile* tile = m_strip->tile(stage.id))
                tile->setError(QString());
            break;
        }
    }
    applyBusyToTiles();
    emit requestWrite(route, query);
}

// One body's rows, with every stage still inside its settling window held at
// the setting the strip already shows. Nothing optimistic happens here: the
// held setting is the one a gate answer put there, not the one that was asked
// for.
QList<ChainStage> AetherGateChainWindow::holdPendingStages(const QList<ChainStage>& fresh)
{
    if (m_pending.isEmpty())
        return fresh;
    // A stage that has left the chain entirely (the gate stopped sending it)
    // would otherwise keep its entry for ever; the window is what expires it.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->age.elapsed() > kSettleMs)
            it = m_pending.erase(it);
        else
            ++it;
    }
    QList<ChainStage> out = fresh;
    for (ChainStage& row : out) {
        auto it = m_pending.find(row.id);
        if (it == m_pending.end())
            continue;
        if (it->age.elapsed() > kSettleMs) {
            // The write's answer never came. Stop holding: the gate's word,
            // even a stale one, beats the app's memory.
            m_pending.erase(it);
            continue;
        }
        if (row.settingKey() != it->before) {
            // The gate has answered with something other than the pre-write
            // setting. THIS is the write's effect; take it, and let the
            // control work again. The entry stays until the window closes so
            // that a straggling poll cannot put the old setting back.
            it->confirmed = true;
            continue;
        }
        // A body reporting the pre-write setting inside the settling window
        // was measured before the write, so the WHOLE row is stale: its
        // headline sentence and its meters were computed from the same status
        // read. The row keeps what the last answer put there rather than
        // showing a switch that says one thing and a sentence that says the
        // other.
        if (const AetherGateChainTile* tile = m_strip->tile(row.id))
            row = tile->stage();
    }
    return out;
}

void AetherGateChainWindow::applyBusyToTiles()
{
    for (const ChainStage& stage : m_strip->stages()) {
        auto it = m_pending.constFind(stage.id);
        const bool busy = it != m_pending.constEnd() && !it->confirmed;
        if (AetherGateChainTile* tile = m_strip->tile(stage.id))
            tile->setBusy(busy);
        if (m_detailControl && m_strip->selectedId() == stage.id)
            m_detailControl->setBusy(busy);
    }
}

void AetherGateChainWindow::applyFilter(const QJsonObject& filter)
{
    if (filter.isEmpty())
        return;
    const QString error = filter.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        // The gate refused the write. Say so ON THE TILE that asked -- a
        // status line nobody reads is where the last build put this, and the
        // operator's answer was that the roof select still looked live
        // (design §0.3 item 6). The row does not move: a refused value never
        // happened.
        if (!m_lastWriteStage.isEmpty()) {
            m_pending.remove(m_lastWriteStage);
            if (AetherGateChainTile* tile = m_strip->tile(m_lastWriteStage))
                tile->setError(error);
        }
        applyBusyToTiles();
        showStage(m_strip->selectedId());
        // A set that was running says its own sentence -- "CW SET stopped: <the
        // gate's words>" -- which carries the refusal AND which line of the set
        // died on it. Only a lone write gets the bare refusal.
        if (m_preset->running())
            m_preset->noteError(error);
        else
            setStatus(tr("gate refused: %1").arg(error), true);
        return;
    }
    if (!looksLikeFilterStatus(filter))
        return;

    bool fromGate = false;
    const QList<ChainStage> stages = holdPendingStages(chainFromFilter(filter, &fromGate));
    m_fromGate = fromGate;
    m_strip->setStages(stages);
    applyBusyToTiles();
    setElided(m_source,
              fromGate ? tr("%1 stages · authored by the gate").arg(stages.size())
                       : tr("%1 stages · assembled by the app from this gate's "
                            "filter status")
                             .arg(stages.size()),
              kTipWidth);
    showStage(m_strip->selectedId());
    if (m_preset->running())
        m_preset->noteFilterBody();
    else
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
    m_preset->abort();
    m_pending.clear();
    m_lastWriteStage.clear();
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
    m_detailName->setText(tr("SELECTED: %1").arg(stage.name));
    m_detailName->setAccessibleDescription(m_detailName->text());
    setElided(m_detailText, stage.detail.isEmpty() ? emDash() : stage.detail,
              kDetailTextWidth);
    setElided(m_detailLevels, chainLevelText(stage), kDetailTextWidth);
    const QString tip = stage.tip.isEmpty() ? stage.why : stage.tip;
    setElided(m_detailTip, tip.isEmpty() ? emDash() : tip, kTipWidth);

    m_detailControl = new AetherGateChainControl(stage, QStringLiteral("gateChainDetail"),
                                                 /*large=*/true, nullptr);
    connect(m_detailControl, &AetherGateChainControl::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    auto it = m_pending.constFind(stage.id);
    m_detailControl->setBusy(it != m_pending.constEnd() && !it->confirmed);
    m_detailControlBox->addWidget(m_detailControl);
}

void AetherGateChainWindow::setStatus(const QString& text, bool live)
{
    m_status->setText(text);
    DiversityWidgets::setLive(m_status, live);
}

} // namespace AetherSDR
