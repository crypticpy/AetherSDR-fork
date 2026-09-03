#include "gui/AetherGateChainStrip.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

constexpr int kCardSpacing = 8;

// The gap an arrow sits in between two groups. Four columns and three gutters
// come to 1088 px at the initial width, which is inside the 1104 px of body
// the window has at 1120 with its margins taken off -- so the whole diagram is
// on screen the moment the window opens and nothing scrolls.
constexpr int kArrowGutter = 22;

// How far down the arrow sits: level with the first card, not level with the
// group caption above it.
constexpr int kArrowDrop = 30;

// PASSBAND is the long group. Up to seven stages it stays one column, which
// keeps it the same shape as PAIR; past that it splits in two so the diagram
// does not grow a scrollbar.
constexpr int kPassbandSplitAt = 7;

constexpr int kFoldColumns = 4;

// What the FRONT END card's cal note has to fit inside: the card's own
// width, less the same left/right padding the hint line under it already
// budgets for.
constexpr int kFrontEndCardTextWidth = kChainSummaryWidth - 14;

// The four groups, in signal order, which is also left-to-right.
const ChainGroup kGroups[] = {ChainGroup::FrontEnd, ChainGroup::Pair,
                              ChainGroup::Passband, ChainGroup::Out};
constexpr int kGroupCount = 4;

// The header of the collapsed fold. Its own style rather than a toggle button
// because it is a disclosure, not a switch on the receiver: nothing about it
// reaches the radio.
const char* kFoldStyle =
    "QPushButton { color: {{color.text.secondary}}; font-size: 10px;"
    " font-weight: bold; background: transparent; border: none;"
    " text-align: left; padding: 2px 0px; }"
    "QPushButton:hover { color: {{color.accent.bright}}; }";

int groupIndex(ChainGroup group)
{
    for (int i = 0; i < kGroupCount; ++i) {
        if (kGroups[i] == group)
            return i;
    }
    return 0;
}

} // namespace

