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

// What the gate thinks each candidate is, second only to where it is: the
// answer to "should I bother going there" comes before every number that
// qualifies it.
constexpr int kKindColumn = 1;

// kHz, kind, score, SNR, syllabic, active, last heard, phase, coherence,
// gain, Tune. The kind column is wide enough for "carrier 0.55" unelided:
// a verdict that reads as "carr..." is a verdict nobody trusts.
constexpr int kColumnWidths[] = {74, 84, 46, 52, 56, 56, 56, 48, 56, 52, 68};
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

// Every verdict the gate can send, each on a token this theme already owns.
// Nothing here invents a colour: a user theme that recolours the accent
// recolours the finder's voice bars with it.
//
// The gate's kind list grew past the five this build used to know by name --
// RTTY, FT8, FT4 and PSK31 are now named from the band plan instead of
// folding into plain "data", and "signal" means "something is here and the
// gate will not guess what" (docs/DIVERSITY.md, "FINDER"). Before this an
// unnamed kind fell through to an EMPTY token, and the strip's own fallback
// for an empty token is the accent -- the same colour "voice" uses. On a
// gate sending any of those newer words that read as "everything is blue":
// every row that was not plainly voice, CW, carrier or noise painted as if
// it were voice. So the fallback here is never empty: a kind this build has
// still never met (truly unknown, not just newly documented) gets the same
// token "signal" does -- CW's amber, not the accent -- because both are
// "the gate found something and is not calling it a conversation", and
// neither should be mistaken for one. Only a kind that did not arrive at all
// (an older gate with no verdict to give) stays uncoloured: there is no
// colour to be honest about yet.
QString kindToken(const QString& kind)
{
    if (kind.isEmpty())
        return QString();
    if (kind == QLatin1String("voice"))
        return QStringLiteral("color.accent.bright");
    if (kind == QLatin1String("cw"))
        return QStringLiteral("color.accent.warning");
    if (kind == QLatin1String("data") || kind == QLatin1String("rtty")
        || kind == QLatin1String("ft8") || kind == QLatin1String("ft4")
        || kind == QLatin1String("psk31"))
        return QStringLiteral("color.accent.success");
    if (kind == QLatin1String("carrier"))
        return QStringLiteral("color.accent.danger");
    if (kind == QLatin1String("noise"))
        return QStringLiteral("color.text.secondary");
    // "signal", and anything this build has never met: see the function
    // comment above for why this is CW's token rather than the accent.
    return QStringLiteral("color.accent.warning");
}

// Every token kindToken() can return, for declareWidgetTokens(): a widget
// that paints from tokens has to say which ones, or Inspect mode cannot show
// an operator why a bar is the colour it is.
QStringList kindTokens()
{
    return QStringList{
        QStringLiteral("color.accent.bright"), QStringLiteral("color.accent.warning"),
        QStringLiteral("color.accent.success"), QStringLiteral("color.accent.danger"),
        QStringLiteral("color.text.secondary"),
    };
}

// "cw" is CW on the air and in every logbook; RTTY/FT8/FT4/PSK31 are their
// band-plan names rather than the lower case the gate sends them in; the rest
// read as the gate sends them. A word this build does not know at all is
// shown verbatim rather than as a dash -- the gate saying something new is
// news, not a missing measurement.
QString kindLabel(const QString& kind)
{
    if (kind == QLatin1String("voice"))
        return QCoreApplication::translate("DiversityFinderPanel", "voice");
    if (kind == QLatin1String("cw"))
        return QCoreApplication::translate("DiversityFinderPanel", "CW");
    if (kind == QLatin1String("data"))
        return QCoreApplication::translate("DiversityFinderPanel", "data");
    if (kind == QLatin1String("rtty"))
        return QCoreApplication::translate("DiversityFinderPanel", "RTTY");
    if (kind == QLatin1String("ft8"))
        return QCoreApplication::translate("DiversityFinderPanel", "FT8");
    if (kind == QLatin1String("ft4"))
        return QCoreApplication::translate("DiversityFinderPanel", "FT4");
    if (kind == QLatin1String("psk31"))
        return QCoreApplication::translate("DiversityFinderPanel", "PSK31");
    if (kind == QLatin1String("carrier"))
        return QCoreApplication::translate("DiversityFinderPanel", "carrier");
    if (kind == QLatin1String("noise"))
        return QCoreApplication::translate("DiversityFinderPanel", "noise");
    if (kind == QLatin1String("signal"))
        return QCoreApplication::translate("DiversityFinderPanel", "signal");
    return kind;
}

