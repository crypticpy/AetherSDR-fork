#include "AetherGateDiversityPanel.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/AetherGateDiversityFormat.h"
#include "gui/DiversityMapStrip.h"
#include "gui/DiversityScope.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

// Debounce for the phase slider / ratio spinbox / noise-blanker threshold: a
// slider drag fires valueChanged many times a second, and each write is a
// real HTTP round trip once the applet turns it into a request — coalesce to
// one request per ~150ms of quiet.
static constexpr int kDiversityDebounceMs = 150;

// Row labels resolve their colour from a theme token instead of a literal —
// same {{token}}-through-applyStyleSheet() shape AetherGateApplet.cpp's own
// copy uses for its resolution/device rows (docs/style/theme-style-guide.md;
// this keeps the file off the hardcoded-colour ratchet in static-checks.yml).
static const char* kRowLabelStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; }";

// The four collapsible section headers -- same accent.bright/11px/bold
// RadioSetupDialog::addSectionHeader already uses for its own section
// captions, adapted to a QToolButton selector so the arrow/checked state
// still reads as a caption rather than an ordinary button.
static const char* kDiversityHeaderStyle =
    "QToolButton { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "padding: 4px 0 1px 0; border: none; text-align: left; }";

namespace {

void styleRowLabel(QLabel* label)
{
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kRowLabelStyle));
}

} // namespace

