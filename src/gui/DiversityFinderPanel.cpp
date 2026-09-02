#include "gui/DiversityFinderPanel.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRectF>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// Gate-reported and unbounded on the wire -- the same defensive cap the
// spatial waterfall and the noise map keep on their own arrays.
constexpr int kMaxActivityBins = 4096;

// The gate contract caps this at 12; the cap here is ours, so a gate that
// forgets cannot fill the page.
constexpr int kMaxCandidates = 12;

constexpr int kStripHeight = 16;

// kHz, score, SNR, syllabic, active, last heard, phase, coherence, gain, Tune.
constexpr int kColumnWidths[] = {74, 46, 52, 56, 56, 56, 48, 56, 52, 68};
constexpr int kColumnCount = int(sizeof(kColumnWidths) / sizeof(kColumnWidths[0]));
constexpr int kTuneColumn = kColumnCount - 1;
constexpr int kRowHeight = 24;
constexpr int kTableMinHeight = 150;

// Same table dressing the TALKERS table uses, so the two read as one family of
// instrument rather than two tables that happen to be in the same window.
const char* kFinderTableStyle =
    "QTableWidget { background: transparent; color: {{color.text.primary}};"
    " font-size: 11px; border: none; }"
    "QTableWidget::item { padding: 0px 3px; }"
    "QTableWidget::item:selected { background: {{color.background.2}};"
    " color: {{color.text.primary}}; }"
    "QHeaderView::section { background: {{color.background.1}};"
    " color: {{color.text.secondary}}; font-size: 10px; font-weight: bold;"
    " border: none; padding: 3px 3px; }";

const char* kTuneButtonStyle =
    "QPushButton { color: {{color.accent.bright}}; font-size: 10px; font-weight: bold;"
    " padding: 1px 6px; border: 1px solid {{color.accent}}; border-radius: 3px;"
    " background: transparent; }"
    "QPushButton:hover { background: {{color.background.1}}; }"
    "QPushButton:pressed { background: {{color.background.3}}; }";

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

// Signed, because a diversity gain of -2 dB is a real and useful answer: it
// says the pair is currently costing you something there.
QString signedNumber(const QJsonObject& obj, const char* key, int decimals)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return emDash();
    return QString::asprintf("%+.*f", decimals, v.toDouble());
}

// "3:04" for how long an exchange has been going, so a two-minute net and a
// two-second click are told apart at a glance rather than by counting digits.
QString minutesSeconds(const QJsonObject& obj, const char* key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return emDash();
    const qint64 total = qint64(std::llround(std::max(0.0, v.toDouble())));
    return QStringLiteral("%1:%2")
        .arg(total / 60)
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// "now" while somebody is mid-syllable, seconds otherwise: "0 s ago" and
// "still talking" are different things to an operator deciding where to go.
QString lastHeard(const QJsonObject& obj)
{
    const QJsonValue v = obj.value(QStringLiteral("last_s"));
    if (!v.isDouble())
        return emDash();
    const double s = v.toDouble();
    if (s < 1.0)
        return QCoreApplication::translate("DiversityFinderPanel", "now");
    return QCoreApplication::translate("DiversityFinderPanel", "%1 s")
        .arg(qint64(std::llround(s)));
}

} // namespace

// --------------------------------------------------------------------------
// DiversityActivityStrip
// --------------------------------------------------------------------------

