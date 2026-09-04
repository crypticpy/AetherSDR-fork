#include "gui/AetherGateChainVisual.h"

#include "core/AudioEngine.h"
#include "core/ClientEq.h"
#include "core/ThemeManager.h"
#include "gui/ClientEqFftAnalyzer.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityHelp.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace AetherSDR {

namespace {

// The one readout under the picture, at its widest. Every field has a fixed
// format so the line cannot reflow when a digit appears: five figures of Hz,
// one decimal of dB, two figures of notches.
QString visualReadoutWorstCase()
{
    return QStringLiteral("20000-20000 Hz · floor -100.0 dB · 12 notches"
                          " · AUTO 20000-20000 · SHARP · 4095 taps"
                          " · 2000 Hz skirt");
}

// The corner readout, at its widest.
QString cursorWorstCase()
{
    return QStringLiteral("20 000 Hz · +100.0 dB over floor");
}

// "1450" -> "1 450". A thousands space and no comma: this is a FREQUENCY, and
// every other frequency in this application is grouped the same way.
QString groupedHz(qint64 hz)
{
    QString digits = QString::number(std::abs(hz));
    for (int at = digits.size() - 3; at > 0; at -= 3)
        digits.insert(at, QLatin1Char(' '));
    return hz < 0 ? QLatin1Char('-') + digits : digits;
}

const char* kCursorStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 10px;"
    " background: transparent; }";

// makeCaption()'s own rule, plus the [live] selector it has no reason to
// carry for every caption in the app: this one caption is where a failed poll
// is said out loud, and setLive() on a label whose style sheet has no [live]
// rule would set a property nothing reads. Same token the AUTO CLEAN banner's
// own readout line turns warning-coloured with.
// The legend row: secondary, like every other line of small print in this
// window. The colour that matters in it is inside each swatch's own span.
const char* kLegendStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }";

const char* kVisualCaptionStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

// The caption's own walkthrough -- everything the 90-character tooltip rule
// costs the hover, said in full for a screen reader. Its own function because
// refreshCaption() re-states it on every change of the caption's text.
QString captionWalkthrough()
{
    return QCoreApplication::translate(
        "AetherSDR::AetherGateChainVisual",
        "What the filter does to everything arriving, drawn over what is "
        "actually arriving, with a second trace for what you are actually "
        "hearing. Drag an edge to move it, double-click to notch what is "
        "under the pointer, drag a notch mark to move it, right-click one to "
        "take it away, click any mark to go to its stage on the CHAIN tab. "
        "Shift+click a signal to SQUEEZE a null or notch onto it; Shift+click "
        "or right-click the SQUEEZE mark, or press RELEASE, to let it go.");
}

// "null"/"notch" -> "NULL"/"NOTCH", the gate's own two SQUEEZE tools. See
// DiversityFilterPanelSqueeze.cpp's copy of the same mapping for the picture
// itself; this one is for the status line's own words.
QString squeezeToolWord(const QString& tool)
{
    if (tool == QLatin1String("null"))
        return QStringLiteral("NULL");
    if (tool == QLatin1String("notch"))
        return QStringLiteral("NOTCH");
    return QString();
}

} // namespace

