#include "gui/DiversityTalkerControls.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>

namespace AetherSDR {

namespace {

// The same 22 px every other button on the FILTER page is, so the strip sits
// in the page's own rhythm rather than reading as a bolt-on.
constexpr int kRowHeight = 22;

// Same check dressing DiversityFilterControls uses for ANF, NB and the rest.
const char* kCheckStyle =
    "QCheckBox { color: {{color.text.primary}}; font-size: 11px; spacing: 5px;"
    " background: transparent; }"
    "QCheckBox::indicator { width: 12px; height: 12px; border-radius: 2px;"
    " border: 1px solid {{color.toggle.border}};"
    " background: {{color.toggle.background}}; }"
    "QCheckBox::indicator:checked {"
    " background: {{color.toggle.accent.background.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }";

const char* kTalkerTip =
    QT_TR_NOOP("A known talker's filter comes back the block they key up: the "
               "edges, shape and tone the gate last had on them, recalled the "
               "way the combiner already recalls their weight. FAST snaps to it "
               "on the block boundary; SMOOTH glides over about a second. Off, "
               "one filter serves everybody.");

} // namespace

DiversityTalkerControls::DiversityTalkerControls(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFilterTalkerStrip"));
    setAccessibleName(tr("Per talker filter"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("PER TALKER"), QStringLiteral("diversityWindowFilterTalkerBox"), body, this);
    frame->setToolTip(tr("Per-talker filter recall - reapplies each talker's saved filter "
                         "when they key up."));
    frame->setAccessibleDescription(tr(kTalkerTip));
    root->addWidget(frame);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    body->addLayout(row);

    m_check = new QCheckBox(tr("PER TALKER"), frame);
    m_check->setObjectName(QStringLiteral("diversityWindowFilterTalkerCheck"));
    m_check->setAccessibleName(tr("Recall each talker's own filter"));
    m_check->setToolTip(tr("Per-talker filter recall - reapplies each talker's saved filter "
                           "when they key up."));
    m_check->setAccessibleDescription(tr(kTalkerTip));
    ThemeManager::instance().applyStyleSheet(m_check, QString::fromLatin1(kCheckStyle));
    m_controls.append(m_check);
    // "1"/"0" rather than "on"/"off": the gate takes either, and every other
    // flag on this page goes out as a digit (see DiversityFilterControls::
    // buildCheck). One page, one spelling.
    connect(m_check, &QCheckBox::clicked, this, [this](bool on) {
        set(QStringLiteral("talker"), on ? QStringLiteral("1") : QStringLiteral("0"));
    });
    row->addWidget(m_check);

    // Two exclusive checkable buttons, exactly like the SHAPE and AGC rows in
    // the columns above: this is a discrete choice between two words the gate
    // owns, not a value with a range.
    m_snapGroup = new QButtonGroup(this);
    m_snapGroup->setExclusive(true);
    const QStringList labels{tr("FAST"), tr("SMOOTH")};
    const QStringList values{QStringLiteral("fast"), QStringLiteral("smooth")};
    const QStringList names{QStringLiteral("diversityWindowFilterTalkerSnapFast"),
                            QStringLiteral("diversityWindowFilterTalkerSnapSmooth")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(labels.at(i), frame);
        button->setObjectName(names.at(i));
        button->setAccessibleName(tr("talker_snap %1").arg(labels.at(i)));
        button->setToolTip(i == 0
                               ? tr("Snap to the recalled filter the instant this talker "
                                    "keys up.")
                               : tr("Glide to the recalled filter over about a second "
                                    "instead of snapping."));
        button->setAccessibleDescription(tr(kTalkerTip));
        button->setCheckable(true);
        button->setFixedHeight(kRowHeight);
        button->setProperty("filterValue", values.at(i));
        applyToggleButtonStyle(button);
        m_snapGroup->addButton(button);
        m_controls.append(button);
        // clicked(), not toggled(): applyTalker() checks a button back from the
        // poll and must not turn that read-back into another write.
        connect(button, &QPushButton::clicked, this, [this, value = values.at(i)] {
            set(QStringLiteral("talker_snap"), value);
        });
        row->addWidget(button);
    }
    row->addStretch(1);
}

void DiversityTalkerControls::set(const QString& key, const QString& value)
{
    QUrlQuery q;
    q.addQueryItem(key, value);
    emit requestFilter(QStringLiteral("/filter/set"), q);
}

void DiversityTalkerControls::applyTalker(const QJsonValue& talker)
{
    if (!talker.isObject()) {
        // A gate with no per-talker filter at all. Dead controls rather than an
        // "off" nothing said: the same rule the AGC threshold spin keeps.
        clear();
        for (QWidget* control : m_controls)
            control->setEnabled(false);
        return;
    }
    const QJsonObject t = talker.toObject();
    for (QWidget* control : m_controls)
        control->setEnabled(true);

    m_enabled = t.value(QStringLiteral("enabled")).toBool();
    {
        const QSignalBlocker block(m_check);
        m_check->setChecked(m_enabled);
    }

    const QString snap = t.value(QStringLiteral("snap")).toString();
    for (QAbstractButton* button : m_snapGroup->buttons()) {
        if (button->property("filterValue").toString() != snap)
            continue;
        const QSignalBlocker block(button);
        button->setChecked(true);
        break;
    }

    const QJsonValue id = t.value(QStringLiteral("id"));
    m_haveId = id.isDouble();
    m_id = m_haveId ? int(std::lround(id.toDouble())) : 0;
}

void DiversityTalkerControls::setTalkerNames(const QJsonArray& memory)
{
    m_names.clear();
    for (const QJsonValue& v : memory) {
        const QJsonObject entry = v.toObject();
        const QJsonValue id = entry.value(QStringLiteral("id"));
        const QJsonValue name = entry.value(QStringLiteral("name"));
        if (!id.isDouble() || !name.isString() || name.toString().isEmpty())
            continue;
        m_names.insert(int(std::lround(id.toDouble())), name.toString());
    }
}

void DiversityTalkerControls::clear()
{
    m_enabled = false;
    m_haveId = false;
    m_id = 0;
    {
        const QSignalBlocker block(m_check);
        m_check->setChecked(false);
    }
    if (QAbstractButton* checked = m_snapGroup->checkedButton()) {
        const QSignalBlocker block(checked);
        m_snapGroup->setExclusive(false);
        checked->setChecked(false);
        m_snapGroup->setExclusive(true);
    }
}

QString DiversityTalkerControls::stateClause() const
{
    // The recall being off, and no talker having keyed up yet, are both
    // "nothing is in force" -- and neither is worth a clause on a line whose
    // whole value is that every clause on it is a thing that IS switched on.
    if (!m_enabled || !m_haveId)
        return QString();
    const QString name = m_names.value(m_id);
    if (name.isEmpty())
        return tr("filter: #%1").arg(m_id);
    return tr("filter: %1's (#%2)").arg(name, QString::number(m_id));
}

} // namespace AetherSDR
