// The SITE page's BEACONS, second half: the station's own locator, the
// per-band propagation summary, the pattern plot's column, and the BEACON
// CHECK countdown.
//
// A file of its own for the reason DiversityWindowSite.cpp and
// DiversityWindowPanels.cpp are: these are members of DiversityBeaconPanel and
// DiversityBeaconPanel.cpp is at the file-size budget AGENTS.md asks for. The
// split falls where the subject changes rather than where the line count did,
// though -- everything here is about the STATION (where it is, what it can
// hear across all five bands, and the one procedure that measures it), while
// what stays beside applyBeacons() is about the eighteen-row schedule on the
// band you are tuned to right now.
//
// THE CHECK'S ONE RULE. It moves the frequency and puts it back. It does not
// touch the mode, the combiner, the filter or the blanker, and it refuses to
// start at all when it does not know where the radio currently is -- a check
// that could not come home would be worse than no check.

#include "gui/DiversityBeaconPanel.h"

#include "core/ThemeManager.h"
#include "gui/DiversityBeaconPattern.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

QString emDash()
{
    return QStringLiteral("—");
}

// An integer field, or a dash. Its twin in DiversityBeaconPanel.cpp is the
// same three lines in that file's own anonymous namespace: two internal
// helpers rather than one shared symbol, because a header for a three-line
// formatter would be a worse trade than the repetition.
QString integerField(const QJsonObject& obj, const char* key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return emDash();
    return QString::number(qint64(std::llround(v.toDouble())));
}

// One full eighteen-slot cycle is 180 s. Ten more so a check that started in
// the middle of a slot still sees every beacon's whole turn -- the point of the
// button is that you get the complete rota without having to time it yourself.
constexpr int kCheckSeconds = 190;
// After a run comes home the poll stays wanted this long: the last slot is
// scored at its boundary, up to ten seconds after the countdown ends, and the
// report must not miss it.
constexpr int kSettleMs = 12000;

// The five frequencies the project shares, in band order. This is a fact about
// the world rather than something the gate reports, exactly like the schedule
// above it.
struct BeaconBand {
    const char* name;
    double      hz;
};

constexpr BeaconBand kBands[] = {
    {"20 m", 14100000.0},
    {"17 m", 18110000.0},
    {"15 m", 21150000.0},
    {"12 m", 24930000.0},
    {"10 m", 28200000.0},
};

constexpr int kBandCount = int(sizeof(kBands) / sizeof(kBands[0]));

// At most one line per beacon band, and the block is a fixed five lines high
// whether or not the gate has sampled that many: a block that grew as the night
// went on would move the table under it.
constexpr int kPropagationLines = kBandCount;

const char* kGridEditStyle =
    "QLineEdit { background: {{color.background.0}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 4px; }";

const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

constexpr int kGridEditWidth = 76;
constexpr int kSmallButtonHeight = 20;

// The band name for one of the five beacon frequencies, or the frequency
// itself for anything else -- a gate that grows a sixth band must not print a
// wrong one of the five.
QString bandName(double hz)
{
    for (const BeaconBand& band : kBands) {
        if (std::abs(band.hz - hz) < 1000.0)
            return QString::fromLatin1(band.name);
    }
    return QCoreApplication::translate("DiversityBeaconPanel", "%1 MHz")
        .arg(hz / 1.0e6, 0, 'f', 3);
}

// "4 min ago" from an absolute epoch stamp. Same units as the Age column, for
// the same reason: the rota turns every three minutes, so minutes is the unit
// in which "is this current?" has an answer.
QString sinceText(double epochSeconds)
{
    const qint64 age = QDateTime::currentSecsSinceEpoch() - qint64(epochSeconds);
    if (age < 0)
        return QCoreApplication::translate("DiversityBeaconPanel", "now");
    if (age < 60)
        return QCoreApplication::translate("DiversityBeaconPanel", "%1 s ago").arg(age);
    if (age < 3600)
        return QCoreApplication::translate("DiversityBeaconPanel", "%1 min ago")
            .arg(age / 60);
    return QCoreApplication::translate("DiversityBeaconPanel", "%1 h ago").arg(age / 3600);
}

