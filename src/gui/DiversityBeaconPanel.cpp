#include "gui/DiversityBeaconPanel.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonValue>
#include <QLabel>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
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

// Call, location, heard, SNR, A, B, phase, coherence, gain, steps, age.
constexpr int kColumnWidths[] = {64, 186, 46, 50, 46, 46, 52, 44, 48, 58, 76};
constexpr int kColumnCount = int(sizeof(kColumnWidths) / sizeof(kColumnWidths[0]));
constexpr int kStepsColumn = 9;
constexpr int kRowHeight = 22;
constexpr int kHeaderHeight = 24;

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

// "2 min ago". Beacons come round every three minutes, so a result older than
// a couple of cycles is history rather than news and the units say which.
QString ageText(const QJsonObject& result)
{
    const QJsonValue v = result.value(QStringLiteral("at"));
    if (!v.isDouble())
        return emDash();
    const qint64 age = QDateTime::currentSecsSinceEpoch() - qint64(v.toDouble());
    if (age < 0)
        return QCoreApplication::translate("DiversityBeaconPanel", "now");
    if (age < 60)
        return QCoreApplication::translate("DiversityBeaconPanel", "%1 s ago").arg(age);
    if (age < 3600)
        return QCoreApplication::translate("DiversityBeaconPanel", "%1 min ago")
            .arg(age / 60);
    return QCoreApplication::translate("DiversityBeaconPanel", "%1 h ago").arg(age / 3600);
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

QString resultKey(double bandHz, const QString& call)
{
    return QStringLiteral("%1|%2").arg(qint64(std::llround(bandHz))).arg(call);
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

    m_table = new QTableWidget(kBeaconCount, kColumnCount, this);
    m_table->setObjectName(QStringLiteral("diversityWindowBeaconTable"));
    m_table->setAccessibleName(tr("Beacon watch"));
    m_table->setHorizontalHeaderLabels({tr("Call"), tr("Location"), tr("Heard"),
                                        tr("SNR"), tr("A"), tr("B"), tr("Phase"),
                                        tr("Coh"), tr("Gain"), tr("Steps"),
                                        tr("Age")});
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
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_table);

    // Explicit line breaks, never word wrap: a wrapping label is
    // height-for-width and would put a scrollbar on a page that fits.
    m_caption = DiversityWidgets::makeFieldLabel(
        tr("Each beacon sends its call then 1 s dashes at 100, 10, 1 and 0.1 W "
           "every 3 min;\n"
           "the lowest step heard is the band's real reach. Phase from a known "
           "direction is what a geometry solve wants."),
        this);
    m_caption->setObjectName(QStringLiteral("diversityWindowBeaconCaption"));
    m_caption->setAccessibleName(tr("Beacon watch legend"));
    root->addWidget(m_caption);

    clear();
}

void DiversityBeaconPanel::clear()
{
    m_results.clear();
    m_bandHz = 0.0;
    m_nowCall.clear();
    m_header->setText(tr("beacon watch: %1").arg(emDash()));
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
        m_nowCall.clear();
        m_header->setText(tr("beacon watch: not available from this gate"));
        renderRows();
        return;
    }

    const QJsonValue bandValue = beacons.value(QStringLiteral("band_hz"));
    m_bandHz = bandValue.isDouble() ? bandValue.toDouble() : 0.0;

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

    const QJsonValue nowValue = beacons.value(QStringLiteral("now"));
    const QJsonObject now = nowValue.toObject();
    m_nowCall = now.value(QStringLiteral("call")).toString();

    if (m_bandHz <= 0.0) {
        m_header->setText(
            tr("no beacon frequency in the span — tune 14.100 / 18.110 / "
               "21.150 / 24.930 / 28.200"));
        renderRows();
        return;
    }

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
            m_bandHz > 0.0 ? m_results.value(resultKey(m_bandHz, call)) : QJsonObject();
        const bool haveResult = !result.isEmpty();
        const bool heard = result.value(QStringLiteral("heard")).toBool();

        QStringList cells;
        cells << call << where;
        if (!haveResult) {
            cells << emDash() << emDash() << emDash() << emDash() << emDash()
                  << emDash() << emDash() << emDash() << emDash();
        } else {
            cells << (heard ? QStringLiteral("●") : QStringLiteral("○"))
                  << signedNumber(result, "snr_db", 1)
                  << signedNumber(result, "snr_a", 1)
                  << signedNumber(result, "snr_b", 1)
                  << number(result, "phase_deg", 0)
                  << number(result, "coherence", 2)
                  << signedNumber(result, "gain_db", 1) << stepsText(result)
                  << ageText(result);
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

} // namespace AetherSDR
