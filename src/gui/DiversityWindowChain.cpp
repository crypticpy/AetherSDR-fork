// The Diversity window's two top rows, and the two buttons on the second one
// that DO something rather than set something.
//
// WHY TWO ROWS. v2 put the four page tabs and the six pair controls in one
// strip, and the operator read it left to right as one sentence: "slice band
// site filter, then mode, then hear, then realign, then capture -- I can't tell
// if the controls across the top change based on what tab I'm on." They never
// did. So the tabs are now a row of their own (WHERE YOU ARE) and the pair
// controls a row of their own under it (WHAT THE PAIR IS DOING, on every page),
// with a PAIR caption in front of them saying so in the same voice the MODE and
// HEAR captions already speak in. The cost is about thirty pixels of height and
// the gain is that neither row can be misread as governing the other.
//
// WHY REALIGN AND CAPTURE ANSWER. Both were writes into silence. REALIGN sent
// its request and the button came straight back up, so "it doesn't seem to do
// anything" was a fair reading of a thing that had in fact worked. CAPTURE was
// worse: it recorded a real file and said so in a small readout at the bottom
// of the SLICE page's EVENTS box, invisible from the other three pages -- so
// from BAND, SITE or FILTER the button did nothing at all, twice, and then the
// operator stopped trusting it. Neither needed a new route; both needed the
// answer put where the hand that pressed the button already is. So each button
// now narrates on its own face (ALIGNING… -> LAG -63; REC 10 s -> SAVED), and
// each also puts one line on the footer status strip, which is the only thing
// in this window visible from every page.
//
// Nothing here is optimistic. The realign result is the gate's own lag from the
// status that ended the realign, compared against the gate's own lag from the
// status before it started; the capture line is the gate's own path. The
// countdown is the only number this file invents, and it counts the operator's
// own spin box down rather than claiming to know anything about the gate.
//
// Its own file for the reason DiversityWindowBand.cpp, DiversityWindowSite.cpp
// and DiversityWindowPanels.cpp are: these are members of DiversityWindow, and
// DiversityWindow.cpp is at the file-size budget AGENTS.md asks for.

#include "gui/DiversityWindow.h"

#include "core/ThemeManager.h"
#include "gui/DiversityHelp.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUrlQuery>
#include <QWidget>

#include <cmath>