// "-3.3" with a real minus sign rather than a hyphen: this is a number in a
// sentence, not a cell in a column of numbers, and the sentence is read.
QString signedDb(double v, int decimals)
{
    if (v < 0.0)
        return QStringLiteral("\u2212%1").arg(-v, 0, 'f', decimals);
    return QStringLiteral("+%1").arg(v, 0, 'f', decimals);
}

// "0.1" rather than "0.100000": the four steps are 100, 10, 1 and 0.1 W and
// each of them should print as itself.
QString wattsText(double w)
{
    return QString::number(w, 'g', 3);
}

} // namespace

// --------------------------------------------------------------------------
// The station's own locator
// --------------------------------------------------------------------------

QWidget* DiversityBeaconPanel::buildGridRow()
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("diversityWindowBeaconGridRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    m_gridRowLayout = layout;

    QLabel* caption = DiversityWidgets::makeFieldLabel(tr("Station grid"), row);
    caption->setObjectName(QStringLiteral("diversityWindowBeaconGridCaption"));
    caption->setAccessibleName(tr("Station grid caption"));
    layout->addWidget(caption);

    m_gridEdit = new QLineEdit(row);
    m_gridEdit->setObjectName(QStringLiteral("diversityWindowBeaconGridEdit"));
    m_gridEdit->setAccessibleName(tr("Station Maidenhead locator"));
    m_gridEdit->setMaxLength(6);
    m_gridEdit->setFixedWidth(kGridEditWidth);
    m_gridEdit->setPlaceholderText(tr("EM10"));
    m_gridEdit->setToolTip(
        tr("Your own Maidenhead locator, four or six characters (EM10, or "
           "EM10bk for the extra precision). It is the second point every "
           "bearing on this page needs; the gate already knows where the "
           "beacons are. Case does not matter. Nothing else in this window "
           "uses it and it never leaves the gate."));
    m_gridEdit->setAccessibleDescription(m_gridEdit->toolTip());
    ThemeManager::instance().applyStyleSheet(m_gridEdit,
                                             QString::fromLatin1(kGridEditStyle));
    connect(m_gridEdit, &QLineEdit::returnPressed, this,
            [this] { m_gridSetButton->click(); });
    layout->addWidget(m_gridEdit);

    m_gridSetButton = new QPushButton(tr("SET"), row);
    m_gridSetButton->setObjectName(QStringLiteral("diversityWindowBeaconGridSet"));
    m_gridSetButton->setAccessibleName(tr("Set the station grid"));
    m_gridSetButton->setToolTip(tr("Tell the gate this locator. It answers with "
                                   "the bearing and distance to every beacon it "
                                   "has ever heard, on every band."));
    m_gridSetButton->setAccessibleDescription(m_gridSetButton->toolTip());
    m_gridSetButton->setFixedHeight(kSmallButtonHeight);
    applyToggleButtonStyle(m_gridSetButton);
    connect(m_gridSetButton, &QPushButton::clicked, this, [this] {
        const QString typed = m_gridEdit->text().trimmed().toUpper();
        if (typed.isEmpty())
            return;
        m_actionPending = true;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("grid"), typed);
        emit actionRequested(QStringLiteral("/diversity/set"), q);
    });
    layout->addWidget(m_gridSetButton);

    m_gridForgetButton = new QPushButton(tr("FORGET"), row);
    m_gridForgetButton->setObjectName(QStringLiteral("diversityWindowBeaconGridForget"));
    m_gridForgetButton->setAccessibleName(tr("Forget the station grid"));
    m_gridForgetButton->setToolTip(tr("Drop the locator. Bearings and distances "
                                      "go back to dashes; every result the gate "
                                      "has heard is kept."));
    m_gridForgetButton->setAccessibleDescription(m_gridForgetButton->toolTip());
    m_gridForgetButton->setFixedHeight(kSmallButtonHeight);
    applyToggleButtonStyle(m_gridForgetButton);
    connect(m_gridForgetButton, &QPushButton::clicked, this, [this] {
        m_actionPending = true;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("grid"), QStringLiteral("off"));
        emit actionRequested(QStringLiteral("/diversity/set"), q);
    });
    layout->addWidget(m_gridForgetButton);

    m_gridHint = DiversityWidgets::makeFieldLabel(QString(), row);
    m_gridHint->setObjectName(QStringLiteral("diversityWindowBeaconGridHint"));
    m_gridHint->setAccessibleName(tr("Station grid state"));
    layout->addWidget(m_gridHint);
    layout->addStretch(1);
    return row;
}

