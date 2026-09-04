#include "gui/DiversityBeaconPanel.h"

#include "core/ThemeManager.h"
#include "gui/DiversityBeaconPattern.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// The NCDXF/IARU schedule, in transmission order. This is a fixed fact about
// the world, not something the gate reports: the eighteen beacons key in this
// sequence, ten seconds apart, and the cycle repeats every three minutes. The
// order is the whole point of showing it -- it says who is on next.
struct Beacon {
    const char* call;
    const char* location;
};

constexpr Beacon kSchedule[] = {
    {"4U1UN", "United Nations, New York"},
    {"VE8AT", "Eureka, Nunavut"},
    {"W6WX", "Mt Umunhum, California"},
    {"KH6RS", "Maui, Hawaii"},
    {"ZL6B", "Masterton, New Zealand"},
    {"VK6RBP", "Rolystone, Australia"},
    {"JA2IGY", "Mt Asama, Japan"},
    {"RR9O", "Novosibirsk, Russia"},
    {"VR2B", "Hong Kong"},
    {"4S7B", "Colombo, Sri Lanka"},
    {"ZS6DN", "Pretoria, South Africa"},
    {"5Z4B", "Kikuyu, Kenya"},
    {"4X6TU", "Tel Aviv, Israel"},
    {"OH2B", "Lohja, Finland"},
    {"CS3B", "Sao Jorge, Madeira"},
    {"LU4AA", "Buenos Aires, Argentina"},
    {"OA4B", "Lima, Peru"},
    {"YV5B", "Caracas, Venezuela"},
};

constexpr int kBeaconCount = int(sizeof(kSchedule) / sizeof(kSchedule[0]));

// The five beacon frequencies, named. bandName() below is DiversityBeaconPanel
// duplicating the same tiny lookup DiversityBeaconControls.cpp's bandName()
// carries -- see integerField()'s comment above for why a header for a
// five-line table would be the worse trade.
struct BeaconBandName {
    double      hz;
    const char* name;
};

constexpr BeaconBandName kBandNames[] = {
    {14100000.0, "20 m"}, {18110000.0, "17 m"}, {21150000.0, "15 m"},
    {24930000.0, "12 m"}, {28200000.0, "10 m"},
};

// "20 m" for one of the five, the frequency itself for anything else -- a
// gate that grows a sixth band must not print a wrong one of the five.
QString bandName(double hz)
{
    for (const BeaconBandName& band : kBandNames) {
        if (std::abs(band.hz - hz) < 1000.0)
            return QString::fromLatin1(band.name);
    }
    return QCoreApplication::translate("DiversityBeaconPanel", "%1 MHz")
        .arg(hz / 1.0e6, 0, 'f', 3);
}

// No spaces round the slashes: with a stored band's name and age ahead of it
// ("showing 20 m · checked 1 min ago · no beacon frequency in the span --
// tune ..."), the header is the longest line on the page and has to stay on
// one line at the window's opening width.
const QString kTuneHint =
    QStringLiteral("14.100/18.110/21.150/24.930/28.200");

// Call, location, last pass, SNR, A, B, phase, coherence, gain, steps, age,
// bearing, distance, heard-of-samples. The first eleven are in the order they
// have always been in: the three the station grid made possible are appended
// rather than slotted in beside the numbers they belong with, because a column
// that moved would silently change what every existing hover and every existing
// test is pointing at.
constexpr int kColumnWidths[] = {62, 106, 30, 44, 44, 44, 40, 40, 40, 58, 72,
                                 40, 44, 40};
constexpr int kColumnCount = int(sizeof(kColumnWidths) / sizeof(kColumnWidths[0]));
constexpr int kSnrColumn = 3;
constexpr int kStepsColumn = 9;
constexpr int kBearingColumn = 11;
constexpr int kDistanceColumn = 12;
constexpr int kHeardColumn = 13;
// 17 rather than the 19 the table opened at. Eighteen fixed rows are the
// whole schedule and none of them can be dropped, so when the window grew a
// tab row and a FLOW strip above the pages this is where the SITE page's
// height came from: two pixels a row, thirty-six over the table, and the text
// in them is the same size it was.
constexpr int kRowHeight = 17;
constexpr int kHeaderHeight = 22;