// What the verdict is made of, in the operator's terms rather than the
// detector's, plus the honest limit: the gate's map is a quarter of a
// kilohertz per column and thirty frames a second, so some things genuinely
// cannot be told apart and the confidence is where it says so.
QString kindExplanation(const QString& kind)
{
    if (kind == QLatin1String("voice"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "Voice: a phone-wide patch whose loudness swings at syllable rate. "
            "Somebody is talking.");
    if (kind == QLatin1String("cw"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "CW: a few hundred hertz wide, keyed hard on and off. Morse, not "
            "speech.");
    if (kind == QLatin1String("data"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "Data: a fixed width and a level envelope, in a part of the band "
            "the gate does not have a name for.");
    if (kind == QLatin1String("rtty"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "RTTY: a fixed-width digital signal in RTTY's part of the band "
            "plan -- named from where it is, not from its shape.");
    if (kind == QLatin1String("ft8"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "FT8: a fixed-width digital signal in FT8's part of the band "
            "plan -- named from where it is, not from its shape.");
    if (kind == QLatin1String("ft4"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "FT4: a fixed-width digital signal in FT4's part of the band "
            "plan -- named from where it is, not from its shape.");
    if (kind == QLatin1String("psk31"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "PSK31: a fixed-width digital signal in PSK31's part of the band "
            "plan -- named from where it is, not from its shape.");
    if (kind == QLatin1String("signal"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "Signal: something stands here above the noise, and the gate is "
            "not guessing which of the others it is.");
    if (kind == QLatin1String("carrier"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "Carrier: one column of the map, no modulation at all. A "
            "heterodyne or an unattended transmitter, not a conversation.");
    if (kind == QLatin1String("noise"))
        return QCoreApplication::translate(
            "DiversityFinderPanel",
            "Noise: nothing standing above the band's own floor here, or "
            "something impulsive. Static, a spark, a switching supply.");
    return QCoreApplication::translate("DiversityFinderPanel",
                                       "A kind this build has not met before.");
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
                  "of band is bare. Where the gate has named what it heard, "
                  "the bar takes that kind's colour -- the same colour the "
                  "row below carries."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these -- declare them so Inspect mode surfaces
    // the tokens actually read, and repaint on a live theme switch.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
    } + kindTokens());
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversityActivityStrip::setActivity(const QVector<float>& activity)
{
    m_activity = activity.mid(0, kMaxActivityBins);
    update();
}

void DiversityActivityStrip::setKindBands(const QVector<DiversityKindBand>& bands)
{
    m_bands = bands;
    update();
}

void DiversityActivityStrip::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    p.fillRect(rect(), tm.color(this, QStringLiteral("color.background.spectrum")));
    if (m_activity.isEmpty())
        return;

    // The bar colour is a token's own hue and saturation with the VALUE
    // carrying the number -- computed at paint time from a token rather than
    // from a literal, so a user theme still owns the colour. Which token
    // depends on what the gate said is there: voice by default, and the kind's
    // colour across a named candidate's stretch of the span.
    const QColor accent = tm.color(this, QStringLiteral("color.accent.bright"));
    QVector<QColor> bandColours;
    bandColours.reserve(m_bands.size());
    for (const DiversityKindBand& band : m_bands) {
        const QString token = kindToken(band.kind);
        bandColours.push_back(token.isEmpty() ? accent : tm.color(this, token));
    }

    const int n = int(m_activity.size());
    const double w = double(width()) / double(n);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        const double a = std::clamp(double(m_activity[i]), 0.0, 1.0);
        if (a <= 0.0)
            continue;
        QColor hue = accent;
        const float here = float(i + 0.5) / float(n);
        for (int b = 0; b < m_bands.size(); ++b) {
            if (here >= m_bands[b].from && here <= m_bands[b].to) {
                hue = bandColours[b];
                break;
            }
        }
        p.setBrush(QColor::fromHsvF(hue.hueF() < 0.0 ? 0.0 : hue.hueF(),
                                    hue.saturationF(), a * hue.valueF()));
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
        tr("activity: share of the last 10 min each column carried voice; "
           "colour: what the gate thinks is there"), this);
    m_stripCaption->setObjectName(QStringLiteral("diversityWindowActivityCaption"));
    m_stripCaption->setAccessibleName(tr("Activity strip legend"));
    root->addWidget(m_stripCaption);

    m_table = new QTableWidget(0, kColumnCount, this);
    m_table->setObjectName(QStringLiteral("diversityWindowFinderTable"));
    m_table->setAccessibleName(tr("Candidate conversations"));
    m_table->setHorizontalHeaderLabels({tr("kHz"), tr("Kind"), tr("Score"), tr("SNR"),
                                        tr("Syll"), tr("Active"), tr("Heard"),
                                        tr("Phase"), tr("Coh"), tr("Gain"),
                                        tr("Tune")});
    ThemeManager::instance().applyStyleSheet(m_table,
                                             QString::fromLatin1(kFinderTableStyle));

    // One hover explanation per column, written for somebody who has never met
    // a diversity combiner. "Phase", not "bearing": two loops give the phase
    // difference between the antennas and nothing more.
    static const struct { int column; const char* tip; } kHeaderTips[] = {
        {0, QT_TR_NOOP("Centre frequency of the conversation, in kilohertz. "
                       "Tune here and the receiver goes to this frequency.")},
        {1, QT_TR_NOOP("What the gate thinks is there -- voice, CW, data (or "
                       "RTTY/FT8/FT4/PSK31 by name, off the band plan), a "
                       "bare carrier, noise, or plain \"signal\" when the gate "
                       "will not guess further -- and how sure it is, from 0 "
                       "to 1. The colour is the same one the strip above uses "
                       "for this stretch of the band. Hover a row for what "
                       "the verdict was made of.")},
        {2, QT_TR_NOOP("How confident the gate is that this is a conversation "
                       "worth your time: voice shape, strength and how long it "
                       "has been going, combined. The table is sorted by it.")},
        {3, QT_TR_NOOP("Signal-to-noise of the better loop at this frequency, "
                       "in decibels.")},
        {4, QT_TR_NOOP("How speech-shaped the envelope is: human speech "
                       "modulates at a few syllables a second, which a carrier, "
                       "a data mode and a noise blanker do not. Near 1 is "
                       "clearly voice.")},
        {5, QT_TR_NOOP("How long this frequency has carried voice inside the "
                       "last ten minutes, as minutes and seconds.")},
        {6, QT_TR_NOOP("How long ago somebody last spoke here. \"now\" means "
                       "the gate is hearing them as you read this.")},
        {7, QT_TR_NOOP("The phase difference between the two loops for this "
                       "signal. It is a PHASE, not a bearing -- two antennas "
                       "cannot tell which of two directions it came from -- but "
                       "two stations with different phases are in different "
                       "places.")},
        {8, QT_TR_NOOP("How alike the two loops see this signal. High means one "
                       "direction and a null is available; low means scatter, "
                       "and only maximal-ratio gain is on offer.")},
        {9, QT_TR_NOOP("The diversity gain the pair can earn here, over the "
                       "better single loop. Often near zero on plain sky noise: "
                       "that is the physics, not a fault.")},
        {10, QT_TR_NOOP("Tune the receiver to this conversation. The combiner "
                        "is switched to track so it starts solving for whoever "
                        "is talking as soon as you arrive.")},
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

    m_caption = DiversityWidgets::makeFieldLabel(legendText(), this);
    m_caption->setObjectName(QStringLiteral("diversityWindowFinderCaption"));
    m_caption->setAccessibleName(tr("Finder legend"));
    root->addWidget(m_caption);

    // The kind cells are coloured in code from these tokens rather than by the
    // table's stylesheet, so declare them for Inspect mode and re-colour on a
    // live theme switch instead of waiting four seconds for the next poll.
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, kindTokens());
    connect(&tm, &ThemeManager::themeChanged, this,
            [this] { applyKindColours(); });
}

QString DiversityFinderPanel::legendText()
{
    return tr("voice / CW / data / RTTY / carrier / noise / FT8 / FT4 / PSK31 / signal,\n"
              "with how sure the gate is; gain is the diversity gain the pair can earn there");
}

void DiversityFinderPanel::clear()
{
    m_strip->setActivity({});
    m_strip->setKindBands({});
    m_rowHz.clear();
    m_rowKind.clear();
    m_table->setRowCount(0);
    m_caption->setText(legendText());
}

// The gate says why there is nothing to find ("not aligned" while the loops
// are still being lined up, "no frames yet" for a few seconds after a tune or
// a span change). That goes where the legend was -- the legend describes rows,
// and there are none -- on the same two lines so the page does not move.
void DiversityFinderPanel::showReason(const QString& reason)
{
    QString why = reason.trimmed();
    if (why == QLatin1String("not aligned"))
        why = tr("the loops are not aligned");
    else if (why == QLatin1String("no frames yet"))
        why = tr("no frames yet after the tune");
    else if (why.isEmpty())
        why = tr("the gate sent nothing");
    m_caption->setText(tr("nothing to find yet: %1;\n"
                          "gain is the diversity gain the pair can earn there")
                           .arg(why));
}

void DiversityFinderPanel::applyFinder(const QJsonObject& finder)
{
    const bool available = finder.value(QStringLiteral("available")).toBool()
                           && !finder.contains(QStringLiteral("error"));
    if (!available) {
        clear();
        showReason(finder.value(QStringLiteral("reason")).toString());
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

    // Rows again: the legend that describes them comes back with them.
    if (m_caption->text() != legendText())
        m_caption->setText(legendText());
    setCandidates(finder);
}

void DiversityFinderPanel::setCandidates(const QJsonObject& finder)
{
    const QJsonArray candidates = finder.value(QStringLiteral("candidates")).toArray();
    const int rows = std::min(int(candidates.size()), kMaxCandidates);

    // The span the strip is drawn across, so a candidate's own frequency can
    // become a stretch of it. Without it there are no bands: a band drawn on a
    // guessed span would be pointing at the wrong part of the picture.
    const QJsonArray span = finder.value(QStringLiteral("span_hz")).toArray();
    const bool haveSpan = span.size() == 2 && span.at(0).isDouble()
                          && span.at(1).isDouble()
                          && span.at(1).toDouble() > span.at(0).toDouble();
    const double spanLo = haveSpan ? span.at(0).toDouble() : 0.0;
    const double spanHz = haveSpan ? span.at(1).toDouble() - spanLo : 1.0;
    QVector<DiversityKindBand> bands;

    m_rowHz.clear();
    m_rowHz.reserve(rows);
    m_rowKind.clear();
    m_rowKind.reserve(rows);
    m_table->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const QJsonObject c = candidates[r].toObject();
        const QJsonValue hzValue = c.value(QStringLiteral("hz"));
        const double hz = hzValue.isDouble() ? hzValue.toDouble() : 0.0;
        m_rowHz.push_back(hz);

        // "voice 0.82". An older gate says nothing about what it found, and a
        // dash is the honest rendering of that -- not "noise", which would be
        // a verdict nobody reached.
        const QString kind = c.value(QStringLiteral("kind")).toString();
        m_rowKind.push_back(kind);
        const QJsonValue confValue = c.value(QStringLiteral("kind_conf"));
        const QString kindText =
            kind.isEmpty()
                ? emDash()
                : (confValue.isDouble()
                       ? QStringLiteral("%1 %2").arg(kindLabel(kind),
                                                     number(c, "kind_conf", 2))
                       : kindLabel(kind));

        // The dial the gate quotes sits just outside the energy on the carrier
        // side, so the stretch an operator SEES runs one candidate width into
        // the sideband: above the dial on USB, below it on LSB.
        const QJsonValue widthValue = c.value(QStringLiteral("width_hz"));
        if (haveSpan && hz > 0.0 && !kind.isEmpty() && widthValue.isDouble()) {
            const double width = std::max(0.0, widthValue.toDouble());
            const QString mode = c.value(QStringLiteral("mode")).toString();
            double from = hz - width / 2.0;
            double to = hz + width / 2.0;
            if (mode == QLatin1String("USB")) {
                from = hz;
                to = hz + width;
            } else if (mode == QLatin1String("LSB")) {
                from = hz - width;
                to = hz;
            }
            const float a = float((from - spanLo) / spanHz);
            const float b = float((to - spanLo) / spanHz);
            if (b > 0.0f && a < 1.0f)
                bands.push_back({std::clamp(a, 0.0f, 1.0f),
                                 std::clamp(b, 0.0f, 1.0f), kind});
        }

        const QStringList cells{
            hz > 0.0 ? QString::number(hz / 1e3, 'f', 2) : emDash(),
            kindText,
            number(c, "score", 2),
            signedNumber(c, "snr_db", 1),
            number(c, "syllabic", 2),
            minutesSeconds(c, "active_s"),
            lastHeard(c),
            number(c, "phase_deg", 0),
            number(c, "coherence", 2),
            signedNumber(c, "gain_db", 1),
        };
        // The kHz cell is the gate's SNAPPED frequency -- it puts candidates on
        // a 500 Hz grid, because a conversation is not a carrier and a centre
        // quoted to the hertz is precision the estimate does not have. When the
        // gate also sends the raw estimate the row says so on the hover, so the
        // snap is visible rather than silent: a 130 Hz difference between the
        // two is normal, and a large one means the detector is unsure.
        const QJsonValue rawValue = c.value(QStringLiteral("hz_raw"));
        const QString tip =
            rawValue.isDouble()
                ? tr("estimate %1 kHz").arg(rawValue.toDouble() / 1e3, 0, 'f', 2)
                : QString();
        // The kind cell explains itself instead: what the verdict was made of,
        // and how sure the gate is that it is the right one.
        const QString kindTip =
            kind.isEmpty()
                ? tr("This gate does not say what it found. Older gates report "
                     "where a conversation is and leave what it is to you.")
                : (confValue.isDouble()
                       ? tr("%1\nHow sure: %2 of 1.")
                             .arg(kindExplanation(kind), number(c, "kind_conf", 2))
                       : kindExplanation(kind));
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(col == kKindColumn ? kindTip : tip);
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
    // Best first, and the first band covering a bin wins the colour: where two
    // candidates overlap on the strip, the one the gate ranked higher paints.
    m_strip->setKindBands(bands);
    applyKindColours();
}

void DiversityFinderPanel::applyKindColours()
{
    auto& tm = ThemeManager::instance();
    for (int r = 0; r < m_rowKind.size() && r < m_table->rowCount(); ++r) {
        QTableWidgetItem* item = m_table->item(r, kKindColumn);
        const QString token = kindToken(m_rowKind[r]);
        // Empty only when the row's kind is empty, i.e. the gate sent no
        // verdict at all: the row still says so in words, and a colour
        // invented for "nothing said" would claim a verdict the gate never
        // gave. Every kind the gate DOES name, known or not, gets a token --
        // see kindToken()'s own comment for why the fallback is never empty.
        if (item && !token.isEmpty())
            item->setForeground(tm.color(m_table, token));
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