void DiversityBeaconPanel::setGridRowExtra(QWidget* extra)
{
    if (!m_gridRowLayout || !extra)
        return;
    extra->setParent(m_gridRowLayout->parentWidget());
    // Where the trailing stretch was: the note is the one thing on this row
    // that wants the leftover width, so it takes it instead.
    m_gridRowLayout->insertWidget(m_gridRowLayout->count() - 1, extra, 1);
}

// --------------------------------------------------------------------------
// The column beside the schedule: the pattern dial and the per-band summary
// --------------------------------------------------------------------------

QWidget* DiversityBeaconPanel::buildPatternColumn()
{
    auto* column = new QWidget(this);
    column->setObjectName(QStringLiteral("diversityWindowBeaconPatternColumn"));
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_pattern = new DiversityBeaconPattern(column);
    layout->addWidget(m_pattern);

    m_propagation = DiversityWidgets::makeFieldLabel(QString(), column);
    m_propagation->setObjectName(QStringLiteral("diversityWindowBeaconPropagationLabel"));
    m_propagation->setAccessibleName(tr("Beacon propagation summary"));
    m_propagation->setToolTip(
        tr("One line per band the gate has sampled, which is every band you "
           "have been tuned to a beacon frequency on since it started. It is "
           "the rest of the log: the eighteen rows beside this are only ever "
           "about the band you are on now. The weakest step is the whole "
           "point -- hearing the 0.1 W dash is thirty decibels of margin over "
           "hearing only the 100 W one."));
    m_propagation->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // Explicit line breaks, never word wrap -- and a height fixed at the full
    // five lines whether or not they are all filled, so a block that fills up
    // as the night goes on cannot move the table beside it.
    //
    // Sized off the font's own metrics rather than off a placeholder's
    // sizeHint(): sizeHint() and the QLabel::paintEvent() that later draws
    // the real text ask QFontMetrics two different questions (the tight
    // bounding box of the string vs. where drawText() actually lays lines
    // out), and on the real display the answers do not quite agree -- the
    // fifth line's descenders clipped into the feeds line under it
    // (2026-09-03, on the air). Zero slack was always the wrong amount of
    // slack for a fixed-height multi-line label; half a line of headroom
    // below the last line costs nothing here (the column has room to spare:
    // it is the table's fixed 330 px that sets this box's height, not the
    // other way round).
    //
    // ensurePolished() first: this label's QSS (10 px bold, kFieldLabelStyle)
    // has not been applied to its QFont yet at construction time, so an
    // unpolished fontMetrics() answers for whatever font it inherited from
    // its parent instead -- a bigger one, on this style, which would have
    // made the fixed height float loose of the font it is actually sized
    // for rather than short of it. Forcing the polish here is what makes
    // this box's height a fact about the font that draws into it.
    m_propagation->ensurePolished();
    const QFontMetrics propFm(m_propagation->fontMetrics());
    m_propagation->setFixedHeight(propFm.lineSpacing() * kPropagationLines
                                  + propFm.lineSpacing() / 2);
    layout->addWidget(m_propagation);

    // What the results are used for, because the operator asked on the air
    // (2026-09-03): "we don't see what we're doing with that information".
    // It sits in the room under the propagation block rather than on a row
    // of its own: the page has to fit the window it opens at.
    m_feedsLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBeaconFeedsLine"),
        tr("feeds → pattern dial (18 points) · propagation lines"),
        tr("Where these results go. The pattern dial plots loop B against loop A "
           "by bearing, one point per beacon heard on both loops (bearings need "
           "your grid). The propagation lines summarise each band. Nothing else "
           "reads them yet: the talker bearings and the FINDER are on their own."),
        column);
    m_feedsLine->setAccessibleName(tr("What the beacon results feed"));
    layout->addWidget(m_feedsLine);

    // The rest of what BEACON CHECK's report line could not fit on its own
    // row (buildCheckRow() sits above five band buttons, SWEEP ALL and
    // CANCEL, so the report gets whatever width is left of 1120 px and a
    // SWEEP's five-band report does not fit it). This is the spare room
    // renderReport() said it would use rather than leaving the rest of the
    // sentence in a tooltip nobody thought to hover (the operator's own
    // word for the old behaviour: "truncated").
    m_reportOverflow = DiversityWidgets::makeFieldLabel(QString(), column);
    m_reportOverflow->setObjectName(QStringLiteral("diversityWindowBeaconReportOverflow"));
    m_reportOverflow->setAccessibleName(tr("Beacon check report, in full"));
    m_reportOverflow->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // Ignored, horizontally: a five-band SWEEP report is one un-wrapped line
    // and can run to 600+ px, and this label's OWN sizeHint() is exactly
    // that string's width with nothing capping it. Left at the default
    // Preferred policy, setting that text once made the column ask the row
    // it sits in for its full width, which the window granted -- the whole
    // DiversityWindow grew past 1120 px on the very next layout pass to fit
    // a label that was supposed to fit in the column's existing spare room,
    // and every OTHER row's width computed on the next render (checkLine's
    // among them) inherited the wider window. Ignored tells the layout this
    // label's content is not a demand on its neighbours' room; renderReport()
    // still elides its text to whatever width that leaves it, the same way
    // it elides m_checkLine's.
    m_reportOverflow->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_reportOverflow->hide();
    layout->addWidget(m_reportOverflow);
    layout->addStretch(1);
    return column;
}