DiversityActivityStrip::DiversityActivityStrip(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowActivityStrip"));
    setFixedHeight(kStripHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Band activity"));
    setAccessibleDescription(
        tr("How much of the last ten minutes each part of the span carried "
           "voice. Read-only."));
    setToolTip(tr("The share of the last ten minutes each column of the "
                  "waterfall above carried voice-shaped energy. A regular net "
                  "is a solid bar; a single over is a faint one; a dead patch "
                  "of band is bare."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these -- declare them so Inspect mode surfaces
    // the tokens actually read, and repaint on a live theme switch.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.accent.bright"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversityActivityStrip::setActivity(const QVector<float>& activity)
{
    m_activity = activity.mid(0, kMaxActivityBins);
    update();
}

void DiversityActivityStrip::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));
    if (m_activity.isEmpty())
        return;

    // The bar colour is the accent's own hue and saturation with the VALUE
    // carrying the number -- computed at paint time from a token rather than
    // from a literal, so a user theme still owns the colour.
    const QColor accent = tm.color(this, QStringLiteral("color.accent.bright"));
    const int n = int(m_activity.size());
    const double w = double(width()) / double(n);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        const double a = std::clamp(double(m_activity[i]), 0.0, 1.0);
        if (a <= 0.0)
            continue;
        p.setBrush(QColor::fromHsvF(accent.hueF() < 0.0 ? 0.0 : accent.hueF(),
                                    accent.saturationF(), a * accent.valueF()));
        p.drawRect(QRectF(i * w, 0.0, std::max(1.0, w), double(height())));
    }
}

// --------------------------------------------------------------------------
// DiversityFinderPanel
// --------------------------------------------------------------------------

DiversityFinderPanel::DiversityFinderPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFinderPanel"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    m_strip = new DiversityActivityStrip(this);
    root->addWidget(m_strip);

    // No word wrap anywhere on this page: a wrapping label is height-for-width
    // and makes the whole grid it sits in height-for-width too, which is what
    // puts a scrollbar on a window that fits. Line breaks are explicit.
    m_stripCaption = DiversityWidgets::makeFieldLabel(
        tr("activity: share of the last 10 min each column carried voice"), this);
    m_stripCaption->setObjectName(QStringLiteral("diversityWindowActivityCaption"));
    m_stripCaption->setAccessibleName(tr("Activity strip legend"));
    root->addWidget(m_stripCaption);

    m_table = new QTableWidget(0, kColumnCount, this);
    m_table->setObjectName(QStringLiteral("diversityWindowFinderTable"));
    m_table->setAccessibleName(tr("Candidate conversations"));
    m_table->setHorizontalHeaderLabels({tr("kHz"), tr("Score"), tr("SNR"), tr("Syll"),
                                        tr("Active"), tr("Heard"), tr("Phase"),
                                        tr("Coh"), tr("Gain"), tr("Tune")});
    ThemeManager::instance().applyStyleSheet(m_table,
                                             QString::fromLatin1(kFinderTableStyle));

    // One hover explanation per column, written for somebody who has never met
    // a diversity combiner. "Phase", not "bearing": two loops give the phase
    // difference between the antennas and nothing more.
    static const struct { int column; const char* tip; } kHeaderTips[] = {
        {0, QT_TR_NOOP("Centre frequency of the conversation, in kilohertz. "
                       "Tune here and the receiver goes to this frequency.")},
        {1, QT_TR_NOOP("How confident the gate is that this is a conversation "
                       "worth your time: voice shape, strength and how long it "
                       "has been going, combined. The table is sorted by it.")},
        {2, QT_TR_NOOP("Signal-to-noise of the better loop at this frequency, "
                       "in decibels.")},
        {3, QT_TR_NOOP("How speech-shaped the envelope is: human speech "
                       "modulates at a few syllables a second, which a carrier, "
                       "a data mode and a noise blanker do not. Near 1 is "
                       "clearly voice.")},
        {4, QT_TR_NOOP("How long this frequency has carried voice inside the "
                       "last ten minutes, as minutes and seconds.")},
        {5, QT_TR_NOOP("How long ago somebody last spoke here. \"now\" means "
                       "the gate is hearing them as you read this.")},
        {6, QT_TR_NOOP("The phase difference between the two loops for this "
                       "signal. It is a PHASE, not a bearing -- two antennas "
                       "cannot tell which of two directions it came from -- but "
                       "two stations with different phases are in different "
                       "places.")},
        {7, QT_TR_NOOP("How alike the two loops see this signal. High means one "
                       "direction and a null is available; low means scatter, "
                       "and only maximal-ratio gain is on offer.")},
        {8, QT_TR_NOOP("The diversity gain the pair can earn here, over the "
                       "better single loop. Often near zero on plain sky noise: "
                       "that is the physics, not a fault.")},
        {9, QT_TR_NOOP("Tune the receiver to this conversation. The combiner is "
                       "switched to track so it starts solving for whoever is "
                       "talking as soon as you arrive.")},
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
    // No stretched last section: the last column holds the Tune buttons, and a
    // button stretched across half the window reads as the most important
    // thing on the page rather than as the small offer it is. The leftover
    // width stays empty, which is what a nine-number row actually needs.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setMinimumHeight(kTableMinHeight);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { tuneRow(row); });
    root->addWidget(m_table, 1);

    m_caption = DiversityWidgets::makeFieldLabel(
        tr("voice-shaped energy found in the last 10 min of both loops;\n"
           "gain is the diversity gain the pair can earn there"), this);
    m_caption->setObjectName(QStringLiteral("diversityWindowFinderCaption"));
    m_caption->setAccessibleName(tr("Finder legend"));
    root->addWidget(m_caption);
}