namespace AetherSDR {

namespace {

constexpr int kControlHeight = 26;

// How long a finished REALIGN or CAPTURE holds its answer on the button and on
// the status strip. Three seconds is long enough to read a short result at a
// glance and short enough that a button cannot be found still lying about its
// state a minute later; the capture line gets six because its basename is
// worth writing down.
constexpr int kResultMs = 3000;
constexpr int kCaptureStripMs = 6000;
// A realign that has not been answered in this long did not happen. The gate's
// own align route carries a 4 s transfer timeout, so 6 s is "the request is
// gone" rather than "the request is slow".
constexpr int kRealignTimeoutMs = 6000;

QString dash()
{
    return QStringLiteral("—");
}

qint64 roundLag(double lag)
{
    return qint64(std::llround(lag));
}

// What the MAIN panadapter is drawing, not this window's own scope: the gate
// serves both the audio and the panadapter FFT from the same combiner, but
// they answer two different questions, and an operator who has only ever
// watched the spectrum has no way to know that "combined" on the wire can
// silently mean "loop A, because the two tuners are not aligned yet." Spelled
// out here rather than left to be inferred from PAIR's HEAR row, which is
// audio only and cannot speak for what is drawn.
QString spectrumSourceText(const QJsonObject& d, bool aligned)
{
    if (!d.contains(QStringLiteral("pan")))
        return QCoreApplication::translate("DiversityWindow", "pan: %1").arg(dash());
    const QString pan = d.value(QStringLiteral("pan")).toString();
    if (pan == QLatin1String("combined")) {
        return aligned
            ? QCoreApplication::translate("DiversityWindow", "pan: A+B")
            : QCoreApplication::translate("DiversityWindow", "pan: A, not aligned");
    }
    if (pan == QLatin1String("a"))
        return QCoreApplication::translate("DiversityWindow", "pan: A only");
    if (pan == QLatin1String("b"))
        return QCoreApplication::translate("DiversityWindow", "pan: B only");
    if (pan == QLatin1String("nulled"))
        return QCoreApplication::translate("DiversityWindow", "pan: nulled");
    return QCoreApplication::translate("DiversityWindow", "pan: %1").arg(dash());
}

} // namespace

// --------------------------------------------------------------------------
// Row 1: where you are
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildTabRow()
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("diversityWindowTabRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // The tabs themselves still live in DiversityWindowBand.cpp beside the page
    // they switch to; all that has changed is the row they are appended to.
    buildPageSwitch(row);
    layout->addStretch(1);

    // The right-hand hint is what turns four bare words into a control: a tab
    // strip that nobody recognises as a tab strip is four buttons whose effect
    // has to be discovered by pressing them.
    QLabel* hint = DiversityWidgets::makeFieldLabel(tr("pages"), row);
    hint->setObjectName(QStringLiteral("diversityWindowPagesHint"));
    hint->setAccessibleName(tr("Pages"));
    hint->setToolTip(tr("These four buttons choose which page is shown. The "
                        "row under them is the pair itself and applies on "
                        "every page."));
    hint->setAccessibleDescription(hint->toolTip());
    layout->addWidget(hint);

    // ONE help button, at the right-hand end, retargeted to whichever page is
    // showing. Five would have been five more lit boxes on the row this
    // window keeps deliberately quiet -- and the question an operator has is
    // always about the page in front of them.
    m_pageHelpButton = DiversityHelp::button(this, DiversityHelp::Topic::Session);
    m_pageHelpButton->setObjectName(QStringLiteral("diversityHelpButtonPage"));
    layout->addWidget(m_pageHelpButton);
    retargetPageHelp(0);
    return row;
}

// DiversityHelp::button() hard-codes its topic inside the click lambda, so
// pointing it somewhere else means dropping that connection and making a new
// one. Nothing else about the button changes -- it is the same widget, under
// the same name, all session.
void DiversityWindow::retargetPageHelp(int page)
{
    if (!m_pageHelpButton)
        return;
    DiversityHelp::Topic topic = DiversityHelp::Topic::Session;
    switch (page) {
    case DiversitySessionModel::PageSlice:
        topic = DiversityHelp::Topic::Slice;
        break;
    case DiversitySessionModel::PageBand:
        topic = DiversityHelp::Topic::Band;
        break;
    case DiversitySessionModel::PageSite:
        topic = DiversityHelp::Topic::Site;
        break;
    case DiversitySessionModel::PageFilter:
        topic = DiversityHelp::Topic::Filter;
        break;
    default:
        break;
    }
    QObject::disconnect(m_pageHelpButton, &QPushButton::clicked, nullptr, nullptr);
    m_pageHelpButton->setToolTip(tr("Help for this page"));
    m_pageHelpButton->setAccessibleDescription(
        tr("Opens the help for the page you are on."));
    connect(m_pageHelpButton, &QPushButton::clicked, this,
            [this, topic] { DiversityHelp::open(topic, this); });
}

// --------------------------------------------------------------------------
// Row 2: what the pair is doing, on every page
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildChainRow()
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("diversityWindowChainRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel* pair = DiversityWidgets::makeCaption(tr("PAIR"), row);
    pair->setObjectName(QStringLiteral("diversityWindowPairCaption"));
    pair->setAccessibleName(tr("Pair controls"));
    pair->setToolTip(tr("These apply on every page. They are the two tuners "
                        "themselves -- how their weight is solved, which leg "
                        "you hear, when they are realigned and what gets "
                        "recorded -- so nothing on this row changes when you "
                        "switch pages."));
    pair->setAccessibleDescription(pair->toolTip());
    layout->addWidget(pair);

    m_modeGroup = addButtonRow(
        row, tr("MODE"), QStringLiteral("mode"), QStringLiteral("diversityWindowMode"),
        {tr("OFF"), tr("MANUAL"), tr("NULL"), tr("TRACK")},
        {QStringLiteral("off"), QStringLiteral("manual"), QStringLiteral("null"),
         QStringLiteral("track")},
        {tr("Ignore the second loop entirely -- you hear antenna A on its own, "
            "as if this were an ordinary single-tuner receiver. This is the "
            "reference every A/B comparison should be made against."),
         tr("Combine the two loops using the phase and level you set on the "
            "knobs below. Use it when you can hear where the noise is and "
            "want to park a null on it by ear."),
         tr("Solve once for the weight that cancels the strongest interfering "
            "source, then hold it. Right for a steady local noise that is not "
            "going anywhere."),
         tr("Keep re-solving so the null follows a drifting or intermittent "
            "source, and recall a remembered weight the moment a known "
            "station comes back. Costs a little on very weak signals in "
            "exchange for staying on the noise.")});
    layout->addSpacing(10);
    // "HEAR", not "LISTEN": it writes /diversity/set?source=, which is what
    // reaches the operator's ears. OUT is the meters' word for the combined
    // output, and the short label is what lets four choices fit the row. Which
    // leg the PANADAPTER draws is a different key (pan=) and lives with the
    // noise tools, exactly where the sidebar panel puts it.
    m_hearGroup = addButtonRow(
        row, tr("HEAR"), QStringLiteral("source"), QStringLiteral("diversityWindowHear"),
        {tr("OUT"), tr("A"), tr("B"), tr("STEREO")},
        {QStringLiteral("combined"), QStringLiteral("a"), QStringLiteral("b"),
         QStringLiteral("stereo")},
        {tr("Listen to the combiner's output: the two loops added with "
            "whatever weight the current mode has arrived at."),
         tr("Listen to loop A on its own. Nothing is combined and nothing is "
            "nulled."),
         tr("Listen to loop B on its own."),
         tr("Loop A in the left channel, loop B in the right, one AGC for "
            "both: the loops as a soundstage on two speakers. A station "
            "arriving from one side sits off-centre; noise both loops see "
            "sits in the middle. The tracker keeps learning meanwhile.")});

    m_compareButton = new QPushButton(tr("Hear A only"), row);
    m_compareButton->setObjectName(QStringLiteral("diversityWindowCompareButton"));
    m_compareButton->setAccessibleName(tr("Hear antenna A only while pressed"));
    m_compareButton->setToolTip(
        tr("Hold this down to drop the combiner out and hear loop A raw; let "
           "go and it returns to the mode it was in. It is a momentary "
           "switch, not a setting, precisely so it cannot be left engaged by "
           "accident -- and it is the only honest way to hear what the "
           "combiner is actually buying you."));
    m_compareButton->setFixedHeight(kControlHeight);
    m_compareButton->setAutoRepeat(false);
    m_compareButton->setEnabled(false);
    // A press-and-hold A/B check, not a mode the operator can forget they
    // left engaged -- the sidebar panel's contract, kept identical here.
    connect(m_compareButton, &QPushButton::pressed, this, [this] {
        if (!m_present || !m_modeGroup->checkedButton())
            return;
        m_compareResumeMode = m_modeGroup->checkedButton()->property("diversityValue").toString();
        m_compareDown = true;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), QStringLiteral("off"));
        emit requestSet(q);
    });
    connect(m_compareButton, &QPushButton::released, this, [this] {
        if (!m_compareDown)
            return;
        m_compareDown = false;
        const QString mode = m_compareResumeMode;
        m_compareResumeMode.clear();
        if (mode.isEmpty())
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), mode);
        emit requestCompareRestore(q);
    });
    layout->addWidget(m_compareButton);

    m_realignButton = new QPushButton(tr("REALIGN"), row);
    m_realignButton->setObjectName(QStringLiteral("diversityWindowRealignButton"));
    m_realignButton->setAccessibleName(tr("Realign the two tuners"));
    m_realignButton->setToolTip(
        tr("Re-measure how far apart the two tuners' sample streams are and "
           "line them back up. Nothing can be combined until they are "
           "aligned, so press this after changing frequency or sample rate, "
           "or whenever the correlation peak in EVENTS has collapsed. The "
           "button reports the lag it arrived at, and whether that moved."));
    m_realignButton->setFixedHeight(kControlHeight);
    // Wide enough for the longest thing it can ever say, so a realign that
    // finishes does not shove the capture controls sideways for three seconds.
    m_realignButton->setMinimumWidth(
        m_realignButton->fontMetrics().horizontalAdvance(
            tr("LAG -99999 (was +99999)")) + 16);
    connect(m_realignButton, &QPushButton::clicked, this,
            &DiversityWindow::startRealign);
    layout->addWidget(m_realignButton);

    // Display only -- the control that actually sets pan= lives on the noise
    // tools panel (PAN, in DiversityWindowPanels.cpp), where it belongs beside
    // the spectrum it draws. This line just answers the question the control
    // itself does not: given that choice, what is the main panadapter showing
    // RIGHT NOW, including the one case the wire value alone hides -- pan
    // "combined" before the two tuners are aligned, when there is no weight
    // yet to combine with and the gate falls back to loop A alone. Kept to a
    // few words, not a sentence: this row is already at the window's own
    // 1120 px floor (see DiversityWindow.cpp's kInitialWidth), so the fuller
    // explanation lives in the tooltip instead of on the row.
    m_spectrumLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowSpectrumLine"),
        QStringLiteral("pan: A, not aligned"),
        tr("What the MAIN panadapter is drawing, not this window's own scope. "
           "The audio under HEAR is always the per-slice combiner's output. "
           "The spectrum normally matches it -- loop A and loop B added with "
           "the active slice's own weight -- except before the two tuners are "
           "aligned, when there is no weight yet and the spectrum falls back "
           "to loop A alone. \"A\", \"B\" and \"nulled\" mean the panadapter "
           "has been pointed at one leg or the null on purpose, from the PAN "
           "row on the noise tools page."),
        row);
    m_spectrumLine->setAccessibleName(tr("Panadapter spectrum source"));
    layout->addWidget(m_spectrumLine);

    layout->addStretch(1);

    m_captureSpin = new QSpinBox(row);
    m_captureSpin->setObjectName(QStringLiteral("diversityWindowCaptureSpin"));
    m_captureSpin->setAccessibleName(tr("Diversity capture duration"));
    m_captureSpin->setToolTip(
        tr("How many seconds of raw two-channel audio the CAPTURE button "
           "should record."));
    m_captureSpin->setRange(1, 60);
    m_captureSpin->setValue(10);
    m_captureSpin->setSuffix(QStringLiteral(" s"));
    layout->addWidget(m_captureSpin);

    m_captureButton = new QPushButton(tr("CAPTURE"), row);
    m_captureButton->setObjectName(QStringLiteral("diversityWindowCaptureButton"));
    m_captureButton->setAccessibleName(tr("Capture raw diversity audio"));
    m_captureButton->setToolTip(
        tr("Record both loops, uncombined, to a two-channel file on the gate. "
           "That file is the raw material for working out offline why a null "
           "would not form -- it is for the replay lab, not for listening, "
           "and nothing else in this window can be replayed. The button "
           "counts the recording down and then names the file it saved."));
    m_captureButton->setFixedHeight(kControlHeight);
    m_captureButton->setMinimumWidth(
        m_captureButton->fontMetrics().horizontalAdvance(tr("CAPTURE")) + 20);
    connect(m_captureButton, &QPushButton::clicked, this,
            &DiversityWindow::startCapture);
    layout->addWidget(m_captureButton);

    // The three timers are built here rather than in the constructor because
    // everything they touch is on this row. They are named for the same reason
    // the widgets are: a test that wants to make three seconds pass has to be
    // able to find the one timer it means.
    m_realignTimeout = new QTimer(this);
    m_realignTimeout->setObjectName(QStringLiteral("diversityWindowRealignTimeout"));
    m_realignTimeout->setSingleShot(true);
    m_realignTimeout->setInterval(kRealignTimeoutMs);
    connect(m_realignTimeout, &QTimer::timeout, this, [this] {
        if (!m_realignPending)
            return;
        m_realignPending = false;
        m_realignButton->setText(tr("REALIGN"));
        m_realignButton->setEnabled(true);
        addEventLines({tr("realign: no answer")});
    });
    m_resultTimer = new QTimer(this);
    m_resultTimer->setObjectName(QStringLiteral("diversityWindowResultTimer"));
    m_resultTimer->setSingleShot(true);
    m_resultTimer->setInterval(kResultMs);
    connect(m_resultTimer, &QTimer::timeout, this, [this] {
        m_realignButton->setText(tr("REALIGN"));
        resetCapture();
    });
    m_captureCountdown = new QTimer(this);
    m_captureCountdown->setObjectName(QStringLiteral("diversityWindowCaptureCountdown"));
    m_captureCountdown->setInterval(1000);
    connect(m_captureCountdown, &QTimer::timeout, this, [this] {
        if (m_captureRemaining > 0)
            --m_captureRemaining;
        m_captureButton->setText(tr("REC %1 s").arg(m_captureRemaining));
        if (m_captureRemaining <= 0)
            m_captureCountdown->stop();
    });
    return row;
}