AetherGateDiversityPanel::AetherGateDiversityPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gateDiversityBox"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 6, 0, 0);
    root->setSpacing(4);

    // === Combine ============================================================
    auto* combineContent = new QWidget(this);
    auto* combineForm = new QFormLayout(combineContent);
    combineForm->setContentsMargins(0, 0, 0, 0);
    combineForm->setSpacing(4);
    // The sidebar's ~250px inner content width left no slack for a label
    // column sized off whatever the field happens to want, or for a row that
    // wraps instead of clipping.
    combineForm->setLabelAlignment(Qt::AlignLeft);
    combineForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    combineForm->setRowWrapPolicy(QFormLayout::DontWrapRows);

    m_mode = new QComboBox(combineContent);
    m_mode->setObjectName(QStringLiteral("gateDiversityModeCombo"));
    m_mode->addItem(tr("Off"), QStringLiteral("off"));
    m_mode->addItem(tr("Manual"), QStringLiteral("manual"));
    m_mode->addItem(tr("Null"), QStringLiteral("null"));
    m_mode->addItem(tr("Track"), QStringLiteral("track"));
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), m_mode->itemData(idx).toString());
        emit requestSet(q);
    });
    auto* modeLabel = new QLabel(tr("Mode"), combineContent);
    styleRowLabel(modeLabel);
    combineForm->addRow(modeLabel, m_mode);

    auto* phaseRow = new QWidget(combineContent);
    auto* phaseRowLayout = new QHBoxLayout(phaseRow);
    phaseRowLayout->setContentsMargins(0, 0, 0, 0);
    m_phase = new QSlider(Qt::Horizontal, phaseRow);
    m_phase->setObjectName(QStringLiteral("gateDiversityPhaseSlider"));
    m_phase->setRange(0, 360);
    m_phaseValue = new QLabel(QStringLiteral("0°"), phaseRow);
    m_phaseValue->setObjectName(QStringLiteral("gateDiversityPhaseValueLabel"));
    connect(m_phase, &QSlider::valueChanged, this, [this](int v) {
        m_phaseValue->setText(QStringLiteral("%1°").arg(v));
        m_phaseDebounce->start(kDiversityDebounceMs);
    });
    phaseRowLayout->addWidget(m_phase, 1);
    phaseRowLayout->addWidget(m_phaseValue);
    auto* phaseLabel = new QLabel(tr("Phase"), combineContent);
    styleRowLabel(phaseLabel);
    combineForm->addRow(phaseLabel, phaseRow);

    m_ratio = new QDoubleSpinBox(combineContent);
    m_ratio->setObjectName(QStringLiteral("gateDiversityRatioSpin"));
    m_ratio->setRange(-20.0, 20.0);
    m_ratio->setSingleStep(0.5);
    m_ratio->setDecimals(1);
    m_ratio->setSuffix(QStringLiteral(" dB"));
    connect(m_ratio, &QDoubleSpinBox::valueChanged, this, [this](double) {
        m_ratioDebounce->start(kDiversityDebounceMs);
    });
    auto* ratioLabel = new QLabel(tr("Ratio"), combineContent);
    styleRowLabel(ratioLabel);
    combineForm->addRow(ratioLabel, m_ratio);

    // Read-only weight/SNR/status visualisation, directly under mode/phase/
    // ratio -- see DiversityScope's own header comment. Full-width (the
    // single-argument addRow -- QFormLayout's SpanningRole overload).
    m_scope = new DiversityScope(combineContent);
    combineForm->addRow(m_scope);

    addCollapsibleSection(root, tr("Combine"), QStringLiteral("Combine"), QStringLiteral("Combine"),
                           /*defaultExpanded=*/true, combineContent);

    // === Listen ===============================================================
    auto* listenContent = new QWidget(this);
    auto* listenForm = new QFormLayout(listenContent);
    listenForm->setContentsMargins(0, 0, 0, 0);
    listenForm->setSpacing(4);
    listenForm->setLabelAlignment(Qt::AlignLeft);
    listenForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    listenForm->setRowWrapPolicy(QFormLayout::DontWrapRows);

    m_source = new QComboBox(listenContent);
    m_source->setObjectName(QStringLiteral("gateDiversitySourceCombo"));
    m_source->addItem(tr("Combined"), QStringLiteral("combined"));
    m_source->addItem(tr("A"), QStringLiteral("a"));
    m_source->addItem(tr("B"), QStringLiteral("b"));
    connect(m_source, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("source"), m_source->itemData(idx).toString());
        emit requestSet(q);
    });
    auto* sourceLabel = new QLabel(tr("Hear"), listenContent);
    styleRowLabel(sourceLabel);
    listenForm->addRow(sourceLabel, m_source);

    m_realign = new QPushButton(tr("Realign"), listenContent);
    m_realign->setObjectName(QStringLiteral("gateDiversityRealignButton"));
    connect(m_realign, &QPushButton::clicked, this, &AetherGateDiversityPanel::requestAlign);

    // --- "Hear A only" compare hold ---------------------------------------
    // A press-and-hold, not a toggle: the operator wants a quick A/B ear
    // check, not a mode they might forget they left engaged. pressed() and
    // released() bracket exactly one off/resume pair; the eventFilter below
    // and setPresent(false) cover every other way the hold can end (focus
    // loss, the widget being hidden, the gate itself going away) — see
    // restoreCompareHold()'s header comment.
    m_compareButton = new QPushButton(tr("Hear A only"), listenContent);
    m_compareButton->setObjectName(QStringLiteral("gateDiversityCompareButton"));
    m_compareButton->setAccessibleName(tr("Hear antenna A only while pressed"));
    m_compareButton->setAutoRepeat(false);
    m_compareButton->setEnabled(false);   // starts in "off" until a poll says otherwise
    connect(m_compareButton, &QPushButton::pressed, this, [this] {
        if (!m_present)
            return;
        const int idx = m_mode->currentIndex();
        if (idx < 0)
            return;
        m_compareResumeMode = m_mode->itemData(idx).toString();
        m_compareDown = true;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), QStringLiteral("off"));
        emit requestSet(q);
    });
    connect(m_compareButton, &QPushButton::released, this,
            &AetherGateDiversityPanel::restoreCompareHold);
    m_compareButton->installEventFilter(this);

    auto* realignRow = new QWidget(listenContent);
    auto* realignRowLayout = new QHBoxLayout(realignRow);
    realignRowLayout->setContentsMargins(0, 0, 0, 0);
    realignRowLayout->addWidget(m_realign, 1);
    realignRowLayout->addWidget(m_compareButton, 1);
    listenForm->addRow(realignRow);

    m_statusLine = new QLabel(QStringLiteral("—"), listenContent);
    m_statusLine->setObjectName(QStringLiteral("gateDiversityStatusLabel"));
    // Fixed width, not word-wrap -- DiversityFormat::status() returns one of
    // three short, fixed phrases rather than a line that grows with live
    // numbers, but even that small a set of widths still shifted the box
    // next to it by a few px between them -- a minimum width sized to the
    // longest of the three absorbs that instead of the layout doing it.
    styleRowLabel(m_statusLine);
    {
        const QFontMetrics fm = m_statusLine->fontMetrics();
        const int widest = std::max({fm.horizontalAdvance(tr("realigning…")),
                                      fm.horizontalAdvance(tr("not aligned")),
                                      fm.horizontalAdvance(DiversityFormat::statusWorstCasePhrase())});
        m_statusLine->setMinimumWidth(widest + 4);
    }
    listenForm->addRow(m_statusLine);

    addCollapsibleSection(root, tr("Listen"), QStringLiteral("Listen"), QStringLiteral("Listen"),
                           /*defaultExpanded=*/true, listenContent);

    // === Noise ================================================================
    auto* noiseContent = new QWidget(this);
    auto* noiseForm = new QFormLayout(noiseContent);
    noiseForm->setContentsMargins(0, 0, 0, 0);
    noiseForm->setSpacing(4);
    noiseForm->setLabelAlignment(Qt::AlignLeft);
    noiseForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    noiseForm->setRowWrapPolicy(QFormLayout::DontWrapRows);

    // "Blanker" lives in the row label -- the checkbox itself carries no
    // text (only setAccessibleName below).
    auto* nbRow = new QWidget(noiseContent);
    auto* nbRowLayout = new QHBoxLayout(nbRow);
    nbRowLayout->setContentsMargins(0, 0, 0, 0);
    m_nbCheck = new QCheckBox(QString(), nbRow);
    m_nbCheck->setObjectName(QStringLiteral("gateDiversityNbCheck"));
    m_nbCheck->setAccessibleName(tr("Noise blanker"));
    connect(m_nbCheck, &QCheckBox::toggled, this, [this](bool on) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("nb"), on ? QStringLiteral("on") : QStringLiteral("off"));
        emit requestSet(q);
    });
    m_nbSpin = new QDoubleSpinBox(nbRow);
    m_nbSpin->setObjectName(QStringLiteral("gateDiversityNbSpin"));
    m_nbSpin->setRange(0.0, 40.0);
    m_nbSpin->setDecimals(1);
    m_nbSpin->setSuffix(QStringLiteral(" dB"));
    m_nbSpin->setAccessibleName(tr("Noise blanker threshold"));
    connect(m_nbSpin, &QDoubleSpinBox::valueChanged, this, [this](double) {
        m_nbDebounce->start(kDiversityDebounceMs);
    });
    nbRowLayout->addWidget(m_nbCheck);
    nbRowLayout->addWidget(m_nbSpin, 1);
    auto* blankerLabel = new QLabel(tr("Blanker"), noiseContent);
    styleRowLabel(blankerLabel);
    noiseForm->addRow(blankerLabel, nbRow);

    // --- which leg the panadapter itself displays ---------------------------
    m_panCombo = new QComboBox(noiseContent);
    m_panCombo->setObjectName(QStringLiteral("gateDiversityPanCombo"));
    m_panCombo->addItem(tr("A"), QStringLiteral("a"));
    m_panCombo->addItem(tr("B"), QStringLiteral("b"));
    m_panCombo->addItem(tr("Combined"), QStringLiteral("combined"));
    m_panCombo->addItem(tr("Nulled"), QStringLiteral("nulled"));
    connect(m_panCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("pan"), m_panCombo->itemData(idx).toString());
        emit requestSet(q);
    });
    auto* panLabel = new QLabel(tr("Pan"), noiseContent);
    styleRowLabel(panLabel);
    noiseForm->addRow(panLabel, m_panCombo);

    // --- the noise map -------------------------------------------------------
    // Full-width: DiversityMapStrip paints its bars against its OWN width(),
    // so confining it to the field column would compress every bar into a
    // narrower strip than the coherence data actually spans.
    m_mapStrip = new DiversityMapStrip(noiseContent);
    m_mapStrip->setObjectName(QStringLiteral("gateDiversityMapStrip"));
    noiseForm->addRow(m_mapStrip);

    // --- sources + null-selected ---------------------------------------------
    m_sourcesList = new QListWidget(noiseContent);
    m_sourcesList->setObjectName(QStringLiteral("gateDiversitySourcesList"));
    m_sourcesList->setAccessibleName(tr("Diversity sources"));
    m_sourcesList->setFixedHeight(4 * (fontMetrics().height() + 6));
    m_sourcesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sourcesList->setTextElideMode(Qt::ElideRight);
    noiseForm->addRow(m_sourcesList);

    m_nullSourceButton = new QPushButton(tr("Null selected"), noiseContent);
    m_nullSourceButton->setObjectName(QStringLiteral("gateDiversityNullSourceButton"));
    m_nullSourceButton->setEnabled(false);
    connect(m_sourcesList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_nullSourceButton->setEnabled(row >= 0);
    });
    connect(m_nullSourceButton, &QPushButton::clicked, this, [this] {
        // The index sent is the SELECTED ITEM's current position, not a row
        // number cached earlier — applyDiversity() rebuilds this list on
        // every poll and restores the selection by matching the item's own
        // (lo_hz, hi_hz) key rather than its old row, so an array that
        // shrank or reordered between the operator's click and this handler
        // running never sends an index that now names a different source.
        QListWidgetItem* item = m_sourcesList->currentItem();
        if (!item)
            return;
        const int row = m_sourcesList->row(item);
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("null_source"), QString::number(row));
        emit requestSet(q);
    });
    noiseForm->addRow(m_nullSourceButton);

    m_noiseHeader = addCollapsibleSection(root, tr("Noise"), QStringLiteral("Noise"),
                                           QStringLiteral("Noise"), /*defaultExpanded=*/false,
                                           noiseContent);

    // === Memory & capture ======================================================
    auto* memoryContent = new QWidget(this);
    auto* memoryForm = new QFormLayout(memoryContent);
    memoryForm->setContentsMargins(0, 0, 0, 0);
    memoryForm->setSpacing(4);
    memoryForm->setLabelAlignment(Qt::AlignLeft);
    memoryForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    memoryForm->setRowWrapPolicy(QFormLayout::DontWrapRows);

    auto* memoryRow = new QWidget(memoryContent);
    auto* memoryRowLayout = new QHBoxLayout(memoryRow);
    memoryRowLayout->setContentsMargins(0, 0, 0, 0);
    m_memoryLabel = new QLabel(tr("memory: 0 talkers"), memoryRow);
    m_memoryLabel->setObjectName(QStringLiteral("gateDiversityMemoryLabel"));
    styleRowLabel(m_memoryLabel);
    m_memoryClearButton = new QPushButton(tr("Clear"), memoryRow);
    m_memoryClearButton->setObjectName(QStringLiteral("gateDiversityMemoryClearButton"));
    m_memoryClearButton->setAccessibleName(tr("Clear diversity memory"));
    connect(m_memoryClearButton, &QPushButton::clicked, this,
            &AetherGateDiversityPanel::requestMemoryClear);
    memoryRowLayout->addWidget(m_memoryLabel, 1);
    memoryRowLayout->addWidget(m_memoryClearButton);
    memoryForm->addRow(QString(), memoryRow);

    auto* captureRow = new QWidget(memoryContent);
    auto* captureRowLayout = new QHBoxLayout(captureRow);
    captureRowLayout->setContentsMargins(0, 0, 0, 0);
    m_captureSpin = new QSpinBox(captureRow);
    m_captureSpin->setObjectName(QStringLiteral("gateDiversityCaptureSpin"));
    m_captureSpin->setRange(1, 60);
    m_captureSpin->setValue(10);
    m_captureSpin->setSuffix(QStringLiteral(" s"));
    m_captureSpin->setAccessibleName(tr("Diversity capture duration"));
    m_captureButton = new QPushButton(tr("Capture"), captureRow);
    m_captureButton->setObjectName(QStringLiteral("gateDiversityCaptureButton"));
    connect(m_captureButton, &QPushButton::clicked, this, [this] {
        if (!m_present)
            return;
        m_captureButton->setEnabled(false);
        m_captureLabel->setText(tr("recording…"));
        m_captureLabel->setToolTip(QString());
        emit requestCapture(m_captureSpin->value());
    });
    captureRowLayout->addWidget(m_captureSpin);
    captureRowLayout->addWidget(m_captureButton, 1);
    auto* captureLabel = new QLabel(tr("Capture"), memoryContent);
    styleRowLabel(captureLabel);
    memoryForm->addRow(captureLabel, captureRow);

    m_captureLabel = new QLabel(QStringLiteral("—"), memoryContent);
    m_captureLabel->setObjectName(QStringLiteral("gateDiversityCaptureLabel"));
    m_captureLabel->setWordWrap(true);
    styleRowLabel(m_captureLabel);
    memoryForm->addRow(QString(), m_captureLabel);

    addCollapsibleSection(root, tr("Memory & capture"), QStringLiteral("MemoryCapture"),
                           QStringLiteral("MemoryCapture"), /*defaultExpanded=*/false,
                           memoryContent);

    m_phaseDebounce = new QTimer(this);
    m_phaseDebounce->setSingleShot(true);
    connect(m_phaseDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("phase"), QString::number(m_phase->value()));
        emit requestSet(q);
    });

    m_ratioDebounce = new QTimer(this);
    m_ratioDebounce->setSingleShot(true);
    connect(m_ratioDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("ratio"), QString::number(m_ratio->value(), 'f', 1));
        emit requestSet(q);
    });

    m_nbDebounce = new QTimer(this);
    m_nbDebounce->setSingleShot(true);
    connect(m_nbDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("nb_db"), QString::number(m_nbSpin->value(), 'f', 1));
        emit requestSet(q);
    });

    setVisible(false);   // hidden until a poll reports "available": true
}

