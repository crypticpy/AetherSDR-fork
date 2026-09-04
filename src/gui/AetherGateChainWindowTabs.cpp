#include "gui/AetherGateChainWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainBypass.h"
#include "gui/AetherGateChainNow.h"
#include "gui/AetherGateChainPresets.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/AetherGateChainVisual.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

// The two tabs, the MODE/SETUP row above them, the door back from a mark on
// the picture, and the one place a gesture on the picture or a preset turns
// into writes. Split
// from AetherGateChainWindow.cpp because that file was 654 lines before any of
// this and AGENTS.md asks for files under 800 -- and because these things are
// one story: they are the surfaces that do not touch a tile and still have to
// go through the tile's write path.

namespace AetherSDR {

namespace {

// CHAIN is index 0 and VISUAL is index 1 everywhere: the tests name them, the
// automation bridge names them, and a magic 1 in three places is a bug waiting
// for somebody to add a third tab.
constexpr int kTabChain = 0;
constexpr int kTabVisual = 1;

// Mirrors AetherGateChainWindow.cpp's own kModes/kModeCount/kTipWidth: both
// files build parts of the same MODE row (this one the row itself, that one
// setMode()'s bookkeeping), and neither is worth a shared header symbol for
// three constants used inside one function apiece.
const ChainMode kModes[] = {ChainMode::Phone, ChainMode::Cw, ChainMode::Data};
constexpr int kModeCount = 3;
constexpr int kTipWidth = 1020;

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

} // namespace

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
        button->setToolTip(chainModeShortTip(mode));
        button->setAccessibleDescription(chainModeTip(mode));
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
        // What the button DOES, in the operator's terms, not the route count.
        const QString tip = writes.isEmpty()
                                ? tr("No set for data yet.")
                                : tr("Sets up the whole chain for %1. It changes "
                                     "one stage at a time and waits for the "
                                     "receiver after each one, so you can watch "
                                     "it happen on the diagram.")
                                      .arg(chainModeLabel(mode));
        button->setToolTip(
            writes.isEmpty()
                ? tip
                : tr("Writes every stage's default for %1, one at a time, "
                     "watching it land.")
                      .arg(chainModeLabel(mode)));
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

    box->addSpacing(14);