AetherGateChainVisual::AetherGateChainVisual(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("gateChainVisual"));
    setAccessibleName(tr("The filter as a picture"));
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(0, 6, 0, 0);
    box->setSpacing(6);

    auto* caption = DiversityWidgets::makeCaption(tr("PASSBAND"), this);
    m_caption = caption;
    caption->setObjectName(QStringLiteral("gateChainVisualCaption"));
    ThemeManager::instance().applyStyleSheet(
        caption, QString::fromLatin1(kVisualCaptionStyle));
    caption->setAccessibleName(tr("What this picture is"));
    caption->setToolTip(
        tr("The filter drawn live - drag an edge, double-click to notch, "
           "Shift+click to squeeze."));
    caption->setAccessibleDescription(captionWalkthrough());

    // SQUEEZE (B24): the operator's own null or notch, asked for either by
    // pointing (Shift+click on the picture, see DiversityFilterPanel) or, for
    // a comb of carriers where there is no single point to click, from this
    // button. RELEASE is the one control that answers BOTH of Shift+click's
    // targets -- the bracket and the comb -- with the same squeeze=off.
    auto* captionRow = new QHBoxLayout();
    captionRow->setContentsMargins(0, 0, 0, 0);
    captionRow->setSpacing(6);
    captionRow->addWidget(caption);
    // The caption's own tooltip lost the drag/click/SQUEEZE walkthrough to
    // the H1 90-char rule (see caption->setAccessibleDescription() above);
    // this is where that walkthrough lives now that a mouse -- rather than a
    // screen reader -- has to ask for it.
    captionRow->addWidget(DiversityHelp::button(this, DiversityHelp::Topic::Chain));
    captionRow->addStretch(1);
    m_squeezeComb = new QPushButton(tr("SQUEEZE: COMB"), this);
    m_squeezeComb->setObjectName(QStringLiteral("gateChainSqueezeComb"));
    m_squeezeComb->setAccessibleName(tr("Squeeze a comb of carriers"));
    m_squeezeComb->setToolTip(
        tr("Narrows notches onto a whole comb of evenly spaced carriers at once."));
    m_squeezeComb->setAccessibleDescription(
        tr("Asks the gate to find a comb of evenly spaced carriers across this "
           "passband — a switching supply, a multi-tone jammer — and "
           "place a tight null or notch on every tooth at once, instead of the "
           "one signal a Shift+click would target."));
    m_squeezeComb->setFixedHeight(22);
    applyToggleButtonStyle(m_squeezeComb, ToggleTribe::Warning);
    connect(m_squeezeComb, &QPushButton::clicked, this, [this]() {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("squeeze"), QStringLiteral("comb"));
        emit requestWrite(QStringLiteral("/diversity/set"), q);
    });
    captionRow->addWidget(m_squeezeComb, 0);

    m_squeezeRelease = new QPushButton(tr("RELEASE"), this);
    m_squeezeRelease->setObjectName(QStringLiteral("gateChainSqueezeRelease"));
    m_squeezeRelease->setAccessibleName(tr("Let the SQUEEZE go"));
    m_squeezeRelease->setToolTip(tr("Undoes SQUEEZE and gives the full passband back."));
    m_squeezeRelease->setAccessibleDescription(
        tr("Takes away whatever SQUEEZE is armed or holding, one signal or a "
           "whole comb, and returns the passband to its normal width."));
    m_squeezeRelease->setFixedHeight(22);
    m_squeezeRelease->setEnabled(false);
    applyToggleButtonStyle(m_squeezeRelease, ToggleTribe::Warning);
    connect(m_squeezeRelease, &QPushButton::clicked, this, [this]() {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("squeeze"), QStringLiteral("off"));
        emit requestWrite(QStringLiteral("/diversity/set"), q);
    });
    captionRow->addWidget(m_squeezeRelease, 0);
    box->addLayout(captionRow);

    m_squeezeLine = new QLabel(this);
    m_squeezeLine->setObjectName(QStringLiteral("gateChainSqueezeLine"));
    m_squeezeLine->setAccessibleName(tr("SQUEEZE state"));
    m_squeezeLine->setWordWrap(false);
    m_squeezeLine->setTextInteractionFlags(Qt::NoTextInteraction);
    ThemeManager::instance().applyStyleSheet(m_squeezeLine,
                                             QString::fromLatin1(kCursorStyle));
    box->addWidget(m_squeezeLine);

    m_panel = new DiversityFilterPanel(this);
    m_panel->setMinimumHeight(320);
    m_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    box->addWidget(m_panel, 1);

    // The corner readout, IN the picture -- a child of the panel, laid out in
    // its top right corner. It is what makes a double-click notch trustworthy:
    // you can read the frequency you are about to kill before you kill it.
    m_cursor = new QLabel(m_panel);
    m_cursor->setObjectName(QStringLiteral("gateChainVisualCursor"));
    m_cursor->setAccessibleName(tr("Under the pointer"));
    m_cursor->setWordWrap(false);
    m_cursor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // The label sits IN the picture, so it must not eat the double-click that
    // is meant to drop a notch under it.
    m_cursor->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_cursor->setFixedWidth(
        m_cursor->fontMetrics().horizontalAdvance(cursorWorstCase()) + 4);
    ThemeManager::instance().applyStyleSheet(m_cursor,
                                             QString::fromLatin1(kCursorStyle));
    auto* corner = new QHBoxLayout(m_panel);
    corner->setContentsMargins(0, 4, 10, 0);
    corner->addStretch(1);
    corner->addWidget(m_cursor, 0, Qt::AlignTop | Qt::AlignRight);

    m_readout = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainVisualReadout"), visualReadoutWorstCase(),
        tr("Passband, floor, notches, auto edges, shape - the filter in numbers."),
        this);
    m_readout->setAccessibleDescription(
        tr("The passband in force, the noise floor the receiver measured, how "
           "many notches are set, where the automatic width has put the edges "
           "when it is running, and the shape, tap count and skirt width that "
           "say how sharp the filter actually is."));
    m_readout->setAccessibleName(tr("The filter now"));
    box->addWidget(m_readout);

    m_legend = new QLabel(this);
    m_legend->setObjectName(QStringLiteral("gateChainVisualLegend"));
    m_legend->setAccessibleName(tr("What the colours on the picture are"));
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setWordWrap(false);
    m_legend->setTextInteractionFlags(Qt::NoTextInteraction);
    m_legend->setToolTip(
        tr("What each colour on the picture above is, for the marks it has on it."));
    // Raw token colours go into the rich text by name, so applyStyleSheet's
    // reverse map never sees them -- declared here the way
    // DiversitySpatialLegend declares its own, so an Inspect-mode click still
    // surfaces every token this row reads.
    ThemeManager::instance().declareWidgetTokens(
        m_legend, QStringList{
                      QStringLiteral("color.accent"),
                      QStringLiteral("color.accent.bright"),
                      QStringLiteral("color.accent.danger"),
                      QStringLiteral("color.accent.dim"),
                      QStringLiteral("color.accent.success"),
                      QStringLiteral("color.accent.warning"),
                      QStringLiteral("color.spectrum.average"),
                      QStringLiteral("color.spectrum.trace"),
                      QStringLiteral("color.text.label"),
                      QStringLiteral("color.text.secondary"),
                  });
    ThemeManager::instance().applyStyleSheet(m_legend,
                                             QString::fromLatin1(kLegendStyle));
    box->addWidget(m_legend);

    connect(m_panel, &DiversityFilterPanel::edgesDragged, this,
            &AetherGateChainVisual::onEdgesDragged);
    connect(m_panel, &DiversityFilterPanel::notchRequested, this, [this](double hz) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("add"), QString::number(qint64(hz)));
        emit requestWrite(QStringLiteral("/filter/notch"), q);
    });
    connect(m_panel, &DiversityFilterPanel::notchRemoveRequested, this,
            [this](double hz) {
                // The gate's own parameter for one notch: ?clear=<hz> takes
                // that one away, ?clear=1 takes them all.
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("clear"), QString::number(qint64(hz)));
                emit requestWrite(QStringLiteral("/filter/notch"), q);
            });
    connect(m_panel, &DiversityFilterPanel::notchMoveRequested, this,
            [this](double fromHz, double toHz) {
                // /filter/notch has add= and clear= and no move, so a move is
                // both, in that order, through the sequencer -- fired together
                // into a threaded server the add could land first and the clear
                // would then delete it.
                QList<ChainPresetWrite> writes;
                writes.append({QStringLiteral("/filter/notch"),
                               QStringLiteral("clear=%1").arg(qint64(fromHz)),
                               tr("take the notch at %1 Hz away")
                                   .arg(groupedHz(qint64(fromHz)))});
                writes.append({QStringLiteral("/filter/notch"),
                               QStringLiteral("add=%1").arg(qint64(toHz)),
                               tr("put one at %1 Hz").arg(groupedHz(qint64(toHz)))});
                emit requestSequence(writes, tr("MOVE NOTCH"));
            });
    connect(m_panel, &DiversityFilterPanel::markClicked, this,
            &AetherGateChainVisual::stageRequested);
    connect(m_panel, &DiversityFilterPanel::squeezeRequested, this,
            [this](double hz) {
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("squeeze"), QString::number(qint64(hz)));
                emit requestWrite(QStringLiteral("/diversity/set"), q);
            });
    connect(m_panel, &DiversityFilterPanel::squeezeReleaseRequested, this, [this]() {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("squeeze"), QStringLiteral("off"));
        emit requestWrite(QStringLiteral("/diversity/set"), q);
    });
    // ROOFING · DIGITAL PEAK OFFSET (A1): the handle's own release, straight
    // to /filter/set the way an edge drag is -- roof_offset_hz is the gate's
    // own parameter name (docs/DIVERSITY.md's "For scripts" section), sent
    // verbatim, no string built beyond the number itself.
    connect(m_panel, &DiversityFilterPanel::roofOffsetDragged, this,
            [this](int offsetHz) {
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("roof_offset_hz"), QString::number(offsetHz));
                emit requestWrite(QStringLiteral("/filter/set"), q);
            });
    // The corner reads a MEASUREMENT now, not a coordinate: how far the
    // arriving spectrum stands over the gate's own floor at the pointer. "-12
    // dB" used to mean "your pointer is twelve tenths of the way down this
    // widget", which is a fact about the mouse. "+34.0 dB over floor" is a
    // fact about the band, and it is the one that decides whether the thing
    // under the pointer is worth notching.
    connect(m_panel, &DiversityFilterPanel::cursorMoved, this,
            [this](double hz, double dbOverFloor) {
                if (std::isnan(hz)) {
                    setCursorText(QString());
                    return;
                }
                setCursorText(
                    std::isnan(dbOverFloor)
                        ? tr("%1 Hz").arg(groupedHz(qint64(std::lround(hz))))
                        : tr("%1 Hz · %2%3 dB over floor")
                              .arg(groupedHz(qint64(std::lround(hz))),
                                   dbOverFloor >= 0.0 ? QStringLiteral("+")
                                                      : QStringLiteral("\u2212"),
                                   QString::number(std::abs(dbOverFloor), 'f', 1)));
            });

    // A mouse move arrives per PIXEL. Writing a QLabel per pixel is a
    // relayout per pixel on the widget the operator is currently dragging in,
    // which is exactly where "a little laggy" comes from. The text is written
    // at most once every 60 ms, and the last one always lands: the trailing
    // timer is what makes the number under a stopped pointer correct rather
    // than 59 ms stale.
    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    connect(m_cursorTimer, &QTimer::timeout, this,
            &AetherGateChainVisual::flushCursorText);

    // The second trace's tick. Built even when there is no audio engine --
    // updateLocalSpectrumTimer() is what decides whether it ever runs -- so
    // setAudioEngine() after the fact needs no construction of its own.
    m_fftTimer = new QTimer(this);
    m_fftTimer->setInterval(40);   // 25 Hz, the rate StripEqPanel's own uses
    connect(m_fftTimer, &QTimer::timeout, this,
            &AetherGateChainVisual::tickLocalSpectrum);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            &AetherGateChainVisual::refreshLegend);

    clear();
}

