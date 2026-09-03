#include "gui/AetherGateChainWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainAuto.h"
#include "gui/AetherGateChainPresets.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/AetherGateChainVisual.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMargins>
#include <QPushButton>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The frame. The initial size is the one the whole layout is arithmetic for:
// the four groups measure 1094 px across (236 + 196 + 400 + 196, plus three
// 22 px arrow gutters) and the diagram plus the inspector clear 820 px of
// height, so NOTHING scrolls when the window first opens. The minimum is
// smaller on purpose -- below the initial size the scroll area is what keeps
// every stage reachable.
constexpr int kMinWidth = 960;
constexpr int kMinHeight = 560;
constexpr int kInitialWidth = 1120;
constexpr int kInitialHeight = 820;

// The inspector's sentence fields. Fixed, because a label that grew with its
// text would move the control under it every time the selection changed, and
// wide enough that the sentences in AetherGateChainModes.cpp fit whole.
constexpr int kTipWidth = 1020;
constexpr int kDetailTextWidth = 1020;

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
    // The FRONT END summary card is drawn as ONE block, so it carries the
    // frame its seven rows do not.
    "QFrame#gateChainFrontEndCard { border: 1px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QScrollArea { background: transparent; border: none; }"
    // The tab row. A PLAIN QTabBar, deliberately: the Diversity window's page
    // row is five custom toggle buttons and it reads as a set of controls, but
    // these two are not controls -- they are two views of the same receiver,
    // and the platform's own tab is the shape every operator already knows.
    // Only the colours are ours.
    "QTabWidget::pane { border: 1px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; top: -1px; }"
    "QTabBar::tab { color: {{color.text.secondary}}; font-size: 11px;"
    " font-weight: bold; padding: 5px 18px; margin-right: 2px;"
    " border: 1px solid {{color.background.1}}; border-bottom: none;"
    " border-top-left-radius: 4px; border-top-right-radius: 4px;"
    " background: transparent; }"
    "QTabBar::tab:selected { color: {{color.accent.bright}};"
    " border: 1px solid {{color.accent}}; border-bottom: none; }"
    "QTabBar::tab:hover:!selected { color: {{color.text.primary}};"
    " background: {{color.background.1}}; }";

// The one line that says what you would hear WITHOUT the selected stage. Dim,
// because it describes something that is not happening.
const char* kOffStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px;"
    " background: transparent; }";

// The receiver's own words when it refuses. The only warning-coloured thing in
// the inspector, and hidden entirely when there is nothing to refuse.
const char* kNoteStyle =
    "QLabel { color: {{color.accent.warning}}; font-size: 11px;"
    " background: transparent; }";

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

// How many stages at the FRONT of `stages` are in the FrontEnd group -- the
// same walk chainStageGroup() itself does inside AetherGateChainStrip's own
// rebuild()/relayout(), starting from the same ChainGroup::FrontEnd
// "previous". The FrontEnd group is always a leading run in gate order (it
// is what the antenna and the receiver do before anything else), so the
// first row that is NOT FrontEnd ends the run. That index is where the
// frontend guard's two synthetic rows belong: right after the last row the
// gate itself put in this group, so an unknown id (there is no id
// "frontend_guard" in kGroupTable) inherits FrontEnd from its neighbour
// exactly the way any other unrecognised row would.
int chainFrontEndSpan(const QList<ChainStage>& stages)
{
    ChainGroup previous = ChainGroup::FrontEnd;
    int span = 0;
    for (int i = 0; i < stages.size(); ++i) {
        const ChainGroup group = chainStageGroup(stages.at(i).id, previous);
        previous = group;
        if (group != ChainGroup::FrontEnd)
            break;
        span = i + 1;
    }
    return span;
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
    // Through the base class, not on the layout: PersistentDialog re-applies
    // its own body margins (nine a side) on every show, and nine a side plus
    // the tab pane's 1 px border leaves 1 100 px for a diagram that is 1 102
    // wide at 1 120 -- which is the horizontal scrollbar the whole layout
    // exists to avoid. Six a side leaves 1 106.
    setBodyLayoutMargins(QMargins(6, 6, 6, 8), QMargins(6, 4, 6, 8));
    root->setSpacing(6);

    // The two tabs, and everything under them. The window's own caption used
    // to sit here reading "FILTER CHAIN"; the tab row says CHAIN in the same
    // place and a caption over a tab bar is one line of height spent saying
    // what the tab bar already said.
    buildTabs(root);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("gateChainStatusLabel"));
    m_status->setAccessibleName(tr("Connection"));
    ThemeManager::instance().applyStyleSheet(m_status, QString::fromLatin1(kStatusStyle));
    root->addWidget(m_status);

    setLink(ChainLink::Gone);
    showStage(QString());
}