QWidget* DiversityBeaconPanel::buildStatusLine()
{
    m_status = new QLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("diversityWindowBeaconStatusLabel"));
    m_status->setAccessibleName(tr("Beacon action status"));
    ThemeManager::instance().applyStyleSheet(m_status,
                                             QString::fromLatin1(kStatusStyle));
    // A fixed height whether or not it is saying anything: a line that appeared
    // only when the gate refused something would shift the panel under it at
    // the moment the operator most needs it to stay still.
    m_status->setText(tr("the gate refused that"));
    m_status->setFixedHeight(m_status->sizeHint().height());
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_status->setText(QString());

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        m_status->setText(QString());
        DiversityWidgets::setLive(m_status, false);
    });

    m_checkTimer = new QTimer(this);
    m_checkTimer->setObjectName(QStringLiteral("diversityWindowBeaconCheckTimer"));
    m_checkTimer->setInterval(1000);
    connect(m_checkTimer, &QTimer::timeout, this, &DiversityBeaconPanel::checkTick);
    return m_status;
}

// The per-band summary: one sentence for every band the gate has sampled, in
// band order rather than in the gate's. Bands it has never watched are absent
// rather than dashed -- "nothing measured here" is what an empty line already
// says, and five dashed rows would look like five failures.
void DiversityBeaconPanel::renderPropagation(const QJsonValue& propagation)
{
    QStringList lines;
    const QJsonArray rows = propagation.toArray();
    for (const BeaconBand& band : kBands) {
        for (const QJsonValue& v : rows) {
            if (!v.isObject())
                continue;
            const QJsonObject row = v.toObject();
            const QJsonValue hz = row.value(QStringLiteral("band_hz"));
            if (!hz.isDouble() || std::abs(hz.toDouble() - band.hz) >= 1000.0)
                continue;
            const QJsonValue sampled = row.value(QStringLiteral("sampled"));
            if (!sampled.isDouble() || sampled.toDouble() <= 0.0)
                break;

            QStringList parts;
            parts << QString::fromLatin1(band.name);
            parts << tr("%1 of %2 heard")
                         .arg(integerField(row, "heard"), integerField(row, "of"));
            const QJsonValue weakest = row.value(QStringLiteral("best_w"));
            if (weakest.isDouble())
                parts << tr("weakest %1 W").arg(wattsText(weakest.toDouble()));
            const QJsonValue median = row.value(QStringLiteral("median_snr_db"));
            if (median.isDouble())
                parts << tr("median %1 dB").arg(signedDb(median.toDouble(), 1));
            const QJsonValue updated = row.value(QStringLiteral("updated"));
            if (updated.isDouble())
                parts << sinceText(updated.toDouble());
            lines << parts.join(QStringLiteral(" · "));
            break;
        }
    }
    m_propagation->setText(lines.isEmpty()
                               ? tr("no band sampled yet")
                               : lines.join(QChar('\n')));
}