    // SETUP: the whole chain as the operator left it, saved under a name.
    // Design §2.6 moves this off the VISUAL tab and onto the MODE row, beside
    // SET UP FOR <mode> -- both are whole-chain actions and now read as the
    // pair they are. Its own widget, JSON store and "edited" comparison are
    // untouched; only where it lives and what its caption says have changed.
    m_presets = new AetherGateChainPresetBar(row);
    m_presets->setSource([this] { return m_strip ? m_strip->stages() : QList<ChainStage>(); },
                         [this] { return m_mode; });
    connect(m_presets, &AetherGateChainPresetBar::applyRequested, this,
            [this](const QList<ChainPresetWrite>& writes, const QString& name,
                   const QStringList& missing) {
                runSequence(writes, name, missing, /*isPreset=*/true);
            });
    box->addWidget(m_presets);

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

void AetherGateChainWindow::buildTabs(QVBoxLayout* root)
{
    // NOW, above the tabs: the one thing worth changing right now, per the
    // eight-case ladder AetherGateChainNow.cpp runs, on both CHAIN and
    // VISUAL. It replaces the read-only AUTO CLEAN banner this header used
    // to show -- that banner only ever said what the governor was doing;
    // NOW says what to do about it, with a button that writes.
    //
    // m_governor, m_autoCleanRow and m_frontend are already this window's
    // own members, kept current by applyFilter()/applyDevice()
    // (AetherGateChainWindow.cpp), which this header cannot be edited to
    // hook into directly -- but a lambda defined lexically inside a genuine
    // member function, even one whose body lives in this .cpp file, keeps
    // full access to `this`'s private members regardless. No new poll: this
    // timer only re-reads state /filter and /device already delivered, the
    // same 500 ms the banner it replaces re-read its own state at -- a held
    // tool's age (design §2.3 case 4) has to keep counting up between polls
    // that answer byte-identical bodies.
    m_now = new AetherGateChainNow(bodyWidget());
    connect(m_now, &AetherGateChainNow::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    connect(m_now, &AetherGateChainNow::stageLit, this, &AetherGateChainWindow::applyLitStage);

    // NOW shares its row with HEAR RAW at the right end -- the operator's
    // request, verbatim: "a little bypass button where we can temporarily
    // hear the signal without going through the chain" so they can A/B how
    // much the chain is doing. See AetherGateChainBypass.h for the button's
    // own contract; this is only where it lives on screen.
    auto* headerRow = new QWidget(bodyWidget());
    headerRow->setObjectName(QStringLiteral("gateChainHeaderRow"));
    headerRow->setAccessibleName(tr("What to do now, and HEAR RAW"));
    auto* headerBox = new QHBoxLayout(headerRow);
    headerBox->setContentsMargins(0, 0, 0, 0);
    headerBox->setSpacing(6);
    headerBox->addWidget(m_now, 1);
    m_hearRaw = new AetherGateChainHearRawButton(headerRow);
    connect(m_hearRaw, &AetherGateChainHearRawButton::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    headerBox->addWidget(m_hearRaw, 0);
    root->addWidget(headerRow);

    auto* nowTimer = new QTimer(this);
    nowTimer->setObjectName(QStringLiteral("gateChainNowTimer"));
    nowTimer->setInterval(500);
    connect(nowTimer, &QTimer::timeout, this, [this] {
        m_now->refresh(this->m_autoCleanRow, this->m_governor, this->m_frontend);
    });
    nowTimer->start();

    // MODE/SETUP, also above the tabs (design §2.5): the two whole-chain
    // actions -- set up for a mode, and load a saved chain -- have to read
    // the same on VISUAL as on CHAIN, and a row built inside the CHAIN tab
    // alone would vanish the moment the operator flipped to the picture.
    buildModeRow(root);

    m_tabs = new QTabWidget(bodyWidget());
    m_tabs->setObjectName(QStringLiteral("gateChainTabs"));
    m_tabs->setAccessibleName(tr("Chain or picture"));
    m_tabs->setDocumentMode(false);

    // ---- CHAIN: the scrolling diagram, under the MODE row above.
    auto* chainTab = new QWidget(m_tabs);
    chainTab->setObjectName(QStringLiteral("gateChainTabChain"));
    chainTab->setAccessibleName(tr("The chain as a diagram"));
    auto* chainBox = new QVBoxLayout(chainTab);
    chainBox->setContentsMargins(0, 6, 0, 0);
    chainBox->setSpacing(6);

    // Everything below the mode row scrolls, so the window can be dragged
    // smaller than its natural content height without a card becoming
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

    // The FRONT END card's OPEN PANEL button lives inside the strip, which
    // this window does not own the header of; a plain QPushButton with no
    // signal of its own to wire, found the same way AetherGateChainStrip.h's
    // own widgets already are (findChild by objectName) rather than adding
    // one to a header this task does not touch.
    if (QPushButton* openPanel = m_strip->findChild<QPushButton*>(
            QStringLiteral("gateChainOpenPanelButton"))) {
        connect(openPanel, &QPushButton::clicked, this,
                &AetherGateChainWindow::openPanelRequested);
    }

    buildInspector(hostBox, host);
    hostBox->addStretch(1);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName(QStringLiteral("gateChainScroll"));
    m_scroll->setWidget(host);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    chainBox->addWidget(m_scroll, 1);

    m_tabs->addTab(chainTab, tr("CHAIN"));
    m_tabs->setTabToolTip(kTabChain,
                          tr("Every stage between the antenna and your ears, in "
                             "the order the signal goes through them."));

    // ---- VISUAL: the passband as a picture at the width of the window.
    // SETUP used to sit here as its own row -- design §2.6 moved it onto the
    // MODE row above the tabs instead, beside SET UP FOR <mode>, freeing this
    // whole row's height back to the picture.
    auto* visualTab = new QWidget(m_tabs);
    visualTab->setObjectName(QStringLiteral("gateChainTabVisual"));
    visualTab->setAccessibleName(tr("The filter as a picture"));
    auto* visualBox = new QVBoxLayout(visualTab);
    visualBox->setContentsMargins(0, 6, 0, 0);
    visualBox->setSpacing(6);

    m_visual = new AetherGateChainVisual(visualTab);
    connect(m_visual, &AetherGateChainVisual::requestWrite, this,
            &AetherGateChainWindow::onWriteRequested);
    connect(m_visual, &AetherGateChainVisual::requestSequence, this,
            [this](const QList<ChainPresetWrite>& writes, const QString& name) {
                runSequence(writes, name, QStringList(), /*isPreset=*/false);
            });
    connect(m_visual, &AetherGateChainVisual::stageRequested, this,
            &AetherGateChainWindow::jumpToStage);
    visualBox->addWidget(m_visual, 1);

    m_tabs->addTab(visualTab, tr("VISUAL"));
    m_tabs->setTabToolTip(kTabVisual,
                          tr("The passband drawn over what is actually arriving. "
                             "Drag the edges, double-click to notch, right-click "
                             "a notch to take it away, click any mark to go to "
                             "its stage."));

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        refreshVisualActive();
        // The diagram was withheld while VISUAL was in front (applyFilter()'s
        // own skip), but m_filterStages itself stayed current the whole
        // time -- the parse and the presets bar's "edited" comparison are
        // never gated on tab. So the moment CHAIN comes back to front, a
        // plain refreshStrip() off what is already parsed is the whole
        // rebuild; nothing needs re-fetching or re-parsing.
        if (index == kTabChain && !m_filterStages.isEmpty())
            refreshStrip();
    });
    root->addWidget(m_tabs, 1);
}

// NOW's own stageLit(id) handler: walks every tile currently on the diagram
// and sets the "lit" property to match `id`, repolishing only the tiles
// whose property actually changed. A fresh walk each call rather than
// remembering the last id and clearing just that one tile, because
// AetherGateChainStrip::setStages() throws old tiles away and builds new
// ones whenever a row's shape changes -- a remembered id could be pointing
// at a QFrame that no longer exists on screen.
void AetherGateChainWindow::applyLitStage(const QString& id)
{
    if (!m_strip)
        return;
    for (int i = 0; i < m_strip->tileCount(); ++i) {
        AetherGateChainTile* t = m_strip->tileAt(i);
        if (!t)
            continue;
        const bool lit = !id.isEmpty() && t->id() == id;
        if (t->property("lit").toBool() == lit)
            continue;
        t->setProperty("lit", lit);
        t->style()->unpolish(t);
        t->style()->polish(t);
    }
}

// The door back from the picture. The card is selected -- which fills the
// inspector -- and scrolled into view, because a stage in the second PASSBAND
// column of a window dragged short can be below the fold.
void AetherGateChainWindow::jumpToStage(const QString& id)
{
    if (!m_strip || !m_strip->tile(id))
        return;
    setCurrentTab(kTabChain);
    m_strip->selectStage(id);
    if (m_scroll)
        m_scroll->ensureWidgetVisible(m_strip->tile(id));
    m_strip->setFocus(Qt::OtherFocusReason);
}

int AetherGateChainWindow::currentTab() const
{
    return m_tabs ? m_tabs->currentIndex() : kTabChain;
}

void AetherGateChainWindow::setCurrentTab(int index)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(index);
}

