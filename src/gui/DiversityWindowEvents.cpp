#include "gui/DiversityWindowEvents.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindow.h"
#include "gui/DiversityWindowPanels.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// TALKERS. 11px body text (the window's floor is 10), a 10px bold header, and
// no grid noise -- the table is a list of people, not a spreadsheet.
const char* kTalkerTableStyle =
    "QTableWidget { background: transparent; color: {{color.text.primary}};"
    " font-size: 11px; border: none; }"
    "QTableWidget::item { padding: 0px 3px; }"
    "QTableWidget::item:selected { background: {{color.background.2}};"
    " color: {{color.text.primary}}; }"
    "QHeaderView::section { background: {{color.background.1}};"
    " color: {{color.text.secondary}}; font-size: 10px; font-weight: bold;"
    " border: none; padding: 3px 3px; }";

// EVENTS. Same 11px, and no selection highlight worth speaking of: the list
// is read, not operated.
const char* kEventListStyle =
    "QListWidget { background: transparent; color: {{color.text.secondary}};"
    " font-size: 11px; border: 1px solid {{color.background.1}};"
    " border-radius: 3px; }"
    "QListWidget::item { padding: 1px 3px; }";

// # / Name / Phase / Level / Hits / Heard / First. Fixed widths so a poll
// that adds or drops a remembered talker scrolls the table, never resizes the
// panel around it. Height comes from the layout (the table shares the scope's
// stretch row), never from the row count.
constexpr int kTalkerColumnWidths[] = {40, 86, 50, 56, 38, 48, 48};
constexpr int kTalkerColumnCount =
    int(sizeof(kTalkerColumnWidths) / sizeof(kTalkerColumnWidths[0]));
constexpr int kTalkerRowHeight = 22;
constexpr int kTalkerTableMinHeight = 100;
// Sum of the widths above plus the vertical scrollbar and the frame. Set as
// the table's own minimum so the column it lives in can never be squeezed to
// the point of hiding "Heard" and "First" -- a talker list with the ages cut
// off is the v1 problem all over again.
constexpr int kTalkerTableMinWidth = 392;

// Which column carries the operator's own label. Named rather than spelled 1
// in six places, because it is the one column with different rules.
constexpr int kTalkerNameColumn = 1;

// Newest first, and bounded: an event list is a tail, not an archive.
constexpr int kMaxEvents = 200;

// Which leg the operator is hearing, in the words the chain row's own buttons
// use -- "A", not "a", so the line and the button agree.
QString hearWord(const QString& wire)
{
    if (wire == QLatin1String("a"))
        return QStringLiteral("A");
    if (wire == QLatin1String("b"))
        return QStringLiteral("B");
    return wire;
}

// `#2 "Bob"` when the gate has a name for it, `#2` when it does not. A talker
// with no name is not "unnamed" or "(none)": it is just a number, and saying
// so in fewer words is the honest form.
QString talkerTag(int id, const QString& name)
{
    if (name.isEmpty())
        return QStringLiteral("#%1").arg(id);
    return QStringLiteral("#%1 \"%2\"").arg(QString::number(id), name);
}



// Row content is packed into one string per row so an unchanged memory list
// can be detected with a single QStringList compare. The separator is the
// ASCII unit separator rather than a printable character: the Name column is
// operator text, and a callsign with a pipe in it must not be able to make
// two different tables compare equal.
constexpr QChar kRowSep = QChar(0x1f);

QString cellOr(bool have, const QString& text)
{
    return have ? text : QStringLiteral("—");
}

bool memberNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull())
        return false;
    *out = v.toDouble();
    return true;
}

} // namespace

QString DiversityEventLog::shortDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return QStringLiteral("—");
    if (seconds < 60.0)
        return QStringLiteral("%1 s").arg(qint64(std::llround(seconds)));
    if (seconds < 3600.0)
        return QStringLiteral("%1 m").arg(qint64(seconds / 60.0));
    return QStringLiteral("%1 h").arg(qint64(seconds / 3600.0));
}

QString DiversityEventLog::memoryClearedLine()
{
    return QCoreApplication::translate("DiversityEventLog", "memory cleared");
}

QString DiversityEventLog::captureSavedLine(const QString& basename)
{
    return QCoreApplication::translate("DiversityEventLog", "capture saved %1")
        .arg(basename);
}

void DiversityEventLog::reset()
{
    m_have = false;
    m_prev = DiversitySnapshot{};
}

