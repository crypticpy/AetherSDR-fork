#include "gui/DiversityWindow.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/ClientCompKnob.h"
#include "gui/DiversityFilterControls.h"
#include "gui/DiversityFlowStrip.h"
#include "gui/DiversityMapStrip.h"
#include "gui/DiversityScope.h"
#include "gui/DiversityTimeline.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <cmath>

namespace AetherSDR {

namespace {

// v2's frame. The minimum is what the three-column bottom row needs before
// anything inside it starts eliding; the initial size is chosen so that
// NOTHING scrolls when the window first opens -- the operator should not have
// to discover a scrollbar to find the noise panel.
constexpr int kMinWidth = 980;
constexpr int kMinHeight = 720;
constexpr int kInitialWidth = 1120;
constexpr int kInitialHeight = 860;

// Below this the two loops are seeing different noise: there is no single
// source to cancel, and the second antenna can only add gain.
constexpr double kCoherentNoiseThreshold = 0.3;
// Below this the combined passband is tilted enough that one weight cannot
// null the whole channel at once.
constexpr double kPassbandFlatEnough = 0.7;

const char* kStatusStripStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; "
    "background: transparent; }"
    "QLabel[live=\"false\"] { color: {{color.text.disabled}}; }";

const char* kWindowStyle =
    "QWidget { background: {{color.background.0}}; color: {{color.text.primary}}; }"
    "QFrame#stripGroupBox { border: 1px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QScrollArea { background: transparent; border: none; }";

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull())
        return false;
    *out = v.toDouble();
    return true;
}

// "guard" / "inband" are the two values the gate documents; anything else is
// a newer or older gate's own wording and is shown verbatim rather than
// swallowed. Spelled out in words, not the wire token: "guard" alone means
// nothing to somebody meeting a diversity combiner for the first time.
QString noiseReferenceWord(const QJsonValue& rn)
{
    if (rn.isNull() || rn.isUndefined())
        return QStringLiteral("—");
    const QString wire = rn.toString();
    if (wire == QLatin1String("guard"))
        return QCoreApplication::translate("DiversityWindow", "guard band");
    if (wire == QLatin1String("inband"))
        return QCoreApplication::translate("DiversityWindow", "in-band");
    return wire;
}

} // namespace

