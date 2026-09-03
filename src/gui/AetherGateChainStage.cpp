#include "gui/AetherGateChainStage.h"

#include "gui/AetherGateChainModes.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The tile's rows. A block diagram whose blocks change size when a number
// gains a digit is not a diagram, so the width is fixed and the height has a
// floor -- 220 x 84 is the operator's own number (design §0.3 item 8), and a
// tile carrying a refusal is allowed to grow past the floor rather than elide
// the one sentence that says what went wrong.
constexpr int kRowHeight = 20;
constexpr int kLargeRowHeight = 26;
constexpr int kDetailControlWidth = 300;

// A switch is a small thing on a card, not the card's headline. The first
// build gave it the whole width and the operator read the strip as a wall of
// ON buttons with dim words over them; the NAME leads now and the switch is
// the size of the word on it.
constexpr int kSwitchWidth = 54;
constexpr int kMenuWidth = 116;

// What the one measured line has to fit inside, on each of the two shapes.
constexpr int kCardTextWidth = kChainCardWidth - 4 - 14;
constexpr int kLineTextWidth =
    kChainSummaryWidth - 14 - kChainSummaryNameWidth - 6;

// The lowest and highest a synthesised digital roofing filter can be asked
// for. 100 Hz is narrower than any radio's narrowest CW roofing filter and
// 25 kHz is the gate's own decimated rate -- past either end there is nothing
// to design.
constexpr int kFreeEntryMinHz = 100;
constexpr int kFreeEntryMaxHz = 25000;

// The border is 2 px in BOTH states so that selecting a tile cannot move the
// text inside it by a pixel; only the colour changes. The accent is
// color.accent.bright, the same token the detail pane's title uses, which is
// how the tile and the pane say they are about the same stage (design §0.3
// items 3 and 7).
//
// A fixed row gets the dashed frame and no hover: nothing about it responds,
// and a border that looked like the others would be inviting a click that does
// nothing.
const char* kTileStyle =
    "QFrame { border: 2px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QFrame[fixed=\"true\"] { border: 2px dashed {{color.background.1}}; }"
    "QFrame[line=\"true\"] { border: 2px solid transparent;"
    " border-radius: 3px; background: transparent; }"
    "QFrame[selected=\"true\"] { border: 2px solid {{color.accent.bright}}; }"
    "QFrame[line=\"true\"][selected=\"true\"] {"
    " border: 2px solid {{color.accent.bright}}; }";

// makeFieldLabel()/makeValue() with the colour taken down to the disabled
// token, for a row nothing in the product can move. Same size and weight, so
// a dimmed tile still reads as the same kind of object.
const char* kDimNameStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 10px; font-weight: bold;"
    " background: transparent; }";

const char* kDimValueStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 11px; font-weight: bold;"
    " background: transparent; }";

// The line under the value: why a fixed stage cannot move, or -- when the gate
// has just refused a write -- what it said. Two meanings, one line, told apart
// by colour rather than by position, so the tile's height does not change when
// a refusal arrives.
const char* kUnderStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 10px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

const char* kSelectStyle =
    "QComboBox { background: {{color.background.1}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }"
    "QComboBox::drop-down { width: 14px; border: none; }"
    "QComboBox:disabled { color: {{color.text.disabled}}; }";

const char* kFreeStyle =
    "QLineEdit { background: {{color.background.0}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }";

QString emDash()
{
    return QStringLiteral("—");
}

QString suffixFor(const QString& id)
{
    QString clean = id;
    clean.replace(QLatin1Char(' '), QLatin1Char('_'));
    return clean;
}

// Elide rather than wrap. Every detail cell in this window has a fixed field
// width (DiversityWidgets::makeReadoutLine reserves its worst case), and a
// gate sentence longer than the field lives on the hover instead of reflowing
// the row -- there is exactly one setWordWrap() in this codebase and it is
// false.
void setElided(QLabel* label, const QString& text, int width)
{
    const QFontMetrics fm(label->font());
    label->setText(fm.elidedText(text, Qt::ElideRight, width));
    label->setToolTip(text);
    label->setAccessibleDescription(text);
}

