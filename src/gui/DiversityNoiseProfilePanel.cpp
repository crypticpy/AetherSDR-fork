#include "gui/DiversityNoiseProfilePanel.h"

// DISMISS (persistence + the Do column's cell widgets) and rebuildKindsTable()
// live in DiversityNoiseProfileDismiss.cpp -- split out to keep this file
// under the project's ~800-line budget. Both files define methods of the
// same class; see that file's header comment for the split boundary.

#include "core/ThemeManager.h"
#include "gui/DiversityAge.h"
#include "gui/DiversityWindowPanels.h"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
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

// 52 rather than 64: the second twelve pixels the SITE page owed the window's
// new tab and FLOW rows. The strip is a two-minute trend, not a measurement --
// the numbers it is a trend OF are on the lines above it -- so it is the one
// thing on the page that loses nothing by being shorter.
constexpr int kStripHeight = 52;

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
// its size, the Do column -- the one action button, or the action button
// plus a small DISMISS, or "dismissed" plus UNDO -- and Age, how long since
// the gate first saw this finding. Do's width grew from 88 to 108 for the
// DISMISS/UNDO pairing; Detail gave up 20 px for that and 58 more for Age,
// so the table is no wider than before (the gate's sentence is
// always fully available on the cell's own hover regardless of where it gets
// cut). Age is last, same position the beacon table's own Age column holds.
constexpr int kKindColumnWidths[] = {70, 178, 222, 62, 52, 108, 58};
constexpr int kKindColumnCount = int(sizeof(kKindColumnWidths) / sizeof(kKindColumnWidths[0]));
// The Do column's own index is DiversityNoiseProfileDismiss.cpp's
// kKindActionColumn now that rebuildKindsTable() lives there.
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
    setToolTip(tr("The last two minutes: bars are impulses/s, the line is "
                  "hum in dB."));

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
    // makeReadoutLine() set toolTip and accessibleDescription to the same
    // long sentence above; the accessibleDescription is exactly right, but
    // AGENTS.md caps a tooltip literal at one 90-char line, so the tooltip
    // itself gets overridden short here.
    m_verdict->setToolTip(tr("A comb of hum locked to the mains frequency; more harmonics means a harsher source."));
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
    // Same override as m_verdict above: the long form stays the
    // accessibleDescription makeReadoutLine() already set.
    m_impulses->setToolTip(tr("Spikes per second and how loud; what the noise blanker works on."));
    headline->addWidget(m_impulses);

    m_seconds = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseProfileSecondsLabel"),
        tr("measured over 99.9 s"),
        tr("How much audio the profile above was measured from. A short window "
           "is a fresh profile still settling; the numbers steady out as it "
           "grows."),
        this);
    m_seconds->setAccessibleName(tr("Profile measurement window"));
    // Same override as m_verdict above.
    m_seconds->setToolTip(tr("How long the profile above was measured over; short means still settling."));
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
    // Same override as m_verdict above.
    m_periodic->setToolTip(tr("The strongest tones that are not mains harmonics, strongest first."));
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
    // Same override as m_verdict above.
    m_subband->setToolTip(tr("Per-bin combiner weights: on/off, bins refined, dB gained over one weight."));
    lines->addWidget(m_subband);

    // On this row rather than on one of its own: the SITE page has no spare
    // height at the size the window opens at, and a bearing is one more fact
    // about the same noise the two lines beside it describe.
    m_bearing = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowSiteNoiseBearing"),
        tr("impulse from 212° (or 32°) · coh 0.42 · 59 min ago"),
        tr("Which way the noise this profile is about is arriving from. It "
           "needs the beacon compass to have fitted the two loops' geometry "
           "first -- until then the phase between them is a number, not a "
           "direction, and this line says so rather than printing one. Two "
           "elements in a line cannot tell a bearing from its reflection about "
           "the baseline, so when there is a fit you get both and break the tie "
           "by turning something off. The age on the end is how long ago "
           "this direction was last confirmed."),
        this);
    m_bearing->setAccessibleName(tr("Noise bearing"));
    // The long form makeReadoutLine() set stays the accessibleDescription
    // base setBearing() rebuilds from; the tooltip itself gets a short
    // static line there instead (AGENTS.md's 90-char rule).
    m_bearingTip = m_bearing->toolTip();
    lines->addWidget(m_bearing);
    lines->addStretch(1);
    root->addLayout(lines);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    m_kinds = new QTableWidget(0, kKindColumnCount, this);
    m_kinds->setObjectName(QStringLiteral("diversityWindowNoiseKindsTable"));
    m_kinds->setAccessibleName(tr("Noise findings"));
    m_kinds->setHorizontalHeaderLabels({tr("Kind"), tr("What"), tr("Detail"),
                                        tr("Window"), tr("dB"), tr("Do"), tr("Age")});
    ThemeManager::instance().applyStyleSheet(m_kinds,
                                             QString::fromLatin1(kKindTableStyle));
    // shortTip is the <=90-char tooltip; tip is the full explanation, kept on
    // AccessibleDescriptionRole -- same split DiversityFinderPanel.cpp's
    // header tips use.
    static const struct { int column; const char* shortTip; const char* tip; } kHeaderTips[] = {
        {0, QT_TR_NOOP("What sort of noise: MAINS (grid comb), IMPULSE (spikes), "
                       "PERIODIC, TONE, or FLOOR."),
         QT_TR_NOOP("What sort of noise this row is. MAINS is a comb locked "
                       "to the grid, IMPULSE is spikes, PERIODIC is a "
                       "modulation rate of the noise itself, TONE is a line in "
                       "the audio the automatic notch can reach, and FLOOR is "
                       "the gate saying it found none of the others.")},
        {1, QT_TR_NOOP("The gate's own one-line verdict on this finding."),
         QT_TR_NOOP("The gate's own one-line verdict on this finding.")},
        {2, QT_TR_NOOP("What the verdict was measured from - comb spacing, impulse "
                       "rate, or notch depth."),
         QT_TR_NOOP("What the verdict was measured from -- the comb spacing "
                       "and its harmonics, how far the impulses reach over the "
                       "floor, how deep the notch is holding a tone.")},
        {3, QT_TR_NOOP("How long a window this finding was measured over."),
         QT_TR_NOOP("How long a window this finding was measured over. "
                       "Impulses want a longer one than a hum comb does, so "
                       "the two rows can disagree and both be current.")},
        {4, QT_TR_NOOP("Size of this finding in decibels over the local noise floor."),
         QT_TR_NOOP("How big this finding is, in decibels over the local "
                       "noise floor. A dash is a finding with no size to "
                       "report, not a finding of zero.")},
        {5, QT_TR_NOOP("The one action worth taking on this row, named by the gate."),
         QT_TR_NOOP("The one thing worth doing about this row, named by the "
                       "gate rather than by this window. A lit button is an "
                       "action already in force -- press it again to undo it. "
                       "A dashed one means the gate has looked and there is "
                       "nothing to do; its hover says why.")},
        {6, QT_TR_NOOP("How long since the gate first saw this finding."),
         QT_TR_NOOP("How long since the gate first saw this finding -- a "
                       "stored measurement, kept and shown with its age "
                       "rather than blanked. A dash means the gate is too "
                       "old to say.")},
    };
    for (const auto& entry : kHeaderTips) {
        if (QTableWidgetItem* header = m_kinds->horizontalHeaderItem(entry.column)) {
            header->setToolTip(tr(entry.shortTip));
            header->setData(Qt::AccessibleDescriptionRole, tr(entry.tip));
        }
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

    loadDismissed();
    clear();
}