DiversityWindow::DiversityWindow(QWidget* parent)
    : PersistentDialog(tr("Diversity"), QStringLiteral("DiversityWindowGeometry"), parent,
                       /*toolWindow=*/true)
{
    setObjectName(QStringLiteral("diversityWindow"));
    setAccessibleName(tr("Diversity window"));
    // Closing this must never be able to end the application, however the
    // platform decides to count top-level windows.
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(kMinWidth, kMinHeight);
    resize(kInitialWidth, kInitialHeight);
    ThemeManager::instance().applyStyleSheet(this, QString::fromLatin1(kWindowStyle));

    // The debounce timers are built before the panels because the knob
    // handlers those panels install start them.
    m_phaseDebounce = new QTimer(this);
    m_phaseDebounce->setSingleShot(true);
    connect(m_phaseDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("phase"),
                       QString::number(int(std::lround(m_phaseKnob->value()))));
        emit requestSet(q);
    });
    m_ratioDebounce = new QTimer(this);
    m_ratioDebounce->setSingleShot(true);
    connect(m_ratioDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("ratio"), QString::number(m_ratioKnob->value(), 'f', 1));
        emit requestSet(q);
    });
    m_nbDebounce = new QTimer(this);
    m_nbDebounce->setSingleShot(true);
    connect(m_nbDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("nb_db"), QString::number(m_nbKnob->value(), 'f', 1));
        emit requestSet(q);
    });

    auto* root = new QVBoxLayout(bodyWidget());
    root->setContentsMargins(8, 4, 8, 8);
    root->setSpacing(8);
    // Two sticky strips, in the order the operator reads them: where you are,
    // and what the pair is doing on every page. Two rows rather than one
    // because v2's single row read as one sentence and left "do the controls
    // change with the tab?" unanswerable -- see DiversityWindowChain.cpp.
    // Tighter spacing between the two than the 8 px the rest of the window
    // uses: they are one block of "about the window", and a gap as wide as the
    // one under them would read as two unrelated strips.
    //
    // The FLOW line used to be a third row here and is not any more: five lit
    // pills under four lit tabs read as a second tab bar, which is exactly the
    // confusion this block was built to end. It is at the foot of the window
    // now, above the status strip -- see the footer below.
    auto* strips = new QVBoxLayout;
    strips->setContentsMargins(0, 0, 0, 0);
    strips->setSpacing(4);
    strips->addWidget(buildTabRow());
    strips->addWidget(buildChainRow());
    root->addLayout(strips);

    // Everything below the sticky rows scrolls, so the window can be
    // dragged smaller than its natural content height without any control
    // becoming unreachable -- the channel strip's own arrangement. At the
    // initial size nothing scrolls.
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    m_scope = new DiversityScope(this);
    m_scope->setObjectName(QStringLiteral("diversityWindowScope"));
    m_scope->setLarge(true);
    m_timeline = new DiversityTimeline(this);

    grid->addWidget(m_scope, 0, 0, 1, 2);
    grid->addWidget(buildTalkersPanel(), 0, 2);
    grid->addWidget(m_timeline, 1, 0, 1, 3);
    grid->addWidget(buildAntennasPanel(), 2, 0);
    grid->addWidget(buildNoisePanel(), 2, 1);
    grid->addWidget(buildEventsPanel(), 2, 2);

    grid->setColumnStretch(0, 4);
    grid->setColumnStretch(1, 4);
    grid->setColumnStretch(2, 5);
    // The scope row and the bottom row both absorb surplus height -- the
    // scope gets a bigger dial, the events list gets more lines. The timeline
    // is a fixed 120px strip and never stretches.
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 0);
    grid->setRowStretch(2, 1);

    auto* gridHost = new QWidget;
    gridHost->setLayout(grid);
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("diversityWindowSliceScroll"));
    scroll->setWidget(gridHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Both bars are as-needed rather than off: at the initial size neither
    // appears (the grid's minimum is 1063x758 against a 1102x772 viewport),
    // but the window can be dragged down to 980x720, and a control that has
    // been squeezed off the right-hand edge with no way to scroll to it is
    // worse than a scrollbar.
    // Four pages, three shared rows. The BAND page is built here rather than
    // lazily so its widgets exist for the very first poll -- a page that
    // built itself on first show would miss the payload that arrived while
    // it did.
    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("diversityWindowPages"));
    m_pages->addWidget(scroll);
    m_pages->addWidget(buildBandPage());
    m_pages->addWidget(buildSitePage());
    m_pages->addWidget(buildFilterPage());
    root->addWidget(m_pages, 1);

    m_statusStrip = new QLabel(tr("gate not answering"), this);
    m_statusStrip->setObjectName(QStringLiteral("diversityWindowStatusLabel"));
    m_statusStrip->setAccessibleName(tr("Gate status"));
    m_statusStrip->setToolTip(
        tr("Whether the Aether-gate bridge is answering, and whether it is "
           "reporting a working two-tuner diversity setup. Everything above "
           "goes to dashes when this says the gate is not answering -- a dead "
           "poll must never leave last minute's numbers on screen looking "
           "live."));
    ThemeManager::instance().applyStyleSheet(m_statusStrip,
                                             QString::fromLatin1(kStatusStripStyle));
    setStatusStripBase(tr("gate not answering"), false);

    // The footer: the two lines that are true on every page. FLOW says what to
    // do next, the status strip says whether the gate is answering at all, and
    // they sit at the bottom in that order because nothing down here can be
    // mistaken for the tabs. 4 px between them, the same gap the two sticky
    // rows at the top share, because they are one block for the same reason.
    auto* footer = new QVBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(4);
    m_flow = new DiversityFlowStrip(this);
    connect(m_flow, &DiversityFlowStrip::stepActivated, this,
            &DiversityWindow::onFlowStep);
    // The line is about the page in front of the operator, so it follows the
    // stack itself rather than the tab buttons: a page switch made from a FLOW
    // click or a SITE row's button reaches it the same way a tab does.
    connect(m_pages, &QStackedWidget::currentChanged,
            m_flow, &DiversityFlowStrip::setCurrentPage);
    // The dig's three buttons sit at the right-hand end of the FLOW row rather
    // than on a row of their own: a second row of lit boxes under the tabs is
    // exactly what this strip exists to have stopped being. They belong to the
    // window because they write -- see DiversityWindowFilter.cpp.
    auto* flowRow = new QHBoxLayout;
    flowRow->setContentsMargins(0, 0, 0, 0);
    flowRow->setSpacing(6);
    flowRow->addWidget(m_flow, 1);
    flowRow->addWidget(buildDigControls());
    footer->addLayout(flowRow);
    footer->addWidget(m_statusStrip);
    root->addLayout(footer);

    // The dig status poll. Its cadence is the window's business rather than
    // the poller's, because the window is the only thing that knows whether a
    // run is still going -- see updateDigPoll(). bandPageChanged() is emitted
    // on every show, hide and page switch, which is exactly the set of moments
    // the answer can change.
    m_digTimer = new QTimer(this);
    m_digTimer->setObjectName(QStringLiteral("diversityWindowDigTimer"));
    connect(m_digTimer, &QTimer::timeout, this,
            [this] { emit requestDig(QUrlQuery()); });
    connect(this, &DiversityWindow::bandPageChanged, this,
            [this] { updateDigPoll(); });
}

