#include "gui/DiversityWindowPanels.h"

#include "gui/DiversityWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateDiversityFormat.h"

#include <QAccessible>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>

namespace AetherSDR {

namespace {

// The meter's fixed window, matching DiversityScope's own SNR bars so the two
// views of the same three numbers cannot disagree about what "half way up"
// means.
constexpr double kSnrLoDb = -10.0;
constexpr double kSnrHiDb = 30.0;

constexpr int kMeterWidth   = 52;
constexpr int kHeaderHeight = 16;
constexpr int kValueHeight  = 16;
constexpr int kTickColWidth = 22;

// Caption above each stage panel -- the same accent.bright / 11px / bold the
// gate applet's own collapsible section headers use, so the window's panels
// and the sidebar's sections read as one family.
const char* kGroupCaptionStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "background: transparent; }";

// A row-caption above a group of chain buttons ("MODE", "HEAR", "PAN").
const char* kCaptionStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 9px; font-weight: bold; "
    "background: transparent; }";

// Fixed-width numeric readouts. The [live] property selector is how a value
// that has two meanings (realigning vs. steady, gate present vs. absent)
// changes colour without a per-poll setStyleSheet: set the property, re-polish
// once when it actually flips.
const char* kValueStyle =
    "QLabel { color: {{color.text.primary}}; font-size: 11px; font-weight: bold; "
    "background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

const char* kFieldLabelStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; "
    "background: transparent; }";

// Bearing / Level / Hits / Age. Fixed widths so a poll that adds or drops a
// remembered talker scrolls the table, never resizes the panel around it.
// Height comes from the layout (the table shares the scope's stretch row),
// never from the row count.
constexpr int kStationColumnWidths[] = {62, 66, 40, 52};
constexpr int kStationRowHeight = 22;
constexpr int kStationTableMinHeight = 120;

} // namespace

DiversitySnrMeter::DiversitySnrMeter(const QString& header, QWidget* parent)
    : QWidget(parent), m_header(header)
{
    setFixedWidth(kMeterWidth);
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAccessibleName(tr("%1 signal to noise").arg(header));
    setAccessibleDescription(
        tr("Signal-to-noise on a fixed -10 to +30 dB scale. Read-only."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these: declare them so Inspect mode surfaces the
    // tokens actually read, and repaint on a live theme switch (the pattern
    // DiversityScope / DiversityMapStrip already follow).
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.border.subtle"),
        QStringLiteral("color.spectrum.grid"),
        QStringLiteral("color.text.secondary"),
        QStringLiteral("color.text.primary"),
        QStringLiteral("color.meter.bar.fillGradient"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversitySnrMeter::setSnrDb(double db, bool valid)
{
    if (m_valid == valid && (!valid || qFuzzyCompare(m_db + 1.0, db + 1.0)))
        return;
    m_db = db;
    m_valid = valid;
    // /diversity polls at 1 Hz, so this is a settled value rather than a
    // high-rate stream -- announcing every one is information, not spam
    // (docs/a11y.md's throttling note is about the 30 Hz meters).
    QAccessibleValueChangeEvent ev(this, valid ? QVariant(db) : QVariant());
    QAccessible::updateAccessibility(&ev);
    update();
}

void DiversitySnrMeter::clearReading()
{
    setSnrDb(0.0, false);
}

void DiversitySnrMeter::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    // Same split ClientLevelMeter uses: geometry is axis-aligned rectangles
    // that look wrong smeared across a half pixel, text does not.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));
    const QColor primary = tm.color(this, QStringLiteral("color.text.primary"));

    const int stripTop = kHeaderHeight + 4;
    const int stripBot = height() - kValueHeight - 4;
    const int stripH = std::max(1, stripBot - stripTop);
    const int barLeft = kTickColWidth + 2;
    const QRect barR(barLeft, stripTop, width() - barLeft - 2, stripH);

    QFont hf = p.font();
    hf.setPixelSize(10);
    hf.setBold(true);
    p.setFont(hf);
    p.setPen(secondary);
    p.drawText(QRect(0, 0, width(), kHeaderHeight), Qt::AlignCenter, m_header);

    p.fillRect(barR, tm.color(this, QStringLiteral("color.background.spectrum")));

    if (m_valid) {
        const double t = std::clamp((m_db - kSnrLoDb) / (kSnrHiDb - kSnrLoDb), 0.0, 1.0);
        const int fillH = int(t * stripH);
        if (fillH > 0) {
            // Gradient mapped to the FULL strip, not the partial fill, so the
            // colour at a given height always means the same dB -- the bar
            // grows up through a fixed gradient (ClientLevelMeter's rule).
            p.fillRect(QRect(barR.x(), barR.y() + stripH - fillH, barR.width(), fillH),
                       tm.brush(this, QStringLiteral("color.meter.bar.fillGradient"), barR));
        }
    }

    p.setPen(tm.color(this, QStringLiteral("color.border.subtle")));
    p.setBrush(Qt::NoBrush);
    p.drawRect(barR.adjusted(0, 0, -1, -1));

    QFont tf = p.font();
    tf.setPixelSize(8);
    tf.setBold(false);
    p.setFont(tf);
    const QFontMetrics fm(tf);

    struct Tick { double db; const char* label; };
    static constexpr Tick kTicks[] = {
        {  30.0, "+30" },
        {  20.0, "+20" },
        {  10.0, "+10" },
        {   0.0,   "0" },
        { -10.0, "-10" },
    };
    for (const Tick& t : kTicks) {
        const double norm = (t.db - kSnrLoDb) / (kSnrHiDb - kSnrLoDb);
        const int y = stripTop + int((1.0 - norm) * stripH);
        const QString s = QString::fromLatin1(t.label);
        const int ty = std::clamp(y + fm.ascent() / 2 - 1, stripTop + fm.ascent() - 1,
                                  stripTop + stripH - 1);
        p.setPen(secondary);
        p.drawText(kTickColWidth - 2 - fm.horizontalAdvance(s), ty, s);
        p.setPen(tm.color(this, QStringLiteral("color.spectrum.grid")));
        p.drawLine(kTickColWidth - 1, y, barLeft - 1, y);
    }

    // Fixed-width numeric field: right-aligned inside a reserved rectangle so
    // a digit-count change never moves anything beside it.
    QFont vf = p.font();
    vf.setPixelSize(10);
    vf.setBold(true);
    p.setFont(vf);
    p.setPen(primary);
    p.drawText(QRect(0, height() - kValueHeight, width(), kValueHeight), Qt::AlignCenter,
               m_valid ? QString::asprintf("%+.1f", m_db) : QStringLiteral("—"));
}

namespace DiversityWidgets {

QFrame* makeGroupBox(const QString& caption, const QString& objectName,
                     QVBoxLayout*& body, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    // "stripGroupBox" is the name DiversityWindow's own stylesheet rule
    // targets -- same wrapper the Aetherial Audio channel strip puts round
    // each of its stages.
    frame->setObjectName(QStringLiteral("stripGroupBox"));
    frame->setAccessibleName(caption);

    auto* outer = new QVBoxLayout(frame);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(6);

    auto* captionLabel = new QLabel(caption, frame);
    captionLabel->setObjectName(objectName + QStringLiteral("Caption"));
    ThemeManager::instance().applyStyleSheet(captionLabel,
                                             QString::fromLatin1(kGroupCaptionStyle));
    outer->addWidget(captionLabel);

    auto* content = new QWidget(frame);
    content->setObjectName(objectName);
    body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    outer->addWidget(content, 1);
    return frame;
}

QLabel* makeCaption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kCaptionStyle));
    return label;
}

QLabel* makeFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kFieldLabelStyle));
    return label;
}