// The four one-second dashes each beacon sends after its call.
constexpr int kPowerSteps = 4;


// Same table dressing the TALKERS and FINDER tables use, so the three read as
// one family of instrument rather than three tables in one window.
const char* kBeaconTableStyle =
    "QTableWidget { background: transparent; color: {{color.text.primary}};"
    " font-size: 11px; border: none; }"
    "QTableWidget::item { padding: 0px 3px; }"
    "QTableWidget::item:selected { background: {{color.background.2}};"
    " color: {{color.text.primary}}; }"
    "QHeaderView::section { background: {{color.background.1}};"
    " color: {{color.text.secondary}}; font-size: 10px; font-weight: bold;"
    " border: none; padding: 3px 3px; }";

QString emDash()
{
    return QStringLiteral("—");
}

// A field that is absent, null or not a number is "the gate did not report
// this", which is a different claim from zero -- render the dash.
QString number(const QJsonObject& obj, const char* key, int decimals)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return emDash();
    return QString::number(v.toDouble(), 'f', decimals);
}

QString signedNumber(const QJsonObject& obj, const char* key, int decimals)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return emDash();
    return QString::asprintf("%+.*f", decimals, v.toDouble());
}

// "2 min ago" from an absolute epoch stamp. Beacons come round every three
// minutes, so a result older than a couple of cycles is history rather than
// news and the units say which. Split out of ageText() so the header's
// "checked N ago" (which has an epoch but no result object to hand it) reads
// the same clock rather than a second copy of it.
QString ageSince(double atEpoch)
{
    const qint64 age = QDateTime::currentSecsSinceEpoch() - qint64(atEpoch);
    if (age < 0)
        return QCoreApplication::translate("DiversityBeaconPanel", "now");
    if (age < 60)
        return QCoreApplication::translate("DiversityBeaconPanel", "%1 s ago").arg(age);
    if (age < 3600)
        return QCoreApplication::translate("DiversityBeaconPanel", "%1 min ago")
            .arg(age / 60);
    return QCoreApplication::translate("DiversityBeaconPanel", "%1 h ago").arg(age / 3600);
}

QString ageText(const QJsonObject& result)
{
    const QJsonValue v = result.value(QStringLiteral("at"));
    if (!v.isDouble())
        return emDash();
    return ageSince(v.toDouble());
}

// "●●○○": the power ladder, lit from the top down. The lowest lit step is the
// band's reach -- three lit means the 1 W dash made it, which is 20 dB of
// margin over hearing the 100 W one alone.
QString stepsText(const QJsonObject& result)
{
    const QJsonValue v = result.value(QStringLiteral("steps_heard"));
    if (!v.isDouble())
        return emDash();
    const int heard = std::clamp(int(std::lround(v.toDouble())), 0, kPowerSteps);
    QString out;
    for (int i = 0; i < kPowerSteps; ++i)
        out += (i < heard) ? QStringLiteral("●") : QStringLiteral("○");
    return out;
}

// "3/7": how many of this beacon's passes on this band were heard, out of how
// many the gate sampled. Two numbers rather than a percentage because the
// denominator matters -- 1/1 and 7/7 are both "100 %" and only one of them is
// a claim about the path.
QString heardText(const QJsonObject& result)
{
    const QJsonValue n = result.value(QStringLiteral("heard_n"));
    const QJsonValue of = result.value(QStringLiteral("samples"));
    if (!n.isDouble() || !of.isDouble())
        return emDash();
    return QStringLiteral("%1/%2")
        .arg(qint64(std::llround(n.toDouble())))
        .arg(qint64(std::llround(of.toDouble())));
}

QString resultKey(double bandHz, const QString& call)
{
    return QStringLiteral("%1|%2").arg(qint64(std::llround(bandHz))).arg(call);
}