// DISMISS (persistence + rebuildKindsTable()) is defined in
// DiversityNoiseProfileDismiss.cpp.

void DiversityNoiseProfilePanel::setBearing(const QString& text, const QString& reason)
{
    if (!m_bearing)
        return;
    m_bearing->setText(text);
    // The long explanation and the gate's own (short, one-line) reason are
    // both worth having, but only the accessibleDescription is unbounded --
    // the tooltip stays one short static line per AGENTS.md's 90-char rule,
    // with the gate's reason (already documented as one line, see
    // DiversityWindowSite.cpp) appended when there is one.
    const QString desc = reason.isEmpty()
                            ? m_bearingTip
                            : m_bearingTip + QStringLiteral("\n\n") + reason;
    m_bearing->setAccessibleDescription(desc);
    const QString shortTip = tr("Which way this noise arrives from, once the beacon compass has a fit.");
    m_bearing->setToolTip(reason.isEmpty() ? shortTip
                                            : shortTip + QStringLiteral(" ") + reason);
}

void DiversityNoiseProfilePanel::clear()
{
    m_verdict->setText(tr("no noise profile yet — the gate profiles once the "
                          "tuners are aligned"));
    m_impulses->setText(tr("impulses: %1").arg(emDash()));
    m_periodic->setText(tr("lines: %1").arg(emDash()));
    m_seconds->setText(tr("measured over %1 s").arg(emDash()));
    m_subband->setText(tr("Per-bin weights: %1").arg(emDash()));
    setBearing(emDash(), QString());
    m_strip->clearHistory();
    m_actionPending = false;
    m_statusTimer->stop();
    m_status->setText(QString());
    DiversityWidgets::setLive(m_status, false);
    m_kindRows.clear();
    m_keptRows.clear();
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

    // Expire dismissals BEFORE building the display rows: a finding that
    // moved more than kDismissExpiryDb dB, or vanished from this poll's array
    // altogether, no longer counts as handled. Runs over every row the gate
    // sent, not just the kMaxKindRows kept for display -- a dismissal must
    // not survive on a technicality of the display cap.
    QHash<QString, double> currentDb;
    QSet<QString> currentKinds;
    for (const QJsonValue& v : rows) {
        if (!v.isObject())
            continue;
        const QJsonObject row = v.toObject();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        currentKinds.insert(kind);
        double db = 0.0;
        if (jsonNumber(row, "db", &db))
            currentDb.insert(kind, db);
    }
    expireDismissed(currentDb, currentKinds);

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
        // "since" is a gate ask this window may not get an answer to yet
        // (§3.4): an older gate sends no key at all, and that reads as a
        // dash rather than as a measurement of zero age.
        const QJsonValue since = row.value(QStringLiteral("since"));
        const QString age = since.isDouble()
            ? diversityAgeSince(qint64(since.toDouble()), QDateTime::currentSecsSinceEpoch())
            : emDash();
        packed << QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6\x1f%7\x1f%8\x1f%9\x1f%10")
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
                               : QStringLiteral("0"),
                           age);
        keep.push_back(row);
    }
    // Kept regardless of whether packed changed: a DISMISS/UNDO click needs
    // each row's raw kind and dB to act on, and rebuildKindsTable() reads
    // from here rather than re-parsing.
    m_keptRows = keep;

    // Rebuild only on change -- see the header. The `why` text is not part of
    // the key on purpose: it is a hover on a button that cannot be pressed, and
    // rebuilding the table for a reworded reason would be spending a rebuild on
    // something nobody can see. Dismiss state is not part of the key either --
    // dismissKind()/undismissKind() rebuild directly, at the moment they act,
    // rather than waiting for the next poll.
    if (packed == m_kindRows)
        return;
    m_kindRows = packed;
    rebuildKindsTable();
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
        // NOT applyKinds(QJsonValue()): that would run the dismiss-expiry
        // pass over an empty kinds array and read every dismissal as "the
        // kind disappeared from the payload" on a momentary drop-out. A
        // profile that has simply gone quiet for a poll is not the same fact
        // as a finding the gate has stopped reporting.
        m_kindRows.clear();
        m_keptRows.clear();
        m_kinds->setRowCount(0);
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
    setBearing(emDash(), QString());
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
