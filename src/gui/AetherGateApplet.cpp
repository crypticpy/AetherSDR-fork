#include "AetherGateApplet.h"

#include "core/ThemeManager.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

// The gate's --ctl-port default. Not discoverable over the Flex protocol, which
// has no verb to advertise it, so it is a convention shared with the gate.
static constexpr int kGateControlPort = 8731;

// Give up on a gate after this many consecutive failed polls. One miss is a
// dropped packet; a run of them means we are talking to a real Flex (or the
// gate died) and the applet should get out of the way.
static constexpr int kFailuresBeforeAbsent = 3;

// Cadences. On screen the panel tracks the gate once a second. Off screen with
// a gate found there is nothing to learn, so the timer stops until showEvent().
// Off screen with NO gate found it keeps asking, slowly: a gate still starting
// up, or one that dropped three polls in a Wi-Fi blip, has to be able to come
// back on its own — the GATE button is gone by then, so nothing else could
// ever restart the probe.
static constexpr int kPollMs = 1000;
static constexpr int kReprobeMs = 15000;

// /device is re-read every this many status polls, so a hot-swapped device's
// control set replaces the old one while the gate itself stays present.
static constexpr int kDeviceRefreshPolls = 10;

// Debounce for the diversity phase slider / ratio spinbox: a slider drag fires
// valueChanged many times a second, and each write is a real HTTP round trip
// to the gate — coalesce to one request per ~150ms of quiet.
static constexpr int kDiversityDebounceMs = 150;

// Numeric bounds used only when the gate reports none for a setting. Wide on
// purpose: a guessed range clamps in BOTH directions — a write outside it is
// capped before it reaches the device, and a read-back outside it displays as
// the clamp instead of the value the device holds.
static constexpr int kUnboundedInt = 1000000;
static constexpr double kUnboundedDouble = 1.0e9;

// Row labels resolve their colour from a theme token instead of a literal, so a
// user theme can restyle them and a live theme switch repaints them —
// applyStyleSheet() re-resolves every widget it tracks, which a bare
// setStyleSheet() never did (docs/style/theme-style-guide.md). It also keeps
// this file off the hardcoded-colour ratchet in static-checks.yml.
//
// color.text.secondary (#8ea8c0) rather than color.text.label (#506070): the
// literal these labels used, #8090a0, is a light mid-grey, so the label token
// would have visibly darkened them against every sibling applet. TunerApplet
// and ProfileSwitcherApplet still carry their own copy of the old literal;
// converging all three on this token is a follow-up, not this PR's business.
static const char* kRowLabelStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; }";

namespace {

void styleRowLabel(QLabel* label)
{
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kRowLabelStyle));
}

QString formatHz(double hz)
{
    if (hz >= 1.0e6)
        return QStringLiteral("%1 MHz").arg(hz / 1.0e6, 0, 'f', 3);
    return QStringLiteral("%1 kHz").arg(hz / 1.0e3, 0, 'f', 1);
}

QString formatBinWidth(double hz)
{
    if (hz >= 1000.0)
        return QStringLiteral("%1 kHz / bin").arg(hz / 1000.0, 0, 'f', 2);
    return QStringLiteral("%1 Hz / bin").arg(hz, 0, 'f', 1);
}