QStringList DiversityEventLog::apply(const DiversitySnapshot& s)
{
    QStringList lines;
    const DiversitySnapshot prev = m_prev;
    const bool had = m_have;
    m_have = true;
    m_prev = s;

    if (!had)
        return lines;

    // Presence is a barrier -- see this class's header comment.
    if (s.present != prev.present) {
        lines << (s.present
                      ? QCoreApplication::translate("DiversityEventLog", "gate back")
                      : QCoreApplication::translate("DiversityEventLog", "gate lost"));
        return lines;
    }
    if (!s.present || !s.available || !prev.available)
        return lines;

    // --- who is talking ---------------------------------------------------
    const bool talkerChanged = (s.haveTalker != prev.haveTalker)
                               || (s.haveTalker && s.talkerId != prev.talkerId);
    if (talkerChanged && prev.haveTalker) {
        lines << QCoreApplication::translate("DiversityEventLog", "%1 ended after %2")
                     .arg(talkerTag(prev.talkerId, prev.talkerName),
                          shortDuration(prev.talkerSinceS));
    }
    if (talkerChanged && s.haveTalker) {
        if (s.haveTalkerWeight) {
            lines << QCoreApplication::translate("DiversityEventLog",
                                                 "%1 started (phase %2°, %3 dB)")
                         .arg(talkerTag(s.talkerId, s.talkerName),
                              QString::asprintf("%.0f", s.talkerPhaseDeg),
                              QString::asprintf("%+.1f", s.talkerRatioDb));
        } else {
            lines << QCoreApplication::translate("DiversityEventLog", "%1 started")
                         .arg(talkerTag(s.talkerId, s.talkerName));
        }
    }

    // --- new entries in memory -------------------------------------------
    for (int id : s.memoryIds) {
        if (!prev.memoryIds.contains(id)) {
            lines << QCoreApplication::translate("DiversityEventLog",
                                                 "new talker #%1 remembered")
                         .arg(id);
        }
    }

    // --- station focus ----------------------------------------------------
    const bool focusChanged = (s.haveFocus != prev.haveFocus)
                              || (s.haveFocus && s.focusId != prev.focusId);
    if (focusChanged) {
        lines << (s.haveFocus
                      ? QCoreApplication::translate("DiversityEventLog", "locked on %1")
                            .arg(talkerTag(s.focusId, s.focusName))
                      : QCoreApplication::translate("DiversityEventLog", "lock released"));
    }
    if (s.haveFocus && s.focusNulling && !(prev.haveFocus && prev.focusNulling)
            && s.haveTalker) {
        lines << QCoreApplication::translate("DiversityEventLog",
                                             "nulling %1 (not the locked station)")
                     .arg(talkerTag(s.talkerId, s.talkerName));
    }

    // --- steady QRM -------------------------------------------------------
    if (s.haveSteadyQrm && prev.haveSteadyQrm && s.steadyQrm != prev.steadyQrm) {
        lines << (s.steadyQrm
                      ? QCoreApplication::translate("DiversityEventLog",
                                                    "steady carrier nulled")
                      : QCoreApplication::translate("DiversityEventLog",
                                                    "steady carrier gone"));
    }

    // --- chain ------------------------------------------------------------
    if (!s.mode.isEmpty() && s.mode != prev.mode) {
        lines << QCoreApplication::translate("DiversityEventLog", "mode → %1")
                     .arg(s.mode);
    }
    if (!s.hear.isEmpty() && s.hear != prev.hear) {
        lines << QCoreApplication::translate("DiversityEventLog", "hear → %1")
                     .arg(hearWord(s.hear));
    }

    // --- alignment --------------------------------------------------------
    if (s.realigning && !prev.realigning)
        lines << QCoreApplication::translate("DiversityEventLog", "realigning…");
    else if (!s.realigning && prev.realigning && s.aligned) {
        lines << (s.haveLag
                      ? QCoreApplication::translate("DiversityEventLog", "aligned, lag %1")
                            .arg(qint64(std::llround(s.lagSamples)))
                      : QCoreApplication::translate("DiversityEventLog", "aligned"));
    }

    return lines;
}
QWidget* DiversityWindow::buildTalkersPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("TALKERS"), QStringLiteral("diversityWindowTalkers"), body, this);
    frame->setToolTip(
        tr("Stations the gate has heard and kept a combiner weight for. When "
           "one of them comes back on the air the gate recalls its weight "
           "instead of solving from scratch, so the first syllable is already "
           "combined. The row that is lit is whoever is talking right now."));

    m_talkersCount = DiversityWidgets::makeFieldLabel(tr("0 talkers remembered"), frame);
    m_talkersCount->setObjectName(QStringLiteral("diversityWindowTalkersCountLabel"));
    m_talkersCount->setAccessibleName(tr("Remembered talker count"));
    m_talkersCount->setToolTip(
        tr("How many stations are in the gate's memory, and which of them is "
           "on the air right now with how long they have been going."));

    m_talkers = new QTableWidget(0, kTalkerColumnCount, frame);
    m_talkers->setObjectName(QStringLiteral("diversityWindowTalkersTable"));
    m_talkers->setAccessibleName(tr("Remembered talkers"));
    m_talkers->setHorizontalHeaderLabels({tr("#"), tr("Name"), tr("Phase"), tr("Level"),
                                          tr("Hits"), tr("Heard"), tr("First")});
    ThemeManager::instance().applyStyleSheet(m_talkers,
                                             QString::fromLatin1(kTalkerTableStyle));

    // One hover explanation per column. "Phase", not "Bearing": two loops
    // give the phase difference between the antennas and nothing more -- a
    // bearing needs a third baseline to resolve the ambiguity, and calling it
    // one would be claiming a measurement the hardware cannot make.
    static const struct { int column; const char* tip; } kHeaderTips[] = {
        {0, QT_TR_NOOP("The gate's own number for this talker. The same number is "
                       "printed beside its marker on the dial, so a ring out on "
                       "the rim can be matched to a row here.")},
        {1, QT_TR_NOOP("Your own label for this station -- a callsign, a name, "
                       "anything. Double-click to type one; clear it to remove it. "
                       "It is stored on the gate, so it comes back the next time "
                       "they do.")},
        {2, QT_TR_NOOP("The phase difference between the two loops for this "
                       "station. It is a PHASE, not a bearing: with only two "
                       "antennas there is no third baseline to resolve which side "
                       "the signal came from, so the same number covers two "
                       "directions.")},
        {3, QT_TR_NOOP("How much louder loop B is than loop A for this station, "
                       "in decibels. Together with the phase it is the whole "
                       "weight the combiner applies when they come back.")},
        {4, QT_TR_NOOP("How many separate overs the gate has matched to this "
                       "weight. A high count means the weight is well settled.")},
        {5, QT_TR_NOOP("How long ago this station was last heard.")},
        {6, QT_TR_NOOP("How long ago the gate first heard this station.")},
    };
    for (const auto& entry : kHeaderTips) {
        if (QTableWidgetItem* header = m_talkers->horizontalHeaderItem(entry.column))
            header->setToolTip(tr(entry.tip));
    }

    m_talkers->verticalHeader()->setVisible(false);
    // Only the Name column is ever editable, and only deliberately: a
    // single-click edit trigger on a table that repaints every second would
    // open an editor the operator did not ask for.
    m_talkers->setEditTriggers(QAbstractItemView::DoubleClicked
                               | QAbstractItemView::EditKeyPressed);
    m_talkers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_talkers->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int c = 0; c < kTalkerColumnCount; ++c)
        m_talkers->setColumnWidth(c, kTalkerColumnWidths[c]);
    m_talkers->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_talkers->horizontalHeader()->setStretchLastSection(true);
    m_talkers->verticalHeader()->setDefaultSectionSize(kTalkerRowHeight);
    m_talkers->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_talkers->setMinimumHeight(kTalkerTableMinHeight);
    m_talkers->setMinimumWidth(kTalkerTableMinWidth);
    m_talkers->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_talkers, &QTableWidget::itemChanged, this,
            &DiversityWindow::onTalkerItemChanged);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this] { restyleTalkerRows(); });

    m_memoryClearButton = new QPushButton(tr("Clear memory"), frame);
    m_memoryClearButton->setObjectName(QStringLiteral("diversityWindowMemoryClearButton"));
    m_memoryClearButton->setAccessibleName(tr("Clear the remembered talkers"));
    m_memoryClearButton->setToolTip(
        tr("Forget every remembered talker and their weights, names included. "
           "The gate starts solving from scratch for whoever comes up next. "
           "Useful after moving band or antenna, when the old weights describe "
           "a station geometry that no longer exists."));
    connect(m_memoryClearButton, &QPushButton::clicked, this, [this] {
        emit requestMemoryClear();
        // The gate answers this one with the next poll rather than a
        // read-back, so the log line is written here: it records what the
        // operator did, not what was observed.
        addEventLines({DiversityEventLog::memoryClearedLine()});
    });

    // Lock: the DX workflow. With two antennas the combiner can steer at one
    // station or null one interferer, never both; locked on a station it
    // keeps their beam and nulls every other over instead of following each
    // caller in turn.
    m_lockButton = new QPushButton(tr("Lock on station"), frame);
    m_lockButton->setObjectName(QStringLiteral("diversityWindowLockButton"));
    m_lockButton->setAccessibleName(tr("Lock the combiner on the selected talker"));
    m_lockButton->setToolTip(
        tr("Pin the combiner on the selected station. Their overs get the "
           "remembered beam; anyone else who transmits is treated as an "
           "interferer and nulled, so the receiver stays deaf to a pile-up "
           "between the wanted station's overs. Release to go back to "
           "following whoever is talking."));
    m_lockButton->setEnabled(false);
    connect(m_lockButton, &QPushButton::clicked, this, [this] {
        QUrlQuery q;
        if (m_haveFocus) {
            q.addQueryItem(QStringLiteral("focus"), QStringLiteral("off"));
        } else {
            const int id = selectedTalkerId();
            if (id < 0)
                return;
            q.addQueryItem(QStringLiteral("focus"), QString::number(id));
        }
        emit requestSet(q);
    });
    connect(m_talkers, &QTableWidget::itemSelectionChanged, this,
            &DiversityWindow::updateLockButton);

    m_focusLine = DiversityWidgets::makeFieldLabel(QString(), frame);
    m_focusLine->setObjectName(QStringLiteral("diversityWindowFocusLabel"));
    m_focusLine->setAccessibleName(tr("Station lock status"));
    // Not word-wrapped: see the NOISE caption for why no label in this grid
    // may be. The phrase is short and fixed-shape.
    m_focusLine->setToolTip(
        tr("Which station the combiner is locked on, how many of their overs "
           "it has steered, how many other overs it has nulled meanwhile, and "
           "the best output SNR it reached on them."));
    m_focusLine->hide();

    auto* header = new QHBoxLayout;
    header->setSpacing(6);
    header->addWidget(m_talkersCount, 1);
    header->addWidget(m_lockButton);
    header->addWidget(m_memoryClearButton);
    body->addLayout(header);
    body->addWidget(m_focusLine);
    body->addWidget(m_talkers, 1);
    return frame;
}