// --------------------------------------------------------------------------
// REALIGN
// --------------------------------------------------------------------------

void DiversityWindow::startRealign()
{
    if (m_realignPending)
        return;
    m_realignPending = true;
    // The lag to compare against is the one from the status BEFORE the
    // request, which is why it is kept rather than read back afterwards.
    m_realignHaveLagBefore = m_haveLastLag;
    m_realignLagBefore = m_lastLagSamples;
    m_realignButton->setText(tr("ALIGNING…"));
    m_realignButton->setEnabled(false);
    m_realignTimeout->start();
    emit requestAlign();
}

// --------------------------------------------------------------------------
// CAPTURE
// --------------------------------------------------------------------------

void DiversityWindow::startCapture()
{
    if (!m_present || m_captureBusy)
        return;
    m_captureBusy = true;
    m_captureRemaining = m_captureSpin->value();
    m_captureButton->setEnabled(false);
    m_captureSpin->setEnabled(false);
    m_captureButton->setText(tr("REC %1 s").arg(m_captureRemaining));
    m_captureCountdown->start();
    m_captureResult->setText(tr("capture: recording…"));
    m_captureResult->setToolTip(QString());
    emit requestCapture(m_captureSpin->value());
}

void DiversityWindow::resetCapture()
{
    m_captureCountdown->stop();
    m_captureBusy = false;
    m_captureRemaining = 0;
    m_captureButton->setText(tr("CAPTURE"));
    m_captureButton->setEnabled(true);
    m_captureSpin->setEnabled(true);
}