// True when a body is a JSON object — the only shape any gate route answers
// with. An old gate falls through to its web panel (HTML, HTTP 200) on a route
// it does not know, and a stray web server on the port answers the same way,
// so "the request succeeded" on its own says nothing about what answered.
bool parseObject(const QByteArray& body, QJsonObject* out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

// Repopulate without the refill looking like an operator choice: every control
// here writes to the radio on change, so an unblocked rebuild would re-send the
// value the gate just reported back to it, once per poll.
void setComboItems(QComboBox* combo, const QStringList& items, const QString& current)
{
    const QSignalBlocker block(combo);
    QStringList have;
    for (int i = 0; i < combo->count(); ++i)
        have << combo->itemText(i);
    if (have != items) {
        combo->clear();
        combo->addItems(items);
    }
    const int idx = combo->findText(current);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
}

bool sameRates(const QComboBox* combo, const QList<double>& rates)
{
    if (combo->count() != rates.size())
        return false;
    for (int i = 0; i < rates.size(); ++i) {
        if (combo->itemData(i).toDouble() != rates[i])
            return false;
    }
    return true;
}

int indexOfRate(const QComboBox* combo, double rate)
{
    for (int i = 0; i < combo->count(); ++i) {
        if (std::fabs(combo->itemData(i).toDouble() - rate) < 0.5)
            return i;
    }
    return -1;
}

// Reads the gate's {"min","max","step"} for a setting. False when the gate
// reported none (an older gate, or a driver that gave Soapy's 0..0 default).
bool readRange(const QJsonObject& setting, double* lo, double* hi, double* step)
{
    const QJsonObject r = setting.value(QStringLiteral("range")).toObject();
    if (r.isEmpty())
        return false;
    *lo = r.value(QStringLiteral("min")).toDouble();
    *hi = r.value(QStringLiteral("max")).toDouble();
    *step = r.value(QStringLiteral("step")).toDouble();
    return *hi > *lo;
}

int decimalsForStep(double step)
{
    if (step <= 0.0)
        return 3;
    return std::clamp(int(std::ceil(-std::log10(step))), 0, 6);
}

// snr_db's a/b/out members are individually float|null (no signal seen yet on
// that leg) — render the dash the rest of the applet already uses for "no
// value" (m_binWidth's placeholder) rather than a bare "0.0" that reads as a
// measurement.
QString snrText(const QJsonObject& snr, const char* key)
{
    const QJsonValue v = snr.value(QLatin1String(key));
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("—");
    return QString::number(v.toDouble(), 'f', 1);
}

QString formatDiversityStatus(const QJsonObject& d)
{
    const int lag = d.value(QStringLiteral("lag_samples")).toInt();
    const bool aligned = d.value(QStringLiteral("aligned")).toBool();
    const double peak = d.value(QStringLiteral("corr_peak")).toDouble();
    const QJsonObject snr = d.value(QStringLiteral("snr_db")).toObject();
    return QStringLiteral("lag %1 samples · %2 · peak %3 · SNR %4/%5/%6 dB")
        .arg(lag)
        .arg(aligned ? QObject::tr("aligned") : QObject::tr("not aligned"))
        .arg(peak, 0, 'f', 2)
        .arg(snrText(snr, "a"), snrText(snr, "b"), snrText(snr, "out"));
}

} // namespace

