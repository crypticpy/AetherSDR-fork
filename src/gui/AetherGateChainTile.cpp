// AetherGateChainTile -- one stage of the CHAIN window as a card or a summary
// line: the NAME, one measured line, one control, the border that says it is
// selected. Split from AetherGateChainStage.cpp, which keeps the ChainStage
// model and the control builder; the two share AetherGateChainStagePrivate.h.
#include "gui/AetherGateChainStage.h"
#include "gui/AetherGateChainStagePrivate.h"

#include "gui/AetherGateChainModes.h"
#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace AetherSDR {

using namespace chainstage;

namespace {

// The NOW strip's own row highlight: a 2 px left edge in the same accent the
// selected border already uses (design §2.3), so a stage the governor is
// talking about and a stage the operator clicked read as the same kind of
// thing. Appended to kTileStyle rather than added into it --
// AetherGateChainStagePrivate.h is shared with AetherGateChainStage.cpp,
// which this task does not touch -- so this stays a second rule layered on
// by AetherGateChainWindow's own tile()->setProperty("lit", ...), the same
// property-plus-repolish pattern setSelected() below already uses.
const char* const kLitStyle =
    "QFrame[lit=\"true\"] { border-left: 2px solid {{color.accent.bright}}; }";

} // namespace

// --------------------------------------------------------------------------
// AetherGateChainTile
// --------------------------------------------------------------------------

AetherGateChainTile::AetherGateChainTile(const ChainStage& stage,
                                         ChainTileShape shape, QWidget* parent)
    : QFrame(parent), m_stage(stage), m_shape(shape)
{
    setObjectName(QStringLiteral("gateChainTile_") + suffixFor(stage.id));
    setProperty("stageId", stage.id);
    setProperty("fixed", stage.fixed);
    setProperty("line", shape == ChainTileShape::Line);
    setProperty("selected", false);
    setAccessibleName(stage.name);
    // A hand over a stage nothing can move is a promise the card cannot keep.
    // It can still be SELECTED -- the inspector explains a fixed stage as
    // readily as a switchable one -- so the click is live either way.
    setCursor(stage.fixed ? Qt::ArrowCursor : Qt::PointingHandCursor);
    setProperty("lit", false);
    ThemeManager::instance().applyStyleSheet(
        this, QString::fromLatin1(kTileStyle) + QString::fromLatin1(kLitStyle));

    if (shape == ChainTileShape::Line)
        buildLine();
    else
        buildCard();

    setStage(stage);
}

// The block of the diagram: NAME, then the one measured line, then the one
// control. The name is the biggest thing on it.
void AetherGateChainTile::buildCard()
{
    setFixedWidth(kChainCardWidth);
    setMinimumHeight(kChainCardHeight);
    m_lineWidth = kCardTextWidth;

    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(7, 4, 7, 4);
    box->setSpacing(2);

    // makeValue() is the window's bright 11 px bold role. The first build put
    // the name in the dim field-label role and the switch in the bright one,
    // which is exactly backwards: the operator reads the strip for what the
    // stages ARE.
    m_name = DiversityWidgets::makeValue(
        QStringLiteral("gateChainName_") + suffixFor(m_stage.id),
        m_stage.name, this);
    m_name->setText(m_stage.name);
    m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_name->setAccessibleName(m_stage.name);
    if (m_stage.fixed)
        ThemeManager::instance().applyStyleSheet(m_name,
                                                 QString::fromLatin1(kDimValueStyle));
    box->addWidget(m_name);

    m_value = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainValue_") + suffixFor(m_stage.id), QString(),
        m_stage.detail, this);
    m_value->setFixedWidth(m_lineWidth);
    m_value->setAccessibleName(tr("%1 now").arg(m_stage.name));
    applyTabularFigures(m_value);
    if (m_stage.fixed)
        ThemeManager::instance().applyStyleSheet(m_value,
                                                 QString::fromLatin1(kDimNameStyle));
    box->addWidget(m_value);

    m_under = new QLabel(this);
    m_under->setObjectName(QStringLiteral("gateChainWhy_") + suffixFor(m_stage.id));
    m_under->setAccessibleName(tr("%1 note").arg(m_stage.name));
    m_under->setWordWrap(false);
    ThemeManager::instance().applyStyleSheet(m_under, QString::fromLatin1(kUnderStyle));
    box->addWidget(m_under);

    m_control = new AetherGateChainControl(m_stage, QStringLiteral("gateChain"),
                                           /*large=*/false, this);
    connect(m_control, &AetherGateChainControl::requestWrite, this,
            &AetherGateChainTile::requestWrite);
    m_control->setVisible(m_control->hasControl());
    box->addWidget(m_control, 0, Qt::AlignLeft);
    box->addStretch(1);
}