AetherGateChainVisual::~AetherGateChainVisual() = default;

void AetherGateChainVisual::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    // Coming to the front, the picture catches up with the last body rather
    // than sitting empty until the next poll half a second later.
    if (m_active && !m_last.isEmpty())
        applyFilter(m_last);
    updateLocalSpectrumTimer();
}

// Handed the engine, not asked for it. A null one is the normal state in
// every test and on a build with no audio device, and the tab is expected to
// work without the second trace -- so this is the only place that decides
// whether the timer may run at all.
void AetherGateChainVisual::setAudioEngine(AudioEngine* audio)
{
    if (m_audio == audio)
        return;
    m_audio = audio;
    updateLocalSpectrumTimer();
}

// The 25 Hz FFT runs ONLY while this tab is in front and there is an engine
// to tap: a hidden tab costs nothing is the rule the whole widget is built on
// (see the header), and an FFT on a tab nobody is looking at would break it.
void AetherGateChainVisual::updateLocalSpectrumTimer()
{
    if (!m_fftTimer)
        return;
    const bool wanted = m_active && m_audio != nullptr;
    if (wanted == m_fftTimer->isActive())
        return;
    if (wanted) {
        if (!m_fft)
            m_fft = std::make_unique<ClientEqFftAnalyzer>();
        m_fftTimer->start();
        return;
    }
    m_fftTimer->stop();
    // The smoothing is reset as well as the trace cleared: coming back to the
    // tab must not show a bar chart of what was playing a minute ago.
    if (m_fft)
        m_fft->reset();
    if (m_panel)
        m_panel->clearLocalSpectrum();
}