QLabel* makeValue(const QString& objectName, const QString& worstCase, QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(objectName);
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kValueStyle));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(worstCase) + 8);
    return label;
}

void setLive(QWidget* w, bool live)
{
    if (w->property("live").isValid() && w->property("live").toBool() == live)
        return;
    w->setProperty("live", live);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void applySources(QListWidget* list, const QJsonArray& sources)
{
    if (!list)
        return;

    QStringList rows;
    QStringList tips;
    rows.reserve(sources.size());
    tips.reserve(sources.size());
    for (const QJsonValue& v : sources) {
        const QJsonObject so = v.toObject();
        rows << DiversityFormat::sourceListText(so);
        tips << DiversityFormat::sourceTooltip(so);
    }

    QStringList have;
    QStringList haveTips;
    have.reserve(list->count());
    haveTips.reserve(list->count());
    for (int i = 0; i < list->count(); ++i) {
        have << list->item(i)->text();
        haveTips << list->item(i)->toolTip();
    }

    // Compared on text AND tooltip: the short row text alone (freq + coh) can
    // stay identical between two polls while phase/ratio -- visible only in
    // the tooltip -- moved, and that must still trigger a rebuild.
    if (have == rows && haveTips == tips)
        return;

    const QVariant prevKey =
        list->currentItem() ? list->currentItem()->data(Qt::UserRole) : QVariant();
    const QSignalBlocker block(list);
    list->clear();
    for (int i = 0; i < sources.size(); ++i) {
        const QJsonObject so = sources[i].toObject();
        auto* item = new QListWidgetItem(rows[i], list);
        item->setToolTip(tips[i]);
        item->setData(Qt::UserRole,
                      QVariantList{so.value(QStringLiteral("lo_hz")).toDouble(),
                                   so.value(QStringLiteral("hi_hz")).toDouble()});
    }
    if (!prevKey.isValid())
        return;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole) == prevKey) {
            list->setCurrentRow(i);
            break;
        }
    }
}

} // namespace DiversityWidgets