// Tabular figures: every digit the same width, so a number that changes twice
// a second does not shuffle the characters beside it. Purely a font feature --
// the face, the size and the colour token are all unchanged.
void applyTabularFigures(QLabel* label)
{
    QFont f = label->font();
    f.setFeature(QFont::Tag("tnum"), 1);
    label->setFont(f);
}

// The one reason the FRONT END card prints once, under all of its rows,
// instead of once per row. It is the gate's own wording.
QString frontEndSharedWhy()
{
    return QCoreApplication::translate("AetherGateChainStage",
                                       "set on the setup page");
}

} // namespace

QString chainFrontEndSharedWhy()
{
    return frontEndSharedWhy();
}

QString chainUnderlineStyleSheet()
{
    return QString::fromLatin1(kUnderStyle);
}

// --------------------------------------------------------------------------
// ChainStage
// --------------------------------------------------------------------------

QUrlQuery ChainStage::queryFor(const QString& appended) const
{
    if (actionQuery.endsWith(QLatin1Char('=')))
        return QUrlQuery(actionQuery + appended);
    return QUrlQuery(actionQuery);
}

QString ChainStage::shape() const
{
    QString out = id + QLatin1Char('|') + kind + QLatin1Char('|')
                  + (fixed ? QLatin1Char('1') : QLatin1Char('0')) + QLatin1Char('|')
                  + (freeEntryHz ? QLatin1Char('1') : QLatin1Char('0')) + QLatin1Char('|')
                  + actionRoute + QLatin1Char('|')
                  + QString::number(int(hasIn) + 2 * int(hasOut));
    for (const ChainOption& opt : options) {
        out += QLatin1Char('|') + opt.group + QLatin1Char('/') + opt.value
               + (opt.enabled ? QLatin1Char('+') : QLatin1Char('-'));
    }
    return out;
}

QString ChainStage::settingKey() const
{
    QString key = (enabled ? QStringLiteral("1") : QStringLiteral("0")) + QLatin1Char('|') + value;
    // The GUARD row has a second thing a write can move that `enabled`/
    // `value` say nothing about: the floor. Folded in only for a stage that
    // carries one, so no other row's confirmation check changes shape.
    if (hasFloorControl)
        key += QLatin1Char('|') + floorValue;
    return key;
}

QString chainLevelText(const ChainStage& stage)
{
    const QString in = stage.hasIn ? QString::number(stage.inDb, 'f', 1) : emDash();
    const QString out = stage.hasOut ? QString::number(stage.outDb, 'f', 1) : emDash();
    return QCoreApplication::translate("AetherGateChainStage", "in %1 · out %2 dB")
        .arg(in, out);
}

QString chainLevelWorstCase()
{
    return QCoreApplication::translate("AetherGateChainStage",
                                       "in -199.9 · out -199.9 dB");
}

// Shortening that never leaves three dots behind. Whole words come off the
// end until what is left fits; the whole string stays on the hover. An
// elided line is the operator's own complaint -- "the content doesn't fit in
// the box so you can't read all of it" -- and three dots do not fix it, they
// just admit it.
QString chainFitToWidth(const QLabel* label, const QString& text, int width)
{
    const QFontMetrics fm(label->font());
    QString out = text;
    while (fm.horizontalAdvance(out) > width && out.contains(QLatin1Char(' '))) {
        out.truncate(out.lastIndexOf(QLatin1Char(' ')));
        out = out.trimmed();
        while (out.endsWith(QLatin1Char(',')) || out.endsWith(QLatin1Char(':')))
            out.chop(1);
    }
    return out;
}

// --------------------------------------------------------------------------
// AetherGateChainControl
// --------------------------------------------------------------------------