// The RX tap, post-everything: this is the client's own output, so the chain
// it has been through is the gate's AND this application's. That is the point
// -- the response curve is what the gate PROMISED, this is what survived.
void AetherGateChainVisual::tickLocalSpectrum()
{
    if (!m_audio || !m_fft || !m_panel)
        return;
    std::vector<float> samples(size_t(ClientEqFftAnalyzer::kFftSize), 0.0f);
    if (!m_audio->copyRecentClientEqRxSamples(samples.data(),
                                              ClientEqFftAnalyzer::kFftSize))
        return;
    m_fft->update(samples.data(), ClientEqFftAnalyzer::kFftSize);
    ClientEq* eq = m_audio->clientEqRx();
    const double fs = eq ? eq->sampleRate() : 24000.0;
    m_panel->setLocalSpectrum(m_fft->magnitudesDb(), fs);
}

void AetherGateChainVisual::setCursorText(const QString& text)
{
    if (!m_cursor)
        return;
    m_cursorPending = text;
    if (m_cursorClock.isValid() && m_cursorClock.elapsed() < kCursorThrottleMs) {
        if (m_cursorTimer && !m_cursorTimer->isActive())
            m_cursorTimer->start(kCursorThrottleMs - int(m_cursorClock.elapsed()));
        return;
    }
    flushCursorText();
}