AetherGateApplet::AetherGateApplet(QWidget* parent, QNetworkAccessManager* net)
    : QWidget(parent)
{
    theme::setContainer(this, QStringLiteral("applet/gate"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(6);

    m_status = new QLabel(tr("looking for a gate…"), this);
    m_status->setObjectName(QStringLiteral("gateStatusLabel"));
    styleRowLabel(m_status);
    root->addWidget(m_status);

    // --- panadapter resolution ------------------------------------------
    // Duplicated with the pan's own zoom on purpose: the zoom is a gesture and
    // this is a readout you can set exactly, and only this one shows what the
    // bin width actually came out as.
    m_resBox = new QWidget(this);
    m_resBox->setObjectName(QStringLiteral("gateResolutionBox"));
    auto* resForm = new QFormLayout(m_resBox);
    resForm->setContentsMargins(0, 0, 0, 0);
    resForm->setSpacing(4);

    m_span = new QComboBox(m_resBox);
    m_span->setObjectName(QStringLiteral("gateSpanCombo"));
    m_span->setToolTip(tr("Receiver sample rate. On an SDR the rate IS the "
                          "panadapter span, so a narrower span means finer bins."));
    connect(m_span, &QComboBox::currentIndexChanged, this, [this](int) { sendResolution(); });

    m_bins = new QComboBox(m_resBox);
    m_bins->setObjectName(QStringLiteral("gateBinsCombo"));
    m_bins->setToolTip(tr("FFT bins across the span. Capped by what one UDP "
                          "datagram can carry."));
    connect(m_bins, &QComboBox::currentIndexChanged, this, [this](int) { sendResolution(); });

    m_binWidth = new QLabel(QStringLiteral("—"), m_resBox);

    auto* spanLabel = new QLabel(tr("Span"), m_resBox);
    auto* binsLabel = new QLabel(tr("Bins"), m_resBox);
    styleRowLabel(spanLabel);
    styleRowLabel(binsLabel);
    resForm->addRow(spanLabel, m_span);
    resForm->addRow(binsLabel, m_bins);
    resForm->addRow(QString(), m_binWidth);
    m_resBox->setVisible(false);
    root->addWidget(m_resBox);

    // --- device controls, built from what the gate reports ---------------
    m_deviceBox = new QWidget(this);
    m_deviceBox->setObjectName(QStringLiteral("gateDeviceBox"));
    m_deviceForm = new QFormLayout(m_deviceBox);
    m_deviceForm->setContentsMargins(0, 6, 0, 0);
    m_deviceForm->setSpacing(4);
    m_deviceBox->setVisible(false);
    root->addWidget(m_deviceBox);

    m_deviceHint = new QLabel(tr("device controls need a newer Aether-gate"), this);
    m_deviceHint->setObjectName(QStringLiteral("gateDeviceHint"));
    m_deviceHint->setWordWrap(true);
    styleRowLabel(m_deviceHint);
    m_deviceHint->setVisible(false);
    root->addWidget(m_deviceHint);

    // --- diversity, RSPduo dual-tuner combining --------------------------
    // Hidden until a /diversity poll reports "available": true (a gate not
    // bridging a dual-tuner device, or one that predates the feature).
    m_diversityBox = new QWidget(this);
    m_diversityBox->setObjectName(QStringLiteral("gateDiversityBox"));
    auto* divForm = new QFormLayout(m_diversityBox);
    divForm->setContentsMargins(0, 6, 0, 0);
    divForm->setSpacing(4);

    m_diversityMode = new QComboBox(m_diversityBox);
    m_diversityMode->setObjectName(QStringLiteral("gateDiversityModeCombo"));
    m_diversityMode->addItem(tr("Off"), QStringLiteral("off"));
    m_diversityMode->addItem(tr("Manual"), QStringLiteral("manual"));
    m_diversityMode->addItem(tr("Null"), QStringLiteral("null"));
    m_diversityMode->addItem(tr("Track"), QStringLiteral("track"));
    connect(m_diversityMode, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), m_diversityMode->itemData(idx).toString());
        sendDiversitySet(q);
    });
    auto* modeLabel = new QLabel(tr("Mode"), m_diversityBox);
    styleRowLabel(modeLabel);
    divForm->addRow(modeLabel, m_diversityMode);

    auto* phaseRow = new QWidget(m_diversityBox);
    auto* phaseRowLayout = new QHBoxLayout(phaseRow);
    phaseRowLayout->setContentsMargins(0, 0, 0, 0);
    m_diversityPhase = new QSlider(Qt::Horizontal, phaseRow);
    m_diversityPhase->setObjectName(QStringLiteral("gateDiversityPhaseSlider"));
    m_diversityPhase->setRange(0, 360);
    m_diversityPhaseValue = new QLabel(QStringLiteral("0°"), phaseRow);
    m_diversityPhaseValue->setObjectName(QStringLiteral("gateDiversityPhaseValueLabel"));
    connect(m_diversityPhase, &QSlider::valueChanged, this, [this](int v) {
        m_diversityPhaseValue->setText(QStringLiteral("%1°").arg(v));
        m_diversityPhaseDebounce->start(kDiversityDebounceMs);
    });
    phaseRowLayout->addWidget(m_diversityPhase, 1);
    phaseRowLayout->addWidget(m_diversityPhaseValue);
    auto* phaseLabel = new QLabel(tr("Phase"), m_diversityBox);
    styleRowLabel(phaseLabel);
    divForm->addRow(phaseLabel, phaseRow);

    m_diversityRatio = new QDoubleSpinBox(m_diversityBox);
    m_diversityRatio->setObjectName(QStringLiteral("gateDiversityRatioSpin"));
    m_diversityRatio->setRange(-20.0, 20.0);
    m_diversityRatio->setSingleStep(0.5);
    m_diversityRatio->setDecimals(1);
    m_diversityRatio->setSuffix(QStringLiteral(" dB"));
    connect(m_diversityRatio, &QDoubleSpinBox::valueChanged, this, [this](double) {
        m_diversityRatioDebounce->start(kDiversityDebounceMs);
    });
    auto* ratioLabel = new QLabel(tr("Ratio"), m_diversityBox);
    styleRowLabel(ratioLabel);
    divForm->addRow(ratioLabel, m_diversityRatio);

    m_diversitySource = new QComboBox(m_diversityBox);
    m_diversitySource->setObjectName(QStringLiteral("gateDiversitySourceCombo"));
    m_diversitySource->addItem(tr("Combined"), QStringLiteral("combined"));
    m_diversitySource->addItem(tr("A"), QStringLiteral("a"));
    m_diversitySource->addItem(tr("B"), QStringLiteral("b"));
    connect(m_diversitySource, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("source"), m_diversitySource->itemData(idx).toString());
        sendDiversitySet(q);
    });
    auto* sourceLabel = new QLabel(tr("Source"), m_diversityBox);
    styleRowLabel(sourceLabel);
    divForm->addRow(sourceLabel, m_diversitySource);

    m_diversityRealign = new QPushButton(tr("Realign"), m_diversityBox);
    m_diversityRealign->setObjectName(QStringLiteral("gateDiversityRealignButton"));
    connect(m_diversityRealign, &QPushButton::clicked, this,
            &AetherGateApplet::sendDiversityAlign);
    divForm->addRow(QString(), m_diversityRealign);

    m_diversityStatusLine = new QLabel(QStringLiteral("—"), m_diversityBox);
    m_diversityStatusLine->setObjectName(QStringLiteral("gateDiversityStatusLabel"));
    m_diversityStatusLine->setWordWrap(true);
    styleRowLabel(m_diversityStatusLine);
    divForm->addRow(QString(), m_diversityStatusLine);

    m_diversityBox->setVisible(false);
    root->addWidget(m_diversityBox);

    root->addStretch(1);

    m_diversityPhaseDebounce = new QTimer(this);
    m_diversityPhaseDebounce->setSingleShot(true);
    connect(m_diversityPhaseDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("phase"), QString::number(m_diversityPhase->value()));
        sendDiversitySet(q);
    });

    m_diversityRatioDebounce = new QTimer(this);
    m_diversityRatioDebounce->setSingleShot(true);
    connect(m_diversityRatioDebounce, &QTimer::timeout, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("ratio"),
                       QString::number(m_diversityRatio->value(), 'f', 1));
        sendDiversitySet(q);
    });

    m_net = net ? net : new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    m_timer->setObjectName(QStringLiteral("gatePollTimer"));
    connect(m_timer, &QTimer::timeout, this, &AetherGateApplet::poll);
}

