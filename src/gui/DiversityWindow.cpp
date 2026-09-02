#include "gui/DiversityWindow.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/ClientCompKnob.h"
#include "gui/DiversityMapStrip.h"
#include "gui/DiversityScope.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFileInfo>
#include <QAbstractButton>
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
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <cmath>

namespace AetherSDR {

namespace {

// Same ~150ms coalescing the sidebar panel uses: a knob drag fires
// valueChanged many times a second and each one becomes a real HTTP round
// trip once the applet turns it into a request.
constexpr int kDebounceMs = 150;

constexpr int kMinWidth = 900;
constexpr int kMinHeight = 620;
constexpr int kInitialWidth = 1040;
constexpr int kInitialHeight = 700;

// The map strip is the noise panel's main readout here rather than the
// sidebar's 24px glance strip.
constexpr int kMapStripHeight = 64;

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

    auto* root = new QVBoxLayout(bodyWidget());
    root->setContentsMargins(8, 4, 8, 8);
    root->setSpacing(8);
    root->addWidget(buildChainRow());

    // Everything below the (sticky) chain row scrolls, so the window can be
    // dragged smaller than its natural content height without any control
    // becoming unreachable -- the channel strip's own arrangement.
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    // Two rows, three columns. The scope takes two thirds of the top row with
    // the STATIONS table beside it (the table is the one other thing that
    // benefits from height); ANTENNAS, NOISE and ALIGNMENT & CAPTURE share
    // the second row at content height. At the initial size nothing scrolls.
    m_scope = new DiversityScope(this);
    m_scope->setObjectName(QStringLiteral("diversityWindowScope"));
    m_scope->setLarge(true);
    grid->addWidget(m_scope, 0, 0, 1, 2);
    grid->addWidget(buildStationsPanel(), 0, 2);
    grid->addWidget(buildAntennasPanel(), 1, 0);
    grid->addWidget(buildNoisePanel(), 1, 1);
    grid->addWidget(buildAlignmentPanel(), 1, 2);

    grid->setColumnStretch(0, 4);
    grid->setColumnStretch(1, 4);
    grid->setColumnStretch(2, 3);
    // Only the top row absorbs surplus height, so a tall window does not open
    // dead space inside the lower panels' borders.
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 0);

    auto* gridHost = new QWidget;
    gridHost->setLayout(grid);
    auto* scroll = new QScrollArea;
    scroll->setWidget(gridHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scroll, 1);

    m_statusStrip = new QLabel(tr("gate not answering"), this);
    m_statusStrip->setObjectName(QStringLiteral("diversityWindowStatusLabel"));
    m_statusStrip->setAccessibleName(tr("Gate status"));
    ThemeManager::instance().applyStyleSheet(m_statusStrip,
                                             QString::fromLatin1(kStatusStripStyle));
    DiversityWidgets::setLive(m_statusStrip, false);
    root->addWidget(m_statusStrip);

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
    return window;
}