void AetherGateChainVisual::flushCursorText()
{
    if (!m_cursor)
        return;
    m_cursorClock.restart();
    m_cursor->setText(m_cursorPending);
    m_cursor->setAccessibleDescription(m_cursorPending);
}

bool AetherGateChainVisual::dragging() const
{
    return m_panel && m_panel->dragging();
}

void AetherGateChainVisual::clear()
{
    m_last = QJsonObject();
    m_panel->clear();
    m_cursorPending.clear();
    m_cursorClock.invalidate();
    if (m_cursorTimer)
        m_cursorTimer->stop();
    m_cursor->setText(QString());
    m_cursor->setAccessibleDescription(QString());
    m_readout->setText(QString());
    refreshCaption();
    refreshSqueezeLine();
    refreshLegend();
}

// The gate came or went. Everything the picture OFFERS has to go with it: a
// SQUEEZE: COMB button that is still live once there is nothing on the other
// end is a button whose write can only ever fail, and "Shift+click a signal"
// under an empty plot is an instruction for a gesture that has nothing to
// land on. Same shape AetherGateChainHearRawButton::setPresent() has.
void AetherGateChainVisual::setPresent(bool present)
{
    if (m_present == present)
        return;
    m_present = present;
    if (m_squeezeComb)
        m_squeezeComb->setEnabled(present);
    refreshSqueezeLine();
}