QToolButton* AetherGateDiversityPanel::addCollapsibleSection(QVBoxLayout* root,
                                                               const QString& caption,
                                                               const QString& objectNameSuffix,
                                                               const QString& settingsKey,
                                                               bool defaultExpanded,
                                                               QWidget* content)
{
    auto* header = new QToolButton(this);
    header->setObjectName(QStringLiteral("gateDiversity%1Header").arg(objectNameSuffix));
    header->setText(caption);
    header->setAccessibleName(caption);
    header->setCheckable(true);
    header->setAutoRaise(true);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ThemeManager::instance().applyStyleSheet(header, QString::fromLatin1(kDiversityHeaderStyle));

    // Persisted the way WaveApplet persists its own settings-drawer open
    // state ("WaveApplet_DrawerExpanded", src/gui/WaveApplet.cpp) — a plain
    // AppSettings string key, "True"/"False", read back with the caller's
    // default when the key has never been written.
    const QString key = QStringLiteral("AetherGateDiversityPanel_%1Expanded").arg(settingsKey);
    const bool expanded = AppSettings::instance()
        .value(key, defaultExpanded ? QStringLiteral("True") : QStringLiteral("False"))
        .toString()
        == QStringLiteral("True");
    header->setChecked(expanded);
    header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    content->setVisible(expanded);

    // Collapsing/expanding only ever changes this section's own height —
    // nothing else in the panel moves, and nothing here runs off a poll, so
    // "nothing shifts on a poll; user toggles may" holds.
    connect(header, &QToolButton::toggled, this, [header, content, key](bool checked) {
        header->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        content->setVisible(checked);
        AppSettings::instance().setValue(key,
                                          checked ? QStringLiteral("True")
                                                  : QStringLiteral("False"));
    });

    root->addWidget(header);
    root->addWidget(content);
    return header;
}