DiversityWindow* DiversityWindow::createFor(AetherGateDiversityPanel* panel)
{
    auto* window = new DiversityWindow(panel);
    // Signal-to-signal: a write made here is indistinguishable, from
    // AetherGateApplet's side, from one made in the sidebar.
    connect(window, &DiversityWindow::requestSet,
            panel, &AetherGateDiversityPanel::requestSet);
    connect(window, &DiversityWindow::requestCompareRestore,
            panel, &AetherGateDiversityPanel::requestCompareRestore);
    connect(window, &DiversityWindow::requestAlign,
            panel, &AetherGateDiversityPanel::requestAlign);
    connect(window, &DiversityWindow::requestCapture,
            panel, &AetherGateDiversityPanel::requestCapture);
    connect(window, &DiversityWindow::requestMemoryClear,
            panel, &AetherGateDiversityPanel::requestMemoryClear);
    connect(window, &DiversityWindow::requestMemoryName,
            panel, &AetherGateDiversityPanel::requestMemoryName);
    connect(window, &DiversityWindow::requestTune,
            panel, &AetherGateDiversityPanel::requestTune);
    connect(window, &DiversityWindow::requestFilter,
            panel, &AetherGateDiversityPanel::requestFilter);
    connect(window, &DiversityWindow::requestSite,
            panel, &AetherGateDiversityPanel::requestSite);
    connect(window, &DiversityWindow::requestDig,
            panel, &AetherGateDiversityPanel::requestDig);
    return window;
}

void DiversityWindow::endCompareHold()
{
    // Never leave the gate parked in "off" because the window went away or
    // the gate dropped mid-hold: an unconditional resume, every time.
    if (!m_compareDown)
        return;
    m_compareDown = false;
    if (!m_compareResumeMode.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), m_compareResumeMode);
        emit requestCompareRestore(q);
    }
    m_compareResumeMode.clear();
}

void DiversityWindow::closeEvent(QCloseEvent* event)
{
    endCompareHold();
    endBeaconCheck();
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
    PersistentDialog::closeEvent(event);
}

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------