// A poll that did not answer. The curve is not cleared -- what is drawn was
// true a second ago and blanking it would be a bigger lie than keeping it --
// so the caption carries the cue instead, in the same [live] property the
// AUTO CLEAN banner uses.
void AetherGateChainVisual::setStale(bool stale)
{
    if (m_stale == stale)
        return;
    m_stale = stale;
    refreshCaption();
}

void AetherGateChainVisual::applyFilter(const QJsonObject& filter)
{
    if (filter.isEmpty() || !filter.value(QStringLiteral("error")).toString().isEmpty())
        return;
    m_last = filter;
    // A body arriving while the tab is behind is REMEMBERED and not drawn:
    // parsing it, comparing it and repainting a widget nobody can see is
    // exactly the cost the operator called lag.
    if (!m_active)
        return;
    // A 2 Hz poll landing mid-drag would snatch the handle out from under the
    // pointer, so the picture is fed between gestures and not during one.
    if (m_panel->dragging())
        return;
    const QJsonValue low = filter.value(QStringLiteral("low_hz"));
    const QJsonValue high = filter.value(QStringLiteral("high_hz"));
    if (low.isDouble())
        m_gateLowHz = int(std::lround(low.toDouble()));
    if (high.isDouble())
        m_gateHighHz = int(std::lround(high.toDouble()));
    m_panel->applyStatus(filter);
    refreshCaption();
    refreshReadout();
    refreshSqueezeLine();
    refreshLegend();
}

// Which edge moved? The one that is no longer where the GATE said it was. A
// drag of the low handle writes low= alone rather than re-asserting a high=
// the auto-width tracker may own, which would fight the tracker every time an
// operator touched the other edge.
void AetherGateChainVisual::onEdgesDragged(int lowHz, int highHz)
{
    QList<ChainPresetWrite> writes;
    if (lowHz != m_gateLowHz) {
        writes.append({QStringLiteral("/filter/set"),
                       QStringLiteral("low=%1").arg(lowHz),
                       tr("low edge to %1 Hz").arg(groupedHz(lowHz))});
    }
    if (highHz != m_gateHighHz) {
        writes.append({QStringLiteral("/filter/set"),
                       QStringLiteral("high=%1").arg(highHz),
                       tr("high edge to %1 Hz").arg(groupedHz(highHz))});
    }
    if (writes.isEmpty())
        return;
    if (writes.size() == 1) {
        emit requestWrite(writes.first().route, QUrlQuery(writes.first().query));
        return;
    }
    // Both edges at once is the WIDTH presets' case, not a drag's. Two writes
    // in order, each waited for, rather than two GETs racing each other.
    emit requestSequence(writes, tr("PASSBAND"));
}