// An integer field, or a dash. Distances and bearings are whole numbers on the
// wire and rounding one here would be inventing a precision the gate did not
// claim.
QString integerField(const QJsonObject& obj, const char* key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return emDash();
    return QString::number(qint64(std::llround(v.toDouble())));
}

} // namespace

DiversityBeaconPanel::DiversityBeaconPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowBeaconPanel"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    m_header = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBeaconHeaderLabel"),
        tr("14.100 MHz · slot 18 · now: VK6RBP Rolystone, Australia · 10 s left"),
        tr("The beacon frequency inside the gate's span, which beacon is "
           "transmitting on it at this instant (the gate works that out from "
           "UTC, not from hearing it), and how long is left of its slot. Each "
           "beacon holds the frequency for ten seconds and the whole cycle of "
           "eighteen takes three minutes."),
        this);
    m_header->setAccessibleName(tr("Beacon schedule"));
    root->addWidget(m_header);

    root->addWidget(buildGridRow());
    root->addWidget(buildCheckRow());

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    m_table = new QTableWidget(kBeaconCount, kColumnCount, this);
    m_table->setObjectName(QStringLiteral("diversityWindowBeaconTable"));
    m_table->setAccessibleName(tr("Beacon watch"));
    m_table->setHorizontalHeaderLabels({tr("Call"), tr("Location"), tr("Last"),
                                        tr("SNR"), tr("A"), tr("B"), tr("Phase"),
                                        tr("Coh"), tr("Gain"), tr("Steps"),
                                        tr("Age"), tr("Brg"), tr("km"),
                                        tr("Heard")});
    ThemeManager::instance().applyStyleSheet(m_table,
                                             QString::fromLatin1(kBeaconTableStyle));

    // One hover explanation per column, written for somebody who has never used
    // the beacon project as an instrument before.
    static const struct { int column; const char* tip; } kHeaderTips[] = {
        {0, QT_TR_NOOP("The beacon's callsign. The eighteen rows are in "
                       "SCHEDULE order -- the order they transmit in -- so the "
                       "row under the one lit now is who you will hear next.")},
        {1, QT_TR_NOOP("Where the transmitter is. This is the whole value of a "
                       "beacon: the path is known, so what the receiver reports "
                       "is a measurement of your station rather than a guess "
                       "about theirs.")},
        {2, QT_TR_NOOP("Whether the gate correlated this beacon's dashes on its "
                       "last pass. A blank row is one that has not come round "
                       "yet on this band; a hollow mark is one that came round "
                       "and was not heard.")},
        {3, QT_TR_NOOP("Signal-to-noise of the combined output in a 500 Hz "
                       "bandwidth -- the standard the beacon project's own "
                       "reports are quoted in, so the number is comparable with "
                       "everybody else's.")},
        {4, QT_TR_NOOP("Signal-to-noise on loop A alone, in the same 500 Hz.")},
        {5, QT_TR_NOOP("Signal-to-noise on loop B alone. A beacon that is "
                       "several dB better on one loop every night is telling "
                       "you about that loop, not about propagation.")},
        {6, QT_TR_NOOP("The phase difference between the two loops on this "
                       "beacon. Unlike every other phase in this window it has "
                       "a KNOWN answer, because the transmitter's position is "
                       "known -- which is what a geometry solve needs to "
                       "calibrate itself against.")},
        {7, QT_TR_NOOP("How alike the two loops saw this beacon. Low coherence "
                       "on a signal this clean means multipath rather than "
                       "noise.")},
        {8, QT_TR_NOOP("What combining the two loops earned over the better one "
                       "on this beacon, in decibels.")},
        {9, QT_TR_NOOP("The four one-second power steps -- 100, 10, 1 and "
                       "0.1 W -- lit for the ones that were heard. The lowest "
                       "lit step is the path's real margin: each step down is "
                       "10 dB you did not need.")},
        {10, QT_TR_NOOP("How long ago this result was measured. The cycle "
                        "repeats every three minutes, so anything older than "
                        "that was missed on the last pass.")},
        {11, QT_TR_NOOP("The bearing to this beacon in degrees TRUE, worked out "
                        "from its locator and yours. Dashes until you have set "
                        "the station grid above -- a bearing needs two points "
                        "and the gate only knows one of them.")},
        {12, QT_TR_NOOP("Great-circle distance to the transmitter in "
                        "kilometres. It is what turns a signal report into a "
                        "path: the same SNR at 2,400 km and at 16,000 km are "
                        "not the same measurement.")},
        {13, QT_TR_NOOP("How many of this beacon's passes on this band you have "
                        "actually heard, out of how many the gate has sampled. "
                        "One in seven is a path that opens; seven in seven is a "
                        "path that is simply there.")},
    };
    for (const auto& entry : kHeaderTips) {
        if (QTableWidgetItem* header = m_table->horizontalHeaderItem(entry.column))
            header->setToolTip(tr(entry.tip));
    }

    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSortingEnabled(false);
    for (int c = 0; c < kColumnCount; ++c)
        m_table->setColumnWidth(c, kColumnWidths[c]);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setFixedHeight(kHeaderHeight);
    m_table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // A fixed eighteen rows, always: the table is the schedule, and a schedule
    // that grew and shrank with what had been heard would not be one.
    m_table->setFixedHeight(kHeaderHeight + kBeaconCount * kRowHeight + 2);
    int tableWidth = 2;
    for (int c = 0; c < kColumnCount; ++c)
        tableWidth += kColumnWidths[c];
    m_table->setFixedWidth(tableWidth);
    m_table->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    body->addWidget(m_table);

    body->addWidget(buildPatternColumn(), 1);
    root->addLayout(body);

    // Explicit line breaks, never word wrap: a wrapping label is
    // height-for-width and would put a scrollbar on a page that fits.
    // One line, not two: the page has to fit the window it opens at, and the
    // sentence this used to carry second ("phase from a known direction is
    // what a geometry solve wants") is already on the Phase column's hover,
    // where somebody wondering about that column will actually look.
    m_caption = DiversityWidgets::makeFieldLabel(
        tr("Each beacon sends its call then 1 s dashes at 100, 10, 1 and 0.1 W "
           "every 3 min; the lowest step heard is the band's real reach."),
        this);
    m_caption->setObjectName(QStringLiteral("diversityWindowBeaconCaption"));
    m_caption->setAccessibleName(tr("Beacon watch legend"));
    root->addWidget(m_caption);

    root->addWidget(buildStatusLine());

    clear();
}

