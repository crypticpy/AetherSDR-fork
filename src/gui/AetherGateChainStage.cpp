#include "gui/AetherGateChainStage.h"
#include "gui/AetherGateChainStagePrivate.h"

#include "gui/AetherGateChainModes.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QCheckBox>
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

using namespace chainstage;


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
    // Which checks this row carries, and where each one writes -- not
    // whether they are ON, which settingKey() below is for. A gate that adds
    // or removes a check, or changes its route, is a WIDGET change; a gate
    // that flips one on or off is not.
    for (const ChainCheck& check : checks) {
        out += QLatin1Char('|') + QStringLiteral("chk:") + check.key
               + QLatin1Char('/') + check.route;
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
    // Every check's own on/off, in the gate's own order -- what a write to
    // roof_offset (or any future check) is confirmed against.
    for (const ChainCheck& check : checks)
        key += QLatin1Char('|') + (check.on ? QStringLiteral("1") : QStringLiteral("0"));
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

    // The generic checks[] row, built regardless of `kind` -- a select row
    // (ROOFING · DIGITAL) can carry one exactly as a toggle or a fixed row
    // could.
    buildChecks(prefix, large);

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

const ChainCheck* AetherGateChainControl::findCheck(const QString& key) const
{
    for (const ChainCheck& check : m_stage.checks) {
        if (check.key == key)
            return &check;
    }
    return nullptr;
}

// One QCheckBox per checks[] entry, stacked under whatever the row's own
// `kind` already built (or under nothing, on a fixed row). A malformed entry
// -- no key, or nothing to write with -- draws no box at all rather than one
// that could crash on the click; AetherGateChainRows.cpp already drops those
// before this ever sees them, but a defensive skip here costs nothing and
// means a future gate's mistake still cannot crash this window.
void AetherGateChainControl::buildChecks(const QString& prefix, bool large)
{
    for (const ChainCheck& check : m_stage.checks) {
        if (check.key.isEmpty() || check.route.isEmpty())
            continue;
        auto* box = new QCheckBox(
            check.label.isEmpty() ? check.key : check.label, this);
        const QString name =
            prefix == QStringLiteral("gateChain")
                ? QStringLiteral("gateChainCheck_%1_%2").arg(m_stage.id, check.key)
                : prefix + QStringLiteral("Check_") + suffixFor(m_stage.id)
                      + QLatin1Char('_') + check.key;
        box->setObjectName(name);
        box->setAccessibleName(tr("%1 %2").arg(m_stage.name, check.label));
        box->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
        ThemeManager::instance().applyStyleSheet(box, QString::fromLatin1(kCheckStyle));
        const QString tip =
            tr("Switches %1. The box moves only when the receiver says the "
               "check actually changed.")
                .arg(check.label.isEmpty() ? check.key : check.label);
        box->setToolTip(tip);
        box->setAccessibleDescription(tip);
        const QString key = check.key;
        // clicked(), not toggled(): the box goes back where the gate had it
        // and greys until an answer comes back, the same rule every other
        // control in this window keeps -- nothing here is optimistic.
        connect(box, &QCheckBox::clicked, this, [this, key](bool) {
            const ChainCheck* c = findCheck(key);
            if (!c || m_busy) {
                syncToGate();
                return;
            }
            setBusy(true);
            syncToGate();
            emit requestWrite(c->route, QUrlQuery(c->on ? c->queryOff : c->queryOn));
        });
        layout()->addWidget(box);
        m_checks.append(box);
    }
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
           || m_action != nullptr || m_floor != nullptr || !m_checks.isEmpty();
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
    // Every check on the row greys with the rest of it: one write out on
    // this stage must not let a second one leave from its own check while
    // the first is still on the wire.
    for (QCheckBox* box : m_checks)
        box->setEnabled(live);
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
    // Zipped by position, not by key: shape() already guarantees the widget
    // list and m_stage.checks agree on count and keys, in order, or this
    // control would have been rebuilt rather than resynced (AetherGateChain
    // Strip.cpp only calls setStage() when shape() has not changed).
    for (int i = 0; i < m_checks.size() && i < m_stage.checks.size(); ++i) {
        const QSignalBlocker block(m_checks.at(i));
        m_checks.at(i)->setChecked(m_stage.checks.at(i).on);
    }
    applyBusy();
}

} // namespace AetherSDR
