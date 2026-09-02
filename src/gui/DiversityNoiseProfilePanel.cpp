#include "gui/DiversityNoiseProfilePanel.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
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

// Two minutes at the /diversity poll's own 1 Hz. Long enough that "I unplugged
// the charger" is visible as a step, short enough that the step is still on
// screen when you get back to the radio.
constexpr int kHistorySeconds = 120;

constexpr int kStripHeight = 64;

// The impulse axis auto-scales, but never below this: a strip whose top is
// 0.4 impulses a second would draw a quiet site as a wall of bars.
constexpr float kImpulseFloorPerS = 5.0f;

// The hum trace's fixed window. Hum is reported as dB over the local noise
// floor, and above 40 dB the question stops being "how much" and starts being
// "which breaker".
constexpr float kHumFullScaleDb = 40.0f;

// At most three, as the gate contract caps it -- the cap here is ours, so a
// gate that forgets cannot push the strip off the panel.
constexpr int kMaxPeriodicLines = 3;

// How long a refusal from the gate stays on the status line. Long enough to
// read a sentence, short enough that it cannot be mistaken for a permanent
// state of the panel.
constexpr int kTransientMs = 5000;

// Kind, what it is, the detail the gate wrote, the window it was measured over,
// its size, and the one button.
constexpr int kKindColumnWidths[] = {70, 178, 300, 62, 52, 88};
constexpr int kKindColumnCount = int(sizeof(kKindColumnWidths) / sizeof(kKindColumnWidths[0]));
constexpr int kKindActionColumn = kKindColumnCount - 1;
constexpr int kKindRowHeight = 22;
constexpr int kKindHeaderHeight = 22;

// The gate caps its own list at mains + impulse + three periodic + three tone,
// but the height of this table is part of a page with a no-scroll promise, so
// the cap is enforced here too. Six rows is every finding a real site has
// produced (a mains comb, an impulse rate, three modulation rates and one
// tone); a seventh would be a second tone, which the row above it has already
// told the operator about.
constexpr int kMaxKindRows = 6;

// Same table dressing the beacon, talkers and finder tables use, so they read
// as one family of instrument rather than four tables in one window.
const char* kKindTableStyle =
    "QTableWidget { background: transparent; color: {{color.text.primary}};"
    " font-size: 11px; border: none; }"
    "QTableWidget::item { padding: 0px 3px; }"
    "QTableWidget::item:selected { background: {{color.background.2}};"
    " color: {{color.text.primary}}; }"
    "QHeaderView::section { background: {{color.background.1}};"
    " color: {{color.text.secondary}}; font-size: 10px; font-weight: bold;"
    " border: none; padding: 3px 3px; }";

const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

constexpr int kActionButtonHeight = 18;

QString emDash()
{
    return QStringLiteral("—");
}