// One row of the FRONT END summary card: a dim name, then either the one
// measured line or -- when the row can actually be set -- its control in the
// same slot, because a menu already reads out the value it is showing.
void AetherGateChainTile::buildLine()
{
    setFixedWidth(kChainSummaryWidth - 14);
    setMinimumHeight(kChainSummaryRowHeight);
    m_lineWidth = kLineTextWidth;

    auto* box = new QHBoxLayout(this);
    box->setContentsMargins(2, 0, 2, 0);
    box->setSpacing(6);

    m_name = DiversityWidgets::makeFieldLabel(m_stage.name, this);
    m_name->setObjectName(QStringLiteral("gateChainName_") + suffixFor(m_stage.id));
    m_name->setAccessibleName(m_stage.name);
    m_name->setFixedWidth(kChainSummaryNameWidth);
    m_name->setText(chainFitToWidth(m_name, m_stage.name, kChainSummaryNameWidth));
    m_name->setToolTip(m_stage.name);
    m_name->setAccessibleDescription(m_stage.name);
    box->addWidget(m_name);

    m_value = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainValue_") + suffixFor(m_stage.id), QString(),
        m_stage.detail, this);
    m_value->setFixedWidth(m_lineWidth);
    m_value->setAccessibleName(tr("%1 now").arg(m_stage.name));
    applyTabularFigures(m_value);
    box->addWidget(m_value);

    m_control = new AetherGateChainControl(m_stage, QStringLiteral("gateChain"),
                                           /*large=*/false, this);
    connect(m_control, &AetherGateChainControl::requestWrite, this,
            &AetherGateChainTile::requestWrite);
    m_control->setVisible(m_control->hasControl());
    m_value->setVisible(!m_control->hasControl());
    box->addWidget(m_control);
    box->addStretch(1);

    // The why line is built but starts hidden: on a summary row it appears
    // only for something the card's own hint does not already cover.
    m_under = new QLabel(this);
    m_under->setObjectName(QStringLiteral("gateChainWhy_") + suffixFor(m_stage.id));
    m_under->setAccessibleName(tr("%1 note").arg(m_stage.name));
    m_under->setWordWrap(false);
    ThemeManager::instance().applyStyleSheet(m_under, QString::fromLatin1(kUnderStyle));
    box->addWidget(m_under);
}

void AetherGateChainTile::setStage(const ChainStage& stage)
{
    m_stage = stage;
    // The hover is the one-line purpose (shortTip, falling back to why); the
    // paragraph in `tip` goes to the accessible description only, so a
    // screen reader still gets the long form without the tile's own hover
    // reading like a developer note.
    const QString shortTip = stage.shortTip.isEmpty()
                                 ? (stage.why.isEmpty() ? stage.tip : stage.why)
                                 : stage.shortTip;
    const QString longTip = stage.tip.isEmpty() ? shortTip : stage.tip;
    setToolTip(shortTip);
    setAccessibleDescription(longTip);
    refreshPrimary();
    m_control->setStage(stage);
    if (m_shape == ChainTileShape::Line)
        m_value->setVisible(!m_control->hasControl());
    refreshUnderline();
}

QString AetherGateChainTile::primaryText() const
{
    return m_value ? m_value->text() : QString();
}

// The one line, and the rule that makes it honest: parts are dropped whole
// from the end until what is left fits, and then WORDS are dropped whole off
// the last part. Nothing is ever cut through the middle, and nothing is ever
// followed by three dots. The sentence the gate wrote is on the hover.
void AetherGateChainTile::refreshPrimary()
{
    const QStringList parts = chainPrimaryParts(m_stage);
    const QFontMetrics fm(m_value->font());
    const QString join = QStringLiteral(" · ");

    QStringList kept = parts;
    QString text = kept.join(join);
    while (kept.size() > 1 && fm.horizontalAdvance(text) > m_lineWidth) {
        kept.removeLast();
        text = kept.join(join);
    }
    while (fm.horizontalAdvance(text) > m_lineWidth
           && text.contains(QLatin1Char(' '))) {
        text.truncate(text.lastIndexOf(QLatin1Char(' ')));
        text = text.trimmed();
    }
    if (text.isEmpty())
        text = emDash();

    m_value->setText(text);
    // The FRONT END card's HEADROOM row wears the same warning tone the
    // underline already carries for a refusal -- makeReadoutLine()'s own
    // [live="true"] rule, not a new one.
    DiversityWidgets::setLive(m_value, m_stage.warn);
    // The hover and the screen reader get the whole thing, always.
    const QString whole = m_stage.detail.isEmpty() ? text : m_stage.detail;
    m_value->setToolTip(whole);
    m_value->setAccessibleDescription(whole);
}

// The line under the value says one of two things and never both: what the
// receiver just refused (warning-coloured, and the more urgent of the two), or
// why a fixed row cannot move. A row that is neither refused nor fixed hides
// it, so the card does not carry an empty line.
//
// On a summary ROW the shared "all set on the setup page" hint under the card
// already answers the common case, so only a DIFFERENT reason shows.
void AetherGateChainTile::refreshUnderline()
{
    const bool refused = !m_error.isEmpty();
    QString text = refused ? m_error : m_stage.why;
    if (!refused && m_shape == ChainTileShape::Line
        && text == chainFrontEndSharedWhy()) {
        text.clear();
    }
    // A summary ROW never carries one: the card's single hint under all seven
    // rows says the one thing they have in common, and anything else is on
    // the hover and in the inspector. Seven reasons stacked in a 244 px
    // column was the "there is a lot of stuff" the operator read.
    if (m_shape == ChainTileShape::Line)
        text.clear();
    m_under->setVisible(!text.isEmpty());
    DiversityWidgets::setLive(m_under, refused);
    m_under->setText(chainFitToWidth(m_under, text, m_lineWidth));
    m_under->setToolTip(text);
    m_under->setAccessibleDescription(text);
}

void AetherGateChainTile::setSelected(bool on)
{
    if (m_selected == on)
        return;
    m_selected = on;
    setProperty("selected", on);
    style()->unpolish(this);
    style()->polish(this);
}

void AetherGateChainTile::setBusy(bool busy)
{
    m_control->setBusy(busy);
}

void AetherGateChainTile::setError(const QString& error)
{
    if (m_error == error)
        return;
    m_error = error;
    refreshUnderline();
}

void AetherGateChainTile::activateSwitch()
{
    m_control->activateSwitch();
}

void AetherGateChainTile::mousePressEvent(QMouseEvent* event)
{
    emit clicked(m_stage.id);
    QFrame::mousePressEvent(event);
}

} // namespace AetherSDR