AetherGateChainControl::AetherGateChainControl(const ChainStage& stage,
                                               const QString& prefix, bool large,
                                               QWidget* parent)
    : QWidget(parent), m_stage(stage)
{
    setObjectName(prefix + QStringLiteral("Control_") + suffixFor(stage.id));
    setAccessibleName(tr("%1 control").arg(stage.name));
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(3);

    if (stage.kind == QLatin1String("toggle"))
        buildToggle(prefix, large);
    else if (stage.kind == QLatin1String("select"))
        buildSelect(prefix, large);
    else if (stage.kind == QLatin1String("value") && !stage.options.isEmpty())
        buildSelect(prefix, large);
    else if (stage.kind == QLatin1String("value") && stage.actionable())
        buildAction(prefix, large);
    // A `value` row with neither a menu nor an action builds NOTHING. The old
    // build gave it a one-item menu that could not be opened, showing the same
    // sentence the card had already printed and elided in the middle of it.
    // kind == "fixed" (and any kind a future gate invents that carries no
    // action) deliberately builds nothing: the row's `why` is printed on the
    // tile and there is no control to press.

    syncToGate();
}

void AetherGateChainControl::buildToggle(const QString& prefix, bool large)
{
    // The WORD, not a dot and not the gate's action label: the operator asked
    // to be told what the stage IS, and "ON" over a stage that is off because
    // the gate labelled the action rather than the state is the confusion
    // design §0.3 item 3 is about. The gate's own label goes on the hover with
    // the query it will send.
    m_toggle = new QPushButton(m_stage.enabled ? tr("ON") : tr("OFF"), this);
    // The override name is for the CARD's control only. The detail pane
    // builds a second, larger instance of this same class (prefix
    // "gateChainDetail") for every stage, and it must not share an
    // objectName with the card's -- the automation bridge and the tests
    // both assume one name finds one widget.
    m_toggle->setObjectName(
        (m_stage.toggleObjectName.isEmpty() || prefix != QStringLiteral("gateChain"))
            ? prefix + QStringLiteral("Toggle_") + suffixFor(m_stage.id)
            : m_stage.toggleObjectName);
    m_toggle->setAccessibleName(tr("Switch %1").arg(m_stage.name));
    m_toggle->setCheckable(true);
    m_toggle->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_toggle->setMinimumWidth(kDetailControlWidth);
    else
        m_toggle->setFixedWidth(kSwitchWidth);
    applyToggleButtonStyle(m_toggle);
    const QString tip = m_stage.actionable()
                            ? tr("Switches %1. The switch stays where you leave it "
                                 "and moves only when the receiver says the stage "
                                 "actually changed.")
                                  .arg(m_stage.name)
                            : (m_stage.why.isEmpty()
                                   ? tr("Nothing in this window can switch this stage.")
                                   : m_stage.why);
    m_toggle->setToolTip(tip);
    m_toggle->setAccessibleDescription(tip);
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (!m_stage.actionable() || m_busy) {
            syncToGate();
            return;
        }
        // Straight back where the gate had it, then greyed until an answer
        // comes back. One write leaves; the answer to it is what moves the
        // switch.
        setBusy(true);
        syncToGate();
        emit requestWrite(m_stage.actionRoute, m_stage.queryFor());
    });
    layout()->addWidget(m_toggle);

    if (m_stage.hasFloorControl)
        buildFloor(prefix, large);
}

// The GUARD row's second control: how far down the switch above is allowed
// to take the LNA. Stacked under the toggle rather than beside it, the same
// shape buildSelect() uses for the digital roof's free-entry field, so the
// row stays the width of one switch rather than the width of a switch plus a
// menu.
void AetherGateChainControl::buildFloor(const QString& prefix, bool large)
{
    m_floor = new QComboBox(this);
    // Same rule as the toggle's override name: the card's control only.
    m_floor->setObjectName(
        (m_stage.floorObjectName.isEmpty() || prefix != QStringLiteral("gateChain"))
            ? prefix + QStringLiteral("Floor_") + suffixFor(m_stage.id)
            : m_stage.floorObjectName);
    m_floor->setAccessibleName(tr("%1 floor").arg(m_stage.name));
    m_floor->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_floor->setMinimumWidth(kDetailControlWidth);
    else
        m_floor->setFixedWidth(kSwitchWidth);
    ThemeManager::instance().applyStyleSheet(m_floor, QString::fromLatin1(kSelectStyle));
    for (const ChainOption& opt : m_stage.floorOptions)
        m_floor->addItem(opt.label, opt.value);
    const QString tip = tr("The lowest LNA state the guard will step down to. "
                           "It steps the gain back up on its own; it never "
                           "goes below this floor.");
    m_floor->setToolTip(tip);
    m_floor->setAccessibleDescription(tip);
    connect(m_floor, &QComboBox::activated, this, [this](int index) {
        const QString wire = m_floor->itemData(index).toString();
        if (wire.isEmpty() || m_busy) {
            syncToGate();
            return;
        }
        setBusy(true);
        syncToGate();
        emit requestWrite(m_stage.floorActionRoute, QUrlQuery(m_stage.floorActionQuery + wire));
    });
    layout()->addWidget(m_floor);
}