void AetherGateChainVisual::refreshReadout()
{
    QStringList parts;
    parts << tr("%1-%2 Hz").arg(m_panel->lowHz()).arg(m_panel->highHz());
    const double floorDb = m_panel->spectrumFloorDb();
    // A dash, not a zero: before the gate has heard a block there is no floor,
    // and "0.0 dB" would be a measurement nobody made.
    parts << (std::isnan(floorDb) ? tr("floor —")
                                  : tr("floor %1 dB").arg(floorDb, 0, 'f', 1));
    const int notches = m_panel->notchCount();
    parts << (notches == 1 ? tr("1 notch") : tr("%1 notches").arg(notches));
    if (!std::isnan(m_panel->autoLowHz()) && !std::isnan(m_panel->autoHighHz())) {
        parts << tr("AUTO %1-%2")
                     .arg(qint64(m_panel->autoLowHz()))
                     .arg(qint64(m_panel->autoHighHz()));
    }
    // WHAT SHAPE OF FILTER IT IS. All three are top-level on /filter
    // (core/filter.py's status()) and none of them was shown anywhere: the
    // drawn curve's skirt is two pixels wide at this scale, so "how sharp is
    // it" was a question the picture could not answer and the numbers could.
    const QString shape = m_last.value(QStringLiteral("shape")).toString().toUpper();
    if (!shape.isEmpty())
        parts << shape;
    const QJsonValue taps = m_last.value(QStringLiteral("taps"));
    if (taps.isDouble())
        parts << tr("%1 taps").arg(qint64(std::llround(taps.toDouble())));
    const QJsonValue skirt = m_last.value(QStringLiteral("transition_hz"));
    if (skirt.isDouble())
        parts << tr("%1 Hz skirt").arg(qint64(std::llround(skirt.toDouble())));

    const QString text = parts.join(QStringLiteral(" · "));
    m_readout->setText(text);
    m_readout->setAccessibleDescription(text);
}

// The key under the plot. Built by the panel -- it is the widget that knows
// both which families are on the picture and which token each is painted in
// -- and merely shown here. Empty when there is nothing drawn, so a dead tab
// does not carry a key to marks that are not there.
void AetherGateChainVisual::refreshLegend()
{
    if (!m_legend || !m_panel)
        return;
    const QString html = m_panel->legendHtml();
    m_legend->setText(html);
    m_legend->setVisible(!html.isEmpty());
    // The words without the swatches: a screen reader gets no colour out of a
    // coloured square, so the description says what the row is FOR instead.
    m_legend->setAccessibleDescription(
        html.isEmpty()
            ? QString()
            : tr("Which colour on the picture above is which mark: the "
                 "passband edges, the response curve, the band arriving, what "
                 "you are hearing, and every notch, SQUEEZE, AUTO or roofing "
                 "mark the gate has put on it."));
}

// WHAT THE PICTURE IS. A response curve on an always-positive audio axis is
// the same picture on USB, on LSB and on CW-R -- the gate abs-ifies every
// edge before it answers (see DiversityFilterPanel.h) -- so the one thing the
// drawing genuinely cannot say is which of them it is OF. Both keys are
// top-level on /filter: `sideband` is the gate's own "lsb"/"usb"
// (core/filter.py's status()), `mode` the adapter's slice mode ("CW-R",
// "USB", "PKTUSB"). A mode that is only its own sideband spelled again
// ("USB" over "usb") is said once.
void AetherGateChainVisual::refreshCaption()
{
    if (!m_caption)
        return;
    QStringList parts;
    parts << tr("PASSBAND");
    const QString sideband =
        m_last.value(QStringLiteral("sideband")).toString().toUpper();
    const QString mode = m_last.value(QStringLiteral("mode")).toString().toUpper();
    if (!sideband.isEmpty())
        parts << sideband;
    if (!mode.isEmpty() && mode != sideband)
        parts << mode;
    if (m_stale)
        parts << tr("NOT ANSWERING");
    m_caption->setText(parts.join(QStringLiteral(" · ")));
    DiversityWidgets::setLive(m_caption, m_stale);
    // The walkthrough is what a screen reader needs from this label whether or
    // not the poll is answering, so the staleness sentence is added to it
    // rather than put in its place.
    m_caption->setAccessibleDescription(
        m_stale ? tr("The picture is old - the last poll to the gate did not "
                     "answer. ")
                      + captionWalkthrough()
                : captionWalkthrough());
}