// --------------------------------------------------------------------------
// DiversityWindow panel builders defined here rather than in
// DiversityWindow.cpp -- see this file's header comment.
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildStationsPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("STATIONS"), QStringLiteral("diversityWindowStations"), body, this);

    m_stationsCount = DiversityWidgets::makeFieldLabel(tr("0 stations remembered"), frame);
    m_stationsCount->setObjectName(QStringLiteral("diversityWindowStationsCountLabel"));
    m_stationsCount->setAccessibleName(tr("Remembered station count"));

    m_stations = new QTableWidget(0, 4, frame);
    m_stations->setObjectName(QStringLiteral("diversityWindowStationsTable"));
    m_stations->setAccessibleName(tr("Remembered stations"));
    m_stations->setHorizontalHeaderLabels(
        {tr("Bearing"), tr("Level"), tr("Hits"), tr("Age")});
    m_stations->verticalHeader()->setVisible(false);
    m_stations->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stations->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stations->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int c = 0; c < 4; ++c)
        m_stations->setColumnWidth(c, kStationColumnWidths[c]);
    m_stations->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_stations->horizontalHeader()->setStretchLastSection(true);
    m_stations->verticalHeader()->setDefaultSectionSize(kStationRowHeight);
    m_stations->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stations->setMinimumHeight(kStationTableMinHeight);
    m_stations->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_memoryClearButton = new QPushButton(tr("Clear memory"), frame);
    m_memoryClearButton->setObjectName(QStringLiteral("diversityWindowMemoryClearButton"));
    m_memoryClearButton->setAccessibleName(tr("Clear the remembered stations"));
    connect(m_memoryClearButton, &QPushButton::clicked, this,
            &DiversityWindow::requestMemoryClear);

    auto* header = new QHBoxLayout;
    header->setSpacing(6);
    header->addWidget(m_stationsCount, 1);
    header->addWidget(m_memoryClearButton);
    body->addLayout(header);
    body->addWidget(m_stations, 1);
    return frame;
}

QWidget* DiversityWindow::buildAlignmentPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("ALIGNMENT & CAPTURE"), QStringLiteral("diversityWindowAlignment"), body, this);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    const auto addField = [&](int row, const QString& caption, QLabel* value) {
        grid->addWidget(DiversityWidgets::makeFieldLabel(caption, frame), row, 0);
        grid->addWidget(value, row, 1);
    };

    m_alignedValue = DiversityWidgets::makeValue(
        QStringLiteral("diversityWindowAlignedLabel"), tr("not aligned"), frame);
    m_alignedValue->setAccessibleName(tr("Tuner alignment"));
    addField(0, tr("Aligned"), m_alignedValue);

    // Five digits plus a sign: 4158 samples is what a real RSPduo
    // misalignment looks like, so this is headroom rather than a guess.
    m_lagValue = DiversityWidgets::makeValue(
        QStringLiteral("diversityWindowLagLabel"), QStringLiteral("-99999"), frame);
    m_lagValue->setAccessibleName(tr("Alignment lag in samples"));
    addField(1, tr("Lag"), m_lagValue);

    m_peakValue = DiversityWidgets::makeValue(
        QStringLiteral("diversityWindowPeakLabel"), QStringLiteral("0.000"), frame);
    m_peakValue->setAccessibleName(tr("Correlation peak"));
    addField(2, tr("Corr peak"), m_peakValue);

    m_realigningValue = DiversityWidgets::makeValue(
        QStringLiteral("diversityWindowRealigningLabel"), tr("realigning…"), frame);
    m_realigningValue->setAccessibleName(tr("Realignment state"));
    addField(3, tr("State"), m_realigningValue);
    grid->setColumnStretch(2, 1);
    body->addLayout(grid);

    auto* captureRow = new QHBoxLayout;
    captureRow->setSpacing(6);
    m_captureResult = DiversityWidgets::makeFieldLabel(QStringLiteral("—"), frame);
    m_captureResult->setObjectName(QStringLiteral("diversityWindowCaptureLabel"));
    m_captureResult->setAccessibleName(tr("Last capture"));
    captureRow->addWidget(DiversityWidgets::makeFieldLabel(tr("Capture"), frame));
    captureRow->addWidget(m_captureResult, 1);
    body->addLayout(captureRow);
    body->addStretch(1);
    return frame;
}

} // namespace AetherSDR
