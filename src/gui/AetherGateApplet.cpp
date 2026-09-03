#include "AetherGateApplet.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateAppletShared.h"
#include "gui/AetherGateChainWindow.h"
#include "gui/AetherGateDeviceStrip.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>
#include <QVBoxLayout>

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

// The CHAIN door. Character for character the Diversity panel's own
// kOpenWindowStyle (AetherGateDiversityPanel.cpp:45-50): the operator's first
// note on this window was that the two doors did not look like the same kind
// of thing (design §0.3 item 1), and they did not -- this one had the subtle
// border, the smaller padding and a left-aligned label. Same border, same
// padding, same centring, so the pair reads as a pair.
static const char* kOpenChainStyle =
    "QPushButton { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "padding: 5px 8px; border: 1px solid {{color.accent}}; border-radius: 4px; "
    "background: transparent; }"
    "QPushButton:hover { background: {{color.background.1}}; }"
    "QPushButton:pressed { background: {{color.background.3}}; }";

namespace {

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
    GateApplet::styleRowLabel(m_status);
    root->addWidget(m_status);

    // --- what is plugged in, and the way out of diversity (B13) ----------
    // "A gate is answering" and "both tuners are running" are one question.
    m_deviceStrip = new AetherGateDeviceStrip(this);
    connect(m_deviceStrip, &AetherGateDeviceStrip::requestDiversitySet, this,
            &AetherGateApplet::onDiversityRequestSet);
    root->addWidget(m_deviceStrip);

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
    GateApplet::styleRowLabel(spanLabel);
    GateApplet::styleRowLabel(binsLabel);
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
    GateApplet::styleRowLabel(m_deviceHint);
    m_deviceHint->setVisible(false);
    root->addWidget(m_deviceHint);