void AetherGateApplet::setRadioModel(RadioModel* model)
{
    m_model = model;
    if (!m_model)
        return;

    // infoChanged, NOT connectionStateChanged.  The gate answers at the RADIO's
    // address, and ip() is filled in by the reply to "info" — which the model
    // only sends AFTER it announces the connection.  Probing on the connect
    // edge finds an empty address, and a probe with nowhere to go never counts
    // a failure, so the applet would give up without ever having asked.  (Same
    // ordering trap the TX power-scale wiring hit in #4813.)
    connect(m_model, &RadioModel::infoChanged, this, [this] {
        setRadioAddress(m_model ? m_model->ip() : QString());
    });
    // The down edge clears the address, so a reconnect to the same radio asks
    // again: the gate may have been restarted, or replaced by a real Flex at
    // the same address.
    connect(m_model, &RadioModel::connectionStateChanged, this, [this](bool up) {
        if (!up)
            setRadioAddress(QString());
    });

    setRadioAddress(m_model->ip());
}

void AetherGateApplet::setRadioAddress(const QString& ip)
{
    if (ip == m_ip)
        return;                       // already asking this one

    // A different address is a different radio: drop what we knew rather than
    // showing the previous gate's controls.
    m_ip = ip;
    m_failures = 0;
    m_deviceFetched = false;
    m_pollsSinceDevice = 0;
    setPresent(false);
    if (m_ip.isEmpty())
        return;                       // setPresent() has already parked the timer
    // Probe even while hidden.  AppletPanel keeps the GATE button out of the
    // bar until presence is confirmed, so an applet that polled only when
    // visible could never become visible — it would be waiting on the answer to
    // a question it had no way to ask.
    poll();
    scheduleTimer();
}

QString AetherGateApplet::baseUrl() const
{
    if (m_ip.isEmpty())
        return {};
    return QStringLiteral("http://%1:%2").arg(m_ip).arg(kGateControlPort);
}