// --------------------------------------------------------------------------
// BEACON CHECK
// --------------------------------------------------------------------------

QWidget* DiversityBeaconPanel::buildCheckRow()
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("diversityWindowBeaconCheckRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel* caption = DiversityWidgets::makeFieldLabel(tr("Beacon check"), row);
    caption->setObjectName(QStringLiteral("diversityWindowBeaconCheckCaption"));
    caption->setAccessibleName(tr("Beacon check caption"));
    layout->addWidget(caption);

    for (int i = 0; i < kBandCount; ++i) {
        const QString name = QString::fromLatin1(kBands[i].name);
        auto* button = new QPushButton(name, row);
        button->setObjectName(
            QStringLiteral("diversityWindowBeaconCheck%1").arg(i));
        button->setAccessibleName(tr("Check the %1 beacon frequency").arg(name));
        button->setToolTip(
            tr("Tune the active slice to %1 MHz and leave it there for one full "
               "cycle of all eighteen beacons (%2 seconds), then put the radio "
               "back exactly where it was. Nothing else moves -- not the mode, "
               "not the combiner, not the filter -- so what you measure is the "
               "station you actually use.")
                .arg(QString::number(kBands[i].hz / 1.0e6, 'f', 3))
                .arg(kCheckSeconds));
        button->setAccessibleDescription(button->toolTip());
        button->setFixedHeight(kSmallButtonHeight);
        applyToggleButtonStyle(button);
        connect(button, &QPushButton::clicked, this, [this, i] { startCheck(i); });
        layout->addWidget(button);
    }
    m_sweepButton = new QPushButton(tr("SWEEP ALL"), row);
    m_sweepButton->setObjectName(QStringLiteral("diversityWindowBeaconSweep"));
    m_sweepButton->setAccessibleName(tr("Sweep all five beacon frequencies"));
    m_sweepButton->setToolTip(
        tr("The five checks in a row, 20 m through 10 m: about %1 minutes away "
           "from where you are, then home, with one report for all five bands. "
           "CANCEL at any point comes straight home and reports the bands done.")
            .arg((kCheckSeconds * kBandCount + 30) / 60));
    m_sweepButton->setAccessibleDescription(m_sweepButton->toolTip());
    m_sweepButton->setFixedHeight(kSmallButtonHeight);
    applyToggleButtonStyle(m_sweepButton);
    connect(m_sweepButton, &QPushButton::clicked, this, &DiversityBeaconPanel::startSweep);
    layout->addWidget(m_sweepButton);

    m_checkLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBeaconCheckLine"),
        tr("CHECK 20 m · 3:10 left"),
        tr("How long the running check has left before the radio goes back to "
           "where it was. One cycle of the rota is three minutes; the extra ten "
           "seconds are so a check started mid-slot still hears every beacon's "
           "whole turn. Once it is home this line is the report: what the run "
           "heard, band by band, and hovering it gives every band's calls."),
        row);
    m_checkLine->setAccessibleName(tr("Beacon check countdown and report"));
    // Stretch 1, and no trailing addStretch(1) below: this label's own
    // sizeHint() tracks whatever text it last had, not the row's actual
    // budget, so a checkLine left at the default stretch of 0 just sits at
    // its own width forever and every byte of slack goes to an invisible
    // spacer at the row's end -- which is also why elidedText() against
    // width() only ever worked by accident before this. Letting the label
    // itself claim the leftover space is what makes width() a fact about
    // the row rather than about the string that happened to be in it last.
    layout->addWidget(m_checkLine, 1);

    m_checkCancelButton = new QPushButton(tr("CANCEL"), row);
    m_checkCancelButton->setObjectName(QStringLiteral("diversityWindowBeaconCheckCancel"));
    m_checkCancelButton->setAccessibleName(tr("Cancel the beacon check"));
    m_checkCancelButton->setToolTip(tr("Stop the check and tune straight back "
                                       "to where the radio was."));
    m_checkCancelButton->setAccessibleDescription(m_checkCancelButton->toolTip());
    m_checkCancelButton->setFixedHeight(kSmallButtonHeight);
    m_checkCancelButton->setEnabled(false);
    applyToggleButtonStyle(m_checkCancelButton);
    connect(m_checkCancelButton, &QPushButton::clicked, this,
            &DiversityBeaconPanel::cancelCheck);
    layout->addWidget(m_checkCancelButton);
    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    connect(m_settleTimer, &QTimer::timeout, this, [this] { emit checkStateChanged(); });
    renderReport();
    return row;
}

