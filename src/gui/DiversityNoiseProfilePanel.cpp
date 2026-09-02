#include "gui/DiversityNoiseProfilePanel.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QColor>
#include <QHBoxLayout>
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

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    auto* left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(2);

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
    left->addWidget(m_verdict);

    // Explicit line breaks, never word wrap: a wrapping label is
    // height-for-width, which makes the grid it sits in height-for-width too
    // and puts a scrollbar on a page that fits.
    QLabel* mainsCaption = DiversityWidgets::makeFieldLabel(
        tr("a 100 or 120 Hz comb is a rectifier: a supply, an LED driver,\n"
           "a dimmer, a charger — walk the house and unplug them one at a time"),
        this);
    mainsCaption->setObjectName(QStringLiteral("diversityWindowNoiseProfileMainsCaption"));
    mainsCaption->setAccessibleName(tr("Mains hum legend"));
    left->addWidget(mainsCaption);

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
    left->addWidget(m_impulses);

    QLabel* impulseCaption = DiversityWidgets::makeFieldLabel(
        tr("impulses: electric fences, vehicle ignition, arcing insulators,\n"
           "power-line telecoms — the blanker's own quarry"),
        this);
    impulseCaption->setObjectName(QStringLiteral("diversityWindowNoiseProfileImpulseCaption"));
    impulseCaption->setAccessibleName(tr("Impulse noise legend"));
    left->addWidget(impulseCaption);

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
    left->addWidget(m_periodic);

    QLabel* periodicCaption = DiversityWidgets::makeFieldLabel(
        tr("the strongest lines that are not mains harmonics"), this);
    periodicCaption->setObjectName(QStringLiteral("diversityWindowNoiseProfileLinesCaption"));
    periodicCaption->setAccessibleName(tr("Periodic lines legend"));
    left->addWidget(periodicCaption);

    m_seconds = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseProfileSecondsLabel"),
        tr("measured over 99.9 s"),
        tr("How much audio the profile above was measured from. A short window "
           "is a fresh profile still settling; the numbers steady out as it "
           "grows."),
        this);
    m_seconds->setAccessibleName(tr("Profile measurement window"));
    left->addWidget(m_seconds);

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
    left->addWidget(m_subband);
    left->addStretch(1);
    root->addLayout(left);

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
    root->addLayout(right, 1);

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
        return;
    }
    const QJsonObject p = profile.toObject();

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