// off/armed/held -- told apart the same way DiversityFilterPanel itself
// tells them apart (see squeezeOff()/squeezeArmed()/squeezeHeld()): "since"
// null is off, "since" set but not yet "held" is armed (the gate is still
// listening for enough coherence to commit), "held" is the mark actually on
// the picture. The gate's own `why` is carried verbatim, per the task -- it
// is the one sentence that explains a NULL-vs-NOTCH choice the operator did
// not make.
QString AetherGateChainVisual::squeezeLineText() const
{
    if (!m_panel)
        return QString();
    // No gate, no offer: "Shift+click a signal" under an empty plot is an
    // instruction for a gesture with nothing to land on.
    if (!m_present)
        return tr("SQUEEZE off - the gate is away");
    if (m_panel->squeezeOff())
        return tr("SQUEEZE off — Shift+click a signal, or SQUEEZE: COMB");

    const QString target = m_panel->squeezeTarget();
    const QString why = m_panel->squeezeWhy();
    const bool comb = target == QLatin1String("comb");

    if (m_panel->squeezeArmed()) {
        const double spacingHz = m_panel->squeezeCombSpacingHz();
        // squeeze=comb with no comb found yet: every comb number is null
        // (core/squeeze.py's set_comb_auto(), CombDetector still feeding), so
        // there is no spacing to name -- "spacing 0 Hz" was a measurement
        // nobody made. The gate's `reason` ("no comb found") stands in for
        // the `why` it has not written yet.
        if (comb && (std::isnan(spacingHz) || spacingHz <= 0.0)) {
            const QString said = why.isEmpty() ? m_panel->squeezeReason() : why;
            return said.isEmpty() ? tr("SQUEEZE looking for a comb to notch")
                                  : tr("SQUEEZE looking for a comb to notch — %1").arg(said);
        }
        const QString where =
            comb ? tr("a comb, spacing %1 Hz").arg(groupedHz(qint64(std::lround(spacingHz))))
                 : tr("%1 Hz").arg(groupedHz(qint64(std::lround(m_panel->squeezeHz()))));
        return why.isEmpty() ? tr("SQUEEZE arming on %1").arg(where)
                              : tr("SQUEEZE arming on %1 — %2").arg(where, why);
    }

    const QString tool = squeezeToolWord(m_panel->squeezeTool());
    const QString where =
        comb ? tr("comb, %1 teeth").arg(m_panel->squeezeCombTeethInBandCount())
             : tr("%1 Hz").arg(groupedHz(qint64(std::lround(m_panel->squeezeHz()))));
    QString text = tool.isEmpty() ? tr("SQUEEZE held on %1").arg(where)
                                  : tr("SQUEEZE %1 on %2").arg(tool, where);
    if (!why.isEmpty())
        text += tr(" — %1").arg(why);
    return text;
}

// Recomputed on every filter tick and every resize: the RELEASE button's
// enabled state and the status line's own text both depend on state that
// only DiversityFilterPanel::applyStatus() (via parseSqueeze()) knows.
void AetherGateChainVisual::refreshSqueezeLine()
{
    if (!m_squeezeLine || !m_panel)
        return;
    const QString full = squeezeLineText();
    m_squeezeLine->setAccessibleDescription(full);
    const QFontMetrics fm = m_squeezeLine->fontMetrics();
    m_squeezeLine->setText(fm.elidedText(full, Qt::ElideRight, m_squeezeLine->width()));
    // The line ELIDES, so the sentence it is eliding has to be reachable
    // without a screen reader too. The 90-character rule still applies to the
    // hover; past that the hover is elided as well and the whole thing is in
    // the accessible description above.
    m_squeezeLine->setToolTip(full.length() > 90 ? full.left(87) + QStringLiteral("…")
                                                 : full);
    if (m_squeezeRelease)
        m_squeezeRelease->setEnabled(m_present && !m_panel->squeezeOff());
}

void AetherGateChainVisual::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    refreshSqueezeLine();
}

} // namespace AetherSDR
