#include "gui/AetherGateChainWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainAuto.h"
#include "gui/AetherGateChainBypass.h"
#include "gui/AetherGateChainNow.h"
#include "gui/AetherGateChainPresets.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/AetherGateChainVisual.h"
#include "gui/DiversityWindowPanels.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMargins>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The frame. The initial size is the one the whole layout is arithmetic for:
// the four groups measure 1094 px across (236 + 196 + 400 + 196, plus three
// 22 px arrow gutters) and the diagram plus the pane clear 820 px of
// height, so NOTHING scrolls when the window first opens. The minimum is
// smaller on purpose -- below the initial size the scroll area is what keeps
// every stage reachable.
constexpr int kMinWidth = 960;
constexpr int kMinHeight = 560;
constexpr int kInitialWidth = 1120;
constexpr int kInitialHeight = 820;

// The pane's sentence fields. Fixed, because a label that grew with its
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

// Mirrors AetherGateChainWindowTabs.cpp's own kTabChain: two translation
// units of one class, and currentTab()'s contract ("0 CHAIN, 1 VISUAL") is
// the header's own doc comment, not something worth a shared header symbol
// for one comparison.
constexpr int kTabChain = 0;

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

const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }"
    "QLabel[live=\"false\"] { color: {{color.text.disabled}}; }";

void setElided(QLabel* label, const QString& text, int width)
{
    const QFontMetrics fm(label->font());
    label->setText(fm.elidedText(text, Qt::ElideRight, width));
    // H1's 90-char tooltip rule; the full sentence still reaches a screen
    // reader below.
    label->setToolTip(text.length() > 90 ? text.left(87) + QStringLiteral("…") : text);
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

// A stage that has left the chain entirely (the gate stopped sending it), or
// a write the gate never answered, would otherwise keep its settling-window
// entry for ever; this is what expires it. Called on every valid poll --
// see applyFilter() -- not just the ones that go on to rebuild the diagram.
void AetherGateChainWindow::prunePendingWrites()
{
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->age.elapsed() > kSettleMs)
            it = m_pending.erase(it);
        else
            ++it;
    }
}