bool AetherGateDiversityPanel::wantsMapPoll() const
{
    return !isHidden() && m_noiseHeader && m_noiseHeader->isChecked();
}

bool AetherGateDiversityPanel::eventFilter(QObject* obj, QEvent* event)
{
    // The compare button losing focus or being hidden out from under a held
    // press are exactly as much "the hold ended" as its own released() — an
    // operator who alt-tabs away, or a radio drop that hides this whole
    // panel, must not leave the gate parked in "off" until they happen to
    // click the button again. restoreCompareHold() is a no-op when nothing
    // is pressed, so this never fires an extra request.
    if (obj == m_compareButton
        && (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide)) {
        restoreCompareHold();
    }
    return QWidget::eventFilter(obj, event);
}

void AetherGateDiversityPanel::setPresent(bool present)
{
    m_present = present;
    if (present)
        return;

    setVisible(false);
    m_phaseDebounce->stop();
    m_ratioDebounce->stop();
    m_nbDebounce->stop();
    m_mapStrip->setMap({});
    m_scope->clear();
    // Covers the way the hold can end OTHER than a radio-address change
    // (AetherGateApplet::setRadioAddress() calls restoreCompareHold()
    // itself, before this): repeated poll failures with the address
    // unchanged.
    restoreCompareHold();
    // Every v2 widget that shows a PREVIOUS gate's data rather than just a
    // setpoint the operator can freely re-enter: without this, a reconnect
    // at the same address to an older (v1) gate would leave the old gate's
    // source rows on screen with an armed Null button that now names
    // nothing this gate reported.
    m_sourcesList->clear();
    m_nullSourceButton->setEnabled(false);
    m_memoryLabel->setText(tr("memory: 0 talkers"));
    setCaptureResultLabel(QString());
    m_captureButton->setEnabled(true);
    m_captureLocalResult = false;
    m_lastMode.clear();
}

