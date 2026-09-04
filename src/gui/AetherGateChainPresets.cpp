#include "gui/AetherGateChainPresets.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace AetherSDR {

namespace {

// How long the undo stays on offer after a delete. Eight seconds is long
// enough to read the line and reach the mouse, and short enough that the row
// is not still carrying the last thing you did when you come back to it.
constexpr int kUndoMs = 8000;

constexpr int kPickWidth = 250;

// The menu keeps each preset's name as its data and its mode one role along,
// so the "(edited)" suffix can be put on and taken off without reading the
// file again.
constexpr int kModeRole = Qt::UserRole + 1;

const char* kButtonStyle =
    "QPushButton { color: {{color.text.primary}}; font-size: 11px;"
    " padding: 3px 9px; border: 1px solid {{color.border.subtle}};"
    " border-radius: 4px; background: transparent; }"
    "QPushButton:hover { background: {{color.background.1}}; }"
    "QPushButton:pressed { background: {{color.background.3}}; }"
    "QPushButton:disabled { color: {{color.text.disabled}};"
    " border: 1px solid {{color.background.1}}; }";

// The undo line. Warning-coloured because something has just gone away, and a
// button rather than a label because the whole sentence IS the affordance --
// a screen reader reading "deleted Net night, UNDO" has to be able to press it.
const char* kNoticeStyle =
    "QPushButton { color: {{color.accent.warning}}; font-size: 11px;"
    " border: none; background: transparent; text-align: left; padding: 0px; }";

const char* kPickStyle =
    "QComboBox { background: {{color.background.1}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }"
    "QComboBox::drop-down { width: 14px; border: none; }"
    "QComboBox:disabled { color: {{color.text.disabled}}; }";

const char* kNameStyle =
    "QLineEdit { background: {{color.background.0}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.accent}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }";

// "nb=off" -> "nb"; "shape=" -> "shape". Both forms are in the contract: a
// toggle's action query is the OPPOSITE of where the row is now, and a select's
// ends in "=" for the app to append to. A preset asks for one specific value,
// so it needs the parameter's name out of either form.
QString parameterOf(const ChainStage& stage)
{
    const int eq = stage.actionQuery.indexOf(QLatin1Char('='));
    return eq < 0 ? stage.actionQuery : stage.actionQuery.left(eq);
}

// The value a preset keeps for one row: the parameter's value IN FORCE on a
// toggle, the value in force on a select, and nothing at all on anything else.
// A `value` row (ALIGN's lag, the passband's width) is a MEASUREMENT, not a
// setting, and a preset that carried one would be promising to restore a
// number the gate computes for itself.
//
// A toggle's action is the opposite of where the row is, so its value in force
// is the OPPOSITE of the action's: SLICE FILTER enabled carries "bypass=on" and
// the value to keep is bypass=off. Reading enabled as "on" would have written
// bypass=on to put a working filter back, which is the one thing a preset
// must never do.
QString capturedValue(const ChainStage& stage)
{
    if (!stage.actionable())
        return QString();
    if (stage.kind == QLatin1String("select"))
        return stage.value;
    if (stage.kind != QLatin1String("toggle"))
        return QString();
    const int eq = stage.actionQuery.indexOf(QLatin1Char('='));
    const QString action = eq < 0 ? QString() : stage.actionQuery.mid(eq + 1);
    if (action == QLatin1String("on"))
        return QStringLiteral("off");
    if (action == QLatin1String("off"))
        return QStringLiteral("on");
    return stage.enabled ? QStringLiteral("on") : QStringLiteral("off");
}

// What an option's value is CALLED, for the line the operator watches go past
// while the set runs. Falls back to the wire form when the gate did not label
// it -- "3000" is a worse word than "3.0 kHz" and a better one than nothing.
QString labelFor(const ChainStage& stage, const QString& value)
{
    for (const ChainOption& opt : stage.options) {
        if (opt.value == value)
            return opt.label;
    }
    return value;
}

} // namespace