void AetherGateChainControl::buildSelect(const QString& prefix, bool large)
{
    m_select = new QComboBox(this);
    m_select->setObjectName(prefix + QStringLiteral("Select_") + suffixFor(m_stage.id));
    m_select->setAccessibleName(tr("%1 setting").arg(m_stage.name));
    m_select->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_select->setMinimumWidth(kDetailControlWidth);
    else
        m_select->setFixedWidth(kMenuWidth);
    ThemeManager::instance().applyStyleSheet(m_select, QString::fromLatin1(kSelectStyle));

    auto* model = qobject_cast<QStandardItemModel*>(m_select->model());
    const auto disableLastItem = [this, model] {
        if (model && model->item(m_select->count() - 1))
            model->item(m_select->count() - 1)->setEnabled(false);
    };

    QString group;
    for (const ChainOption& opt : m_stage.options) {
        if (opt.group != group) {
            group = opt.group;
            if (!group.isEmpty()) {
                // A group header is an item that cannot be chosen: the radios
                // in §0 are how an operator finds "the 3 kHz one", not values
                // in their own right.
                m_select->addItem(group);
                disableLastItem();
            }
        }
        m_select->addItem(opt.label, opt.value);
        // A width the gate did not list stays on the menu and stays unpickable
        // (design §0.3 item 6): the operator learns this receiver cannot make
        // it, instead of watching it disappear.
        if (!opt.enabled)
            disableLastItem();
    }
    const QString tip = m_stage.actionable()
                            ? tr("Sets %1. The list does not move until the "
                                 "receiver reports the new value.")
                                  .arg(m_stage.name)
                            : (m_stage.why.isEmpty()
                                   ? tr("Nothing in this window can set this stage.")
                                   : m_stage.why);
    m_select->setToolTip(tip);
    m_select->setAccessibleDescription(tip);
    connect(m_select, &QComboBox::activated, this, [this](int index) {
        const QString wire = m_select->itemData(index).toString();
        if (wire.isEmpty() || !m_stage.actionable() || m_busy) {
            syncToGate();
            return;
        }
        setBusy(true);
        syncToGate();
        emit requestWrite(m_stage.actionRoute, m_stage.queryFor(wire));
    });
    layout()->addWidget(m_select);

    if (!m_stage.freeEntryHz)
        return;

    m_free = new QLineEdit(this);
    m_free->setObjectName(prefix + QStringLiteral("Free_") + suffixFor(m_stage.id));
    m_free->setAccessibleName(tr("%1 in hertz").arg(m_stage.name));
    m_free->setPlaceholderText(tr("Hz (%1–%2)").arg(kFreeEntryMinHz).arg(kFreeEntryMaxHz));
    m_free->setValidator(new QIntValidator(kFreeEntryMinHz, kFreeEntryMaxHz, m_free));
    m_free->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_free->setMinimumWidth(kDetailControlWidth);
    else
        m_free->setFixedWidth(kMenuWidth);
    ThemeManager::instance().applyStyleSheet(m_free, QString::fromLatin1(kFreeStyle));
    const QString freeTip = tr("Any width from %1 Hz to %2 Hz, typed. Outside that "
                               "range nothing is sent: there is no filter to "
                               "design either side of it.")
                                .arg(kFreeEntryMinHz)
                                .arg(kFreeEntryMaxHz);
    m_free->setToolTip(freeTip);
    m_free->setAccessibleDescription(freeTip);
    connect(m_free, &QLineEdit::returnPressed, this, [this] {
        if (!m_free->hasAcceptableInput() || !m_stage.actionable() || m_busy)
            return;
        setBusy(true);
        emit requestWrite(m_stage.actionRoute, m_stage.queryFor(m_free->text()));
    });
    layout()->addWidget(m_free);
}