// "over 2 s" -- and "over 2 s" rather than "over 2.0 s", because the window is
// a property of the detector rather than a measurement and a decimal point on
// it would invite reading it as one.
QString windowText(const QJsonObject& row)
{
    const QJsonValue v = row.value(QStringLiteral("window_s"));
    if (!v.isDouble())
        return emDash();
    return QCoreApplication::translate("DiversityNoiseProfilePanel", "over %1 s")
        .arg(QString::number(v.toDouble(), 'g', 3));
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
// DiversityNoiseHistoryStrip
// --------------------------------------------------------------------------

DiversityNoiseHistoryStrip::DiversityNoiseHistoryStrip(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowNoiseHistoryStrip"));
    setFixedHeight(kStripHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Noise history"));
    setAccessibleDescription(
        tr("The last two minutes of impulse rate and mains hum, as a picture "
           "of the two numbers stated beside it. Read-only."));
    setToolTip(tr("Two minutes of this site's noise. The bars are impulses a "
                  "second and the line is the mains hum in decibels. Watch it "
                  "while you switch things off at the consumer unit: the "
                  "offender shows up as a step, which no single reading can "
                  "tell you."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these -- declare them so Inspect mode surfaces the
    // tokens actually read, and repaint on a live theme switch.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.accent.bright"),
        QStringLiteral("color.text.secondary"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversityNoiseHistoryStrip::pushSample(double impulsesPerS, bool haveImpulses,
                                            double humDb, bool haveHum)
{
    Sample s;
    s.haveImpulses = haveImpulses;
    s.impulses = haveImpulses ? float(std::max(0.0, impulsesPerS)) : 0.0f;
    s.haveHum = haveHum;
    s.hum = haveHum ? float(humDb) : 0.0f;
    m_samples.push_back(s);
    while (m_samples.size() > kHistorySeconds)
        m_samples.pop_front();
    update();
}

void DiversityNoiseHistoryStrip::clearHistory()
{
    m_samples.clear();
    update();
}

void DiversityNoiseHistoryStrip::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));
    if (m_samples.isEmpty())
        return;

    // A fixed time axis: the newest sample is always at the right-hand edge and
    // one second is always the same width, whether there are five samples or a
    // hundred and twenty. A strip that stretched five samples across the whole
    // width would show a settling measurement as a violent history.
    const double w = double(width()) / double(kHistorySeconds);
    const int n = int(m_samples.size());
    const int firstColumn = kHistorySeconds - n;

    float peak = kImpulseFloorPerS;
    for (const Sample& s : m_samples) {
        if (s.haveImpulses)
            peak = std::max(peak, s.impulses);
    }

    const QColor bar = tm.color(this, QStringLiteral("color.accent.bright"));
    p.setPen(Qt::NoPen);
    p.setBrush(bar);
    for (int i = 0; i < n; ++i) {
        const Sample& s = m_samples[i];
        if (!s.haveImpulses || s.impulses <= 0.0f)
            continue;
        const double h = double(height()) * double(s.impulses / peak);
        p.drawRect(QRectF((firstColumn + i) * w, double(height()) - h,
                          std::max(1.0, w - 1.0), h));
    }

    // The hum trace is drawn over the bars rather than beside them: the two
    // numbers are about the same site at the same moment, and a rectifier that
    // starts humming often starts sparking at the same time.
    QPainterPath path;
    bool started = false;
    for (int i = 0; i < n; ++i) {
        const Sample& s = m_samples[i];
        if (!s.haveHum) {
            started = false;
            continue;
        }
        const double x = (double(firstColumn + i) + 0.5) * w;
        const double frac = std::clamp(double(s.hum / kHumFullScaleDb), 0.0, 1.0);
        const double y = double(height()) * (1.0 - frac);
        if (!started) {
            path.moveTo(QPointF(x, y));
            started = true;
        } else {
            path.lineTo(QPointF(x, y));
        }
    }
    if (!path.isEmpty()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(tm.color(this, QStringLiteral("color.text.secondary")));
        p.drawPath(path);
    }
}

// --------------------------------------------------------------------------
// DiversityNoiseProfilePanel
// --------------------------------------------------------------------------

DiversityNoiseProfilePanel::DiversityNoiseProfilePanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowNoiseProfilePanel"));

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        m_status->setText(QString());
        DiversityWidgets::setLive(m_status, false);
    });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    // The four sentences, two to a row. They said the whole story before the
    // table below them existed and they still say the part of it that is a
    // measurement rather than a decision -- and the SITE page's own tests read
    // them, which is the cheapest guard there is on the parse underneath.
    auto* headline = new QHBoxLayout;
    headline->setContentsMargins(0, 0, 0, 0);
    headline->setSpacing(12);

    m_verdict = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseProfileVerdictLabel"),
        tr("60 Hz grid: 120 Hz hum 99.9 dB, 9 harmonics"),
        tr("Whether the noise floor carries a comb of lines locked to the mains "
           "frequency, and how strong it is. A comb at twice the mains rate -- "
           "100 Hz on a 50 Hz grid, 120 Hz on a 60 Hz one -- is a rectifier "
           "somewhere: a switching supply, an LED driver, a dimmer, a phone "
           "charger. The more harmonics, the harsher the switching edge."),
        this);
    m_verdict->setAccessibleName(tr("Mains hum verdict"));
    headline->addWidget(m_verdict);

    m_impulses = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseProfileImpulsesLabel"),
        tr("impulses: 999 /s at 99.9 dB"),
        tr("How many short, loud spikes a second the gate is counting, and how "
           "far above the noise floor they reach. A steady one or two a second "
           "is an electric fence; a continuous rasp is an arcing joint or a "
           "failing insulator; bursts that follow the traffic outside are "
           "vehicle ignition. This is what the noise blanker works on."),
        this);
    m_impulses->setAccessibleName(tr("Impulse noise"));
    headline->addWidget(m_impulses);

    m_seconds = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseProfileSecondsLabel"),
        tr("measured over 99.9 s"),
        tr("How much audio the profile above was measured from. A short window "
           "is a fresh profile still settling; the numbers steady out as it "
           "grows."),
        this);
    m_seconds->setAccessibleName(tr("Profile measurement window"));
    headline->addWidget(m_seconds);
    headline->addStretch(1);
    root->addLayout(headline);

    auto* lines = new QHBoxLayout;
    lines->setContentsMargins(0, 0, 0, 0);
    lines->setSpacing(12);

    m_periodic = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseProfilePeriodicLabel"),
        tr("lines: 9999.9 Hz 99.9 dB · 9999.9 Hz 99.9 dB · 9999.9 Hz 99.9 dB"),
        tr("The strongest periodic lines that are NOT mains harmonics, "
           "strongest first. A single tone at some odd frequency is usually one "
           "device's own oscillator -- a monitor, a cheap plug-top supply, a "
           "solar inverter -- and moving the antenna a few metres often does "
           "more about it than any amount of combining."),
        this);
    m_periodic->setAccessibleName(tr("Periodic noise lines"));
    lines->addWidget(m_periodic);

    m_subband = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowSubbandLineLabel"),
        tr("Per-bin weights: on, 9999 bins refined, +99.9 dB"),
        tr("Whether the combiner is solving one weight per passband bin instead "
           "of one weight for the whole channel, how many bins it refined on "
           "the last solve, and what that bought over the single-weight answer. "
           "It earns most where the noise arrives from several directions at "
           "once; on a single clean source it is worth about nothing, and says "
           "so."),
        this);
    m_subband->setAccessibleName(tr("Per-bin weight refinement"));
    lines->addWidget(m_subband);
    lines->addStretch(1);
    root->addLayout(lines);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    m_kinds = new QTableWidget(0, kKindColumnCount, this);
    m_kinds->setObjectName(QStringLiteral("diversityWindowNoiseKindsTable"));
    m_kinds->setAccessibleName(tr("Noise findings"));
    m_kinds->setHorizontalHeaderLabels({tr("Kind"), tr("What"), tr("Detail"),
                                        tr("Window"), tr("dB"), tr("Do")});
    ThemeManager::instance().applyStyleSheet(m_kinds,
                                             QString::fromLatin1(kKindTableStyle));
    static const struct { int column; const char* tip; } kHeaderTips[] = {
        {0, QT_TR_NOOP("What sort of noise this row is. MAINS is a comb locked "
                       "to the grid, IMPULSE is spikes, PERIODIC is a "
                       "modulation rate of the noise itself, TONE is a line in "
                       "the audio the automatic notch can reach, and FLOOR is "
                       "the gate saying it found none of the others.")},
        {1, QT_TR_NOOP("The gate's own one-line verdict on this finding.")},
        {2, QT_TR_NOOP("What the verdict was measured from -- the comb spacing "
                       "and its harmonics, how far the impulses reach over the "
                       "floor, how deep the notch is holding a tone.")},
        {3, QT_TR_NOOP("How long a window this finding was measured over. "
                       "Impulses want a longer one than a hum comb does, so "
                       "the two rows can disagree and both be current.")},
        {4, QT_TR_NOOP("How big this finding is, in decibels over the local "
                       "noise floor. A dash is a finding with no size to "
                       "report, not a finding of zero.")},
        {5, QT_TR_NOOP("The one thing worth doing about this row, named by the "
                       "gate rather than by this window. A lit button is an "
                       "action already in force -- press it again to undo it. "
                       "A dashed one means the gate has looked and there is "
                       "nothing to do; its hover says why.")},
    };
    for (const auto& entry : kHeaderTips) {
        if (QTableWidgetItem* header = m_kinds->horizontalHeaderItem(entry.column))
            header->setToolTip(tr(entry.tip));
    }
    m_kinds->verticalHeader()->setVisible(false);
    m_kinds->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_kinds->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_kinds->setSelectionMode(QAbstractItemView::SingleSelection);
    m_kinds->setSortingEnabled(false);
    for (int c = 0; c < kKindColumnCount; ++c)
        m_kinds->setColumnWidth(c, kKindColumnWidths[c]);
    m_kinds->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_kinds->horizontalHeader()->setStretchLastSection(false);
    m_kinds->horizontalHeader()->setFixedHeight(kKindHeaderHeight);
    m_kinds->verticalHeader()->setDefaultSectionSize(kKindRowHeight);
    m_kinds->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_kinds->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // A fixed height for the CAP rather than for the row count: a table that
    // grew and shrank as findings came and went would move the strip beside it
    // and the beacon panel under it on every poll.
    m_kinds->setFixedHeight(kKindHeaderHeight + kMaxKindRows * kKindRowHeight + 2);
    m_kinds->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    int kindsWidth = 2;
    for (int c = 0; c < kKindColumnCount; ++c)
        kindsWidth += kKindColumnWidths[c];
    m_kinds->setFixedWidth(kindsWidth);
    body->addWidget(m_kinds);

    auto* right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(2);
    m_strip = new DiversityNoiseHistoryStrip(this);
    right->addWidget(m_strip);
    QLabel* stripCaption = DiversityWidgets::makeFieldLabel(
        tr("last %1 s · bars: impulses per second · line: hum dB")
            .arg(kHistorySeconds),
        this);
    stripCaption->setObjectName(QStringLiteral("diversityWindowNoiseHistoryCaption"));
    stripCaption->setAccessibleName(tr("Noise history legend"));
    right->addWidget(stripCaption);
    right->addStretch(1);
    body->addLayout(right, 1);
    root->addLayout(body);

    m_status = new QLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("diversityWindowNoiseActionStatusLabel"));
    m_status->setAccessibleName(tr("Noise action status"));
    ThemeManager::instance().applyStyleSheet(m_status,
                                             QString::fromLatin1(kStatusStyle));
    // A fixed height whether or not it is saying anything: a line that appeared
    // only when the gate refused something would shift everything under it at
    // the moment the operator most needs it to stay still.
    m_status->setText(tr("the gate refused that"));
    m_status->setFixedHeight(m_status->sizeHint().height());
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_status->setText(QString());
    root->addWidget(m_status);

    clear();
}