void DiversityWindow::applyCaptureResult(bool ok, const QString& pathOrError)
{
    m_captureCountdown->stop();
    m_captureSpin->setEnabled(true);
    m_captureButton->setEnabled(true);
    m_captureButton->setText(ok ? tr("SAVED") : tr("FAILED"));
    // m_captureBusy stays set until this timer fires: until then a poll's own
    // capture.active must not put "CAPTURE" back over the answer.
    m_resultTimer->start();
    if (!ok) {
        // An error this request reported must survive the very next poll:
        // capture.active is already false by then and its "path" is still the
        // last SUCCESSFUL capture's.
        m_captureResult->setText(tr("capture: %1").arg(pathOrError));
        m_captureResult->setToolTip(QString());
        m_captureLocalResult = true;
        return;
    }
    if (pathOrError.isEmpty()) {
        m_captureResult->setText(tr("capture: %1").arg(dash()));
        m_captureResult->setToolTip(QString());
    } else {
        // The basename on the button and the strip, the whole path in the
        // readout's tooltip: a gate path is longer than any of the three
        // places this answer appears and would elide every one of them.
        const QString base = QFileInfo(pathOrError).fileName();
        m_captureResult->setText(tr("capture: %1").arg(base));
        m_captureResult->setToolTip(pathOrError);
        setStatusStripTransient(tr("capture saved %1").arg(base), kCaptureStripMs);
        if (base != m_lastCaptureAnnounced) {
            m_lastCaptureAnnounced = base;
            addEventLines({DiversityEventLog::captureSavedLine(base)});
        }
    }
    m_captureLocalResult = false;
}