QString chainPresetSlug(const QString& name)
{
    QString slug;
    slug.reserve(name.size());
    for (const QChar c : name) {
        if (c.isLetterOrNumber())
            slug.append(c.toLower());
        else if (!slug.endsWith(QLatin1Char('-')))
            slug.append(QLatin1Char('-'));
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    while (slug.startsWith(QLatin1Char('-')))
        slug.remove(0, 1);
    return slug.isEmpty() ? QStringLiteral("preset") : slug;
}

QString chainPresetDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // macOS nests AppDataLocation as <org>/<app>, and both are "AetherSDR":
    // collapse the doubled segment so presets sit beside the app's other
    // stores (.../AetherSDR/chain-presets), the same way models and the EiBi
    // cache do. A no-op on Linux and Windows, where it is one segment.
    const QFileInfo fi(base);
    if (fi.fileName() == fi.dir().dirName())
        base = fi.absolutePath();
    const QString dir = base + QStringLiteral("/chain-presets");
    QDir().mkpath(dir);
    return dir;
}

ChainPresetDoc chainPresetCapture(const QList<ChainStage>& rows, ChainMode mode,
                                  const QString& name)
{
    ChainPresetDoc doc;
    doc.name = name.trimmed();
    doc.mode = chainModeId(mode);
    doc.saved = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (const ChainStage& stage : rows) {
        const QString value = capturedValue(stage);
        if (!value.isEmpty())
            doc.stages.append(qMakePair(stage.id, value));
    }
    return doc;
}

bool chainPresetSave(ChainPresetDoc& doc)
{
    if (doc.name.isEmpty())
        return false;
    const QString dir = chainPresetDir();
    const QString slug = chainPresetSlug(doc.name);
    // A slug already spoken for by a preset with a DIFFERENT name gets a
    // suffix. "Net night" saved twice overwrites itself, which is what saving
    // under a name you already used means.
    QString path = dir + QLatin1Char('/') + slug + QStringLiteral(".json");
    for (int n = 2; n < 100; ++n) {
        if (!QFile::exists(path) || chainPresetRead(path).name == doc.name)
            break;
        path = QStringLiteral("%1/%2-%3.json").arg(dir, slug).arg(n);
    }

    QJsonObject stages;
    // The order lives in a second array: a JSON object's keys are a set, and
    // the order the writes go out in is the whole reason AUTO WIDTH lands
    // before the edges. Readers that only want the values can ignore it.
    QJsonArray order;
    for (const auto& entry : doc.stages) {
        stages.insert(entry.first, entry.second);
        order.append(entry.first);
    }
    QJsonObject root;
    root.insert(QStringLiteral("name"), doc.name);
    root.insert(QStringLiteral("saved"), doc.saved);
    root.insert(QStringLiteral("mode"), doc.mode);
    root.insert(QStringLiteral("stages"), stages);
    root.insert(QStringLiteral("order"), order);

    // QSaveFile, so a preset is never half a preset: it writes a temporary and
    // renames it into place, which is the atomic write the app's other JSON
    // stores use.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;
    doc.path = path;
    return true;
}

ChainPresetDoc chainPresetRead(const QString& path)
{
    ChainPresetDoc doc;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return doc;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    doc.name = root.value(QStringLiteral("name")).toString();
    if (doc.name.isEmpty())
        return ChainPresetDoc();
    doc.saved = root.value(QStringLiteral("saved")).toString();
    doc.mode = root.value(QStringLiteral("mode")).toString();
    doc.path = path;
    const QJsonObject stages = root.value(QStringLiteral("stages")).toObject();
    const QJsonArray order = root.value(QStringLiteral("order")).toArray();
    QStringList ids;
    for (const QJsonValue& id : order)
        ids.append(id.toString());
    // A preset dropped in the folder by hand may have no order array at all.
    // Its keys are then the order, which is as good a guess as exists.
    if (ids.isEmpty())
        ids = stages.keys();
    for (const QString& id : ids) {
        if (stages.contains(id))
            doc.stages.append(qMakePair(id, stages.value(id).toString()));
    }
    return doc;
}