// The picture is fed only when a human could be looking at it. Everything else
// -- a closed window, a minimised one, the CHAIN tab in front -- is a poll that
// costs a JSON walk, a fingerprint and a repaint for nobody.
void AetherGateChainWindow::refreshVisualActive()
{
    // A minimised window is still isVisible() on most platforms -- Qt's own
    // definition of visible is "would be shown if raised", not "on screen
    // right now" -- so isMinimized() is checked too: nobody is looking at a
    // minimised window's VISUAL tab either.
    if (m_visual)
        m_visual->setActive(isVisible() && !isMinimized() && currentTab() == kTabVisual);
}

void AetherGateChainWindow::showEvent(QShowEvent* ev)
{
    PersistentDialog::showEvent(ev);
    refreshVisualActive();
}

void AetherGateChainWindow::hideEvent(QHideEvent* ev)
{
    QDialog::hideEvent(ev);
    refreshVisualActive();
    // Covers a plain hide(), a close(), AND Escape -- QDialog's own default
    // keyPressEvent rejects (closes, which hides) on an unhandled Escape, so
    // this one hook is also where that release happens.
    if (m_hearRaw)
        m_hearRaw->releaseIfHeld();
}

void AetherGateChainWindow::changeEvent(QEvent* ev)
{
    PersistentDialog::changeEvent(ev);
    // The window is still VISIBLE here -- alt-tabbed away, or another window
    // raised over it -- which hideEvent() above does not see at all.
    if (ev->type() == QEvent::ActivationChange && !isActiveWindow() && m_hearRaw)
        m_hearRaw->releaseIfHeld();
    // Minimising or restoring does not fire showEvent()/hideEvent() either --
    // it is neither shown nor hidden, Qt's own state machine calls it a
    // WindowStateChange -- and refreshVisualActive() is what turns a
    // minimised VISUAL tab's feed off.
    if (ev->type() == QEvent::WindowStateChange)
        refreshVisualActive();
}

// A preset, or a gesture that is genuinely two writes. Either way it goes into
// the same sequencer the mode sets use: one write at a time, each waited for,
// with the same settling window and the same refusals on the same tiles.
void AetherGateChainWindow::runSequence(const QList<ChainPresetWrite>& writes,
                                        const QString& name, const QStringList& missing,
                                        bool isPreset)
{
    // A stage the gate no longer has is said out loud rather than failing the
    // load. An operator who saved a preset on the dual-tuner pair and loaded it
    // on one tuner has not done anything wrong.
    const QString skipped =
        missing.isEmpty() ? QString()
                          : tr("this receiver has no %1 — the rest was set")
                                .arg(missing.join(QStringLiteral(", ")));
    if (writes.isEmpty()) {
        setNote(skipped);
        setSetProgress(missing.isEmpty() ? QString() : tr("nothing to set"));
        return;
    }
    // Only a PRESET's own writes are exempt from marking the menu edited. A
    // notch move is two writes and is still the operator's own hand.
    m_loadingPreset = isPreset;
    setSetProgress(tr("%1...").arg(name));
    setLink(ChainLink::Applying);
    m_preset->start(writes, name);
    // AFTER the first step is on the wire, not before: every write clears the
    // note, so a skip said first would be wiped by the write that followed it.
    setNote(skipped);
}

} // namespace AetherSDR