void DiversityBeaconPanel::clear()
{
    m_results.clear();
    m_bandHz = 0.0;
    m_shownBandHz = 0.0;
    m_nowCall.clear();
    m_stationGrid.clear();
    m_actionPending = false;
    m_header->setText(tr("beacon watch: %1").arg(emDash()));
    if (!m_gridEdit->hasFocus()) {
        const QSignalBlocker blockGrid(m_gridEdit);
        m_gridEdit->clear();
    }
    m_gridHint->setText(tr("not set — bearings need it"));
    m_status->setText(QString());
    DiversityWidgets::setLive(m_status, false);
    // A check outlives neither the gate nor the window: the radio must not be
    // left parked on a beacon frequency by a countdown nobody can see.
    cancelCheck();
    updateCheckLabel();
    renderPropagation(QJsonValue());
    m_pattern->clearPattern();
    renderRows();
}

void DiversityBeaconPanel::applyBeacons(const QJsonObject& beacons)
{
    const bool available = beacons.value(QStringLiteral("available")).toBool()
                           && !beacons.contains(QStringLiteral("error"));
    if (!available) {
        // Not a band with no beacons on it: a gate that is too old for the
        // route, or one that has not aligned its tuners yet. Say which claim is
        // being made -- and keep the results, because they are still true about
        // the bands they were heard on.
        m_bandHz = 0.0;
        m_shownBandHz = 0.0;
        m_nowCall.clear();
        m_header->setText(tr("beacon watch: not available from this gate"));
        renderRows();
        return;
    }

    const QJsonValue bandValue = beacons.value(QStringLiteral("band_hz"));
    m_bandHz = bandValue.isDouble() ? bandValue.toDouble() : 0.0;

    // The locator, the per-band log and the dial are facts about the STATION,
    // not about the band in the span, so they are read before the schedule and
    // they survive a retune.
    const QJsonValue gridValue = beacons.value(QStringLiteral("station_grid"));
    m_stationGrid = gridValue.isString() ? gridValue.toString() : QString();
    // Never while the operator is typing into it: a poll that overwrote a
    // half-typed locator once a second would make the field unusable.
    if (!m_gridEdit->hasFocus()) {
        const QSignalBlocker blockGrid(m_gridEdit);
        m_gridEdit->setText(m_stationGrid);
    }
    m_gridHint->setText(m_stationGrid.isEmpty()
                            ? tr("not set — bearings need it")
                            : tr("set: %1").arg(m_stationGrid));

    renderPropagation(beacons.value(QStringLiteral("propagation")));
    m_pattern->applyPattern(beacons.value(QStringLiteral("pattern")).toArray(),
                            !m_stationGrid.isEmpty());
    renderFeeds(beacons);

    const auto remember = [this](const QJsonValue& value) {
        if (!value.isObject())
            return;
        const QJsonObject result = value.toObject();
        const QString call = result.value(QStringLiteral("call")).toString();
        const QJsonValue band = result.value(QStringLiteral("band_hz"));
        if (call.isEmpty() || !band.isDouble())
            return;
        m_results.insert(resultKey(band.toDouble(), call), result);
    };
    const QJsonArray results = beacons.value(QStringLiteral("results")).toArray();
    for (const QJsonValue& v : results)
        remember(v);
    remember(beacons.value(QStringLiteral("last")));
    renderReport();

    const QJsonValue nowValue = beacons.value(QStringLiteral("now"));
    const QJsonObject now = nowValue.toObject();
    m_nowCall = now.value(QStringLiteral("call")).toString();

    if (m_bandHz <= 0.0) {
        // No beacon frequency in the span right now -- but a CHECK or a
        // SWEEP taken earlier this session left real rows behind, keyed by
        // the band they were heard on, and the operator's last question was
        // "did my results just disappear?" (2026-09-03: seen after a
        // relaunch). The table follows whichever band was checked most
        // recently rather than going blank the instant the slice is
        // somewhere else -- that band's rows are still true.
        double checkedAt = 0.0;
        m_shownBandHz = newestStoredBandHz(&checkedAt);
        m_header->setText(m_shownBandHz > 0.0
                              ? tr("showing %1 · checked %2 · no beacon "
                                   "frequency in the span — tune %3")
                                    .arg(bandName(m_shownBandHz), ageSince(checkedAt), kTuneHint)
                              : tr("no beacon frequency in the span — tune %1")
                                    .arg(kTuneHint));
        renderRows();
        return;
    }
    m_shownBandHz = m_bandHz;

    QStringList parts;
    parts << tr("%1 MHz").arg(m_bandHz / 1e6, 0, 'f', 3);
    const QJsonValue slot = beacons.value(QStringLiteral("slot"));
    if (slot.isDouble())
        parts << tr("slot %1").arg(qint64(std::llround(slot.toDouble())));
    if (!m_nowCall.isEmpty()) {
        const QString where = now.value(QStringLiteral("location")).toString();
        parts << tr("now: %1 %2").arg(m_nowCall, where.isEmpty() ? emDash() : where);
        const QJsonValue left = now.value(QStringLiteral("seconds_left"));
        if (left.isDouble())
            parts << tr("%1 s left").arg(qint64(std::llround(left.toDouble())));
    } else {
        parts << tr("no beacon in this slot");
    }
    m_header->setText(parts.join(QStringLiteral(" · ")));

    renderRows();
}