void DiversityNoiseProfilePanel::clear()
{
    m_verdict->setText(tr("no noise profile yet — the gate profiles once the "
                          "tuners are aligned"));
    m_impulses->setText(tr("impulses: %1").arg(emDash()));
    m_periodic->setText(tr("lines: %1").arg(emDash()));
    m_seconds->setText(tr("measured over %1 s").arg(emDash()));
    m_subband->setText(tr("Per-bin weights: %1").arg(emDash()));
    m_strip->clearHistory();
    m_actionPending = false;
    m_statusTimer->stop();
    m_status->setText(QString());
    DiversityWidgets::setLive(m_status, false);
    m_kindRows.clear();
    m_kinds->setRowCount(0);
}

void DiversityNoiseProfilePanel::showTransient(const QString& text)
{
    m_status->setText(text);
    DiversityWidgets::setLive(m_status, true);
    m_statusTimer->start(kTransientMs);
}

void DiversityNoiseProfilePanel::applyActionReply(const QJsonObject& reply)
{
    if (!m_actionPending)
        return;
    m_actionPending = false;
    const QString error = reply.value(QStringLiteral("error")).toString();
    if (error.isEmpty())
        return;
    // No row moves. The next /diversity poll is what puts this row back where
    // the gate actually has it, so the table never shows a state the radio was
    // never in.
    showTransient(error);
}