void AetherGateDiversityPanel::restoreCompareHold()
{
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
}

void AetherGateDiversityPanel::applyDiversity(const QJsonObject& d, bool isJson)
{
    if (!isJson) {
        setVisible(false);
        return;
    }

    const bool available = d.value(QStringLiteral("available")).toBool();
    setVisible(available);
    if (!available) {
        m_scope->clear();
        return;
    }

    const QString mode = d.value(QStringLiteral("mode")).toString();
    // A mode change (operator-driven or gate-driven) invalidates whatever the
    // capture label was showing about the PREVIOUS mode's local result.
    if (mode != m_lastMode) {
        m_lastMode = mode;
        m_captureLocalResult = false;
    }
    // Written from a poll only when the combo is neither focused nor has its
    // popup open — an operator with the dropdown open deciding between Null
    // and Track must not have it snap to whatever the gate currently reports
    // out from under their cursor.
    if (!m_mode->hasFocus() && !m_mode->view()->isVisible()) {
        const QSignalBlocker block(m_mode);
        const int idx = m_mode->findData(mode);
        if (idx >= 0)
            m_mode->setCurrentIndex(idx);
    }
    // Only Manual takes a phase/ratio setpoint — Null and Track solve for
    // their own weight, and Off applies none.
    const bool manual = (mode == QLatin1String("manual"));
    m_phase->setEnabled(manual);
    m_ratio->setEnabled(manual);
    // While the compare hold has forced mode=off, leave the button itself
    // alone: it is not "off" by the operator's choice, and disabling a
    // pressed button can swallow the released() that is supposed to end the
    // hold.
    if (!m_compareDown)
        m_compareButton->setEnabled(mode != QLatin1String("off"));

    const QString source = d.value(QStringLiteral("source")).toString();
    {
        const QSignalBlocker block(m_source);
        const int idx = m_source->findData(source);
        if (idx >= 0)
            m_source->setCurrentIndex(idx);
    }

    // In null/track mode, phase/ratio are disabled above and are NOT written
    // here at all — they keep the operator's last manual value rather than
    // silently tracking whatever Null/Track's own solve produced. Manual is
    // the only mode where a poll may move them, and even there only when:
    // not focused/mid-debounce, AND the polled value actually differs from
    // what is already showing.
    if (manual && !m_phase->hasFocus() && !m_phaseDebounce->isActive()) {
        const int phase = int(std::lround(d.value(QStringLiteral("phase_deg")).toDouble()));
        if (phase != m_phase->value()) {
            const QSignalBlocker block(m_phase);
            m_phase->setValue(phase);
            m_phaseValue->setText(QStringLiteral("%1°").arg(phase));
        }
    }
    if (manual && !m_ratio->hasFocus() && !m_ratioDebounce->isActive()) {
        const double ratio = d.value(QStringLiteral("ratio_db")).toDouble();
        if (std::abs(ratio - m_ratio->value()) > 0.05) {
            const QSignalBlocker block(m_ratio);
            m_ratio->setValue(ratio);
        }
    }

    // Everything below is v2-only and independently optional on the wire —
    // an older gate carries none of these keys, and each widget stays at its
    // constructor default when its own key is missing.
    // Guarded by isObject(), not just contains(): QJsonValue::toObject() on a
    // malformed "nb" (e.g. a stray boolean) silently returns {}, which would
    // read as "blanker off, threshold 0" and stomp both widgets instead of
    // leaving them alone the way every other malformed/absent v2 field does.
    if (d.contains(QStringLiteral("nb")) && d.value(QStringLiteral("nb")).isObject()) {
        const QJsonObject nb = d.value(QStringLiteral("nb")).toObject();
        {
            const QSignalBlocker block(m_nbCheck);
            m_nbCheck->setChecked(nb.value(QStringLiteral("enabled")).toBool());
        }
        if (!m_nbSpin->hasFocus() && !m_nbDebounce->isActive()) {
            const QSignalBlocker block(m_nbSpin);
            m_nbSpin->setValue(nb.value(QStringLiteral("threshold_db")).toDouble());
        }
    }

    if (d.contains(QStringLiteral("pan"))) {
        const QSignalBlocker block(m_panCombo);
        const int idx = m_panCombo->findData(d.value(QStringLiteral("pan")).toString());
        if (idx >= 0)
            m_panCombo->setCurrentIndex(idx);
    }

    if (d.contains(QStringLiteral("sources")))
        applySources(d.value(QStringLiteral("sources")).toArray());

    if (d.contains(QStringLiteral("memory"))) {
        const int talkers = d.value(QStringLiteral("memory")).toArray().size();
        m_memoryLabel->setText(tr("memory: %1 talkers").arg(talkers));
    }

    // capture.active is the gate's own live state, so it wins over whatever
    // the /diversity/capture trigger last said — the button/label must never
    // show "idle" while a capture the operator started is still recording.
    if (d.contains(QStringLiteral("capture"))) {
        const QJsonObject capture = d.value(QStringLiteral("capture")).toObject();
        const bool active = capture.value(QStringLiteral("active")).toBool();
        m_captureButton->setEnabled(!active);
        if (active) {
            m_captureLabel->setText(tr("recording…"));
            m_captureLabel->setToolTip(QString());
            m_captureLocalResult = false;
        } else if (!m_captureLocalResult) {
            // Skipped while m_captureLocalResult is set: applyCaptureResult()
            // just wrote a LOCAL error the gate's own "path" (the last
            // SUCCESSFUL capture, which may be older) must not clobber
            // within the same poll cycle it landed in.
            const QString path = capture.value(QStringLiteral("path")).toString();
            if (!path.isEmpty())
                setCaptureResultLabel(path);
        }
    }

    m_statusLine->setText(DiversityFormat::status(d));

    // Every poll (and every write's read-back) feeds the scope raw — it does
    // its own defensive field-by-field reading, so an old gate's payload
    // (none of the v2 keys) or a malformed one leaves it painting whatever
    // it already had rather than crashing or inventing zeros.
    m_scope->setState(d);
}