// --------------------------------------------------------------------------
// One status object, as far as this row is concerned
// --------------------------------------------------------------------------

void DiversityWindow::applyChainStatus(const QJsonObject& d, bool aligned,
                                       bool realigning, bool haveLag, double lag)
{
    m_spectrumLine->setText(spectrumSourceText(d, aligned));

    // --- did the realign this row asked for finish? ------------------------
    // Resolved BEFORE the new lag is remembered, because the comparison is
    // against the lag from the poll before the request went out.
    if (m_realignPending && !realigning) {
        m_realignPending = false;
        m_realignTimeout->stop();
        const QString now = haveLag ? QString::number(roundLag(lag)) : dash();
        const bool moved = m_realignHaveLagBefore && haveLag
                           && roundLag(lag) != roundLag(m_realignLagBefore);
        if (moved) {
            const QString was = QString::asprintf(
                "%+lld", static_cast<long long>(roundLag(m_realignLagBefore)));
            m_realignButton->setText(tr("LAG %1 (was %2)").arg(now, was));
            setStatusStripTransient(tr("realigned · lag %1 · was %2").arg(now, was),
                                    kResultMs);
            addEventLines({tr("realign: lag %1, was %2").arg(now, was)});
        } else {
            m_realignButton->setText(tr("LAG %1").arg(now));
            setStatusStripTransient(tr("realigned · lag %1 · unchanged").arg(now),
                                    kResultMs);
            addEventLines({tr("realign: lag %1, unchanged").arg(now)});
        }
        m_realignButton->setEnabled(true);
        m_resultTimer->start();
    }
    Q_UNUSED(aligned);
    m_haveLastLag = haveLag;
    m_lastLagSamples = lag;

    // --- capture -----------------------------------------------------------
    // capture.active is the gate's own live state, so it wins over whatever
    // the /diversity/capture trigger last said -- except while this window's
    // own button is mid-countdown or holding its answer, which is the one
    // thing the gate cannot know about.
    if (!d.contains(QStringLiteral("capture")) || m_captureBusy)
        return;
    const QJsonObject capture = d.value(QStringLiteral("capture")).toObject();
    const bool active = capture.value(QStringLiteral("active")).toBool();
    m_captureButton->setEnabled(!active);
    if (active) {
        m_captureResult->setText(tr("capture: recording…"));
        m_captureResult->setToolTip(QString());
        m_captureLocalResult = false;
        return;
    }
    if (m_captureLocalResult)
        return;
    const QString path = capture.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return;
    const QString base = QFileInfo(path).fileName();
    m_captureResult->setText(tr("capture: %1").arg(base));
    m_captureResult->setToolTip(path);
    if (base != m_lastCaptureAnnounced) {
        m_lastCaptureAnnounced = base;
        addEventLines({DiversityEventLog::captureSavedLine(base)});
    }
}