void AetherGateApplet::scheduleTimer()
{
    if (m_ip.isEmpty()) {
        m_timer->stop();              // nowhere to ask
        return;
    }
    if (isVisible())
        m_timer->start(kPollMs);
    else if (m_present)
        m_timer->stop();              // found; nothing to learn off screen
    else
        m_timer->start(kReprobeMs);   // not found; keep the door open
}

void AetherGateApplet::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    poll();
    scheduleTimer();
}

void AetherGateApplet::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    scheduleTimer();
}

void AetherGateApplet::get(const QString& path,
                           void (AetherGateApplet::*handler)(const QJsonObject&, bool))
{
    const QString base = baseUrl();
    if (base.isEmpty())
        return;
    QNetworkRequest req{QUrl(base + path)};
    req.setTransferTimeout(2000);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (++m_failures >= kFailuresBeforeAbsent)
                setPresent(false);
            return;
        }
        m_failures = 0;
        // Only a JSON object confirms a gate. Anything else that answered
        // neither confirms nor denies one: the handler decides what a non-gate
        // body means for its own route.
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        if (json)
            setPresent(true);
        (this->*handler)(obj, json);
    });
}

void AetherGateApplet::setPresent(bool present)
{
    if (m_present == present)
        return;
    m_present = present;
    if (!present) {
        m_status->setText(tr("no Aether-gate answering on this radio"));
        // A gate run with --ctl-port 0 looks exactly like no gate from here.
        m_status->setToolTip(tr("Nothing answered on port %1. A gate started "
                                "with --ctl-port 0 has no control port and "
                                "cannot be reached from here.")
                                 .arg(kGateControlPort));
        m_resBox->setVisible(false);
        m_deviceBox->setVisible(false);
        m_deviceHint->setVisible(false);
        m_controlsFingerprint.clear();
        m_deviceFetched = false;
        m_diversityBox->setVisible(false);
        m_diversityPhaseDebounce->stop();
        m_diversityRatioDebounce->stop();
    } else {
        m_status->setToolTip(QString());
    }
    emit gatePresenceChanged(present);
    scheduleTimer();
}

void AetherGateApplet::poll()
{
    // No radio address (wired at startup, before any connect) — nothing to
    // probe, and get() would silently drop the request without ever counting a
    // failure, leaving the timer spinning on a no-op forever.
    if (m_ip.isEmpty()) {
        m_timer->stop();
        return;
    }
    get(QStringLiteral("/status"), &AetherGateApplet::applyStatus);
    pollDiversity();               // piggyback — never a second timer
}

void AetherGateApplet::refreshDeviceControls()
{
    get(QStringLiteral("/device"), &AetherGateApplet::applyDeviceControls);
}

void AetherGateApplet::applyStatus(const QJsonObject& root, bool isJson)
{
    if (!isJson)
        return;                       // something on the port, but not a gate

    const bool connected = root.value(QStringLiteral("connected")).toBool();
    const bool streaming = root.value(QStringLiteral("streaming")).toBool();
    m_status->setText(connected
                          ? tr("gate connected · %1").arg(streaming ? tr("streaming")
                                                                    : tr("idle"))
                          : tr("gate up · waiting for the app"));

    // A gate older than the "res" field is still a gate: keep presence and the
    // device controls, and only fold away the rows it cannot serve.
    const QJsonObject res = root.value(QStringLiteral("res")).toObject();
    m_resBox->setVisible(!res.isEmpty());
    if (!res.isEmpty()) {
        const double spanHz = res.value(QStringLiteral("span_hz")).toDouble();
        const double binHz = res.value(QStringLiteral("bin_hz")).toDouble();
        const int bins = res.value(QStringLiteral("bins")).toInt();
        const int maxBins = res.value(QStringLiteral("max_bins")).toInt(4096);
        m_binWidth->setText(formatBinWidth(binHz));

        // Span list comes from the device, not from us — see the header
        // comment.  Each entry carries the raw rate as item data: the label is
        // rounded for reading and is neither what gets sent back nor what the
        // running rate is matched against (2 000 000 and 2 000 400 both read
        // "2.000 MHz").
        QList<double> rates;
        for (const QJsonValue& v : res.value(QStringLiteral("rates")).toArray())
            rates << v.toDouble();
        const bool canSetRate = res.value(QStringLiteral("can_set_rate")).toBool();
        m_span->setEnabled(canSetRate && !rates.isEmpty());
        if (!rates.isEmpty()) {
            const QSignalBlocker block(m_span);
            if (!sameRates(m_span, rates)) {
                m_span->clear();
                for (double r : rates)
                    m_span->addItem(formatHz(r), QVariant(r));
            }
            const double running = res.value(QStringLiteral("samp_rate")).toDouble(spanHz);
            const int idx = indexOfRate(m_span, running);
            if (idx >= 0)
                m_span->setCurrentIndex(idx);
        }

        QStringList binItems;
        for (int n = 1024; n <= 16384; n *= 2) {
            if (n <= maxBins)
                binItems << QString::number(n);
        }
        if (!binItems.contains(QString::number(bins)))
            binItems << QString::number(bins);
        setComboItems(m_bins, binItems, QString::number(bins));
    }

    // The control SET only changes when the device does, which is rare but
    // real (a hot swap on the gate's side), so /device is read once up front
    // and then every kDeviceRefreshPolls rather than on every poll.
    if (!m_deviceFetched || ++m_pollsSinceDevice >= kDeviceRefreshPolls)
        refreshDeviceControls();
}