void DiversityWindow::closeEvent(QCloseEvent* event)
{
    // Never leave the gate parked in "off" because the window went away
    // mid-hold -- the same unconditional resume the sidebar panel does.
    if (m_compareDown) {
        m_compareDown = false;
        if (!m_compareResumeMode.isEmpty()) {
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("mode"), m_compareResumeMode);
            emit requestCompareRestore(q);
        }
        m_compareResumeMode.clear();
    }
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
                                            const QStringList& values)
{
    auto* layout = qobject_cast<QHBoxLayout*>(row->layout());
    layout->addWidget(DiversityWidgets::makeCaption(caption, row));
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(labels[i], row);
        button->setObjectName(objectPrefix + values[i]);
        button->setAccessibleName(tr("%1 %2").arg(caption, labels[i]));
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

QWidget* DiversityWindow::buildChainRow()
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("diversityWindowChainRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_modeGroup = addButtonRow(row, tr("MODE"), QStringLiteral("mode"),
                               QStringLiteral("diversityWindowMode"),
                               {tr("OFF"), tr("MANUAL"), tr("NULL"), tr("TRACK")},
                               {QStringLiteral("off"), QStringLiteral("manual"),
                                QStringLiteral("null"), QStringLiteral("track")});
    layout->addSpacing(10);
    // "HEAR", not "LISTEN": it writes /diversity/set?source=, which is what
    // reaches the operator's ears. Which leg the PANADAPTER draws is a
    // different key (pan=) and lives with the noise tools, exactly where the
    // sidebar panel puts it.
    m_hearGroup = addButtonRow(row, tr("HEAR"), QStringLiteral("source"),
                               QStringLiteral("diversityWindowHear"),
                               {tr("COMBINED"), tr("A"), tr("B")},
                               {QStringLiteral("combined"), QStringLiteral("a"),
                                QStringLiteral("b")});

    m_compareButton = new QPushButton(tr("Hear A only"), row);
    m_compareButton->setObjectName(QStringLiteral("diversityWindowCompareButton"));
    m_compareButton->setAccessibleName(tr("Hear antenna A only while pressed"));
    m_compareButton->setFixedHeight(26);
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
    m_realignButton->setFixedHeight(26);
    connect(m_realignButton, &QPushButton::clicked, this, &DiversityWindow::requestAlign);
    layout->addWidget(m_realignButton);

    layout->addStretch(1);

    m_captureSpin = new QSpinBox(row);
    m_captureSpin->setObjectName(QStringLiteral("diversityWindowCaptureSpin"));
    m_captureSpin->setAccessibleName(tr("Diversity capture duration"));
    m_captureSpin->setRange(1, 60);
    m_captureSpin->setValue(10);
    m_captureSpin->setSuffix(QStringLiteral(" s"));
    layout->addWidget(m_captureSpin);

    m_captureButton = new QPushButton(tr("CAPTURE"), row);
    m_captureButton->setObjectName(QStringLiteral("diversityWindowCaptureButton"));
    m_captureButton->setAccessibleName(tr("Capture raw diversity audio"));
    m_captureButton->setFixedHeight(26);
    connect(m_captureButton, &QPushButton::clicked, this, [this] {
        if (!m_present)
            return;
        m_captureButton->setEnabled(false);
        m_captureResult->setText(tr("recording…"));
        m_captureResult->setToolTip(QString());
        emit requestCapture(m_captureSpin->value());
    });
    layout->addWidget(m_captureButton);
    return row;
}

QWidget* DiversityWindow::buildAntennasPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("ANTENNAS"), QStringLiteral("diversityWindowAntennas"), body, this);

    auto* meters = new QHBoxLayout;
    meters->setSpacing(10);
    m_meterA = new DiversitySnrMeter(tr("A"), frame);
    m_meterA->setObjectName(QStringLiteral("diversityWindowMeterA"));
    m_meterB = new DiversitySnrMeter(tr("B"), frame);
    m_meterB->setObjectName(QStringLiteral("diversityWindowMeterB"));
    m_meterOut = new DiversitySnrMeter(tr("OUT"), frame);
    m_meterOut->setObjectName(QStringLiteral("diversityWindowMeterOut"));
    meters->addWidget(m_meterA);
    meters->addWidget(m_meterB);
    meters->addWidget(m_meterOut);
    meters->addSpacing(12);

    // Phase and ratio are a MANUAL-mode setpoint: Null and Track solve for
    // their own weight and Off applies none, so outside manual these are
    // disabled and never written by a poll. The caption greys with them --
    // a knob that merely stops responding, with no visible reason, is the
    // kind of dead control Principle XI is about.
    auto* manual = new QVBoxLayout;
    manual->setSpacing(4);
    m_manualCaption = DiversityWidgets::makeCaption(tr("MANUAL WEIGHT"), frame);
    m_manualCaption->setObjectName(QStringLiteral("diversityWindowManualCaption"));
    manual->addWidget(m_manualCaption);

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(8);
    m_phaseKnob = new ClientCompKnob(frame);
    m_phaseKnob->setObjectName(QStringLiteral("diversityWindowPhaseKnob"));
    m_phaseKnob->setAccessibleName(tr("Manual phase"));
    m_phaseKnob->setFixedSize(84, 96);
    m_phaseKnob->setLabel(tr("PHASE"));
    m_phaseKnob->setRange(0.0f, 360.0f);
    m_phaseKnob->setDefault(0.0f);
    m_phaseKnob->setLabelFormat([](float v) { return QString::asprintf("%.0f°", double(v)); });
    connect(m_phaseKnob, &ClientCompKnob::valueChanged, this,
            [this](float) { m_phaseDebounce->start(kDebounceMs); });
    knobs->addWidget(m_phaseKnob);

    m_ratioKnob = new ClientCompKnob(frame);
    m_ratioKnob->setObjectName(QStringLiteral("diversityWindowRatioKnob"));
    m_ratioKnob->setAccessibleName(tr("Manual ratio"));
    m_ratioKnob->setFixedSize(84, 96);
    m_ratioKnob->setLabel(tr("RATIO"));
    m_ratioKnob->setRange(-20.0f, 20.0f);
    m_ratioKnob->setDefault(0.0f);
    m_ratioKnob->setLabelFormat([](float v) { return QString::asprintf("%+.1f dB", double(v)); });
    connect(m_ratioKnob, &ClientCompKnob::valueChanged, this,
            [this](float) { m_ratioDebounce->start(kDebounceMs); });
    knobs->addWidget(m_ratioKnob);
    knobs->addStretch(1);
    manual->addLayout(knobs);
    manual->addStretch(1);
    meters->addLayout(manual, 1);
    body->addLayout(meters);
    return frame;
}