QButtonGroup* DiversityWindow::addButtonRow(QWidget* row, const QString& caption,
                                            const QString& key, const QString& objectPrefix,
                                            const QStringList& labels,
                                            const QStringList& values,
                                            const QStringList& tips)
{
    auto* layout = qobject_cast<QHBoxLayout*>(row->layout());
    layout->addWidget(DiversityWidgets::makeCaption(caption, row));
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(labels[i], row);
        button->setObjectName(objectPrefix + values[i]);
        button->setAccessibleName(tr("%1 %2").arg(caption, labels[i]));
        if (i < tips.size()) {
            button->setToolTip(tips[i]);
            button->setAccessibleDescription(tips[i]);
        }
        button->setCheckable(true);
        button->setFixedHeight(26);
        button->setProperty("diversityValue", values[i]);
        applyToggleButtonStyle(button);
        group->addButton(button);
        layout->addWidget(button);
        // clicked(), not toggled(): applyDiversity() checks a button back from
        // the poll and must not turn that read-back into another write.
        connect(button, &QPushButton::clicked, this, [this, key, value = values[i]] {
            QUrlQuery q;
            q.addQueryItem(key, value);
            emit requestSet(q);
        });
    }
    return group;
}

void DiversityWindow::checkValue(QButtonGroup* group, const QString& value)
{
    for (QAbstractButton* button : group->buttons()) {
        if (button->property("diversityValue").toString() == value) {
            const QSignalBlocker block(button);
            button->setChecked(true);
            return;
        }
    }
}

// --------------------------------------------------------------------------
// Incoming state
// --------------------------------------------------------------------------

bool DiversityWindow::knobBusy(const ClientCompKnob* knob, const QTimer* debounce)
{
    if (debounce->isActive())
        return true;
    const QWidget* focus = QApplication::focusWidget();
    return focus && (focus == knob || knob->isAncestorOf(focus));
}