// --------------------------------------------------------------------------
// The footer status strip
// --------------------------------------------------------------------------
//
// The strip is the only widget in this window visible from every page, which
// is precisely why REALIGN and CAPTURE borrow it. So it has two layers: a BASE
// line, which is the presence sentence and is always what is true, and a
// TRANSIENT one that covers it for a few seconds. A base written while a
// transient is up is remembered and painted when the transient expires --
// never dropped, because the base is the one that is true.

void DiversityWindow::setStatusStripBase(const QString& text, bool live)
{
    m_statusBase = text;
    m_statusBaseLive = live;
    if (m_statusTransient && m_statusTransient->isActive())
        return;
    m_statusStrip->setText(text);
    DiversityWidgets::setLive(m_statusStrip, live);
}

void DiversityWindow::setStatusStripTransient(const QString& text, int ms)
{
    if (!m_statusTransient) {
        m_statusTransient = new QTimer(this);
        m_statusTransient->setSingleShot(true);
        connect(m_statusTransient, &QTimer::timeout, this, [this] {
            m_statusStrip->setText(m_statusBase);
            DiversityWidgets::setLive(m_statusStrip, m_statusBaseLive);
        });
    }
    m_statusStrip->setText(text);
    DiversityWidgets::setLive(m_statusStrip, true);
    m_statusTransient->start(ms);
}

} // namespace AetherSDR