QWidget* DiversityWindow::buildNoisePanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("NOISE"), QStringLiteral("diversityWindowNoise"), body, this);

    auto* topRow = new QWidget(frame);
    auto* top = new QHBoxLayout(topRow);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    top->addWidget(DiversityWidgets::makeCaption(tr("BLANKER"), topRow));
    m_nbButton = new QPushButton(tr("NB"), topRow);
    m_nbButton->setObjectName(QStringLiteral("diversityWindowNbButton"));
    m_nbButton->setAccessibleName(tr("Noise blanker"));
    m_nbButton->setCheckable(true);
    m_nbButton->setFixedHeight(26);
    applyToggleButtonStyle(m_nbButton, ToggleTribe::Warning);
    connect(m_nbButton, &QPushButton::clicked, this, [this](bool on) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("nb"), on ? QStringLiteral("on") : QStringLiteral("off"));
        emit requestSet(q);
    });
    top->addWidget(m_nbButton);

    m_nbKnob = new ClientCompKnob(topRow);
    m_nbKnob->setObjectName(QStringLiteral("diversityWindowNbKnob"));
    m_nbKnob->setAccessibleName(tr("Noise blanker threshold"));
    m_nbKnob->setFixedSize(84, 88);
    m_nbKnob->setLabel(tr("THRESH"));
    m_nbKnob->setRange(0.0f, 40.0f);
    m_nbKnob->setDefault(0.0f);
    m_nbKnob->setLabelFormat([](float v) { return QString::asprintf("%.1f dB", double(v)); });
    connect(m_nbKnob, &ClientCompKnob::valueChanged, this,
            [this](float) { m_nbDebounce->start(kDebounceMs); });
    top->addWidget(m_nbKnob);
    top->addStretch(1);
    body->addWidget(topRow);

    // Which leg the panadapter draws, on its own row: with the blanker knob
    // beside it the two would not fit the column at the window's minimum.
    auto* panRow = new QWidget(frame);
    auto* pan = new QHBoxLayout(panRow);
    pan->setContentsMargins(0, 0, 0, 0);
    pan->setSpacing(6);
    m_panGroup = addButtonRow(panRow, tr("PAN"), QStringLiteral("pan"),
                              QStringLiteral("diversityWindowPan"),
                              {tr("A"), tr("B"), tr("COMBINED"), tr("NULLED")},
                              {QStringLiteral("a"), QStringLiteral("b"),
                               QStringLiteral("combined"), QStringLiteral("nulled")});
    pan->addStretch(1);
    body->addWidget(panRow);

    m_mapStrip = new DiversityMapStrip(frame);
    m_mapStrip->setObjectName(QStringLiteral("diversityWindowMapStrip"));
    m_mapStrip->setStripHeight(kMapStripHeight);
    body->addWidget(m_mapStrip);

    m_sourcesList = new QListWidget(frame);
    m_sourcesList->setObjectName(QStringLiteral("diversityWindowSourcesList"));
    m_sourcesList->setAccessibleName(tr("Diversity sources"));
    m_sourcesList->setFixedHeight(4 * (fontMetrics().height() + 6));
    m_sourcesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sourcesList->setTextElideMode(Qt::ElideRight);
    body->addWidget(m_sourcesList);

    auto* bottomRow = new QWidget(frame);
    auto* bottom = new QHBoxLayout(bottomRow);
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(6);
    m_nullSourceButton = new QPushButton(tr("Null selected"), bottomRow);
    m_nullSourceButton->setObjectName(QStringLiteral("diversityWindowNullSourceButton"));
    m_nullSourceButton->setAccessibleName(tr("Null the selected noise source"));
    m_nullSourceButton->setEnabled(false);
    connect(m_sourcesList, &QListWidget::currentRowChanged, this,
            [this](int row) { m_nullSourceButton->setEnabled(row >= 0); });
    connect(m_nullSourceButton, &QPushButton::clicked, this, [this] {
        // The index sent is the SELECTED ITEM's current position, never a row
        // cached earlier: the list is rebuilt from every poll and "sources" can
        // shrink or reorder between the click and this handler running.
        QListWidgetItem* item = m_sourcesList->currentItem();
        if (!item)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("null_source"),
                       QString::number(m_sourcesList->row(item)));
        emit requestSet(q);
    });
    bottom->addWidget(m_nullSourceButton);
    m_noiseStatus =
        DiversityWidgets::makeFieldLabel(tr("noise ref: — · coherence —"), bottomRow);
    m_noiseStatus->setObjectName(QStringLiteral("diversityWindowNoiseStatusLabel"));
    m_noiseStatus->setAccessibleName(tr("Noise reference"));
    m_noiseStatus->setMinimumWidth(m_noiseStatus->fontMetrics().horizontalAdvance(
                                       tr("noise ref: in-band · coherence 0.00"))
                                   + 8);
    bottom->addWidget(m_noiseStatus);
    bottom->addStretch(1);
    body->addWidget(bottomRow);
    return frame;
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
        m_statusStrip->setText(m_present ? tr("gate connected · diversity unavailable")
                                         : tr("gate not answering"));
        DiversityWidgets::setLive(m_statusStrip, m_present);
        return;
    }
    m_statusStrip->setText(tr("gate connected · diversity live"));
    DiversityWidgets::setLive(m_statusStrip, true);

    const QString mode = d.value(QStringLiteral("mode")).toString();
    checkValue(m_modeGroup, mode);
    checkValue(m_hearGroup, d.value(QStringLiteral("source")).toString());
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
    const auto applyLeg = [&snr](DiversitySnrMeter* meter, const char* key) {
        double db = 0.0;
        if (jsonNumber(snr, key, &db))
            meter->setSnrDb(db, true);
        else
            meter->clearReading();
    };
    applyLeg(m_meterA, "a");
    applyLeg(m_meterB, "b");
    applyLeg(m_meterOut, "out");

    // Same isObject() guard the sidebar keeps: a malformed "nb" must not read
    // as "blanker off, threshold 0" through toObject()'s silent {}.
    if (d.value(QStringLiteral("nb")).isObject()) {
        const QJsonObject nb = d.value(QStringLiteral("nb")).toObject();
        const QSignalBlocker block(m_nbButton);
        m_nbButton->setChecked(nb.value(QStringLiteral("enabled")).toBool());
        if (!knobBusy(m_nbKnob, m_nbDebounce))
            m_nbKnob->setValue(float(nb.value(QStringLiteral("threshold_db")).toDouble()));
    }

    if (d.contains(QStringLiteral("sources"))) {
        DiversityWidgets::applySources(m_sourcesList,
                                       d.value(QStringLiteral("sources")).toArray());
        m_nullSourceButton->setEnabled(m_sourcesList->currentRow() >= 0);
    }
    if (d.contains(QStringLiteral("memory")))
        applyMemory(d.value(QStringLiteral("memory")).toArray());

    const QJsonValue rn = d.value(QStringLiteral("rn_source"));
    const QString rnWord = (rn.isNull() || rn.isUndefined())
        ? QStringLiteral("—")
        : (rn.toString() == QLatin1String("inband") ? tr("in-band") : rn.toString());
    double coh = 0.0;
    m_noiseStatus->setText(tr("noise ref: %1 · coherence %2")
                               .arg(rnWord,
                                    jsonNumber(d, "noise_coherence", &coh)
                                        ? QString::number(coh, 'f', 2)
                                        : QStringLiteral("—")));

    const bool aligned = d.value(QStringLiteral("aligned")).toBool();
    m_alignedValue->setText(aligned ? tr("aligned") : tr("not aligned"));
    double lag = 0.0;
    m_lagValue->setText(jsonNumber(d, "lag_samples", &lag)
                            ? QString::number(qint64(std::llround(lag)))
                            : QStringLiteral("—"));
    double peak = 0.0;
    m_peakValue->setText(jsonNumber(d, "corr_peak", &peak) ? QString::number(peak, 'f', 3)
                                                           : QStringLiteral("—"));
    const bool realigning = d.value(QStringLiteral("realigning")).toBool();
    m_realigningValue->setText(realigning ? tr("realigning…") : tr("steady"));
    DiversityWidgets::setLive(m_realigningValue, realigning);

    // capture.active is the gate's own live state, so it wins over whatever
    // the /diversity/capture trigger last said.
    if (d.contains(QStringLiteral("capture"))) {
        const QJsonObject capture = d.value(QStringLiteral("capture")).toObject();
        const bool active = capture.value(QStringLiteral("active")).toBool();
        m_captureButton->setEnabled(!active);
        if (active) {
            m_captureResult->setText(tr("recording…"));
            m_captureResult->setToolTip(QString());
            m_captureLocalResult = false;
        } else if (!m_captureLocalResult) {
            const QString path = capture.value(QStringLiteral("path")).toString();
            if (!path.isEmpty()) {
                m_captureResult->setText(QFileInfo(path).fileName());
                m_captureResult->setToolTip(path);
            }
        }
    }

    m_scope->setState(d);
}

