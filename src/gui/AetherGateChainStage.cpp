#include "gui/AetherGateChainStage.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The tile is a fixed frame on purpose. A block diagram whose blocks change
// size when a number gains a digit is not a diagram, and the strip's whole
// no-scroll promise is arithmetic over these two constants.
constexpr int kTileWidth = 200;
constexpr int kTileHeight = 120;
constexpr int kRowHeight = 20;
constexpr int kLargeRowHeight = 26;
constexpr int kDetailControlWidth = 300;

// The lowest and highest a synthesised digital roofing filter can be asked
// for. 100 Hz is narrower than any radio's narrowest CW roofing filter and
// 25 kHz is the gate's own decimated rate -- past either end there is nothing
// to design.
constexpr int kFreeEntryMinHz = 100;
constexpr int kFreeEntryMaxHz = 25000;

// Applied to the tile itself rather than to a shared objectName: every tile
// needs a NAME OF ITS OWN, because the automation bridge and a screen reader
// both address this window by objectName and thirteen widgets called the same
// thing are one widget as far as either is concerned.
const char* kTileStyle =
    "QFrame { border: 1px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QFrame[selected=\"true\"] { border: 1px solid {{color.accent.bright}}; }";

const char* kDotStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 12px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.bright}}; }";

const char* kNameStyle =
    "QToolButton { color: {{color.text.primary}}; font-size: 11px;"
    " font-weight: bold; background: transparent; border: none;"
    " text-align: left; padding: 0px; }"
    "QToolButton:hover { color: {{color.accent.bright}}; }";

const char* kSelectStyle =
    "QComboBox { background: {{color.background.1}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }"
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
    for (const ChainOption& opt : options)
        out += QLatin1Char('|') + opt.group + QLatin1Char('/') + opt.value;
    return out;
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
    // action) deliberately builds nothing: the row's `why` is on the tile's
    // hover and there is no control to press.

    syncToGate();
}

void AetherGateChainControl::buildToggle(const QString& prefix, bool large)
{
    m_toggle = new QPushButton(m_stage.actionLabel.isEmpty()
                                   ? (m_stage.enabled ? tr("ON") : tr("OFF"))
                                   : m_stage.actionLabel,
                               this);
    m_toggle->setObjectName(prefix + QStringLiteral("Toggle_") + suffixFor(m_stage.id));
    m_toggle->setAccessibleName(tr("Switch %1").arg(m_stage.name));
    m_toggle->setCheckable(true);
    m_toggle->setEnabled(m_stage.actionable());
    m_toggle->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_toggle->setMinimumWidth(kDetailControlWidth);
    applyToggleButtonStyle(m_toggle);
    const QString tip = m_stage.actionable()
                            ? tr("GET %1?%2 on the gate. The latch stays where you "
                                 "leave it, and it moves only when the gate's next "
                                 "answer says the stage actually changed.")
                                  .arg(m_stage.actionRoute, m_stage.actionQuery)
                            : (m_stage.why.isEmpty()
                                   ? tr("The gate nominated no action for this stage.")
                                   : m_stage.why);
    m_toggle->setToolTip(tip);
    m_toggle->setAccessibleDescription(tip);
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        // Straight back where the gate had it. One write leaves; the answer
        // to it is what moves the latch.
        syncToGate();
        if (m_stage.actionable())
            emit requestWrite(m_stage.actionRoute, m_stage.queryFor());
    });
    layout()->addWidget(m_toggle);
}