QList<ChainPresetDoc> chainPresetAll()
{
    QList<ChainPresetDoc> all;
    const QStringList files =
        QDir(chainPresetDir()).entryList({QStringLiteral("*.json")}, QDir::Files,
                                         QDir::Name);
    for (const QString& file : files) {
        const ChainPresetDoc doc =
            chainPresetRead(chainPresetDir() + QLatin1Char('/') + file);
        if (!doc.isNull())
            all.append(doc);
    }
    std::sort(all.begin(), all.end(),
              [](const ChainPresetDoc& a, const ChainPresetDoc& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return all;
}

QList<ChainPresetWrite> chainPresetSet(const ChainPresetDoc& doc,
                                       const QList<ChainStage>& rows,
                                       QStringList* missing)
{
    QList<ChainPresetWrite> writes;
    for (const auto& entry : doc.stages) {
        const ChainStage* found = nullptr;
        for (const ChainStage& row : rows) {
            if (row.id == entry.first) {
                found = &row;
                break;
            }
        }
        if (!found || !found->actionable() || parameterOf(*found).isEmpty()) {
            if (missing)
                missing->append(found ? found->name : entry.first);
            continue;
        }
        ChainPresetWrite write;
        write.route = found->actionRoute;
        write.query = parameterOf(*found) + QLatin1Char('=') + entry.second;
        write.why = AetherGateChainPresetBar::tr("%1 to %2")
                        .arg(found->name, labelFor(*found, entry.second));
        writes.append(write);
    }
    return writes;
}

// --------------------------------------------------------------------------
// The row
// --------------------------------------------------------------------------

AetherGateChainPresetBar::AetherGateChainPresetBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("gateChainPresetRow"));
    setAccessibleName(tr("Saved chain settings"));
    auto* box = new QHBoxLayout(this);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);

    auto* caption = DiversityWidgets::makeCaption(tr("PRESETS"), this);
    caption->setObjectName(QStringLiteral("gateChainPresetCaption"));
    caption->setToolTip(tr("Every stage, saved under a name and put back one at a time."));
    caption->setAccessibleDescription(
        tr("Every stage the receiver lets you change, saved "
           "under a name you choose and put back one stage at "
           "a time."));
    box->addWidget(caption);

    m_pick = new QComboBox(this);
    m_pick->setObjectName(QStringLiteral("gateChainPresetPick"));
    m_pick->setAccessibleName(tr("Saved settings"));
    m_pick->setToolTip(tr("The settings you have saved. Each one says which "
                          "listening mode it was saved for."));
    m_pick->setAccessibleDescription(m_pick->toolTip());
    m_pick->setFixedWidth(kPickWidth);
    m_pick->setFixedHeight(24);
    ThemeManager::instance().applyStyleSheet(m_pick, QString::fromLatin1(kPickStyle));
    box->addWidget(m_pick);

    // The inline name field, in the menu's own place. No dialog: a modal box
    // over a window whose whole job is watching a receiver would hide the
    // thing you are naming.
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("gateChainPresetName"));
    m_name->setAccessibleName(tr("Name for these settings"));
    m_name->setPlaceholderText(tr("name these settings, then press Enter"));
    m_name->setToolTip(tr("Type a name and press Enter to save. Press Escape to "
                          "leave everything as it was."));
    m_name->setAccessibleDescription(m_name->toolTip());
    m_name->setFixedWidth(kPickWidth);
    m_name->setFixedHeight(24);
    m_name->setVisible(false);
    m_name->installEventFilter(this);
    ThemeManager::instance().applyStyleSheet(m_name, QString::fromLatin1(kNameStyle));
    box->addWidget(m_name);

    // Which preset the receiver is actually set to, as distinct from which one
    // the menu happens to be showing. The menu is a choice; this is a fact.
    m_state = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainPresetState"),
        tr("in force: %1 (edited)").arg(QString(24, QLatin1Char('M'))),
        tr("The preset the receiver was last set to. \"edited\" means it drifted."),
        this);
    m_state->setAccessibleDescription(
        tr("The preset the receiver was last set to. \"edited\" means at least "
           "one stage no longer reads the way that preset says; put it back and "
           "the word goes away."));
    m_state->setAccessibleName(tr("Preset in force"));
    box->addWidget(m_state);

    const auto makeButton = [&](const QString& text, const QString& name,
                                const QString& tip, const QString& description) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(name);
        button->setAccessibleName(text);
        button->setToolTip(tip);
        button->setAccessibleDescription(description);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(24);
        ThemeManager::instance().applyStyleSheet(button,
                                                 QString::fromLatin1(kButtonStyle));
        box->addWidget(button);
        return button;
    };

    m_load = makeButton(tr("LOAD"), QStringLiteral("gateChainPresetLoad"),
                        tr("Puts these settings back, one stage at a time."),
                        tr("Puts these settings back into the receiver, one "
                           "stage at a time, waiting for it after each one."));
    m_save = makeButton(tr("SAVE AS..."), QStringLiteral("gateChainPresetSave"),
                        tr("Saves every stage the receiver is set to right now "
                           "under a name of your own."),
                        tr("Saves every stage the receiver is set to right now "
                           "under a name of your own."));
    m_delete = makeButton(tr("DELETE"), QStringLiteral("gateChainPresetDelete"),
                          tr("Throws these settings away. You get eight seconds "
                             "to change your mind."),
                          tr("Throws these settings away. You get eight seconds "
                             "to change your mind."));
    connect(m_load, &QPushButton::clicked, this, &AetherGateChainPresetBar::doLoad);
    connect(m_save, &QPushButton::clicked, this,
            &AetherGateChainPresetBar::beginSaveAs);
    connect(m_delete, &QPushButton::clicked, this,
            &AetherGateChainPresetBar::doDelete);

    m_notice = new QPushButton(this);
    m_notice->setObjectName(QStringLiteral("gateChainPresetNotice"));
    m_notice->setAccessibleName(tr("Undo the delete"));
    m_notice->setCursor(Qt::PointingHandCursor);
    m_notice->setFlat(true);
    m_notice->setVisible(false);
    m_notice->setFixedHeight(24);
    ThemeManager::instance().applyStyleSheet(m_notice,
                                             QString::fromLatin1(kNoticeStyle));
    connect(m_notice, &QPushButton::clicked, this, [this] {
        if (m_deleted.isNull())
            return;
        ChainPresetDoc back = m_deleted;
        m_deleted = ChainPresetDoc();
        chainPresetSave(back);
        showNotice(QString());
        reload(back.name);
    });
    box->addWidget(m_notice);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    m_noticeTimer->setInterval(kUndoMs);
    connect(m_noticeTimer, &QTimer::timeout, this, [this] {
        m_deleted = ChainPresetDoc();
        showNotice(QString());
    });

    box->addStretch(1);

    reload();
    refreshState();
}