void DiversityBeaconPanel::setActiveSliceHz(double hz)
{
    // Only while nothing is running: during a check the radio is on a beacon
    // frequency BECAUSE of this panel, and remembering that as "where the
    // operator was" would strand them on it.
    if (m_checkBand < 0)
        m_activeSliceHz = hz;
}

void DiversityBeaconPanel::startCheck(int bandIndex)
{
    m_sweepQueue.clear();
    m_swept.clear();
    beginCheck(bandIndex);
}

void DiversityBeaconPanel::startSweep()
{
    m_sweepQueue.clear();
    m_swept.clear();
    for (int i = 1; i < kBandCount; ++i)
        m_sweepQueue << i;
    beginCheck(0);
    if (m_checkBand < 0)                    // refused: nowhere to come back to
        m_sweepQueue.clear();
}

void DiversityBeaconPanel::beginCheck(int bandIndex)
{
    if (bandIndex < 0 || bandIndex >= kBandCount)
        return;
    if (m_checkBand < 0) {
        // A check with nowhere to come back to is not a check. Without a slice
        // frequency (no radio model wired yet) it would tune away and leave
        // the operator to find their own way home.
        if (m_activeSliceHz <= 0.0) {
            showTransient(tr("no slice to tune — nowhere to come back to"));
            return;
        }
        m_checkReturnHz = m_activeSliceHz;
        m_runStartedAt = double(QDateTime::currentSecsSinceEpoch());
        m_settleUntilMs = 0;
    }
    // A band pressed while another is out keeps the same home: the radio is
    // where it is BECAUSE of this panel, so there is no new "where you were".
    m_checkBand = bandIndex;
    m_checkLeftS = kCheckSeconds;
    m_checkCancelButton->setEnabled(true);
    updateCheckLabel();
    renderReport();
    m_checkTimer->start();
    emit tuneRequested(kBands[bandIndex].hz);
    emit checkStateChanged();
}

void DiversityBeaconPanel::checkTick()
{
    if (m_checkBand < 0)
        return;
    --m_checkLeftS;
    if (m_checkLeftS > 0) {
        updateCheckLabel();
        return;
    }
    finishCheck();
}

void DiversityBeaconPanel::finishCheck()
{
    m_swept << m_checkBand;
    if (!m_sweepQueue.isEmpty()) {
        beginCheck(m_sweepQueue.takeFirst());   // straight on, not via home
        return;
    }
    endRun();
}

void DiversityBeaconPanel::cancelCheck()
{
    m_sweepQueue.clear();                       // a cancelled sweep does not resume
    if (m_checkBand < 0)
        return;
    endRun();
}