    // --- diversity, RSPduo dual-tuner combining --------------------------
    // Hidden until a /diversity poll reports "available": true (a gate not
    // bridging a dual-tuner device, or one that predates the feature); the
    // panel manages its own visibility for that, see its own setVisible()
    // calls. All the controls/presentation live in AetherGateDiversityPanel
    // now; this applet's job is turning every signal it emits into the
    // matching GET and feeding the read-back back in.
    m_diversityPanel = new AetherGateDiversityPanel(this);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestSet, this,
            &AetherGateApplet::onDiversityRequestSet);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestCompareRestore, this,
            &AetherGateApplet::onDiversityRequestCompareRestore);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestAlign, this,
            &AetherGateApplet::onDiversityRequestAlign);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestCapture, this,
            &AetherGateApplet::onDiversityRequestCapture);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestMemoryClear, this,
            &AetherGateApplet::onDiversityRequestMemoryClear);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestMemoryName, this,
            &AetherGateApplet::onDiversityRequestMemoryName);
    // The Diversity window's FILTER page has its own OPEN CHAIN button, so the
    // chain is reachable from where its neighbouring stages are drawn as well
    // as from the sidebar door below. Same slot either way: the window is built
    // once and then kept, so whichever door is used second raises the one that
    // already exists rather than making a second.
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestOpenChain, this,
            &AetherGateApplet::toggleChainWindow);
    root->addWidget(m_diversityPanel);

    // --- the other door --------------------------------------------------
    // CHAIN sits beside the Diversity one and is NOT inside the diversity
    // panel: that panel hides itself until a gate reports two tuners, and the
    // chain is a receiver feature that works on one. See
    // AetherGateChainWindow.h.
    m_openChainButton = new QPushButton(tr("Open Chain window"), this);
    m_openChainButton->setObjectName(QStringLiteral("gateOpenChainWindowButton"));
    m_openChainButton->setAccessibleName(tr("Open the filter chain window"));
    m_openChainButton->setToolTip(tr("Every stage between the antenna and your "
                                     "ears, in signal order, with the switch for "
                                     "each one the gate says it has."));
    m_openChainButton->setAccessibleDescription(m_openChainButton->toolTip());
    m_openChainButton->setCursor(Qt::PointingHandCursor);
    ThemeManager::instance().applyStyleSheet(m_openChainButton,
                                             QString::fromLatin1(kOpenChainStyle));
    connect(m_openChainButton, &QPushButton::clicked, this,
            &AetherGateApplet::toggleChainWindow);
    root->addWidget(m_openChainButton);

    root->addStretch(1);

    m_net = net ? net : new QNetworkAccessManager(this);
    // The BAND page's routes, on their own cadence and off unless that page is
    // on screen — see DiversityBandPoller.h.
    m_bandPoller = new DiversityBandPoller(m_net, this);
    connect(m_bandPoller, &DiversityBandPoller::spatialReceived, m_diversityPanel,
            &AetherGateDiversityPanel::applySpatial);
    connect(m_bandPoller, &DiversityBandPoller::finderReceived, m_diversityPanel,
            &AetherGateDiversityPanel::applyFinder);
    connect(m_bandPoller, &DiversityBandPoller::beaconsReceived, m_diversityPanel,
            &AetherGateDiversityPanel::applyBeacons);
    m_bandPoller->attachFilter(m_diversityPanel);
    connect(m_diversityPanel, &AetherGateDiversityPanel::bandPollChanged, this,
            &AetherGateApplet::updateBandPoll);
    connect(m_diversityPanel, &AetherGateDiversityPanel::requestTune, this,
            &AetherGateApplet::onDiversityRequestTune);
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

    // Fire the compare hold's resume BEFORE m_ip changes below: it builds its
    // request from baseUrl(), which has to still name the gate the hold was
    // started against, not whatever radio (or nothing) we are about to ask
    // instead. Idempotent — see restoreCompareHold()'s own comment — so this
    // is harmless when nothing is pressed.
    m_diversityPanel->restoreCompareHold();

    // A different address is a different radio: drop what we knew rather than
    // showing the previous gate's controls.
    m_ip = ip;
    m_failures = 0;
    m_deviceFetched = false;
    m_pollsSinceDevice = 0;
    m_mapFetched = false;
    m_pollsSinceMap = 0;
    setPresent(false);
    updateBandPoll();
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
        const bool json = GateApplet::parseObject(reply->readAll(), &obj);
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
        m_mapFetched = false;
        m_pollsSinceMap = 0;
        // Everything diversity-specific — the box itself, the debounce
        // timers, the scope/map/sources/memory/capture widgets, and the
        // compare hold's forced resume (see restoreCompareHold()'s own
        // comment on why that one is unconditional) — is the panel's own
        // job now.
        m_diversityPanel->setPresent(false);
        if (m_chainWindow)
            m_chainWindow->setPresent(false);
    } else {
        m_status->setToolTip(QString());
        m_diversityPanel->setPresent(true);
        if (m_chainWindow)
            m_chainWindow->setPresent(true);
    }
    emit gatePresenceChanged(present);
    updateBandPoll();
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
    // The SITE page's BEACON CHECK has to know where the radio is before it
    // tunes away, and this applet is the only object in the diversity section
    // that can see a SliceModel. Pushed on the poll that already runs rather
    // than watched for: a frequency that is one second stale is exactly as
    // good, because what the check wants is somewhere to come home to.
    if (m_diversityPanel) {
        const SliceModel* slice = activeSlice();
        m_diversityPanel->setActiveSliceHz(slice ? slice->frequency() * 1.0e6 : 0.0);
    }
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

    // Optional: an older gate sends no "device", and the strip shows a dash.
    m_deviceStrip->applyDevice(root.value(QStringLiteral("device")));

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

// A write whose answer is not read: the next poll carries the result.
void AetherGateApplet::sendFireAndForget(const QString& path,
                                         const QUrlQuery& query, int timeoutMs)
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrl url(base + path);
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void AetherGateApplet::sendResolution()
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("bins"), m_bins->currentText());
    if (m_span->isEnabled()) {
        const int idx = m_span->currentIndex();
        if (idx >= 0)
            q.addQueryItem(QStringLiteral("rate"),
                           QString::number(m_span->itemData(idx).toDouble(), 'f', 0));
    }
    // 8 s, not the usual 4: a rate change restarts the stream.
    sendFireAndForget(QStringLiteral("/resolution"), q, 8000);
}

} // namespace AetherSDR