// One row per finding, in the gate's own order -- mains, impulse, periodic,
// tone -- because that is the order in which an operator can act on them: the
// comb is a plug to pull, the impulses are a blanker to arm, the tone is a
// notch to place.
void DiversityNoiseProfilePanel::applyKinds(const QJsonValue& kinds)
{
    const QJsonArray rows = kinds.toArray();

    QStringList packed;
    QVector<QJsonObject> keep;
    for (const QJsonValue& v : rows) {
        if (packed.size() >= kMaxKindRows)
            break;
        if (!v.isObject())
            continue;
        const QJsonObject row = v.toObject();
        const QJsonObject action = row.value(QStringLiteral("action")).toObject();
        double db = 0.0;
        packed << QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6\x1f%7\x1f%8\x1f%9")
                      .arg(row.value(QStringLiteral("kind")).toString().toUpper(),
                           row.value(QStringLiteral("label")).toString(),
                           row.value(QStringLiteral("detail")).toString(),
                           windowText(row),
                           jsonNumber(row, "db", &db) ? QString::number(db, 'f', 1)
                                                      : emDash(),
                           action.value(QStringLiteral("label")).toString(),
                           action.value(QStringLiteral("route")).toString(),
                           action.value(QStringLiteral("query")).toString(),
                           row.value(QStringLiteral("active")).toBool()
                               ? QStringLiteral("1")
                               : QStringLiteral("0"));
        keep.push_back(row);
    }

    // Rebuild only on change -- see the header. The `why` text is not part of
    // the key on purpose: it is a hover on a button that cannot be pressed, and
    // rebuilding the table for a reworded reason would be spending a rebuild on
    // something nobody can see.
    if (packed == m_kindRows)
        return;
    m_kindRows = packed;

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

        const QString label = cells.at(5);
        const QString route = cells.at(6);
        const QString query = cells.at(7);
        const bool active = cells.at(8) == QStringLiteral("1");
        const bool haveAction = !route.isEmpty() && !label.isEmpty();

        auto* button = new QPushButton(haveAction ? label : emDash(), m_kinds);
        button->setObjectName(QStringLiteral("diversityWindowNoiseKindAction%1").arg(row));
        button->setFixedHeight(kActionButtonHeight);
        button->setCheckable(true);
        button->setChecked(active);
        button->setEnabled(haveAction);
        applyToggleButtonStyle(button);
        if (haveAction) {
            button->setAccessibleName(tr("%1 the %2 finding").arg(label, cells.at(0)));
            button->setToolTip(tr("GET %1?%2 on the gate. %3")
                                   .arg(route, query,
                                        active
                                            ? tr("This action is in force now.")
                                            : tr("The gate nominated this action "
                                                 "for this finding.")));
            connect(button, &QPushButton::clicked, this, [this, route, query] {
                m_actionPending = true;
                emit actionRequested(route, QUrlQuery(query));
            });
        } else {
            const QString why = keep.at(row).value(QStringLiteral("why")).toString();
            button->setAccessibleName(tr("Nothing to do about the %1 finding")
                                          .arg(cells.at(0)));
            button->setToolTip(why.isEmpty()
                                   ? tr("The gate nominated no action for this "
                                        "finding.")
                                   : why);
        }
        button->setAccessibleDescription(button->toolTip());
        m_kinds->setCellWidget(row, kKindActionColumn, button);
    }
}