AetherGateChainStrip::AetherGateChainStrip(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gateChainStrip"));
    setAccessibleName(tr("Filter chain, in signal order"));
    // The strip itself takes focus, so the arrow keys have somewhere to land
    // that is not one particular card's switch.
    setFocusPolicy(Qt::StrongFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    buildColumns(root);

    // The stages the mode does not reach for. Collapsed by default and never
    // empty of meaning: an APF on phone is not broken, it is not what you
    // reach for.
    m_foldToggle = new QPushButton(this);
    m_foldToggle->setObjectName(QStringLiteral("gateChainNotForModeToggle"));
    m_foldToggle->setAccessibleName(tr("Show the stages this mode does not use"));
    m_foldToggle->setToolTip(tr("The stages this mode does not normally reach "
                                "for. They are still running and still "
                                "switchable: the mode chooses what is on the "
                                "diagram, it does not turn anything off."));
    m_foldToggle->setAccessibleDescription(m_foldToggle->toolTip());
    m_foldToggle->setCursor(Qt::PointingHandCursor);
    m_foldToggle->setCheckable(true);
    ThemeManager::instance().applyStyleSheet(m_foldToggle,
                                             QString::fromLatin1(kFoldStyle));
    connect(m_foldToggle, &QPushButton::clicked, this, [this] { relayout(); });
    root->addWidget(m_foldToggle);

    m_fold = new QWidget(this);
    m_fold->setObjectName(QStringLiteral("gateChainNotForMode"));
    m_fold->setAccessibleName(tr("Stages this mode does not use"));
    m_foldGrid = new QGridLayout(m_fold);
    m_foldGrid->setContentsMargins(0, 0, 0, 0);
    m_foldGrid->setHorizontalSpacing(kCardSpacing);
    m_foldGrid->setVerticalSpacing(kCardSpacing);
    m_fold->setVisible(false);
    root->addWidget(m_fold);
}

// Four labelled columns and the three arrows between them. Built once; only
// the cards inside them ever change.
void AetherGateChainStrip::buildColumns(QVBoxLayout* root)
{
    auto* rowHost = new QWidget(this);
    rowHost->setObjectName(QStringLiteral("gateChainGroups"));
    rowHost->setAccessibleName(tr("The chain, in four groups"));
    auto* row = new QHBoxLayout(rowHost);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    for (int i = 0; i < kGroupCount; ++i) {
        const ChainGroup group = kGroups[i];
        const QString id = chainGroupId(group);

        if (i > 0) {
            // The one mark that says this is a chain and not a grid. It is
            // named for the group it LEAVES, so the name reads the way the
            // signal travels.
            auto* arrow = DiversityWidgets::makeValue(
                QStringLiteral("gateChainArrow_") + chainGroupId(kGroups[i - 1]),
                QStringLiteral("→"), rowHost);
            arrow->setText(QStringLiteral("→"));
            arrow->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            arrow->setContentsMargins(0, kArrowDrop, 0, 0);
            arrow->setFixedWidth(kArrowGutter);
            arrow->setAccessibleName(tr("then"));
            arrow->setToolTip(tr("The signal goes this way."));
            arrow->setAccessibleDescription(arrow->toolTip());
            row->addWidget(arrow, 0, Qt::AlignTop);
        }

        Column& column = m_columns[i];
        column.host = new QWidget(rowHost);
        column.host->setObjectName(QStringLiteral("gateChainGroup_") + id);
        column.host->setAccessibleName(chainGroupLabel(group));
        auto* box = new QVBoxLayout(column.host);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(4);

        auto* caption = DiversityWidgets::makeCaption(chainGroupLabel(group),
                                                      column.host);
        caption->setObjectName(QStringLiteral("gateChainGroupCaption_") + id);
        caption->setAccessibleName(chainGroupLabel(group));
        caption->setToolTip(chainGroupTip(group));
        caption->setAccessibleDescription(caption->toolTip());
        box->addWidget(caption);

        if (group == ChainGroup::FrontEnd) {
            // ONE card, a line per item, and a single hint underneath. Seven
            // tiles that each said "set on the setup page" was the operator's
            // own complaint: six dead tiles taking a quarter of the window to
            // say one thing once.
            auto* card = new QFrame(column.host);
            card->setObjectName(QStringLiteral("gateChainFrontEndCard"));
            card->setAccessibleName(chainGroupLabel(group));
            card->setToolTip(chainGroupTip(group));
            card->setAccessibleDescription(card->toolTip());
            card->setFixedWidth(kChainSummaryWidth);
            auto* inner = new QVBoxLayout(card);
            inner->setContentsMargins(7, 5, 7, 5);
            inner->setSpacing(3);

            column.body = new QWidget(card);
            column.body->setObjectName(QStringLiteral("gateChainFrontEndRows"));
            column.body->setAccessibleName(tr("What the receiver does first"));
            column.grid = new QGridLayout(column.body);
            column.grid->setContentsMargins(0, 0, 0, 0);
            column.grid->setHorizontalSpacing(kCardSpacing);
            column.grid->setVerticalSpacing(1);
            inner->addWidget(column.body);

            column.hint = DiversityWidgets::makeFieldLabel(
                tr("THE REST IS SET ON THE SETUP PAGE"), card);
            column.hint->setObjectName(QStringLiteral("gateChainFrontEndHint"));
            column.hint->setAccessibleName(tr("Where the front end is set"));
            column.hint->setToolTip(tr("The antenna port, the traps, the gain and "
                                       "the sample rate all belong to the setup "
                                       "page. GUARD is the one control on this "
                                       "card."));
            column.hint->setAccessibleDescription(column.hint->toolTip());
            inner->addWidget(column.hint);

            // The B23 linearity guard's one caveat: a guard-moved LNA state
            // breaks the gate's own dBm calibration. Built once, kept in the
            // warning tone permanently (setLive(true), not the ordinary
            // role -- this is only ever shown when something needs saying)
            // and hidden until setFrontendCalNote() has something to show.
            column.calNote = DiversityWidgets::makeReadoutLine(
                QStringLiteral("gateChainFrontendCalNote"), QString(), QString(),
                card);
            column.calNote->setAccessibleName(tr("dBm calibration note"));
            DiversityWidgets::setLive(column.calNote, true);
            column.calNote->setVisible(false);
            inner->addWidget(column.calNote);
            box->addWidget(card);
        } else {
            column.body = new QWidget(column.host);
            column.body->setObjectName(QStringLiteral("gateChainGroupBody_") + id);
            column.body->setAccessibleName(chainGroupLabel(group));
            column.grid = new QGridLayout(column.body);
            column.grid->setContentsMargins(0, 0, 0, 0);
            column.grid->setHorizontalSpacing(kCardSpacing);
            column.grid->setVerticalSpacing(kCardSpacing);
            box->addWidget(column.body);
        }
        box->addStretch(1);
        row->addWidget(column.host, 0, Qt::AlignTop);
    }
    row->addStretch(1);
    root->addWidget(rowHost);
}

AetherGateChainStrip::Column& AetherGateChainStrip::columnFor(ChainGroup group)
{
    return m_columns[groupIndex(group)];
}

AetherGateChainTile* AetherGateChainStrip::tileAt(int index) const
{
    return (index >= 0 && index < m_tiles.size()) ? m_tiles.at(index) : nullptr;
}

AetherGateChainTile* AetherGateChainStrip::tile(const QString& id) const
{
    for (AetherGateChainTile* t : m_tiles) {
        if (t->id() == id)
            return t;
    }
    return nullptr;
}

QList<AetherGateChainTile*> AetherGateChainStrip::tilesInMode() const
{
    QList<AetherGateChainTile*> out;
    for (AetherGateChainTile* t : m_tiles) {
        if (chainStageInMode(t->id(), m_mode))
            out.append(t);
    }
    return out;
}

void AetherGateChainStrip::setStages(const QList<ChainStage>& stages)
{
    QString shape;
    for (const ChainStage& stage : stages)
        shape += stage.shape() + QLatin1Char('\n');

    m_stages = stages;
    if (shape != m_shape) {
        m_shape = shape;
        rebuild();
    } else {
        for (int i = 0; i < m_tiles.size() && i < m_stages.size(); ++i)
            m_tiles.at(i)->setStage(m_stages.at(i));
    }

    if (m_selected.isEmpty() && !m_stages.isEmpty())
        selectStage(m_stages.first().id);
    else if (!tile(m_selected) && !m_stages.isEmpty())
        selectStage(m_stages.first().id);
}

void AetherGateChainStrip::setFrontendCalNote(bool show, const QString& text)
{
    QLabel* note = columnFor(ChainGroup::FrontEnd).calNote;
    if (!note)
        return;
    note->setVisible(show);
    if (!show)
        return;
    note->setText(chainFitToWidth(note, text, kFrontEndCardTextWidth));
    note->setToolTip(text);
    note->setAccessibleDescription(text);
}

void AetherGateChainStrip::setMode(ChainMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    // A card's shape depends on its group, not on the mode, so nothing has to
    // be rebuilt: only where each one sits changes.
    relayout();
    // A selection that has just dropped into the collapsed fold is a
    // selection nobody can see. Move it to the first stage the mode DOES show.
    const QList<AetherGateChainTile*> mine = tilesInMode();
    if (mine.isEmpty())
        return;
    for (AetherGateChainTile* t : mine) {
        if (t->id() == m_selected)
            return;
    }
    selectStage(mine.first()->id());
}

void AetherGateChainStrip::clear()
{
    m_stages.clear();
    m_shape.clear();
    m_selected.clear();
    rebuild();
}

// A card in FRONT END is a LINE inside the summary card; everywhere else it is
// a card of its own. That is the only thing the group decides about a stage.
void AetherGateChainStrip::rebuild()
{
    for (AetherGateChainTile* t : m_tiles)
        t->deleteLater();
    m_tiles.clear();

    ChainGroup previous = ChainGroup::FrontEnd;
    for (int i = 0; i < m_stages.size(); ++i) {
        const ChainGroup group = chainStageGroup(m_stages.at(i).id, previous);
        previous = group;
        const ChainTileShape shape = group == ChainGroup::FrontEnd
                                         ? ChainTileShape::Line
                                         : ChainTileShape::Card;
        auto* t = new AetherGateChainTile(m_stages.at(i), shape, this);
        connect(t, &AetherGateChainTile::clicked, this, [this](const QString& id) {
            setFocus(Qt::MouseFocusReason);
            selectStage(id);
        });
        connect(t, &AetherGateChainTile::requestWrite, this,
                &AetherGateChainStrip::requestWrite);
        m_tiles.append(t);
    }
    relayout();
}

// Every grid, from scratch, in gate order. Cheap enough to do on a mode change
// and on a rebuild, and there is no second source of truth about which card is
// where.
void AetherGateChainStrip::relayout()
{
    const auto drain = [](QGridLayout* grid) {
        while (QLayoutItem* item = grid->takeAt(0))
            delete item;
    };
    for (Column& column : m_columns)
        drain(column.grid);
    drain(m_foldGrid);

    // Pass one: which group each card is in, and how many each ends up with.
    // PASSBAND cannot be placed until its total is known, because that total
    // is what decides whether it is one column or two.
    QList<ChainGroup> groups;
    groups.reserve(m_tiles.size());
    for (Column& column : m_columns)
        column.count = 0;
    ChainGroup previous = ChainGroup::FrontEnd;
    for (AetherGateChainTile* t : m_tiles) {
        const ChainGroup group = chainStageGroup(t->id(), previous);
        previous = group;
        groups.append(group);
        if (chainStageInMode(t->id(), m_mode))
            ++columnFor(group).count;
    }

    const int passbandTotal = columnFor(ChainGroup::Passband).count;
    const int passbandCols = passbandTotal > kPassbandSplitAt ? 2 : 1;
    const int passbandRows = passbandCols == 1
                                 ? passbandTotal
                                 : (passbandTotal + passbandCols - 1) / passbandCols;

    int placed[kGroupCount] = {0, 0, 0, 0};
    int folded = 0;
    for (int i = 0; i < m_tiles.size(); ++i) {
        AetherGateChainTile* t = m_tiles.at(i);
        if (!chainStageInMode(t->id(), m_mode)) {
            m_foldGrid->addWidget(t, folded / kFoldColumns, folded % kFoldColumns,
                                  Qt::AlignLeft | Qt::AlignTop);
            ++folded;
            t->setVisible(true);
            continue;
        }
        const int index = groupIndex(groups.at(i));
        const int n = placed[index]++;
        // Down the column, then across: reading order stays signal order.
        const int rowAt = (groups.at(i) == ChainGroup::Passband && passbandRows > 0)
                              ? n % passbandRows
                              : n;
        const int colAt = (groups.at(i) == ChainGroup::Passband && passbandRows > 0)
                              ? n / passbandRows
                              : 0;
        m_columns[index].grid->addWidget(t, rowAt, colAt,
                                         Qt::AlignLeft | Qt::AlignTop);
        t->setVisible(true);
    }

    // A group with nothing in it says so by not being there at all: an empty
    // labelled column is a promise of a stage that does not exist.
    for (int i = 0; i < kGroupCount; ++i)
        m_columns[i].host->setVisible(m_columns[i].count > 0);

    m_foldToggle->setVisible(folded > 0);
    m_fold->setVisible(folded > 0 && m_foldToggle->isChecked());
    m_foldToggle->setText(m_foldToggle->isChecked()
                              ? tr("STAGES THIS MODE DOES NOT USE (%1) - HIDE").arg(folded)
                              : tr("STAGES THIS MODE DOES NOT USE (%1) - SHOW").arg(folded));
}

void AetherGateChainStrip::selectStage(const QString& id)
{
    m_selected = id;
    for (AetherGateChainTile* t : m_tiles)
        t->setSelected(t->id() == id);
    emit stageSelected(id);
}

// Signal order, forwards and backwards, stopping at both ends rather than
// wrapping -- a diagram that wrapped from the last stage to the first would be
// claiming the chain is a loop.
void AetherGateChainStrip::moveSelection(int delta)
{
    const QList<AetherGateChainTile*> mine = tilesInMode();
    if (mine.isEmpty())
        return;
    int at = 0;
    for (int i = 0; i < mine.size(); ++i) {
        if (mine.at(i)->id() == m_selected) {
            at = i;
            break;
        }
    }
    const int next = qBound(0, at + delta, mine.size() - 1);
    if (next != at || m_selected.isEmpty())
        selectStage(mine.at(next)->id());
}

void AetherGateChainStrip::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    // The diagram is columns, so down and right are the same move: the next
    // stage the signal reaches.
    case Qt::Key_Right:
    case Qt::Key_Down:
        moveSelection(1);
        return;
    case Qt::Key_Left:
    case Qt::Key_Up:
        moveSelection(-1);
        return;
    case Qt::Key_Space:
        // One press, one write -- the card's own switch, with the card's own
        // "nothing optimistic" rule and its own busy latch behind it.
        if (AetherGateChainTile* t = tile(m_selected))
            t->activateSwitch();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

} // namespace AetherSDR