// Rebuilds gateDiversitySourcesList only when the built row strings actually
// differ from what is already there, so scroll position and an untouched
// selection survive a poll that reports back the identical sources array.
//
// When a rebuild IS needed, the previously selected item is re-found in the
// new list by its own (lo_hz, hi_hz), not by its old row: "sources" is
// gate-ordered and can shrink or reorder between polls, and restoring by raw
// row number would silently point the Null button at a DIFFERENT source than
// the one the operator selected.
void AetherGateDiversityPanel::applySources(const QJsonArray& sources)
{
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
    have.reserve(m_sourcesList->count());
    haveTips.reserve(m_sourcesList->count());
    for (int i = 0; i < m_sourcesList->count(); ++i) {
        have << m_sourcesList->item(i)->text();
        haveTips << m_sourcesList->item(i)->toolTip();
    }

    // Compared on text AND tooltip: the short row text alone (freq + coh)
    // can stay identical between two polls while phase/ratio -- visible only
    // in the tooltip now -- moved, and that must still trigger a rebuild.
    if (have != rows || haveTips != tips) {
        const QVariant prevKey = m_sourcesList->currentItem()
            ? m_sourcesList->currentItem()->data(Qt::UserRole)
            : QVariant();
        const QSignalBlocker block(m_sourcesList);
        m_sourcesList->clear();
        for (int i = 0; i < sources.size(); ++i) {
            const QJsonObject so = sources[i].toObject();
            auto* item = new QListWidgetItem(rows[i], m_sourcesList);
            item->setToolTip(tips[i]);
            item->setData(Qt::UserRole,
                          QVariantList{so.value(QStringLiteral("lo_hz")).toDouble(),
                                       so.value(QStringLiteral("hi_hz")).toDouble()});
        }
        if (prevKey.isValid()) {
            for (int i = 0; i < m_sourcesList->count(); ++i) {
                if (m_sourcesList->item(i)->data(Qt::UserRole) == prevKey) {
                    m_sourcesList->setCurrentRow(i);
                    break;
                }
            }
        }
    }
    m_nullSourceButton->setEnabled(m_sourcesList->currentRow() >= 0);
}