// A row the gate gave a verb rather than a value: ALIGN's REALIGN. One press,
// one write, and the gate's own word on the button.
void AetherGateChainControl::buildAction(const QString& prefix, bool large)
{
    m_action = new QPushButton(m_stage.actionLabel.isEmpty() ? tr("SET")
                                                             : m_stage.actionLabel,
                               this);
    m_action->setObjectName(prefix + QStringLiteral("Act_") + suffixFor(m_stage.id));
    m_action->setAccessibleName(m_action->text());
    m_action->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_action->setMinimumWidth(kDetailControlWidth);
    else
        m_action->setFixedWidth(kMenuWidth);
    m_action->setCursor(Qt::PointingHandCursor);
    applyToggleButtonStyle(m_action);
    const QString tip = tr("%1. The card does not move until the receiver says "
                           "something changed.").arg(m_action->text());
    m_action->setToolTip(tip);
    m_action->setAccessibleDescription(tip);
    connect(m_action, &QPushButton::clicked, this, [this] {
        if (!m_stage.actionable() || m_busy)
            return;
        setBusy(true);
        emit requestWrite(m_stage.actionRoute, m_stage.queryFor(QString()));
    });
    layout()->addWidget(m_action);
}

bool AetherGateChainControl::hasControl() const
{
    return m_toggle != nullptr || m_select != nullptr || m_free != nullptr
           || m_action != nullptr || m_floor != nullptr;
}

void AetherGateChainControl::setStage(const ChainStage& stage)
{
    m_stage = stage;
    syncToGate();
}

void AetherGateChainControl::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    applyBusy();
}

void AetherGateChainControl::activateSwitch()
{
    if (m_toggle && m_toggle->isEnabled())
        m_toggle->click();
}

void AetherGateChainControl::applyBusy()
{
    const bool live = !m_busy;
    if (m_toggle)
        m_toggle->setEnabled(live && m_stage.actionable());
    if (m_select)
        m_select->setEnabled(live && m_stage.actionable() && !m_stage.options.isEmpty());
    if (m_free)
        m_free->setEnabled(live && m_stage.actionable());
    if (m_action)
        m_action->setEnabled(live && m_stage.actionable());
    if (m_floor)
        m_floor->setEnabled(live && m_stage.hasFloorControl);
}

void AetherGateChainControl::syncToGate()
{
    if (m_toggle) {
        const QSignalBlocker block(m_toggle);
        m_toggle->setChecked(m_stage.enabled);
        m_toggle->setText(m_stage.enabled ? tr("ON") : tr("OFF"));
    }
    if (m_select) {
        const QSignalBlocker block(m_select);
        if (!m_inserted.isEmpty() && m_inserted != m_stage.value) {
            m_select->removeItem(0);
            m_inserted.clear();
        }
        int index = m_select->findData(m_stage.value);
        if (index < 0 && !m_stage.value.isEmpty()) {
            // The value in force is not on the gate's own list -- show it
            // anyway rather than pointing the combo at somebody else's number.
            m_select->insertItem(0, m_stage.detail.isEmpty() ? m_stage.value
                                                             : m_stage.detail,
                                 m_stage.value);
            m_inserted = m_stage.value;
            index = 0;
        }
        if (index >= 0)
            m_select->setCurrentIndex(index);
    }
    if (m_floor) {
        const QSignalBlocker block(m_floor);
        const int index = m_floor->findData(m_stage.floorValue);
        if (index >= 0)
            m_floor->setCurrentIndex(index);
    }
    applyBusy();
}

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
    ThemeManager::instance().applyStyleSheet(this, QString::fromLatin1(kTileStyle));

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
    const QString tip = stage.tip.isEmpty() ? stage.why : stage.tip;
    setToolTip(tip);
    setAccessibleDescription(tip);
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