int DiversityWindow::selectedTalkerId() const
{
    if (!m_talkers)
        return -1;
    const int row = m_talkers->currentRow();
    if (row < 0)
        return -1;
    const QTableWidgetItem* item = m_talkers->item(row, kTalkerNameColumn);
    if (!item)
        return -1;
    bool ok = false;
    const int id = item->data(Qt::UserRole).toInt(&ok);
    return ok ? id : -1;
}

void DiversityWindow::updateLockButton()
{
    if (!m_lockButton)
        return;
    if (m_haveFocus) {
        m_lockButton->setText(tr("Release lock"));
        m_lockButton->setEnabled(true);
        return;
    }
    const int id = selectedTalkerId();
    m_lockButton->setText(id >= 0 ? tr("Lock on #%1").arg(id) : tr("Lock on station"));
    m_lockButton->setEnabled(id >= 0);
}

void DiversityWindow::applyFocus(const QJsonValue& focus, bool haveTalker, int talkerId,
                                 const QString& talkerName)
{
    double id = 0.0;
    const QJsonObject f = focus.isObject() ? focus.toObject() : QJsonObject();
    m_haveFocus = focus.isObject() && memberNumber(f, "id", &id);
    m_focusId = m_haveFocus ? int(std::lround(id)) : -1;
    if (!m_haveFocus) {
        m_focusLine->hide();
        DiversityWidgets::setLive(m_focusLine, false);
        updateLockButton();
        return;
    }
    const QJsonValue nameValue = f.value(QStringLiteral("name"));
    const QString name = nameValue.isString() ? nameValue.toString() : QString();
    QStringList parts;
    parts << tr("LOCKED on %1").arg(talkerTag(m_focusId, name));
    if (f.value(QStringLiteral("nulling")).toBool() && haveTalker)
        parts << tr("nulling %1").arg(talkerTag(talkerId, talkerName));
    parts << tr("%1 overs").arg(f.value(QStringLiteral("overs")).toInt());
    parts << tr("%1 nulled").arg(f.value(QStringLiteral("nulled")).toInt());
    double best = 0.0;
    if (memberNumber(f, "best_db", &best))
        parts << tr("best %1 dB").arg(QString::asprintf("%+.1f", best));
    m_focusLine->setText(parts.join(QStringLiteral(" · ")));
    DiversityWidgets::setLive(m_focusLine, true);
    m_focusLine->show();
    updateLockButton();
}