// The bottom pane. It answers four questions in the order an operator asks
// them: what is this, what is it doing, can I change it, and what would I hear
// without it. It never repeats the card's one line verbatim -- the card has
// the short form, this has the whole of it.
void AetherGateChainWindow::buildInspector(QVBoxLayout* hostBox, QWidget* host)
{
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
    m_detailName->setWordWrap(false);
    m_detailName->setToolTip(tr("The stage the diagram has selected. Its card "
                                "carries a frame in this same colour."));
    m_detailName->setAccessibleDescription(m_detailName->toolTip());
    ThemeManager::instance().applyStyleSheet(m_detailName,
                                             QString::fromLatin1(kSelectedTitleStyle));
    paneBox->addWidget(m_detailName);

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
        tr("What this stage is set to right now, in full. The card beside it "
           "shows the short form of the same thing."),
        pane);
    m_detailText->setAccessibleName(tr("Selected stage now"));
    m_detailText->setFixedWidth(kDetailTextWidth);
    paneBox->addWidget(m_detailText);

    // AUTO CLEAN's own two lines, under its own detail line above: its
    // state and why, then its recent moves newest first. Hidden for every
    // other stage -- see AetherGateChainAuto.h.
    m_detailAutoState = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainAutoState"), QString(),
        tr("What AUTO CLEAN is doing right now, and why."), pane);
    m_detailAutoState->setFixedWidth(kDetailTextWidth);
    m_detailAutoState->setVisible(false);
    paneBox->addWidget(m_detailAutoState);

    m_autoEvents = new QLabel(pane);
    m_autoEvents->setObjectName(QStringLiteral("gateChainAutoEvents"));
    m_autoEvents->setAccessibleName(tr("AUTO CLEAN's recent moves"));
    m_autoEvents->setWordWrap(false);
    m_autoEvents->setFixedWidth(kDetailTextWidth);
    m_autoEvents->setVisible(false);
    ThemeManager::instance().applyStyleSheet(m_autoEvents, QString::fromLatin1(kOffStyle));
    paneBox->addWidget(m_autoEvents);

    // 3. The control, at full size.
    m_detailControlBox = new QVBoxLayout;
    m_detailControlBox->setContentsMargins(0, 0, 0, 0);
    m_detailControlBox->setSpacing(4);
    paneBox->addLayout(m_detailControlBox);

    // 4. What you would hear without it.
    m_detailOff = new QLabel(pane);
    m_detailOff->setObjectName(QStringLiteral("gateChainDetailOff"));
    m_detailOff->setAccessibleName(tr("With this stage off"));
    m_detailOff->setWordWrap(false);
    m_detailOff->setFixedWidth(kDetailTextWidth);
    ThemeManager::instance().applyStyleSheet(m_detailOff, QString::fromLatin1(kOffStyle));
    paneBox->addWidget(m_detailOff);

    m_detailLevels = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetailLevels"), chainLevelWorstCase(),
        tr("What the receiver measured going into this stage and coming out of "
           "it. A dash is a leg nothing measures, never a zero."),
        pane);
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

    detailBody->addWidget(pane);
    hostBox->addWidget(detailFrame);
}