void AetherGateApplet::sendResolution()
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("bins"), m_bins->currentText());
    if (m_span->isEnabled()) {
        const int idx = m_span->currentIndex();
        if (idx >= 0)
            q.addQueryItem(QStringLiteral("rate"),
                           QString::number(m_span->itemData(idx).toDouble(), 'f', 0));
    }
    QUrl url(base + QStringLiteral("/resolution"));
    url.setQuery(q);
    QNetworkRequest req{url};
    req.setTransferTimeout(8000);          // a rate change restarts the stream
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

// One path for every write. A radio drop mid-edit leaves the widgets alive
// under the operator's cursor for a moment; without the guard their next
// change would build a request from an empty base and send it nowhere useful.
void AetherGateApplet::sendDeviceSet(const QUrlQuery& query)
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrl url(base + QStringLiteral("/device/set"));
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(4000);          // the gate settles 0.35 s before reading back
    QNetworkReply* reply = m_net->get(req);
    // The read-back arrives with the reply, so the control always ends up
    // showing what the DEVICE took rather than what we asked for.
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        applyDeviceControls(obj, json);
    });
}

void AetherGateApplet::applyDeviceControls(const QJsonObject& dev, bool isJson)
{
    m_deviceFetched = true;
    m_pollsSinceDevice = 0;
    if (!isJson) {
        // An older gate has no /device route and falls through to its web
        // panel: HTTP 200, HTML.  That is a gate without device controls, not
        // a device without settings — say which, rather than showing nothing.
        m_deviceBox->setVisible(false);
        m_deviceHint->setVisible(true);
        m_controlsFingerprint.clear();
        return;
    }
    m_deviceHint->setVisible(false);
    buildDeviceControls(dev);
}