QWidget* DiversityWindow::buildEventsPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("EVENTS"), QStringLiteral("diversityWindowEvents"), body, this);

    // Alignment is one line, not four fields. It has exactly one question in
    // it -- "are the two tuners lined up?" -- and four labelled boxes made
    // that look like four questions.
    m_alignLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowAlignLabel"),
        QStringLiteral("not aligned · lag -99999 · peak 0.000 · realigning…"),
        tr("The two tuners each start their own sample stream, so before "
           "anything can be combined the gate has to know how far apart they "
           "are. Lag is that offset in samples; the peak is how confident the "
           "correlation was about it (above about 0.5 is solid); realigning "
           "means it is measuring again right now. Press REALIGN after "
           "changing frequency or sample rate."),
        frame);
    m_alignLine->setAccessibleName(tr("Tuner alignment"));
    body->addWidget(m_alignLine);

    m_captureResult = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowCaptureLabel"),
        QStringLiteral("capture: 20260901-120000-diversity.wav"),
        tr("The last raw two-channel capture written by the CAPTURE button, "
           "or the reason the last one failed. Hover for the full path."),
        frame);
    m_captureResult->setAccessibleName(tr("Last capture"));
    m_captureResult->setText(tr("capture: —"));
    body->addWidget(m_captureResult);

    m_events = new QListWidget(frame);
    m_events->setObjectName(QStringLiteral("diversityWindowEventsList"));
    m_events->setAccessibleName(tr("Diversity events"));
    m_events->setToolTip(
        tr("What has changed since you last looked: stations coming up and "
           "dropping out, the combiner re-solving, a steady carrier being "
           "nulled, the gate going away and coming back. Newest at the top, "
           "last two hundred kept."));
    m_events->setAccessibleDescription(m_events->toolTip());
    ThemeManager::instance().applyStyleSheet(m_events,
                                             QString::fromLatin1(kEventListStyle));
    m_events->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_events->setTextElideMode(Qt::ElideRight);
    m_events->setSelectionMode(QAbstractItemView::NoSelection);
    m_events->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(m_events, 1);

    m_eventsClearButton = new QPushButton(tr("Clear"), frame);
    m_eventsClearButton->setObjectName(QStringLiteral("diversityWindowEventsClearButton"));
    m_eventsClearButton->setAccessibleName(tr("Clear the event list"));
    m_eventsClearButton->setToolTip(
        tr("Empty the list. It clears the display only -- nothing on the gate "
           "changes, and new events keep arriving."));
    connect(m_eventsClearButton, &QPushButton::clicked, m_events, &QListWidget::clear);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch(1);
    buttons->addWidget(m_eventsClearButton);
    body->addLayout(buttons);
    return frame;
}