// MODE: a segmented PHONE / CW / DATA, then ONE button that names the mode it
// would set up. Three mode buttons and three set buttons are built, and only
// the set for the current mode is visible -- the automation bridge and the
// screen reader address them by objectName, and a name that existed only in
// one mode would be a name that sometimes is not there.
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

    box->addSpacing(14);

    for (ChainMode mode : kModes) {
        auto* button = new QPushButton(chainSetLabel(mode), row);
        button->setObjectName(QStringLiteral("gateChainSetButton_") + chainModeId(mode));
        button->setAccessibleName(chainSetLabel(mode));
        const QList<ChainPresetWrite> writes = chainPreset(mode);
        // What the button DOES, in the operator's terms. The first build put
        // the number of writes and the route here, which is the app talking
        // to itself about its own plumbing.
        const QString tip = writes.isEmpty()
                                ? tr("No set for data yet.")
                                : tr("Sets up the whole chain for %1. It changes "
                                     "one stage at a time and waits for the "
                                     "receiver after each one, so you can watch "
                                     "it happen on the diagram.")
                                      .arg(chainModeLabel(mode));
        button->setToolTip(tip);
        button->setAccessibleDescription(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(24);
        button->setEnabled(!writes.isEmpty());
        ThemeManager::instance().applyStyleSheet(button,
                                                 QString::fromLatin1(kSetButtonStyle));
        connect(button, &QPushButton::clicked, this, [this, button, mode] {
            // The button says what it is doing while it does it. A set is
            // thirteen writes and several seconds; a button that still read
            // "SET UP FOR PHONE" throughout would look like nothing happened.
            button->setText(chainSetBusyLabel());
            setSetProgress(chainSetBusyLabel());
            setNote(QString());
            m_preset->start(chainPreset(mode), chainSetLabel(mode));
        });
        m_setButtons.append(button);
        box->addWidget(button);
    }

    box->addStretch(1);
    root->addWidget(row);

    // One plain line about what the set does to the SOUND. Not a paragraph,
    // and never a word about the control port.
    m_modeTip = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainModeTipLabel"), QString(),
        tr("What setting up for this mode does to what you hear."), bodyWidget());
    m_modeTip->setAccessibleName(tr("What this set does"));
    m_modeTip->setFixedWidth(kTipWidth);
    root->addWidget(m_modeTip);

    // Where a running set narrates itself, so the status line can stay the
    // three words it is meant to be.
    m_setProgress = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainSetProgressLabel"), QString(),
        tr("How far a set has got, and which stage it is on."), bodyWidget());
    m_setProgress->setAccessibleName(tr("Set progress"));
    m_setProgress->setFixedWidth(kTipWidth);
    m_setProgress->setVisible(false);
    root->addWidget(m_setProgress);

    m_preset = new AetherGateChainPreset(this);
    connect(m_preset, &AetherGateChainPreset::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    connect(m_preset, &AetherGateChainPreset::progress, this,
            [this](const QString& name, int done, int total, const QString& why) {
                Q_UNUSED(name)
                setSetProgress(tr("step %1 of %2: %3").arg(done).arg(total).arg(why));
                setLink(ChainLink::Applying);
            });
    connect(m_preset, &AetherGateChainPreset::finished, this,
            [this](const QString& name, bool ok, const QString& reason) {
                Q_UNUSED(name)
                m_loadingPreset = false;
                if (ok) {
                    setSetProgress(tr("done"));
                } else {
                    // The receiver's own words go where the operator is
                    // looking, not onto a status line reduced to three states.
                    setSetProgress(tr("stopped"));
                    setNote(reason);
                }
                for (int i = 0; i < m_setButtons.size() && i < kModeCount; ++i)
                    m_setButtons.at(i)->setText(chainSetLabel(kModes[i]));
                setLink(m_present ? ChainLink::Live : ChainLink::Gone);
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
        setElided(m_modeTip, chainModeSound(mode), kTipWidth);
    if (m_strip)
        m_strip->setMode(mode);
    // A set that was mid-flight belongs to the mode it was started from.
    if (m_preset && m_preset->running())
        m_preset->abort();
    for (int i = 0; i < m_setButtons.size() && i < kModeCount; ++i)
        m_setButtons.at(i)->setText(chainSetLabel(kModes[i]));
    setSetProgress(QString());
}

// Every write in this window goes through here so that exactly one place
// records what was on screen when it left. Without that record a stale poll --
// one the gate answered from a status it read BEFORE the write applied -- is
// indistinguishable from news.
void AetherGateChainWindow::onWriteRequested(const QString& route, const QUrlQuery& query)
{
    m_lastWriteStage.clear();
    // Which stage asked? The one whose control carries this route and query
    // -- or, for the GUARD row, whose FLOOR control does, the one stage in
    // this window where a single row's control sends two different queries
    // to the same route. A refusal stays on its tile until the operator
    // tries THAT stage again -- clearing it on the next poll would be a
    // 500 ms flash of the one sentence that says why nothing happened.
    const QString sent = query.toString();
    for (const ChainStage& stage : m_strip->stages()) {
        bool matched = false;
        if (stage.actionable() && stage.actionRoute == route) {
            const QString key = stage.actionQuery;
            matched = sent == key || (key.endsWith(QLatin1Char('=')) && sent.startsWith(key));
        }
        if (!matched && stage.hasFloorControl && stage.floorActionRoute == route
            && stage.floorActionQuery.endsWith(QLatin1Char('='))) {
            matched = sent.startsWith(stage.floorActionQuery);
        }
        // A check on this row -- ROOFING · DIGITAL's PEAK OFFSET is the
        // first, and the query is a whole string the gate wrote, not a
        // prefix the app appends to.
        if (!matched) {
            for (const ChainCheck& check : stage.checks) {
                if (check.route == route && (sent == check.queryOn || sent == check.queryOff)) {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
            continue;
        m_lastWriteStage = stage.id;
        PendingWrite pending;
        pending.before = stage.settingKey();
        pending.age.start();
        m_pending.insert(stage.id, pending);
        if (AetherGateChainTile* tile = m_strip->tile(stage.id))
            tile->setError(QString());
        break;
    }
    // A new attempt clears the last refusal: the note is about THIS write.
    setNote(QString());
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
            setNote(error);
        return;
    }
    if (!looksLikeFilterStatus(filter))
        return;

    bool fromGate = false;
    m_filterStages = chainFromFilter(filter, &fromGate);
    m_fromGate = fromGate;
    m_governor = chainAutoParseGovernor(filter);
    const QList<ChainStage> stages = refreshStrip();
    if (m_visual)
        m_visual->applyFilter(filter);
    if (m_preset->running())
        m_preset->noteFilterBody();
    else
        setLink(ChainLink::Live);
    // Held against the preset in force -- AFTER the sequencer has seen the
    // body, because the last step's own answer is what ends a load, and a
    // chain compared while the load was still running would read as edited
    // by its own hand.
    if (m_presets && !m_loadingPreset)
        m_presets->noteRows(stages);
}

// GET /device's "frontend" key. Unlike applyFilter() this never carries an
// {"error"} of its own (a /device poll and a /frontend/set write both answer
// with the same status object, refused or not, the way the gate's other
// write doors do), so there is no refusal branch here -- a write this window
// sent to /frontend/set comes back through the SAME onWriteRequested()
// settling window every other write does, keyed by the GUARD row's
// actionRoute/actionQuery or its floorActionRoute/floorActionQuery.
void AetherGateChainWindow::applyDevice(const QJsonObject& device)
{
    if (device.isEmpty())
        return;
    m_frontend = chainFrontendFromDevice(device);
    refreshStrip();
}

// What applyFilter() and applyDevice() both need done to the strip: merge
// the gate's own chain[] rows with the frontend guard's two synthetic ones
// (when the guard is available at all), hold anything still inside its
// settling window, and hand the result to the strip and the inspector.
QList<ChainStage> AetherGateChainWindow::refreshStrip()
{
    QList<ChainStage> merged = m_filterStages;
    const QList<ChainStage> frontendRows = chainFrontendRows(m_frontend);
    if (!frontendRows.isEmpty()) {
        const int at = chainFrontEndSpan(merged);
        for (int i = 0; i < frontendRows.size(); ++i)
            merged.insert(at + i, frontendRows.at(i));
    }

    const QList<ChainStage> stages = holdPendingStages(merged);
    m_strip->setStages(stages);
    m_strip->setFrontendCalNote(m_frontend.available && !m_frontend.dbmCalibrated,
                                chainFrontendCalNoteText(m_frontend));
    chainAutoApplyNotes(m_strip, m_governor);
    applyBusyToTiles();
    showStage(m_strip->selectedId());
    return stages;
}

void AetherGateChainWindow::setPresent(bool present)
{
    if (m_present == present)
        return;
    m_present = present;
    if (present) {
        setLink(ChainLink::Live);
        return;
    }
    m_preset->abort();
    m_loadingPreset = false;
    m_pending.clear();
    m_lastWriteStage.clear();
    m_filterStages.clear();
    m_frontend = ChainFrontendStatus();
    m_governor = ChainAutoGovernor();
    m_strip->clear();
    m_strip->setFrontendCalNote(false, QString());
    if (m_visual)
        m_visual->clear();
    m_fromGate = false;
    setSetProgress(QString());
    showStage(QString());
    setLink(ChainLink::Gone);
}

// The inspector, in the order the questions get asked: what is this, what is
// it doing, can I change it, what would I hear without it. The "doing" line is
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
        m_detailName->setText(emDash());
        setElided(m_detailTip, tr("Click a stage."), kTipWidth);
        setElided(m_detailText, QString(), kDetailTextWidth);
        setElided(m_detailLevels, QString(), kDetailTextWidth);
        m_detailOff->setVisible(false);
        m_detailAutoState->setVisible(false);
        m_autoEvents->setVisible(false);
        return;
    }

    const ChainStage& stage = tile->stage();
    m_detailName->setText(stage.name);
    m_detailName->setAccessibleDescription(stage.name);

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
    // the card says on/off and the floor, the inspector says the last thing
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

    // One dim line, and which of two things it says depends on whether the
    // stage can move at all. A switchable stage gets "what you would hear
    // without it"; a stage nothing here can change gets the reason, which the
    // card no longer prints on a FRONT END row. Never both, because for any
    // one stage only one of them is true.
    QString aside = stage.actionable() ? chainOffSentence(stage.id) : QString();
    if (aside.isEmpty() && !stage.actionable())
        aside = stage.why;
    // GUARD's own caveat -- a guard-moved LNA state breaks the gate's dBm
    // calibration -- belongs here rather than an "off" sentence nothing
    // asked for.
    if (stage.id == QLatin1String("frontend_guard") && !m_frontend.dbmCalibrated)
        aside = chainFrontendCalNoteText(m_frontend);
    m_detailOff->setVisible(!aside.isEmpty());
    if (!aside.isEmpty())
        setElided(m_detailOff, aside, kDetailTextWidth);

    setElided(m_detailLevels, chainLevelText(stage), kDetailTextWidth);

    // AUTO CLEAN's own inspector: the state+why line under its own detail
    // above, then its event history -- built by AetherGateChainAuto.cpp from
    // the governor block applyFilter() parsed off this same /filter body.
    const bool isAutoClean = stage.id == QLatin1String("auto_clean");
    m_detailAutoState->setVisible(isAutoClean);
    m_autoEvents->setVisible(isAutoClean);
    if (isAutoClean) {
        setElided(m_detailAutoState, chainAutoStateLine(m_governor), kDetailTextWidth);
        m_autoEvents->setText(chainAutoEventLines(m_governor).join(QLatin1Char('\n')));
    }
}

// Three states, and only three. The first build put refusals, set progress
// and connection on one line and the operator read none of it; a refusal now
// goes to the inspector and a set narrates itself under the mode row.
void AetherGateChainWindow::setLink(ChainLink link)
{
    m_link = link;
    QString text;
    switch (link) {
    case ChainLink::Live:     text = tr("live"); break;
    case ChainLink::Applying: text = tr("applying..."); break;
    case ChainLink::Gone:     text = tr("no connection"); break;
    }
    m_status->setText(text);
    m_status->setToolTip(tr("Whether the receiver is connected right now."));
    m_status->setAccessibleDescription(text);
    DiversityWidgets::setLive(m_status, link != ChainLink::Gone);
}

void AetherGateChainWindow::setNote(const QString& text)
{
    if (!m_detailNote)
        return;
    m_detailNote->setVisible(!text.isEmpty());
    setElided(m_detailNote, text, kDetailTextWidth);
}

void AetherGateChainWindow::setSetProgress(const QString& text)
{
    if (!m_setProgress)
        return;
    m_setProgress->setVisible(!text.isEmpty());
    setElided(m_setProgress, text, kTipWidth);
}

} // namespace AetherSDR