void DiversityNoiseProfilePanel::applyProfile(const QJsonValue& profile)
{
    if (!profile.isObject()) {
        // null is the gate's own "I have not measured this yet", which is a
        // different thing from a site with no noise -- say which one it is and
        // keep the history, so a momentary drop-out does not erase two minutes
        // of evidence.
        m_verdict->setText(tr("no noise profile yet — the gate profiles once "
                              "the tuners are aligned"));
        m_impulses->setText(tr("impulses: %1").arg(emDash()));
        m_periodic->setText(tr("lines: %1").arg(emDash()));
        m_seconds->setText(tr("measured over %1 s").arg(emDash()));
        applyKinds(QJsonValue());
        return;
    }
    const QJsonObject p = profile.toObject();
    applyKinds(p.value(QStringLiteral("kinds")));

    double mains = 0.0;
    double hum = 0.0;
    double harmonics = 0.0;
    const bool haveMains = jsonNumber(p, "mains_hz", &mains) && mains > 0.0;
    const bool haveHum = jsonNumber(p, "hum_db", &hum);
    const bool haveHarmonics = jsonNumber(p, "harmonics", &harmonics);
    if (!haveMains) {
        m_verdict->setText(tr("no mains-locked hum"));
    } else {
        const int harmonicCount = haveHarmonics ? int(std::lround(harmonics)) : 0;
        // Spelled out rather than tr("%n harmonic(s)"): Qt's plural form needs
        // a loaded translation to resolve, and an untranslated build would show
        // the operator the literal "(s)".
        QString harmonicText = tr("%1 harmonics").arg(emDash());
        if (haveHarmonics) {
            harmonicText = (harmonicCount == 1)
                               ? tr("1 harmonic")
                               : tr("%1 harmonics").arg(harmonicCount);
        }
        m_verdict->setText(tr("%1 Hz grid: %2 Hz hum %3 dB, %4")
                               .arg(QString::number(mains, 'f', 0),
                                    QString::number(mains * 2.0, 'f', 0),
                                    haveHum ? QString::number(hum, 'f', 1) : emDash(),
                                    harmonicText));
    }

    double rate = 0.0;
    double size = 0.0;
    const bool haveRate = jsonNumber(p, "impulses_per_s", &rate);
    const bool haveSize = jsonNumber(p, "impulse_db", &size);
    if (!haveRate) {
        m_impulses->setText(tr("impulses: %1").arg(emDash()));
    } else if (rate < 0.5) {
        // Rounded to nothing: "none" is what an operator reads off it, and
        // "0 /s at — dB" is the same fact said in a way that looks broken.
        m_impulses->setText(tr("impulses: none"));
    } else {
        m_impulses->setText(
            tr("impulses: %1 /s at %2 dB")
                .arg(QString::number(qint64(std::llround(rate))),
                     haveSize ? QString::number(size, 'f', 1) : emDash()));
    }

    QStringList lines;
    const QJsonArray periodic = p.value(QStringLiteral("periodic")).toArray();
    for (const QJsonValue& v : periodic) {
        if (lines.size() >= kMaxPeriodicLines)
            break;
        const QJsonObject line = v.toObject();
        double hz = 0.0;
        double db = 0.0;
        if (!jsonNumber(line, "hz", &hz))
            continue;
        lines << tr("%1 Hz %2 dB")
                     .arg(QString::number(hz, 'f', 1),
                          jsonNumber(line, "db", &db) ? QString::number(db, 'f', 1)
                                                      : emDash());
    }
    m_periodic->setText(lines.isEmpty()
                            ? tr("lines: none")
                            : tr("lines: %1").arg(lines.join(QStringLiteral(" · "))));

    double seconds = 0.0;
    m_seconds->setText(tr("measured over %1 s")
                           .arg(jsonNumber(p, "seconds", &seconds)
                                    ? QString::number(seconds, 'f', 1)
                                    : emDash()));

    m_strip->pushSample(rate, haveRate, hum, haveMains && haveHum);
}

void DiversityNoiseProfilePanel::applySubband(const QJsonValue& subband)
{
    if (!subband.isObject()) {
        m_subband->setText(tr("Per-bin weights: %1").arg(emDash()));
        return;
    }
    const QJsonObject s = subband.toObject();
    if (!s.value(QStringLiteral("enabled")).toBool()) {
        m_subband->setText(tr("Per-bin weights: off"));
        return;
    }
    double bins = 0.0;
    double extra = 0.0;
    const bool haveBins = jsonNumber(s, "bins", &bins);
    const bool haveExtra = jsonNumber(s, "extra_db", &extra);
    m_subband->setText(
        tr("Per-bin weights: on, %1 bins refined, %2 dB")
            .arg(haveBins ? QString::number(qint64(std::llround(bins))) : emDash(),
                 haveExtra ? QString::asprintf("%+.1f", extra) : emDash()));
}

} // namespace AetherSDR