// --------------------------------------------------------------------------
// The TALKERS table's own bookkeeping.
// --------------------------------------------------------------------------

bool DiversityWindow::talkersBusy() const
{
    // QAbstractItemView::state() is protected and the view has no
    // "editing started" signal -- but an open editor IS a real widget
    // parented to the viewport, and a table with no index widgets has no
    // other direct child there. So the presence of one is read straight off
    // the widget tree rather than mirrored into a flag that could drift out
    // of step with it.
    return m_talkers
           && m_talkers->viewport()->findChild<QWidget*>(
                  QString(), Qt::FindDirectChildrenOnly) != nullptr;
}

void DiversityWindow::applyTalkers(const QJsonArray& memory, bool haveTalker, int talkerId,
                                   double talkerSinceS)
{
    // The header counts and names the live talker whether or not the table
    // itself needs rebuilding -- "talking 14 s" ticks every poll, and it is a
    // fixed-width phrase, so it costs nothing to keep current.
    m_talkersCount->setText(
        haveTalker
            ? tr("%1 talkers remembered · #%2 talking %3")
                  .arg(QString::number(memory.size()), QString::number(talkerId),
                       DiversityEventLog::shortDuration(talkerSinceS))
            : tr("%1 talkers remembered · nobody talking").arg(memory.size()));

    // A rebuild while a Name cell is open would destroy the editor and the
    // half-typed callsign in it.
    if (talkersBusy())
        return;

    QStringList rows;
    QVector<int> ids;
    int liveRow = -1;
    rows.reserve(memory.size());
    ids.reserve(memory.size());
    for (const QJsonValue& v : memory) {
        const QJsonObject entry = v.toObject();
        double id = 0.0;
        double phase = 0.0;
        double ratio = 0.0;
        double age = 0.0;
        double first = 0.0;
        const bool haveId = memberNumber(entry, "id", &id);
        const int idValue = haveId ? int(std::lround(id)) : -1;
        const bool live = haveTalker && haveId && idValue == talkerId;
        if (live)
            liveRow = int(rows.size());
        const QJsonValue nameValue = entry.value(QStringLiteral("name"));
        const QString name = nameValue.isString() ? nameValue.toString() : QString();

        QStringList cells;
        // "● 3" for whoever is on the air: the same filled/hollow distinction
        // the dial uses, so the two views say the same thing the same way.
        cells << cellOr(haveId, live ? QStringLiteral("● %1").arg(idValue)
                                     : QString::number(idValue));
        cells << name;
        cells << cellOr(memberNumber(entry, "phase_deg", &phase),
                        QString::asprintf("%.0f°", phase));
        cells << cellOr(memberNumber(entry, "ratio_db", &ratio),
                        QString::asprintf("%+.1f dB", ratio));
        cells << QString::number(entry.value(QStringLiteral("hits")).toInt());
        cells << cellOr(memberNumber(entry, "age_s", &age),
                        DiversityEventLog::shortDuration(age));
        cells << cellOr(memberNumber(entry, "first_seen_s", &first),
                        DiversityEventLog::shortDuration(first));
        rows << cells.join(kRowSep);
        ids << idValue;
    }

    if (rows == m_talkerRows && liveRow == m_talkerLiveRow)
        return;
    m_talkerRows = rows;
    m_talkerLiveRow = liveRow;

    // itemChanged() is how a Name edit is committed, so every write below
    // would otherwise read as one. The flag is cleared before restyling so a
    // theme repaint cannot leave it stuck set.
    m_talkersRebuilding = true;
    const QSignalBlocker block(m_talkers);
    m_talkers->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList cells = rows[r].split(kRowSep);
        for (int c = 0; c < cells.size() && c < kTalkerColumnCount; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == kTalkerNameColumn) {
                item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                item->setData(Qt::UserRole, ids[r]);
                // Only a talker the gate gave an id for can be named: there
                // is nothing to address the write to otherwise.
                if (ids[r] < 0)
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            } else {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }
            m_talkers->setItem(r, c, item);
        }
    }
    m_talkersRebuilding = false;
    restyleTalkerRows();
}