void DiversityBeaconPanel::renderRows()
{
    // A token-backed brush rather than a stylesheet rule: the highlight moves
    // to the next row every ten seconds, and a per-poll setStyleSheet() would
    // reparse the sheet and drop the view's cached style each time.
    const QBrush live(
        ThemeManager::instance().color(this, QStringLiteral("color.accent.dim")));
    const QBrush none(Qt::NoBrush);
    const QSignalBlocker block(m_table);

    for (int row = 0; row < kBeaconCount; ++row) {
        const QString call = QString::fromLatin1(kSchedule[row].call);
        const QString where = QString::fromLatin1(kSchedule[row].location);
        const QJsonObject result =
            m_shownBandHz > 0.0 ? m_results.value(resultKey(m_shownBandHz, call))
                                : QJsonObject();
        const bool haveResult = !result.isEmpty();
        const bool heard = result.value(QStringLiteral("heard")).toBool();

        QStringList cells;
        cells << call << where;
        if (!haveResult) {
            for (int i = 2; i < kColumnCount; ++i)
                cells << emDash();
        } else {
            cells << (heard ? QStringLiteral("●") : QStringLiteral("○"))
                  << signedNumber(result, "snr_db", 1)
                  << signedNumber(result, "snr_a", 1)
                  << signedNumber(result, "snr_b", 1)
                  << number(result, "phase_deg", 0)
                  << number(result, "coherence", 2)
                  << signedNumber(result, "gain_db", 1) << stepsText(result)
                  << ageText(result)
                  // Bearing and distance are the gate's own, computed from the
                  // station locator. Without one it sends null and the dash is
                  // the honest answer -- a bearing this window worked out from
                  // half the data would be a drawn guess.
                  << integerField(result, "bearing_deg")
                  << integerField(result, "distance_km")
                  << heardText(result);
        }

        for (int col = 0; col < kColumnCount; ++col) {
            QTableWidgetItem* item = m_table->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                item->setTextAlignment(col <= 1 ? (Qt::AlignLeft | Qt::AlignVCenter)
                                                : (Qt::AlignRight | Qt::AlignVCenter));
                m_table->setItem(row, col, item);
            }
            item->setText(cells.at(col));
            item->setBackground(call == m_nowCall ? live : none);
        }

        // The SNR cell shows the LATEST pass; the average over every pass the
        // gate has kept lives on its hover. One number answers "is it open
        // now", the other answers "is this path any good", and putting the
        // second in the cell would make the column stop reacting to the band.
        if (QTableWidgetItem* snr = m_table->item(row, kSnrColumn)) {
            const QJsonValue mean = result.value(QStringLiteral("snr_mean_db"));
            snr->setToolTip(mean.isDouble()
                                ? tr("mean over %1 pass(es): %2 dB")
                                      .arg(integerField(result, "samples"),
                                           QString::asprintf("%+.1f", mean.toDouble()))
                                : tr("no averaged signal-to-noise for this "
                                     "beacon on this band yet"));
        }

        // The lowest step heard is the row's headline number, so it is worth a
        // hover of its own rather than leaving it to the column tooltip.
        if (QTableWidgetItem* steps = m_table->item(row, kStepsColumn)) {
            const QJsonValue lowest = result.value(QStringLiteral("lowest_w"));
            steps->setToolTip(lowest.isDouble()
                                  ? tr("lowest step heard: %1 W")
                                        .arg(lowest.toDouble(), 0, 'g', 3)
                                  : tr("no step heard on this band yet"));
        }
    }
}

double DiversityBeaconPanel::newestStoredBandHz(double* atOut) const
{
    double bestBand = 0.0;
    double bestAt = -1.0;
    for (auto it = m_results.cbegin(); it != m_results.cend(); ++it) {
        const QJsonValue atValue = it.value().value(QStringLiteral("at"));
        const QJsonValue bandValue = it.value().value(QStringLiteral("band_hz"));
        if (!atValue.isDouble() || !bandValue.isDouble())
            continue;
        if (atValue.toDouble() > bestAt) {
            bestAt = atValue.toDouble();
            bestBand = bandValue.toDouble();
        }
    }
    if (atOut)
        *atOut = bestAt;
    return bestBand;
}

} // namespace AetherSDR
