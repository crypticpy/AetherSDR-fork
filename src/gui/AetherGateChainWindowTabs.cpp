#include "gui/AetherGateChainWindow.h"

#include "gui/AetherGateChainPresets.h"
#include "gui/AetherGateChainStrip.h"
#include "gui/AetherGateChainVisual.h"
#include "gui/DiversityWindowPanels.h"

#include <QFrame>
#include <QHideEvent>
#include <QLabel>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

// The two tabs, the PRESETS row, the door back from a mark on the picture, and
// the one place a gesture on the picture or a preset turns into writes. Split
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

} // namespace

void AetherGateChainWindow::buildTabs(QVBoxLayout* root)
{
    // B25 AUTO CLEAN's read-only header, above the tabs: the third of the
    // three surfaces docs/DIVERSITY.md's "AUTO CLEAN: the chain decides"
    // asks the operator be able to SEE it on (the other two carry the
    // switch itself -- gui/AetherGateDiversityPanel.cpp's sidebar toggle and
    // gui/DiversityFlowStripAuto.cpp's FLOW banner). This window has no
    // write path of its own for /diversity/set, so it is read-only here;
    // the tooltip below says where to turn it on or off.
    //
    // m_governor is already this window's own member, kept current by
    // applyFilter() (AetherGateChainWindow.cpp), which this header cannot
    // be edited to hook into directly -- but a lambda defined lexically
    // inside a genuine member function, even one whose body lives in this
    // .cpp file, keeps full access to `this`'s private members regardless.
    // No new poll: this timer only re-reads state /filter already delivered.
    auto* autoBanner = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainAutoCleanBanner"),
        QStringLiteral("AUTO CLEAN ON · settling · mains/squeeze backing off until 12:46"),
        tr("The chain's own governor -- read-only here. Turn it on or off "
           "from the Diversity window or the sidebar's own AUTO CLEAN switch."),
        bodyWidget());
    autoBanner->setAccessibleName(tr("AUTO CLEAN status"));
    // The gate's own `why` has no true worst case -- same Ignored treatment
    // the sidebar's and FLOW strip's own AUTO CLEAN widgets carry, so a long
    // one clips instead of pushing this window's minimum width past the
    // 1120 it opens at.
    autoBanner->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    autoBanner->setMinimumWidth(0);
    autoBanner->setVisible(false);
    root->addWidget(autoBanner);

    auto* autoTimer = new QTimer(this);
    autoTimer->setObjectName(QStringLiteral("gateChainAutoCleanBannerTimer"));
    autoTimer->setInterval(500);
    connect(autoTimer, &QTimer::timeout, this, [this, autoBanner] {
        const QString indicator = chainAutoIndicatorLine(this->m_governor);
        autoBanner->setVisible(!indicator.isEmpty());
        if (!indicator.isEmpty())
            autoBanner->setText(indicator);
        DiversityWidgets::setLive(autoBanner, !indicator.isEmpty());
    });
    autoTimer->start();

    m_tabs = new QTabWidget(bodyWidget());
    m_tabs->setObjectName(QStringLiteral("gateChainTabs"));
    m_tabs->setAccessibleName(tr("Chain or picture"));
    m_tabs->setDocumentMode(false);

    // ---- CHAIN: the mode row and the scrolling diagram.
    auto* chainTab = new QWidget(m_tabs);
    chainTab->setObjectName(QStringLiteral("gateChainTabChain"));
    chainTab->setAccessibleName(tr("The chain as a diagram"));
    auto* chainBox = new QVBoxLayout(chainTab);
    chainBox->setContentsMargins(0, 6, 0, 0);
    chainBox->setSpacing(6);

    buildModeRow(chainBox);

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

    // ---- VISUAL: the presets row, then the passband as a picture at the
    // width of the window. The operator's words: "flip over to the visual
    // filter screen where you can see presets". PRESETS is a whole chain in
    // one press, exactly what the picture shows the effect of, and it sits
    // over the picture rather than over the diagram for that reason.
    auto* visualTab = new QWidget(m_tabs);
    visualTab->setObjectName(QStringLiteral("gateChainTabVisual"));
    visualTab->setAccessibleName(tr("The filter as a picture, and the presets"));
    auto* visualBox = new QVBoxLayout(visualTab);
    visualBox->setContentsMargins(0, 6, 0, 0);
    visualBox->setSpacing(6);

    m_presets = new AetherGateChainPresetBar(visualTab);
    m_presets->setSource([this] { return m_strip ? m_strip->stages() : QList<ChainStage>(); },
                         [this] { return m_mode; });
    connect(m_presets, &AetherGateChainPresetBar::applyRequested, this,
            [this](const QList<ChainPresetWrite>& writes, const QString& name,
                   const QStringList& missing) {
                runSequence(writes, name, missing, /*isPreset=*/true);
            });
    visualBox->addWidget(m_presets);

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
                          tr("The passband drawn over what is actually arriving, "
                             "and your saved presets. Drag the edges, "
                             "double-click to notch, right-click a notch to take "
                             "it away, click any mark to go to its stage."));

    connect(m_tabs, &QTabWidget::currentChanged, this,
            [this](int) { refreshVisualActive(); });
    root->addWidget(m_tabs, 1);
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
    if (m_visual)
        m_visual->setActive(isVisible() && currentTab() == kTabVisual);
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