void DiversityWindow::restyleTalkerRows()
{
    if (!m_talkers)
        return;
    // A token-backed brush rather than a stylesheet rule: the highlight moves
    // from row to row every over, and a per-poll setStyleSheet() would reparse
    // the sheet and drop the view's cached style each time.
    const QBrush live(ThemeManager::instance().color(
        this, QStringLiteral("color.accent.dim")));
    const QBrush none(Qt::NoBrush);
    const QSignalBlocker block(m_talkers);
    for (int r = 0; r < m_talkers->rowCount(); ++r) {
        for (int c = 0; c < kTalkerColumnCount; ++c) {
            if (QTableWidgetItem* item = m_talkers->item(r, c))
                item->setBackground(r == m_talkerLiveRow ? live : none);
        }
    }
}

void DiversityWindow::onTalkerItemChanged(QTableWidgetItem* item)
{
    if (m_talkersRebuilding || !item || item->column() != kTalkerNameColumn)
        return;
    bool ok = false;
    const int id = item->data(Qt::UserRole).toInt(&ok);
    if (!ok || id < 0)
        return;
    emit requestMemoryName(id, item->text());
    // Force the next poll to rebuild from what the gate actually took. If the
    // write did not land, the typed name goes away again -- which is the
    // honest outcome, and the only one that cannot leave the table showing a
    // label the gate has never heard of.
    m_talkerRows.clear();
}

void DiversityWindow::addEventLines(const QStringList& lines)
{
    if (lines.isEmpty() || !m_events)
        return;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    for (const QString& line : lines) {
        m_events->insertItem(0, QStringLiteral("%1  %2").arg(stamp, line));
        // Trim from the bottom: the list is newest-first, so the oldest entry
        // is the last row.
        while (m_events->count() > kMaxEvents)
            delete m_events->takeItem(m_events->count() - 1);
    }
}

} // namespace AetherSDR