// One body's rows, with every stage still inside its settling window held at
// the setting the strip already shows. Nothing optimistic happens here: the
// held setting is the one a gate answer put there, not the one that was asked
// for.
QList<ChainStage> AetherGateChainWindow::holdPendingStages(const QList<ChainStage>& fresh)
{
    if (m_pending.isEmpty())
        return fresh;
    prunePendingWrites();
    QList<ChainStage> out = fresh;
    for (ChainStage& row : out) {
        auto it = m_pending.find(row.id);
        if (it == m_pending.end())
            continue;
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
    if (filter.isEmpty()) {
        // A poll that did not answer. The picture says so -- setStale(true)
        // -- rather than being blanked: what is on screen was true a moment
        // ago. The strip itself is left exactly where it was for the same
        // reason.
        if (m_visual)
            m_visual->setStale(true);
        return;
    }
    // Any other body is the gate answering, refusal or not -- not stale.
    if (m_visual)
        m_visual->setStale(false);
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

    // Whatever this body says about `bypass`, including nothing at all --
    // which is what hides the button for an older gate. See AetherGateChainBypass.h.
    if (m_hearRaw)
        m_hearRaw->applyFilter(filter);
    // VISUAL gates its own repaint on setActive()/dragging(); feeding it
    // every body regardless of what the CHAIN diagram below does is what
    // lets it catch up the instant the operator switches tabs.
    if (m_visual)
        m_visual->applyFilter(filter);

    // The governor block and the write-settling machinery are read every
    // 500 ms by the AUTO CLEAN banner and drive the preset stepper -- both
    // have to stay current regardless of which tab is in front, so neither
    // is behind the skip below.
    m_governor = chainAutoParseGovernor(filter);
    if (m_preset->running())
        m_preset->noteFilterBody();
    else
        setLink(ChainLink::Live);

    // A body value-identical to the one this window already drew changes
    // nothing at all: skip both the parse below and the diagram rebuild it
    // feeds. Compared as parsed JSON, which is what this method is handed --
    // QJsonObject's own operator== is a deep, order-independent compare, so
    // two gate replies read equal here exactly when their bodies were
    // byte-identical.
    const bool unchanged = filter == m_lastFilterBody;
    m_lastFilterBody = filter;
    // A control's settling window has to expire on its own clock even when
    // nothing else in the body is moving -- a write the gate never answered
    // must not stay grey forever just because the rest of the status held
    // still. Cheap either way (a handful of hash entries and existing
    // tiles), so it runs whether or not the rest below does.
    prunePendingWrites();
    applyBusyToTiles();
    if (unchanged)
        return;

    // The parse and the presets bar's "edited" comparison are a JSON walk
    // and a handful of string compares -- not the widget rebuild below --
    // so both run on every body that actually changed, regardless of which
    // tab is in front. A preset that drifted while the operator was looking
    // at VISUAL must still say so the instant they look back at the
    // sidebar, not only once they flip back to CHAIN.
    bool fromGate = false;
    m_filterStages = chainFromFilter(filter, &fromGate, &m_autoCleanRow);
    m_fromGate = fromGate;
    const QList<ChainStage> stages = mergedStages();
    // Held against the preset in force -- AFTER the sequencer has seen the
    // body, because the last step's own answer is what ends a load, and a
    // chain compared while the load was still running would read as edited
    // by its own hand.
    if (m_presets && !m_loadingPreset)
        m_presets->noteRows(stages);

    // The diagram is CHAIN-tab work: rebuilding it while VISUAL is in front
    // competes with the picture's own paint for nothing anybody can see.
    // VISUAL already gates itself this way (setActive()/applyFilter() above);
    // this mirrors it. m_filterStages is current regardless, so the moment
    // the tab flips back to CHAIN the tab-changed handler in
    // AetherGateChainWindowTabs.cpp calls refreshStrip() straight off it --
    // no re-fetch, no re-parse.
    // Either skip leaves the diagram behind the body, so the body is not
    // recorded as drawn: the next poll, identical or not, gets to rebuild.
    // NOW reads m_autoCleanRow/m_governor directly, so it is current the
    // instant a poll lands rather than waiting on its own 500 ms ticker
    // (AetherGateChainWindowTabs.cpp's gateChainNowTimer, which only exists
    // to keep a held tool's age climbing between polls) -- refreshed on both
    // skip paths below, and NOW shows on both tabs regardless.
    if (currentTab() != kTabChain) {
        m_lastFilterBody = QJsonObject();
        if (m_now)
            m_now->refresh(m_autoCleanRow, m_governor, m_frontend);
        return;
    }
    // The operator's hand is on the picture on the OTHER tab, but this
    // window has one diagram: a rebuild landing mid-drag would be exactly as
    // unwelcome here as it would be under the pointer.
    if (m_visual && m_visual->dragging()) {
        m_lastFilterBody = QJsonObject();
        if (m_now)
            m_now->refresh(m_autoCleanRow, m_governor, m_frontend);
        return;
    }
    applyChainBody(stages);
    // NOW's lit-row signal walks the strip's tiles (applyLitStage()), so it
    // has to fire AFTER applyChainBody() has rebuilt them -- refreshing NOW
    // any earlier lights a tile that setStages()'s rebuild() is about to
    // replace out from under it.
    if (m_now)
        m_now->refresh(m_autoCleanRow, m_governor, m_frontend);
}

// Turns already-merged stages into strip/tile updates -- the part of a
// refresh that actually touches widgets, and so the only part gated on
// CHAIN being the front tab and the picture not being dragged.
void AetherGateChainWindow::applyChainBody(const QList<ChainStage>& stages)
{
    m_strip->setStages(stages);
    m_strip->setFrontendCalNote(m_frontend.available && !m_frontend.dbmCalibrated,
                                chainFrontendCalNoteText(m_frontend));
    chainAutoApplyNotes(m_strip, m_governor);
    applyBusyToTiles();
    showStage(m_strip->selectedId());
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
    // See the matching comment in applyFilter(): NOW has to refresh AFTER
    // the strip's tiles are current, or its lit-row signal lands on tiles
    // refreshStrip()'s rebuild() is about to throw away.
    if (m_now)
        m_now->refresh(m_autoCleanRow, m_governor, m_frontend);
}

// What applyFilter() and applyDevice() both need built before either the
// strip or the presets bar can be told anything: merge the gate's own
// chain[] rows with the frontend guard's two synthetic ones (when the guard
// is available at all), and hold anything still inside its settling window.
QList<ChainStage> AetherGateChainWindow::mergedStages()
{
    QList<ChainStage> merged = m_filterStages;
    const QList<ChainStage> frontendRows = chainFrontendRows(m_frontend);
    if (!frontendRows.isEmpty()) {
        const int at = chainFrontEndSpan(merged);
        for (int i = 0; i < frontendRows.size(); ++i)
            merged.insert(at + i, frontendRows.at(i));
    }
    return holdPendingStages(merged);
}

// The strip half of the merge above: hands mergedStages() to the strip and
// the pane. applyDevice() always calls this (a /device poll is not the
// 2 Hz one); applyFilter() only calls it once the CHAIN-tab/dragging gates
// in its own comment have passed.
QList<ChainStage> AetherGateChainWindow::refreshStrip()
{
    const QList<ChainStage> stages = mergedStages();
    applyChainBody(stages);
    return stages;
}

// See the declaration: this window has no use for the engine, it only
// reaches VISUAL through it.
void AetherGateChainWindow::setAudioEngine(AudioEngine* audio)
{
    if (m_visual)
        m_visual->setAudioEngine(audio);
}

void AetherGateChainWindow::setPresent(bool present)
{
    if (m_present == present)
        return;
    m_present = present;
    if (m_hearRaw)
        m_hearRaw->setPresent(present);
    if (m_visual)
        m_visual->setPresent(present);
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
    m_autoCleanRow = ChainStage();
    if (m_now)
        m_now->refresh(m_autoCleanRow, m_governor, m_frontend);
    // A gate gone -- the next reconnect's first body must rebuild the strip
    // even if it happens to match, byte for byte, whatever was on screen
    // before the gate dropped: the strip below was just cleared.
    m_lastFilterBody = QJsonObject();
    m_strip->clear();
    m_strip->setFrontendCalNote(false, QString());
    if (m_visual)
        m_visual->clear();
    m_fromGate = false;
    setSetProgress(QString());
    showStage(QString());
    setLink(ChainLink::Gone);
}

// Three states, and only three. The first build put refusals, set progress
// and connection on one line and the operator read none of it; a refusal now
// goes to the pane and a set narrates itself under the mode row.
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
