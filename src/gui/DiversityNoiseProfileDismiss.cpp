#include "gui/DiversityNoiseProfilePanel.h"

// Split out of DiversityNoiseProfilePanel.cpp to keep that file under the
// project's ~800-line budget (AGENTS.md, "C++ Style Guide"). This file owns
// DISMISS: persisting which noise findings are counted as handled
// (AppSettings key DiversityDismissedNoiseKinds) and building the Do-column
// cell widgets that offer it. Everything else about the panel -- the four
// headline sentences, the history strip, applyProfile()/applySubband() --
// stays in DiversityNoiseProfilePanel.cpp; applyKinds() there still runs the
// per-poll expiry pass by calling expireDismissed() here.

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWidget>

#include <cmath>

namespace AetherSDR {

namespace {

// A dismissal expires the moment the finding it was about moves more than
// this many dB from the value it was dismissed at -- see R2.1 in
// diversity-phase3a-workflow-plan.md: "every noise finding gets DISMISS...
// persisted per finding kind in AppSettings until the finding's dB changes
// by more than 3 dB".
constexpr double kDismissExpiryDb = 3.0;

const char* kDismissedSettingsKey = "DiversityDismissedNoiseKinds";

// "kind|db" entries joined the same way AudioEngine's own list settings are
// (ClientRxChainStages and friends) -- see AppSettings::setValue(), which
// stores via QVariant::toString() and so cannot round-trip a QStringList by
// itself.
constexpr char kDismissedEntrySeparator = ',';
constexpr char kDismissedFieldSeparator = '|';

constexpr int kActionButtonHeight = 18;

// The Do column is the last of DiversityNoiseProfilePanel.cpp's six --
// kept as a literal here rather than duplicating that file's whole
// column-width table, which is presentation-only and belongs with the
// other five columns.
constexpr int kKindActionColumn = 5;

// The same status-line dressing DiversityNoiseProfilePanel.cpp uses for
// m_status, duplicated rather than shared -- this codebase's own convention
// for tiny anonymous-namespace helpers used from more than one file.
const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

QString emDash()
{
    return QStringLiteral("—");
}

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

} // namespace

// --------------------------------------------------------------------------
// DISMISS -- see docs/DIVERSITY.md, "Acting on the noise profile", and
// AppSettings key DiversityDismissedNoiseKinds.
// --------------------------------------------------------------------------

QSet<QString> DiversityNoiseProfilePanel::dismissedKinds() const
{
    QSet<QString> out;
    for (auto it = m_dismissed.cbegin(); it != m_dismissed.cend(); ++it)
        out.insert(it.key());
    return out;
}

void DiversityNoiseProfilePanel::loadDismissed()
{
    m_dismissed.clear();
    const QString stored = AppSettings::instance()
                                .value(QString::fromLatin1(kDismissedSettingsKey))
                                .toString();
    if (stored.isEmpty())
        return;
    for (const QString& entry :
         stored.split(QChar::fromLatin1(kDismissedEntrySeparator), Qt::SkipEmptyParts)) {
        const int sep = entry.indexOf(QChar::fromLatin1(kDismissedFieldSeparator));
        if (sep <= 0)
            continue;
        const QString kind = entry.left(sep);
        bool ok = false;
        const double db = entry.mid(sep + 1).toDouble(&ok);
        if (!ok || kind.isEmpty())
            continue;
        m_dismissed.insert(kind, db);
    }
}

void DiversityNoiseProfilePanel::persistDismissed()
{
    QStringList entries;
    for (auto it = m_dismissed.cbegin(); it != m_dismissed.cend(); ++it) {
        entries << it.key() + QChar::fromLatin1(kDismissedFieldSeparator)
                       + QString::number(it.value(), 'f', 2);
    }
    AppSettings::instance().setValue(
        QString::fromLatin1(kDismissedSettingsKey),
        entries.join(QChar::fromLatin1(kDismissedEntrySeparator)));
    AppSettings::instance().save();
}

void DiversityNoiseProfilePanel::dismissKind(const QString& kind, double db)
{
    if (kind.isEmpty())
        return;
    m_dismissed.insert(kind, db);
    persistDismissed();
    emit dismissedKindsChanged(dismissedKinds());
    rebuildKindsTable();
}

void DiversityNoiseProfilePanel::undismissKind(const QString& kind)
{
    if (m_dismissed.remove(kind) == 0)
        return;
    persistDismissed();
    emit dismissedKindsChanged(dismissedKinds());
    rebuildKindsTable();
}

void DiversityNoiseProfilePanel::expireDismissed(const QHash<QString, double>& currentDb,
                                                 const QSet<QString>& currentKinds)
{
    bool changed = false;
    for (auto it = m_dismissed.begin(); it != m_dismissed.end();) {
        if (!currentKinds.contains(it.key())) {
            // The finding is gone from this poll's kinds array altogether --
            // "dismissed" stops meaning anything once there is nothing left
            // to have dismissed.
            it = m_dismissed.erase(it);
            changed = true;
            continue;
        }
        const auto dbIt = currentDb.constFind(it.key());
        if (dbIt != currentDb.constEnd()
            && std::abs(dbIt.value() - it.value()) > kDismissExpiryDb) {
            it = m_dismissed.erase(it);
            changed = true;
            continue;
        }
        ++it;
    }
    if (changed) {
        persistDismissed();
        emit dismissedKindsChanged(dismissedKinds());
    }
}

// --------------------------------------------------------------------------
// The Do column -- one action button, one action button plus DISMISS, or
// "dismissed" plus UNDO. Split out of applyKinds() so a DISMISS/UNDO click
// redraws its own row at once rather than waiting for the next poll.
// --------------------------------------------------------------------------

void DiversityNoiseProfilePanel::rebuildKindsTable()
{
    const QStringList& packed = m_kindRows;
    const QVector<QJsonObject>& keep = m_keptRows;

    m_kinds->setRowCount(packed.size());
    for (int row = 0; row < packed.size(); ++row) {
        const QStringList cells = packed.at(row).split(QChar(0x1f));
        for (int col = 0; col < kKindActionColumn; ++col) {
            QTableWidgetItem* item = new QTableWidgetItem(cells.at(col));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setTextAlignment(col >= 3 ? (Qt::AlignRight | Qt::AlignVCenter)
                                            : (Qt::AlignLeft | Qt::AlignVCenter));
            // The detail is the gate's sentence and can be longer than the
            // column: the hover is where the rest of it lives, because a
            // column wide enough for the worst one would be most of the page.
            if (col == 2)
                item->setToolTip(cells.at(col));
            m_kinds->setItem(row, col, item);
        }

        const QString kindUpper = cells.at(0);
        const QString kind = keep.at(row).value(QStringLiteral("kind")).toString();
        const QString label = cells.at(5);
        const QString route = cells.at(6);
        const QString query = cells.at(7);
        const bool active = cells.at(8) == QStringLiteral("1");
        const bool haveAction = !route.isEmpty() && !label.isEmpty();
        const bool dismissed = !kind.isEmpty() && m_dismissed.contains(kind);

        auto* cell = new QWidget(m_kinds);
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);

        if (dismissed) {
            auto* dismissedLabel = new QLabel(tr("dismissed"), cell);
            dismissedLabel->setObjectName(
                QStringLiteral("diversityWindowNoiseDismissedLabel%1").arg(row));
            ThemeManager::instance().applyStyleSheet(dismissedLabel,
                                                     QString::fromLatin1(kStatusStyle));
            dismissedLabel->setAccessibleName(
                tr("The %1 finding is dismissed").arg(kindUpper));
            cellLayout->addWidget(dismissedLabel);

            auto* undo = new QPushButton(tr("UNDO"), cell);
            undo->setObjectName(
                QStringLiteral("diversityWindowNoiseUndismiss%1").arg(kindUpper));
            undo->setFixedHeight(kActionButtonHeight);
            undo->setAccessibleName(tr("Undo dismissing this finding"));
            undo->setToolTip(tr("Bring this finding's action back."));
            undo->setAccessibleDescription(undo->toolTip());
            applyToggleButtonStyle(undo);
            connect(undo, &QPushButton::clicked, this,
                    [this, kind] { undismissKind(kind); });
            cellLayout->addWidget(undo);
        } else {
            auto* button = new QPushButton(haveAction ? label : emDash(), cell);
            button->setObjectName(QStringLiteral("diversityWindowNoiseKindAction%1").arg(row));
            button->setFixedHeight(kActionButtonHeight);
            button->setCheckable(true);
            button->setChecked(active);
            button->setEnabled(haveAction);
            applyToggleButtonStyle(button);
            if (haveAction) {
                button->setAccessibleName(tr("%1 the %2 finding").arg(label, kindUpper));
                // The route and query together are worth showing in full --
                // it is what the button actually does -- but as one sentence
                // they can run past AGENTS.md's 90-char tooltip line, so the
                // full form is the accessibleDescription and the tooltip
                // keeps just the route, which fits on its own.
                button->setAccessibleDescription(
                    tr("GET %1?%2 on the gate. %3")
                        .arg(route, query,
                             active ? tr("This action is in force now.")
                                    : tr("The gate nominated this action "
                                         "for this finding.")));
                const QString routeTip = active
                    ? tr("GET %1 — already in force.").arg(route)
                    : tr("GET %1 — the gate's suggested fix for this finding.").arg(route);
                button->setToolTip(routeTip.size() > 90
                                       ? (active ? tr("Already in force; press to reapply.")
                                                 : tr("The gate's own suggested fix for "
                                                      "this finding."))
                                       : routeTip);
                connect(button, &QPushButton::clicked, this, [this, route, query] {
                    m_actionPending = true;
                    emit actionRequested(route, QUrlQuery(query));
                });
            } else {
                const QString why = keep.at(row).value(QStringLiteral("why")).toString();
                button->setAccessibleName(tr("Nothing to do about the %1 finding")
                                              .arg(kindUpper));
                const QString tip = why.isEmpty()
                                        ? tr("The gate nominated no action for this "
                                             "finding.")
                                        : why;
                // `why` is the gate's own sentence and not a literal in this
                // file, so it is out of scope for the 90-char audit the same
                // way DiversityBeaconControls.cpp's runtime report text is --
                // but the accessibleDescription still carries it in full.
                button->setAccessibleDescription(tip);
                button->setToolTip(tip.size() > 90
                                       ? tr("The gate nominated no action for this finding.")
                                       : tip);
            }
            cellLayout->addWidget(button);

            // Only a row the gate itself offered an action for, and only
            // while that action is not already in force -- an active row is
            // already "handled" in the sense DISMISS exists for, and a
            // why-only row has nothing to dismiss.
            if (haveAction && !active && !kind.isEmpty()) {
                double db = 0.0;
                jsonNumber(keep.at(row), "db", &db);
                auto* dismiss = new QPushButton(QStringLiteral("✕"), cell);
                dismiss->setObjectName(
                    QStringLiteral("diversityWindowNoiseDismiss%1").arg(kindUpper));
                dismiss->setFixedHeight(kActionButtonHeight);
                dismiss->setFixedWidth(kActionButtonHeight);
                dismiss->setAccessibleName(tr("Dismiss this finding"));
                dismiss->setToolTip(
                    tr("Count this finding as handled until it changes."));
                dismiss->setAccessibleDescription(dismiss->toolTip());
                applyToggleButtonStyle(dismiss);
                connect(dismiss, &QPushButton::clicked, this,
                        [this, kind, db] { dismissKind(kind, db); });
                cellLayout->addWidget(dismiss);
            }
        }
        m_kinds->setCellWidget(row, kKindActionColumn, cell);
    }
}

} // namespace AetherSDR