void DiversityWindow::applyDiversity(const QJsonObject& d, bool isJson)
{
    const bool available = isJson && d.value(QStringLiteral("available")).toBool();
    if (!available) {
        clearReadouts();
        setStatusStripBase(m_present ? tr("gate connected · diversity unavailable")
                                     : tr("gate not answering"),
                           m_present);
        if (m_flow)
            m_flow->applyDiversity(d, false);
        DiversitySnapshot snapshot;
        snapshot.present = m_present;
        addEventLines(m_eventLog.apply(snapshot));
        return;
    }
    setStatusStripBase(tr("gate connected · diversity live"), true);
    if (m_flow)
        m_flow->applyDiversity(d, true);

    const QString mode = d.value(QStringLiteral("mode")).toString();
    const QString hear = d.value(QStringLiteral("source")).toString();
    checkValue(m_modeGroup, mode);
    checkValue(m_hearGroup, hear);
    if (d.contains(QStringLiteral("pan")))
        checkValue(m_panGroup, d.value(QStringLiteral("pan")).toString());

    const bool manual = (mode == QLatin1String("manual"));
    m_phaseKnob->setEnabled(manual);
    m_ratioKnob->setEnabled(manual);
    m_manualCaption->setEnabled(manual);
    // While the hold has forced mode=off, leave the button alone: it is not
    // "off" by the operator's choice, and disabling a pressed button can
    // swallow the released() that is supposed to end the hold.
    if (!m_compareDown)
        m_compareButton->setEnabled(mode != QLatin1String("off"));

    // Only manual takes a setpoint, and even there a poll may move the knob
    // only when the operator is not holding it and no write is pending.
    if (manual && !knobBusy(m_phaseKnob, m_phaseDebounce)) {
        double phase = 0.0;
        if (jsonNumber(d, "phase_deg", &phase))
            m_phaseKnob->setValue(float(phase));
    }
    if (manual && !knobBusy(m_ratioKnob, m_ratioDebounce)) {
        double ratio = 0.0;
        if (jsonNumber(d, "ratio_db", &ratio))
            m_ratioKnob->setValue(float(ratio));
    }

    // A null leg is "no estimate", not 0 dB -- clearReading(), not setSnrDb(0).
    const QJsonObject snr = d.value(QStringLiteral("snr_db")).toObject();
    double snrA = 0.0;
    double snrB = 0.0;
    double snrOut = 0.0;
    const bool haveA = jsonNumber(snr, "a", &snrA);
    const bool haveB = jsonNumber(snr, "b", &snrB);
    const bool haveOut = jsonNumber(snr, "out", &snrOut);
    haveA ? m_meterA->setSnrDb(snrA, true) : m_meterA->clearReading();
    haveB ? m_meterB->setSnrDb(snrB, true) : m_meterB->clearReading();
    haveOut ? m_meterOut->setSnrDb(snrOut, true) : m_meterOut->clearReading();

    // Same isObject() guard the sidebar keeps: a malformed "nb" must not read
    // as "blanker off, threshold 0" through toObject()'s silent {}.
    if (d.value(QStringLiteral("nb")).isObject()) {
        const QJsonObject nb = d.value(QStringLiteral("nb")).toObject();
        const QSignalBlocker block(m_nbButton);
        m_nbButton->setChecked(nb.value(QStringLiteral("enabled")).toBool());
        if (!knobBusy(m_nbKnob, m_nbDebounce))
            m_nbKnob->setValue(float(nb.value(QStringLiteral("threshold_db")).toDouble()));
    }

    // The SITE page's noise profile and the SLICE page's per-bin checkbox both
    // ride on this one status object -- see DiversityWindowSite.cpp.
    applySite(d);

    if (d.contains(QStringLiteral("sources"))) {
        DiversityWidgets::applySources(m_sourcesList,
                                       d.value(QStringLiteral("sources")).toArray());
        m_nullSourceButton->setEnabled(m_sourcesList->currentRow() >= 0);
    }

    // --- who is talking ---------------------------------------------------
    const QJsonArray memory = d.value(QStringLiteral("memory")).toArray();
    const QJsonValue talkerValue = d.value(QStringLiteral("talker"));
    bool haveTalker = false;
    int talkerId = 0;
    double talkerSinceS = 0.0;
    if (talkerValue.isObject()) {
        const QJsonObject talker = talkerValue.toObject();
        double id = 0.0;
        if (jsonNumber(talker, "id", &id)) {
            haveTalker = true;
            talkerId = int(std::lround(id));
            jsonNumber(talker, "since_s", &talkerSinceS);
        }
    }
    if (d.contains(QStringLiteral("memory")) || haveTalker)
        applyTalkers(memory, haveTalker, talkerId, talkerSinceS);
    if (m_flow)
        m_flow->setTalkerNames(memory);
    QString talkerName;
    for (const QJsonValue& v : memory) {
        const QJsonObject entry = v.toObject();
        double id = 0.0;
        if (haveTalker && jsonNumber(entry, "id", &id) && int(std::lround(id)) == talkerId
                && entry.value(QStringLiteral("name")).isString())
            talkerName = entry.value(QStringLiteral("name")).toString();
    }
    applyFocus(d.value(QStringLiteral("focus")), haveTalker, talkerId, talkerName);

    // --- FILTER page: the pair's own two stages ----------------------------
    // Fed whether or not FILTER is the page on screen, the same reasoning the
    // talker names above are: it costs a hash and it means the page already
    // has the right answer the moment its tab is clicked.
    if (m_filter) {
        m_filter->applyPost(d.value(QStringLiteral("post")).toObject());
        m_filter->applyMrc(d.value(QStringLiteral("mrc")).toObject());
    }

    // --- noise ------------------------------------------------------------
    double coherence = 0.0;
    const bool haveCoherence = jsonNumber(d, "noise_coherence", &coherence);
    m_noiseStatus->setText(
        tr("noise reference: %1 · coherence %2")
            .arg(noiseReferenceWord(d.value(QStringLiteral("rn_source"))),
                 haveCoherence ? QString::number(coherence, 'f', 2)
                               : QStringLiteral("—")));

    // --- balance ----------------------------------------------------------
    m_balanceDelta->setText(
        (haveA && haveB)
            ? tr("A - B: %1 dB").arg(QString::asprintf("%+.1f", snrA - snrB))
            : tr("A - B: —"));
    m_balanceCoherence->setText(
        haveCoherence ? tr("noise coherence %1").arg(QString::number(coherence, 'f', 2))
                      : tr("noise coherence —"));

    const QJsonValue passbandValue = d.value(QStringLiteral("passband"));
    const bool havePassband = passbandValue.isObject();
    double flatness = 0.0;
    double slope = 0.0;
    if (havePassband) {
        const QJsonObject pb = passbandValue.toObject();
        jsonNumber(pb, "flatness", &flatness);
        jsonNumber(pb, "phase_slope_deg_per_khz", &slope);
    }
    m_balancePassband->setText(
        havePassband
            ? tr("passband flat %1 · slope %2°/kHz")
                  .arg(QString::number(flatness, 'f', 2), QString::asprintf("%+.1f", slope))
            : tr("passband —"));

    // The verdict is the point of the block: three numbers the operator has
    // to interpret become one sentence saying what can be done about them.
    QString verdict;
    if (!haveCoherence) {
        verdict = tr("noise character unknown");
    } else if (coherence < kCoherentNoiseThreshold) {
        verdict = tr("isotropic noise: gain only");
    } else {
        verdict = tr("coherent noise: null available");
    }
    if (havePassband && flatness < kPassbandFlatEnough)
        verdict += QStringLiteral(" · ") + tr("passband sloped");
    m_balanceVerdict->setText(verdict);

    // --- alignment --------------------------------------------------------
    const bool aligned = d.value(QStringLiteral("aligned")).toBool();
    const bool realigning = d.value(QStringLiteral("realigning")).toBool();
    double lag = 0.0;
    const bool haveLag = jsonNumber(d, "lag_samples", &lag);
    double peak = 0.0;
    const bool havePeak = jsonNumber(d, "corr_peak", &peak);
    m_alignLine->setText(
        tr("%1 · lag %2 · peak %3 · %4")
            .arg(aligned ? tr("aligned") : tr("not aligned"),
                 haveLag ? QString::number(qint64(std::llround(lag))) : QStringLiteral("—"),
                 havePeak ? QString::number(peak, 'f', 3) : QStringLiteral("—"),
                 realigning ? tr("realigning…") : tr("steady")));
    DiversityWidgets::setLive(m_alignLine, realigning);

    // REALIGN's answer and the gate's own capture state, both of which belong
    // to the pair row -- see DiversityWindowChain.cpp.
    applyChainStatus(d, aligned, realigning, haveLag, lag);

    m_scope->setState(d);

    // --- timeline ---------------------------------------------------------
    DiversityTimeline::Sample sample;
    sample.haveA = haveA;
    sample.a = snrA;
    sample.haveB = haveB;
    sample.b = snrB;
    sample.haveOut = haveOut;
    sample.out = snrOut;
    sample.haveTalker = haveTalker;
    sample.talkerId = talkerId;
    sample.steadyQrm = d.value(QStringLiteral("steady_qrm")).toBool();
    m_timeline->addSample(QDateTime::currentMSecsSinceEpoch(), sample);

    // --- events -----------------------------------------------------------
    DiversitySnapshot snapshot;
    snapshot.present = m_present;
    snapshot.available = true;
    snapshot.haveTalker = haveTalker;
    snapshot.talkerId = talkerId;
    snapshot.talkerSinceS = talkerSinceS;
    for (const QJsonValue& v : memory) {
        const QJsonObject entry = v.toObject();
        double id = 0.0;
        if (!jsonNumber(entry, "id", &id))
            continue;
        const int idValue = int(std::lround(id));
        snapshot.memoryIds << idValue;
        if (!haveTalker || idValue != talkerId)
            continue;
        const QJsonValue nameValue = entry.value(QStringLiteral("name"));
        if (nameValue.isString())
            snapshot.talkerName = nameValue.toString();
        double phase = 0.0;
        double ratio = 0.0;
        if (jsonNumber(entry, "phase_deg", &phase) && jsonNumber(entry, "ratio_db", &ratio)) {
            snapshot.haveTalkerWeight = true;
            snapshot.talkerPhaseDeg = phase;
            snapshot.talkerRatioDb = ratio;
        }
    }
    snapshot.haveFocus = m_haveFocus;
    snapshot.focusId = m_focusId;
    if (m_haveFocus) {
        const QJsonObject f = d.value(QStringLiteral("focus")).toObject();
        if (f.value(QStringLiteral("name")).isString())
            snapshot.focusName = f.value(QStringLiteral("name")).toString();
        snapshot.focusNulling = f.value(QStringLiteral("nulling")).toBool();
    }
    const QJsonValue splits = d.value(QStringLiteral("voice_splits"));
    snapshot.haveVoiceSplits = splits.isDouble();
    snapshot.voiceSplits =
        snapshot.haveVoiceSplits ? int(std::lround(splits.toDouble())) : 0;
    const QJsonValue qrm = d.value(QStringLiteral("steady_qrm"));
    snapshot.haveSteadyQrm = qrm.isBool();
    snapshot.steadyQrm = snapshot.haveSteadyQrm && qrm.toBool();
    snapshot.mode = mode;
    snapshot.hear = hear;
    snapshot.aligned = aligned;
    snapshot.realigning = realigning;
    snapshot.haveLag = haveLag;
    snapshot.lagSamples = lag;
    addEventLines(m_eventLog.apply(snapshot));
}