void DiversityWindow::applyMemory(const QJsonArray& memory)
{
    QStringList rows;
    rows.reserve(memory.size());
    for (const QJsonValue& v : memory) {
        const QJsonObject entry = v.toObject();
        double phase = 0.0;
        double ratio = 0.0;
        double age = 0.0;
        rows << QStringLiteral("%1|%2|%3|%4")
                    .arg(jsonNumber(entry, "phase_deg", &phase)
                             ? QString::asprintf("%.0f°", phase)
                             : QStringLiteral("—"),
                         jsonNumber(entry, "ratio_db", &ratio)
                             ? QString::asprintf("%+.1f dB", ratio)
                             : QStringLiteral("—"),
                         QString::number(entry.value(QStringLiteral("hits")).toInt()),
                         jsonNumber(entry, "age_s", &age)
                             ? QString::asprintf("%.0f s", age)
                             : QStringLiteral("—"));
    }
    m_stationsCount->setText(tr("%1 stations remembered").arg(memory.size()));
    if (rows == m_stationRows)
        return;
    m_stationRows = rows;
    m_stations->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList cells = rows[r].split(QLatin1Char('|'));
        for (int c = 0; c < cells.size() && c < 4; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_stations->setItem(r, c, item);
        }
    }
}

void DiversityWindow::applyMap(const QJsonObject& map)
{
    m_mapStrip->setMap(map);
}