void AetherGateDiversityPanel::applyMap(const QJsonObject& map)
{
    m_mapStrip->setMap(map);
}

void AetherGateDiversityPanel::applyCaptureResult(bool ok, const QString& pathOrError)
{
    m_captureButton->setEnabled(true);
    if (!ok) {
        // An error this request itself reported must survive the very next
        // poll: capture.active is already back to false by then, and its
        // "path" is still whatever the LAST successful capture wrote —
        // without the flag, that poll (arriving within one tick of this
        // reply) silently replaces the error with that stale path.
        m_captureLabel->setText(pathOrError);
        m_captureLabel->setToolTip(QString());
        m_captureLocalResult = true;
        return;
    }
    // A successful local result already carries the same information the
    // next poll's capture.path will — nothing left to protect — so hand the
    // label back to the poll-driven path immediately.
    setCaptureResultLabel(pathOrError);
    m_captureLocalResult = false;
}

// Shows a successful capture's file BASENAME in the label (the sidebar's
// 250px width has no room for a full path) with the full path in the
// label's tooltip. An empty path means "no successful capture yet" — the
// constructor's "—" default, restored by setPresent(false).
void AetherGateDiversityPanel::setCaptureResultLabel(const QString& path)
{
    if (path.isEmpty()) {
        m_captureLabel->setText(QStringLiteral("—"));
        m_captureLabel->setToolTip(QString());
        return;
    }
    m_captureLabel->setText(QFileInfo(path).fileName());
    m_captureLabel->setToolTip(path);
}

} // namespace AetherSDR