void DiversityBeaconPanel::endRun()
{
    const double home = m_checkReturnHz;
    m_checkBand = -1;
    m_checkLeftS = 0;
    m_checkReturnHz = 0.0;
    m_checkTimer->stop();
    m_checkCancelButton->setEnabled(false);
    updateCheckLabel();
    if (home > 0.0)
        emit tuneRequested(home);
    m_settleUntilMs = QDateTime::currentMSecsSinceEpoch() + kSettleMs;
    m_settleTimer->start(kSettleMs);
    renderReport();
    emit checkStateChanged();
}

bool DiversityBeaconPanel::pollWanted() const
{
    return m_checkBand >= 0 || QDateTime::currentMSecsSinceEpoch() < m_settleUntilMs;
}

void DiversityBeaconPanel::updateCheckLabel()
{
    if (m_checkBand < 0) {
        renderReport();
        return;
    }
    const QString left = tr("%1:%2 left")
                             .arg(QString::number(m_checkLeftS / 60),
                                  QStringLiteral("%1").arg(m_checkLeftS % 60, 2, 10,
                                                           QChar('0')));
    const QString band = QString::fromLatin1(kBands[m_checkBand].name);
    const int total = m_swept.size() + 1 + m_sweepQueue.size();
    if (total > 1) {
        m_checkLine->setText(tr("SWEEP %1/%2 · %3 · %4")
                                 .arg(QString::number(m_swept.size() + 1),
                                      QString::number(total), band, left));
        return;
    }
    m_checkLine->setText(tr("CHECK %1 · %2").arg(band, left));
}