void AetherGateChainPresetBar::setSource(std::function<QList<ChainStage>()> rows,
                                         std::function<ChainMode()> mode)
{
    m_rows = std::move(rows);
    m_mode = std::move(mode);
}

QString AetherGateChainPresetBar::currentName() const
{
    return m_pick->currentData().toString();
}

void AetherGateChainPresetBar::reload(const QString& keepName)
{
    const QString want = keepName.isEmpty() ? currentName() : keepName;
    m_pick->blockSignals(true);
    m_pick->clear();
    for (const ChainPresetDoc& doc : chainPresetAll()) {
        // "<name> · <mode>": the mode is part of what a preset IS, and a menu
        // of five names with no clue which of them is the CW one is a menu you
        // have to load to read.
        m_pick->addItem(QStringLiteral("%1 · %2").arg(doc.name, doc.mode), doc.name);
        m_pick->setItemData(m_pick->count() - 1, doc.path, Qt::ToolTipRole);
        m_pick->setItemData(m_pick->count() - 1, doc.mode, kModeRole);
    }
    const int at = m_pick->findData(want);
    if (at >= 0)
        m_pick->setCurrentIndex(at);
    m_pick->blockSignals(false);
    const bool any = m_pick->count() > 0;
    m_load->setEnabled(any);
    m_delete->setEnabled(any);
    refreshState();
}

void AetherGateChainPresetBar::noteRows(const QList<ChainStage>& rows)
{
    if (m_loaded.isNull())
        return;
    bool differs = false;
    for (const auto& entry : m_loaded.stages) {
        for (const ChainStage& row : rows) {
            if (row.id != entry.first)
                continue;
            // A stage the gate has stopped reporting, or one it will not let
            // anybody write, cannot have drifted; only a live value can.
            const QString now = capturedValue(row);
            if (!now.isEmpty() && now != entry.second)
                differs = true;
            break;
        }
        if (differs)
            break;
    }
    if (differs == m_edited)
        return;
    m_edited = differs;
    refreshState();
}