void AetherGateApplet::buildDeviceControls(const QJsonObject& dev)
{
    // Fingerprint the SHAPE (which controls exist, and what kind each is), not
    // the values — rebuilding widgets under the operator's cursor every time a
    // value changed would make the panel unusable.
    QStringList shape;
    const QJsonObject ant = dev.value(QStringLiteral("antenna")).toObject();
    if (!ant.isEmpty())
        shape << QStringLiteral("antenna");
    const QJsonArray settings = dev.value(QStringLiteral("settings")).toArray();
    for (const QJsonValue& v : settings) {
        const QJsonObject so = v.toObject();
        shape << QStringLiteral("%1:%2:%3:%4")
                     .arg(so.value(QStringLiteral("key")).toString(),
                          so.value(QStringLiteral("type")).toString())
                     .arg(so.value(QStringLiteral("options")).toArray().size())
                     .arg(QString::fromUtf8(QJsonDocument(
                              so.value(QStringLiteral("range")).toObject())
                              .toJson(QJsonDocument::Compact)));
    }
    const QString fingerprint = shape.join(QLatin1Char('|'));

    if (fingerprint != m_controlsFingerprint) {
        m_controlsFingerprint = fingerprint;
        m_settingWidgets.clear();
        m_antenna = nullptr;
        while (m_deviceForm->count() > 0) {
            QLayoutItem* item = m_deviceForm->takeAt(0);
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }

        if (!ant.isEmpty()) {
            m_antenna = new QComboBox(m_deviceBox);
            m_antenna->setObjectName(QStringLiteral("gateAntennaCombo"));
            for (const QJsonValue& o : ant.value(QStringLiteral("options")).toArray())
                m_antenna->addItem(o.toString());
            connect(m_antenna, &QComboBox::currentTextChanged, this,
                    [this](const QString& text) {
                        QUrlQuery q;
                        q.addQueryItem(QStringLiteral("antenna"), text);
                        sendDeviceSet(q);
                    });
            auto* label = new QLabel(tr("Antenna"), m_deviceBox);
            styleRowLabel(label);
            m_deviceForm->addRow(label, m_antenna);
        }

        for (const QJsonValue& v : settings) {
            const QJsonObject so = v.toObject();
            const QString key = so.value(QStringLiteral("key")).toString();
            const QString name = so.value(QStringLiteral("name")).toString(key);
            const QString type = so.value(QStringLiteral("type")).toString();
            const QJsonArray options = so.value(QStringLiteral("options")).toArray();

            auto push = [this, key](const QString& value) {
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("key"), key);
                q.addQueryItem(QStringLiteral("value"), value);
                sendDeviceSet(q);
            };

            // The widget follows the Soapy ArgInfo type the gate relays:
            // "0" BOOL, "1" INT, "2" FLOAT, anything else a string. A setting
            // with an option list is a choice whatever its type says.
            double lo = 0.0, hi = 0.0, step = 0.0;
            const bool bounded = readRange(so, &lo, &hi, &step);
            QWidget* w = nullptr;
            if (!options.isEmpty()) {
                auto* combo = new QComboBox(m_deviceBox);
                for (const QJsonValue& o : options)
                    combo->addItem(o.toString());
                connect(combo, &QComboBox::currentTextChanged, this, push);
                w = combo;
            } else if (type == QLatin1String("0")) {
                auto* check = new QCheckBox(m_deviceBox);
                connect(check, &QCheckBox::toggled, this, [push](bool on) {
                    push(on ? QStringLiteral("true") : QStringLiteral("false"));
                });
                w = check;
            } else if (type == QLatin1String("1")) {
                auto* spin = new QSpinBox(m_deviceBox);
                if (bounded) {
                    spin->setRange(int(std::lround(lo)), int(std::lround(hi)));
                    if (step >= 1.0)
                        spin->setSingleStep(int(std::lround(step)));
                } else {
                    spin->setRange(-kUnboundedInt, kUnboundedInt);
                }
                spin->setKeyboardTracking(false);   // one write per committed edit
                connect(spin, &QSpinBox::valueChanged, this, [push](int v) {
                    push(QString::number(v));
                });
                w = spin;
            } else if (type == QLatin1String("2")) {
                auto* spin = new QDoubleSpinBox(m_deviceBox);
                spin->setDecimals(decimalsForStep(bounded ? step : 0.0));
                if (bounded) {
                    spin->setRange(lo, hi);
                    if (step > 0.0)
                        spin->setSingleStep(step);
                } else {
                    spin->setRange(-kUnboundedDouble, kUnboundedDouble);
                }
                spin->setKeyboardTracking(false);
                connect(spin, &QDoubleSpinBox::valueChanged, this, [push](double v) {
                    push(QString::number(v, 'g', 10));
                });
                w = spin;
            } else {
                auto* edit = new QLineEdit(m_deviceBox);
                connect(edit, &QLineEdit::editingFinished, this, [push, edit] {
                    push(edit->text());
                });
                w = edit;
            }
            w->setObjectName(QStringLiteral("gateSetting:") + key);
            m_settingWidgets.insert(key, w);
            auto* label = new QLabel(name, m_deviceBox);
            styleRowLabel(label);
            m_deviceForm->addRow(label, w);
        }
        m_deviceBox->setVisible(!shape.isEmpty());
    }

    // Values, every time — blocked so a refresh never re-sends what it reads,
    // and skipped on a control the operator is in the middle of editing.
    if (m_antenna && !ant.isEmpty()) {
        const QSignalBlocker block(m_antenna);
        const int idx = m_antenna->findText(ant.value(QStringLiteral("value")).toString());
        if (idx >= 0)
            m_antenna->setCurrentIndex(idx);
    }
    for (const QJsonValue& v : settings) {
        const QJsonObject so = v.toObject();
        QWidget* w = m_settingWidgets.value(so.value(QStringLiteral("key")).toString());
        if (!w || w->hasFocus())
            continue;
        const QString value = so.value(QStringLiteral("value")).toString();
        const QSignalBlocker block(w);
        if (auto* combo = qobject_cast<QComboBox*>(w)) {
            const int idx = combo->findText(value);
            if (idx >= 0)
                combo->setCurrentIndex(idx);
        } else if (auto* check = qobject_cast<QCheckBox*>(w)) {
            check->setChecked(value == QLatin1String("true"));
        } else if (auto* spin = qobject_cast<QSpinBox*>(w)) {
            spin->setValue(value.toInt());
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(w)) {
            dspin->setValue(value.toDouble());
        } else if (auto* edit = qobject_cast<QLineEdit*>(w)) {
            edit->setText(value);
        }
    }
}