void DiversityBeaconPanel::renderReport()
{
    // The report a run comes home with lives on the countdown line, which has
    // nothing to say while the radio is home. The operator asked for it on
    // the air (2026-09-03): "it didn't tune to a beacon, so I don't know if
    // it's doing something in the background".
    if (m_checkBand >= 0)
        return;
    if (m_swept.isEmpty()) {
        m_checkLine->setText(tr("idle — a check tunes away for %1 s and comes back")
                                 .arg(kCheckSeconds));
        // Null during buildCheckRow()'s own construction-time call: this row
        // is built before buildPatternColumn() makes the widget the overflow
        // would show in.
        if (m_reportOverflow) {
            m_reportOverflow->hide();
            m_reportOverflow->setText(QString());
        }
        return;
    }
    QStringList lines;      // one per band, with the calls: the hover text
    QStringList brief;      // one per band, counts only: the line itself
    for (int b : m_swept) {
        int sampled = 0;
        QList<QPair<double, QString>> heard;    // (weakest step heard, "CALL n W")
        for (auto it = m_results.cbegin(); it != m_results.cend(); ++it) {
            const QJsonObject& r = it.value();
            if (std::abs(r.value(QStringLiteral("band_hz")).toDouble() - kBands[b].hz) >= 1000.0)
                continue;
            // Results older than this run are another run's; a minute of
            // slack for the gate's clock against ours.
            if (r.value(QStringLiteral("at")).toDouble() < m_runStartedAt - 60.0)
                continue;
            ++sampled;
            if (!r.value(QStringLiteral("heard")).toBool())
                continue;
            const QJsonValue w = r.value(QStringLiteral("lowest_w"));
            const QString call = r.value(QStringLiteral("call")).toString();
            heard << qMakePair(w.isDouble() ? w.toDouble() : 1e9,
                               w.isDouble() ? tr("%1 %2 W").arg(call, wattsText(w.toDouble()))
                                            : call);
        }
        // Strongest path first (heard at the weakest step), then by call: a
        // QHash walk is in no order at all, and a report must read the same
        // twice.
        std::sort(heard.begin(), heard.end());
        QStringList names;
        for (const auto& h : heard)
            names << h.second;
        const QString name = QString::fromLatin1(kBands[b].name);
        if (sampled == 0) {
            lines << tr("%1: nothing scored — the results land with the next poll").arg(name);
            brief << tr("%1: unscored").arg(name);
            continue;
        }
        brief << tr("%1: %2 of %3 heard").arg(name).arg(names.size()).arg(sampled);
        lines << (names.isEmpty()
                      ? tr("%1: 0 of %2 heard — closed").arg(name).arg(sampled)
                      : tr("%1: %2 of %3 heard — %4")
                            .arg(name).arg(names.size()).arg(sampled)
                            .arg(names.join(QStringLiteral(", "))));
    }
    const QString when = tr("home at %1").arg(
        QDateTime::fromSecsSinceEpoch(qint64(m_runStartedAt)).toString(QStringLiteral("HH:mm")));
    // A single band has room for its calls on the line; a sweep gives counts
    // and keeps the calls for the hover. Never wrapped: a wrapping label is
    // height-for-width and would put a scrollbar on a page that fits.
    const QString full = when + QStringLiteral(" · ")
                         + (m_swept.size() == 1 ? lines : brief).join(QStringLiteral(" · "));
    // The row has whatever is left of 1120 px once the caption, five band
    // buttons, SWEEP ALL and CANCEL have theirs, and a five-band SWEEP report
    // can run past it (measured: 688 px wanted, 579 px to give it, worst
    // case). Eliding here rather than letting the label paint past its own
    // width is what stops it clipping mid-word into whatever is beside it;
    // the sentence eliding took is never simply lost, though -- it goes to
    // the spare room under the pattern dial (2026-09-03, on the air: the
    // operator's word for the old clipped line was "truncated"). width() is
    // 0 before this row has ever been laid out, which only happens before
    // any check has run -- m_swept.isEmpty() already returned above by then.
    const QFontMetrics checkFm(m_checkLine->fontMetrics());
    const QString elided = checkFm.elidedText(full, Qt::ElideRight, m_checkLine->width());
    m_checkLine->setText(elided);
    lines.prepend(when);
    m_checkLine->setToolTip(lines.join(QChar('\n')));
    const bool overflowed = elided != full;
    // The column's own width, not this label's: with Ignored set above, this
    // label no longer has a sizeHint()-driven width of its own to measure --
    // it gets whatever the column (m_propagation's own, stable width, ~366 px
    // measured) leaves it. That is narrower than the checkLine row's own 579,
    // so a five-band SWEEP report still elides here too rather than showing
    // whole -- but it is a SECOND line of real estate the row alone never
    // had, so even a two-and-a-half-band elide is strictly more of the
    // report than the row could ever have shown on its own, and the tooltip
    // still carries every band in full for whichever one this line stops
    // short of.
    const QString overflowElided = overflowed
        ? m_reportOverflow->fontMetrics().elidedText(full, Qt::ElideRight,
                                                      m_propagation->width())
        : QString();
    m_reportOverflow->setText(overflowElided);
    m_reportOverflow->setVisible(overflowed);
}

void DiversityBeaconPanel::renderFeeds(const QJsonObject& beacons)
{
    const int points = beacons.value(QStringLiteral("pattern")).toArray().size();
    m_feedsLine->setText(
        tr("feeds → pattern dial (%1 point%2%3) · propagation lines · nothing else")
            .arg(points)
            .arg(points == 1 ? QString() : QStringLiteral("s"),
                 m_stationGrid.isEmpty() ? tr(", no grid") : QString()));
}

void DiversityBeaconPanel::showTransient(const QString& text)
{
    m_status->setText(text);
    DiversityWidgets::setLive(m_status, true);
    m_statusTimer->start(5000);
}

void DiversityBeaconPanel::applyActionReply(const QJsonObject& reply)
{
    if (!m_actionPending)
        return;
    m_actionPending = false;
    const QString error = reply.value(QStringLiteral("error")).toString();
    if (error.isEmpty())
        return;
    // The field is left exactly as the operator typed it. The next beacons poll
    // is what puts the gate's own locator back into it, so the box never shows
    // a locator the gate does not have.
    showTransient(error);
}


} // namespace AetherSDR