void DiversityFinderPanel::clear()
{
    m_strip->setActivity({});
    m_rowHz.clear();
    m_table->setRowCount(0);
}

void DiversityFinderPanel::applyFinder(const QJsonObject& finder)
{
    const bool available = finder.value(QStringLiteral("available")).toBool()
                           && !finder.contains(QStringLiteral("error"));
    if (!available) {
        clear();
        return;
    }

    QVector<float> activity;
    const QJsonValue activityValue = finder.value(QStringLiteral("activity"));
    if (activityValue.isArray()) {
        const QJsonArray arr = activityValue.toArray();
        activity.reserve(std::min(int(arr.size()), kMaxActivityBins));
        for (const QJsonValue& v : arr) {
            if (activity.size() >= kMaxActivityBins)
                break;
            activity.push_back(v.isDouble() ? float(v.toDouble()) : 0.0f);
        }
    }
    m_strip->setActivity(activity);

    setCandidates(finder);
}

void DiversityFinderPanel::setCandidates(const QJsonObject& finder)
{
    const QJsonArray candidates = finder.value(QStringLiteral("candidates")).toArray();
    const int rows = std::min(int(candidates.size()), kMaxCandidates);

    m_rowHz.clear();
    m_rowHz.reserve(rows);
    m_table->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const QJsonObject c = candidates[r].toObject();
        const QJsonValue hzValue = c.value(QStringLiteral("hz"));
        const double hz = hzValue.isDouble() ? hzValue.toDouble() : 0.0;
        m_rowHz.push_back(hz);

        const QStringList cells{
            hz > 0.0 ? QString::number(hz / 1e3, 'f', 2) : emDash(),
            number(c, "score", 2),
            signedNumber(c, "snr_db", 1),
            number(c, "syllabic", 2),
            minutesSeconds(c, "active_s"),
            lastHeard(c),
            number(c, "phase_deg", 0),
            number(c, "coherence", 2),
            signedNumber(c, "gain_db", 1),
        };
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_table->setItem(r, col, item);
        }

        // A button per row rather than one button plus a selection: the whole
        // point of the table is "go there", and a control that needs a
        // selection first is a second step for no gain.
        auto* tune = new QPushButton(tr("Tune"), m_table);
        tune->setObjectName(QStringLiteral("diversityWindowFinderTune"));
        tune->setAccessibleName(hz > 0.0
                                    ? tr("Tune to %1 kHz").arg(hz / 1e3, 0, 'f', 2)
                                    : tr("Tune to this candidate"));
        tune->setToolTip(tr("Tune the receiver here and switch the combiner to "
                            "track."));
        tune->setEnabled(hz > 0.0);
        ThemeManager::instance().applyStyleSheet(tune,
                                                 QString::fromLatin1(kTuneButtonStyle));
        connect(tune, &QPushButton::clicked, this, [this, r] { tuneRow(r); });
        m_table->setCellWidget(r, kTuneColumn, tune);
    }
}

void DiversityFinderPanel::tuneRow(int row)
{
    if (row < 0 || row >= m_rowHz.size())
        return;
    const double hz = m_rowHz[row];
    if (hz <= 0.0)
        return;
    emit tuneRequested(hz);
}

} // namespace AetherSDR