void AetherGateChainControl::buildSelect(const QString& prefix, bool large)
{
    m_select = new QComboBox(this);
    m_select->setObjectName(prefix + QStringLiteral("Select_") + suffixFor(m_stage.id));
    m_select->setAccessibleName(tr("%1 setting").arg(m_stage.name));
    m_select->setEnabled(m_stage.actionable() && !m_stage.options.isEmpty());
    m_select->setFixedHeight(large ? kLargeRowHeight : kRowHeight);
    if (large)
        m_select->setMinimumWidth(kDetailControlWidth);
    ThemeManager::instance().applyStyleSheet(m_select, QString::fromLatin1(kSelectStyle));

    QString group;
    for (const ChainOption& opt : m_stage.options) {
        if (opt.group != group) {
            group = opt.group;
            if (!group.isEmpty()) {
                // A group header is an item that cannot be chosen: the radios
                // in §0 are how an operator finds "the 3 kHz one", not values
                // in their own right.
                m_select->addItem(group);
                auto* model = qobject_cast<QStandardItemModel*>(m_select->model());
                if (model && model->item(m_select->count() - 1))
                    model->item(m_select->count() - 1)->setEnabled(false);
            }
        }
        m_select->addItem(opt.label, opt.value);
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
        syncToGate();
        if (!wire.isEmpty() && m_stage.actionable())
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
    m_free->setEnabled(m_stage.actionable());
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
        if (!m_free->hasAcceptableInput() || !m_stage.actionable())
            return;
        emit requestWrite(m_stage.actionRoute, m_stage.queryFor(m_free->text()));
    });
    layout()->addWidget(m_free);
}

void AetherGateChainControl::setStage(const ChainStage& stage)
{
    m_stage = stage;
    syncToGate();
}

void AetherGateChainControl::syncToGate()
{
    if (m_toggle) {
        const QSignalBlocker block(m_toggle);
        m_toggle->setChecked(m_stage.enabled);
        if (!m_stage.actionLabel.isEmpty())
            m_toggle->setText(m_stage.actionLabel);
        else
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
}

// --------------------------------------------------------------------------
// AetherGateChainTile
// --------------------------------------------------------------------------

AetherGateChainTile::AetherGateChainTile(const ChainStage& stage, QWidget* parent)
    : QFrame(parent), m_stage(stage)
{
    setObjectName(QStringLiteral("gateChainTile_") + suffixFor(stage.id));
    setProperty("stageId", stage.id);
    setAccessibleName(stage.name);
    setFixedSize(kTileWidth, kTileHeight);
    setCursor(Qt::PointingHandCursor);
    ThemeManager::instance().applyStyleSheet(this, QString::fromLatin1(kTileStyle));

    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(6, 5, 6, 5);
    box->setSpacing(3);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(5);
    m_dot = new QLabel(QStringLiteral("●"), this);
    m_dot->setObjectName(QStringLiteral("gateChainDot_") + suffixFor(stage.id));
    m_dot->setAccessibleName(tr("%1 state").arg(stage.name));
    ThemeManager::instance().applyStyleSheet(m_dot, QString::fromLatin1(kDotStyle));
    head->addWidget(m_dot);

    m_name = new QToolButton(this);
    m_name->setObjectName(QStringLiteral("gateChainName_") + suffixFor(stage.id));
    m_name->setText(stage.name);
    m_name->setAccessibleName(stage.name);
    m_name->setCursor(Qt::PointingHandCursor);
    m_name->setFocusPolicy(Qt::StrongFocus);
    ThemeManager::instance().applyStyleSheet(m_name, QString::fromLatin1(kNameStyle));
    connect(m_name, &QToolButton::clicked, this,
            [this] { emit clicked(m_stage.id); });
    head->addWidget(m_name, 1);
    box->addLayout(head);

    m_detail = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainDetail_") + suffixFor(stage.id),
        QStringLiteral("auto · 1450 Hz · -20.0 dB"),
        stage.tip, this);
    m_detail->setAccessibleName(tr("%1 detail").arg(stage.name));
    box->addWidget(m_detail);

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
    DiversityWidgets::setLive(m_dot, stage.enabled);
    m_name->setToolTip(tip);
    m_name->setAccessibleDescription(tip);
    setElided(m_detail, stage.detail.isEmpty() ? emDash() : stage.detail,
              kTileWidth - 16);
    m_levels->setVisible(stage.hasIn || stage.hasOut);
    setElided(m_levels, chainLevelText(stage), kTileWidth - 16);
    m_control->setStage(stage);
}

void AetherGateChainTile::setSelected(bool on)
{
    if (property("selected").isValid() && property("selected").toBool() == on)
        return;
    setProperty("selected", on);
    style()->unpolish(this);
    style()->polish(this);
}

void AetherGateChainTile::mousePressEvent(QMouseEvent* event)
{
    emit clicked(m_stage.id);
    QFrame::mousePressEvent(event);
}

} // namespace AetherSDR