// Not routed through get(): an older gate with no /diversity route 404s here,
// and that says nothing about whether the GATE answered — /status alone
// decides presence — so a failure just hides the section instead of counting
// toward m_failures/setPresent(false) (see the header comment on this method).
void AetherGateApplet::pollDiversity()
{
    const QString base = baseUrl();
    if (base.isEmpty())
        return;
    QNetworkRequest req{QUrl(base + QStringLiteral("/diversity"))};
    req.setTransferTimeout(2000);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_diversityBox->setVisible(false);
            return;
        }
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        applyDiversity(obj, json);
    });
}

void AetherGateApplet::applyDiversity(const QJsonObject& d, bool isJson)
{
    if (!isJson) {
        m_diversityBox->setVisible(false);
        return;
    }

    const bool available = d.value(QStringLiteral("available")).toBool();
    m_diversityBox->setVisible(available);
    if (!available)
        return;

    const QString mode = d.value(QStringLiteral("mode")).toString();
    {
        const QSignalBlocker block(m_diversityMode);
        const int idx = m_diversityMode->findData(mode);
        if (idx >= 0)
            m_diversityMode->setCurrentIndex(idx);
    }
    // Only Manual takes a phase/ratio setpoint — Null and Track solve for
    // their own weight, and Off applies none, so editing either there would
    // write a value the gate is not using.
    const bool manual = (mode == QLatin1String("manual"));
    m_diversityPhase->setEnabled(manual);
    m_diversityRatio->setEnabled(manual);

    const QString source = d.value(QStringLiteral("source")).toString();
    {
        const QSignalBlocker block(m_diversitySource);
        const int idx = m_diversitySource->findData(source);
        if (idx >= 0)
            m_diversitySource->setCurrentIndex(idx);
    }

    // Skip a control the operator is in the middle of dragging/editing — the
    // same guard buildDeviceControls() uses for the settings it re-populates.
    if (!m_diversityPhase->hasFocus()) {
        const QSignalBlocker block(m_diversityPhase);
        const int phase = int(std::lround(d.value(QStringLiteral("phase_deg")).toDouble()));
        m_diversityPhase->setValue(phase);
        m_diversityPhaseValue->setText(QStringLiteral("%1°").arg(phase));
    }
    if (!m_diversityRatio->hasFocus()) {
        const QSignalBlocker block(m_diversityRatio);
        m_diversityRatio->setValue(d.value(QStringLiteral("ratio_db")).toDouble());
    }

    m_diversityStatusLine->setText(formatDiversityStatus(d));
}

// One path for every diversity write, same shape as sendDeviceSet(): the
// read-back arrives with the reply, so a control always ends up showing what
// the gate took rather than what we asked for.
void AetherGateApplet::sendDiversitySet(const QUrlQuery& query)
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrl url(base + QStringLiteral("/diversity/set"));
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(4000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        applyDiversity(obj, json);
    });
}

void AetherGateApplet::sendDiversityAlign()
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QNetworkRequest req{QUrl(base + QStringLiteral("/diversity/align"))};
    req.setTransferTimeout(4000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        applyDiversity(obj, json);
    });
}

} // namespace AetherSDR