void DiversityWindow::applyMap(const QJsonObject& map)
{
    m_mapStrip->setMap(map);
}

void DiversityWindow::setPresent(bool present)
{
    m_present = present;
    // A dig cannot be asked about across a dead gate, and must not be left
    // being asked about either.
    updateDigPoll();
    if (present)
        return;
    m_phaseDebounce->stop();
    m_ratioDebounce->stop();
    m_nbDebounce->stop();
    endCompareHold();
    clearReadouts();
    setStatusStripBase(tr("gate not answering"), false);
    DiversitySnapshot snapshot;
    addEventLines(m_eventLog.apply(snapshot));
}

void DiversityWindow::clearReadouts()
{
    if (m_flow)
        m_flow->clear();
    clearBandReadouts();
    clearSiteReadouts();
    clearFilterReadouts();
    m_scope->clear();
    m_timeline->clear();
    m_mapStrip->setMap({});
    m_meterA->clearReading();
    m_meterB->clearReading();
    m_meterOut->clearReading();
    m_sourcesList->clear();
    m_nullSourceButton->setEnabled(false);
    m_talkerRows.clear();
    m_talkerLiveRow = -1;
    m_talkersRebuilding = true;
    m_talkers->setRowCount(0);
    m_talkersRebuilding = false;
    m_talkersCount->setText(tr("%1 talkers remembered · nobody talking").arg(0));
    m_haveFocus = false;
    m_focusId = -1;
    m_focusLine->hide();
    updateLockButton();
    m_noiseStatus->setText(tr("noise reference: %1 · coherence %2")
                               .arg(QStringLiteral("—"), QStringLiteral("—")));
    m_balanceDelta->setText(tr("A - B: —"));
    m_balanceCoherence->setText(tr("noise coherence —"));
    m_balancePassband->setText(tr("passband —"));
    m_balanceVerdict->setText(QStringLiteral("—"));
    m_alignLine->setText(tr("%1 · lag %2 · peak %3 · %4")
                             .arg(QStringLiteral("—"), QStringLiteral("—"),
                                  QStringLiteral("—"), QStringLiteral("—")));
    DiversityWidgets::setLive(m_alignLine, false);
    m_captureResult->setText(tr("capture: —"));
    m_captureResult->setToolTip(QString());
    resetCapture();
    m_captureLocalResult = false;
    m_lastCaptureAnnounced.clear();
    m_compareButton->setEnabled(false);
}

} // namespace AetherSDR
