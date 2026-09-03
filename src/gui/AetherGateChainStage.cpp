#include "gui/AetherGateChainStage.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
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
    "QFrame[selected=\"true\"] { border: 2px solid {{color.accent.bright}}; }";

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

} // namespace

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
    return (enabled ? QStringLiteral("1") : QStringLiteral("0")) + QLatin1Char('|') + value;
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
    else if (stage.kind == QLatin1String("select") || stage.kind == QLatin1String("value"))
        buildSelect(prefix, large);
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
    m_toggle->setObjectName(prefix + QStringLiteral("Toggle_") + suffixFor(m_stage.id));
    m_toggle->setAccessibleName(tr("Switch %1").arg(m_stage.name));
    m_toggle->setCheckable(true);
    m_toggle->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_toggle->setMinimumWidth(kDetailControlWidth);
    applyToggleButtonStyle(m_toggle);
    const QString tip = m_stage.actionable()
                            ? tr("GET %1?%2 on the gate. The switch stays where you "
                                 "leave it, and it moves only when the gate's next "
                                 "answer says the stage actually changed.")
                                  .arg(m_stage.actionRoute, m_stage.actionQuery)
                            : (m_stage.why.isEmpty()
                                   ? tr("The gate nominated no action for this stage.")
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
}

void AetherGateChainControl::buildSelect(const QString& prefix, bool large)
{
    m_select = new QComboBox(this);
    m_select->setObjectName(prefix + QStringLiteral("Select_") + suffixFor(m_stage.id));
    m_select->setAccessibleName(tr("%1 setting").arg(m_stage.name));
    m_select->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_select->setMinimumWidth(kDetailControlWidth);
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
                            ? tr("GET %1?%2<value> on the gate. The list does not "
                                 "move until the gate's next answer reports the new "
                                 "value.")
                                  .arg(m_stage.actionRoute, m_stage.actionQuery)
                            : (m_stage.why.isEmpty()
                                   ? tr("The gate nominated no action for this stage.")
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
    ThemeManager::instance().applyStyleSheet(m_free, QString::fromLatin1(kFreeStyle));
    const QString freeTip = tr("Any width from %1 Hz to %2 Hz, typed. Out of that "
                               "range nothing is sent -- there is no filter to "
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
    applyBusy();
}

// --------------------------------------------------------------------------
// AetherGateChainTile
// --------------------------------------------------------------------------

AetherGateChainTile::AetherGateChainTile(const ChainStage& stage, QWidget* parent)
    : QFrame(parent), m_stage(stage)
{
    setObjectName(QStringLiteral("gateChainTile_") + suffixFor(stage.id));
    setProperty("stageId", stage.id);
    setProperty("fixed", stage.fixed);
    setProperty("selected", false);
    setAccessibleName(stage.name);
    setFixedWidth(kChainTileWidth);
    setMinimumHeight(kChainTileHeight);
    // A hand over a stage nothing can move is a promise the tile cannot keep.
    setCursor(stage.fixed ? Qt::ArrowCursor : Qt::PointingHandCursor);
    ThemeManager::instance().applyStyleSheet(this, QString::fromLatin1(kTileStyle));

    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(7, 5, 7, 5);
    box->setSpacing(2);

    // The name in the Diversity window's own field-label style, and the
    // headline value in its value style -- the two type roles that window
    // already teaches (design §0.3 item 8).
    m_name = DiversityWidgets::makeFieldLabel(stage.name, this);
    m_name->setObjectName(QStringLiteral("gateChainName_") + suffixFor(stage.id));
    m_name->setAccessibleName(stage.name);
    if (stage.fixed)
        ThemeManager::instance().applyStyleSheet(m_name, QString::fromLatin1(kDimNameStyle));
    box->addWidget(m_name);

    m_value = DiversityWidgets::makeValue(
        QStringLiteral("gateChainDetail_") + suffixFor(stage.id),
        QStringLiteral("auto · 1450 Hz · -20.0 dB"), this);
    m_value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_value->setAccessibleName(tr("%1 value").arg(stage.name));
    if (stage.fixed)
        ThemeManager::instance().applyStyleSheet(m_value, QString::fromLatin1(kDimValueStyle));
    box->addWidget(m_value);

    m_under = new QLabel(this);
    m_under->setObjectName(QStringLiteral("gateChainWhy_") + suffixFor(stage.id));
    m_under->setAccessibleName(tr("%1 note").arg(stage.name));
    ThemeManager::instance().applyStyleSheet(m_under, QString::fromLatin1(kUnderStyle));
    box->addWidget(m_under);

    m_levels = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainLevels_") + suffixFor(stage.id),
        chainLevelWorstCase(),
        tr("What the gate measured going into this stage and coming out of it. "
           "An em dash is a leg the gate does not measure, never a zero."),
        this);
    m_levels->setAccessibleName(tr("%1 levels").arg(stage.name));
    box->addWidget(m_levels);

    m_control = new AetherGateChainControl(stage, QStringLiteral("gateChain"),
                                           /*large=*/false, this);
    connect(m_control, &AetherGateChainControl::requestWrite, this,
            &AetherGateChainTile::requestWrite);
    box->addWidget(m_control);
    box->addStretch(1);

    setStage(stage);
}

void AetherGateChainTile::setStage(const ChainStage& stage)
{
    m_stage = stage;
    const QString tip = stage.tip.isEmpty() ? stage.why : stage.tip;
    setToolTip(tip);
    setAccessibleDescription(tip);
    setElided(m_value, stage.detail.isEmpty() ? emDash() : stage.detail,
              kChainTileWidth - 20);
    m_levels->setVisible(stage.hasIn || stage.hasOut);
    setElided(m_levels, chainLevelText(stage), kChainTileWidth - 20);
    m_control->setStage(stage);
    refreshUnderline();
}

// The line under the value says one of two things and never both: what the
// gate just refused (warning-coloured, and the more urgent of the two), or why
// a fixed row cannot move. A row that is neither refused nor fixed hides it,
// so the tile does not carry an empty line.
void AetherGateChainTile::refreshUnderline()
{
    const bool refused = !m_error.isEmpty();
    const QString text = refused ? m_error : m_stage.why;
    m_under->setVisible(!text.isEmpty());
    DiversityWidgets::setLive(m_under, refused);
    setElided(m_under, text, kChainTileWidth - 20);
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
