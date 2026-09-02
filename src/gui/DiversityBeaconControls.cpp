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
    m_propagation->setText(
        QStringList(kPropagationLines, QStringLiteral("00 m")).join(QChar('\n')));
    m_propagation->setFixedHeight(m_propagation->sizeHint().height());
    layout->addWidget(m_propagation);
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

    m_checkLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBeaconCheckLine"),
        tr("CHECK 20 m · 3:10 left"),
        tr("How long the running check has left before the radio goes back to "
           "where it was. One cycle of the rota is three minutes; the extra ten "
           "seconds are so a check started mid-slot still hears every beacon's "
           "whole turn."),
        row);
    m_checkLine->setAccessibleName(tr("Beacon check countdown"));
    layout->addWidget(m_checkLine);

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
    layout->addStretch(1);
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
    if (bandIndex < 0 || bandIndex >= kBandCount)
        return;
    // A check with nowhere to come back to is not a check. Without a slice
    // frequency (no radio model wired yet) it would tune away and leave the
    // operator to find their own way home.
    if (m_activeSliceHz <= 0.0) {
        showTransient(tr("no slice to tune — nowhere to come back to"));
        return;
    }
    if (m_checkBand >= 0)
        cancelCheck();

    m_checkReturnHz = m_activeSliceHz;
    m_checkBand = bandIndex;
    m_checkLeftS = kCheckSeconds;
    m_checkCancelButton->setEnabled(true);
    updateCheckLabel();
    m_checkTimer->start();
    emit tuneRequested(kBands[bandIndex].hz);
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
    cancelCheck();
}

void DiversityBeaconPanel::cancelCheck()
{
    if (m_checkBand < 0)
        return;
    const double home = m_checkReturnHz;
    m_checkBand = -1;
    m_checkLeftS = 0;
    m_checkReturnHz = 0.0;
    m_checkTimer->stop();
    m_checkCancelButton->setEnabled(false);
    updateCheckLabel();
    if (home > 0.0)
        emit tuneRequested(home);
}

void DiversityBeaconPanel::updateCheckLabel()
{
    if (m_checkBand < 0) {
        m_checkLine->setText(tr("idle — a check tunes away for %1 s and comes back")
                                 .arg(kCheckSeconds));
        return;
    }
    m_checkLine->setText(tr("CHECK %1 · %2:%3 left")
                             .arg(QString::fromLatin1(kBands[m_checkBand].name),
                                  QString::number(m_checkLeftS / 60),
                                  QStringLiteral("%1").arg(m_checkLeftS % 60, 2, 10,
                                                           QChar('0'))));
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