void AetherGateChainPresetBar::refreshState()
{
    // The menu item first: the file is untouched, this is a label.
    for (int i = 0; i < m_pick->count(); ++i) {
        const QString name = m_pick->itemData(i).toString();
        const QString mode = m_pick->itemData(i, kModeRole).toString();
        const bool editedHere = m_edited && name == m_loaded.name;
        m_pick->setItemText(i, editedHere ? tr("%1 · %2 (edited)").arg(name, mode)
                                          : QStringLiteral("%1 · %2").arg(name, mode));
    }
    QString text;
    if (m_loaded.isNull())
        text = tr("no preset in force");
    else if (m_edited)
        text = tr("in force: %1 (edited)").arg(m_loaded.name);
    else
        text = tr("in force: %1").arg(m_loaded.name);
    m_state->setText(text);
    // refreshState() runs from the constructor (reload() below), so the
    // long explanation the H1 90-char rule pushed off the tooltip has to be
    // rebuilt here rather than left to survive untouched -- see the short
    // tooltip set on m_state above.
    m_state->setAccessibleDescription(
        tr("%1. \"edited\" means at least one stage no longer reads the way "
           "that preset says; put it back and the word goes away.")
            .arg(text));
}

void AetherGateChainPresetBar::beginSaveAs()
{
    m_pick->setVisible(false);
    m_name->setVisible(true);
    m_name->setText(currentName());
    m_name->selectAll();
    m_name->setFocus(Qt::OtherFocusReason);
}

void AetherGateChainPresetBar::endSaveAs(bool commit)
{
    const QString name = m_name->text().trimmed();
    m_name->setVisible(false);
    m_pick->setVisible(true);
    if (!commit || name.isEmpty() || !m_rows || !m_mode)
        return;
    ChainPresetDoc doc = chainPresetCapture(m_rows(), m_mode(), name);
    if (!chainPresetSave(doc))
        return;
    // What was just saved IS what is in force, by construction.
    m_loaded = doc;
    m_edited = false;
    reload(doc.name);
}

void AetherGateChainPresetBar::doLoad()
{
    const QString name = currentName();
    if (name.isEmpty() || !m_rows)
        return;
    const int at = m_pick->findData(name);
    const ChainPresetDoc doc = chainPresetRead(m_pick->itemData(at, Qt::ToolTipRole)
                                                   .toString());
    if (doc.isNull())
        return;
    QStringList missing;
    const QList<ChainPresetWrite> writes = chainPresetSet(doc, m_rows(), &missing);
    m_loaded = doc;
    m_edited = false;
    refreshState();
    emit applyRequested(writes, doc.name, missing);
}

void AetherGateChainPresetBar::doDelete()
{
    const QString name = currentName();
    if (name.isEmpty())
        return;
    const int at = m_pick->findData(name);
    const ChainPresetDoc doc = chainPresetRead(m_pick->itemData(at, Qt::ToolTipRole)
                                                   .toString());
    if (doc.isNull() || !QFile::remove(doc.path))
        return;
    m_deleted = doc;
    // The file is gone; the receiver is still set the way it said. The
    // "in force" line keeps saying so until something else is loaded.
    reload();
    // No confirmation box: the delete has happened and the way back is one
    // click on the line it left behind.
    showNotice(tr("deleted \"%1\" — UNDO").arg(doc.name));
    m_noticeTimer->start();
}

void AetherGateChainPresetBar::showNotice(const QString& text)
{
    m_notice->setText(text);
    m_notice->setVisible(!text.isEmpty());
    m_notice->setAccessibleDescription(text);
    if (text.isEmpty())
        m_noticeTimer->stop();
}

// Return and Escape are both TAKEN here, not left for the window. A QLineEdit
// ignores Return after it has emitted returnPressed, on purpose, so that a
// dialog can press its default button with it -- and this row lives in a
// QDialog whose buttons are all autoDefault. Left alone, the Return that saved
// the preset went on to press SAVE AS... again and reopened the field with the
// name it had just saved.
bool AetherGateChainPresetBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_name && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape) {
            endSaveAs(false);
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            endSaveAs(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace AetherSDR