void DiversityWindow::applyCaptureResult(bool ok, const QString& pathOrError)
{
    m_captureButton->setEnabled(true);
    if (!ok) {
        // An error this request reported must survive the very next poll:
        // capture.active is already false by then and its "path" is still the
        // last SUCCESSFUL capture's.
        m_captureResult->setText(pathOrError);
        m_captureResult->setToolTip(QString());
        m_captureLocalResult = true;
        return;
    }
    m_captureResult->setText(pathOrError.isEmpty() ? QStringLiteral("—")
                                                   : QFileInfo(pathOrError).fileName());
    m_captureResult->setToolTip(pathOrError);
    m_captureLocalResult = false;
}

void DiversityWindow::setPresent(bool present)
{
    m_present = present;
    if (present)
        return;
    m_phaseDebounce->stop();
    m_ratioDebounce->stop();
    m_nbDebounce->stop();
    clearReadouts();
    m_statusStrip->setText(tr("gate not answering"));
    DiversityWidgets::setLive(m_statusStrip, false);
}

void DiversityWindow::clearReadouts()
{
    m_scope->clear();
    m_mapStrip->setMap({});
    m_meterA->clearReading();
    m_meterB->clearReading();
    m_meterOut->clearReading();
    m_sourcesList->clear();
    m_nullSourceButton->setEnabled(false);
    m_stationRows.clear();
    m_stations->setRowCount(0);
    m_stationsCount->setText(tr("%1 stations remembered").arg(0));
    m_noiseStatus->setText(tr("noise ref: %1 · coherence %2")
                               .arg(QStringLiteral("—"), QStringLiteral("—")));
    m_alignedValue->setText(QStringLiteral("—"));
    m_lagValue->setText(QStringLiteral("—"));
    m_peakValue->setText(QStringLiteral("—"));
    m_realigningValue->setText(QStringLiteral("—"));
    DiversityWidgets::setLive(m_realigningValue, false);
    m_captureResult->setText(QStringLiteral("—"));
    m_captureResult->setToolTip(QString());
    m_captureButton->setEnabled(true);
    m_captureLocalResult = false;
    m_compareButton->setEnabled(false);
}

} // namespace AetherSDR
