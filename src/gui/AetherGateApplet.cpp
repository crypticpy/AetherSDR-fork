#include "AetherGateApplet.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateDiversityPanel.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShowEvent>
#include <QSignalBlocker>
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

// /diversity/map is heavier than the rest of the section (up to 256 floats
// twice over) and changes slowly compared to phase/ratio, so it is read on
// its own, coarser cadence rather than every /diversity poll.
static constexpr int kDiversityMapRefreshPolls = 2;

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
    root->addWidget(m_diversityPanel);

    root->addStretch(1);

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
        m_mapFetched = false;
        m_pollsSinceMap = 0;
        // Everything diversity-specific — the box itself, the debounce
        // timers, the scope/map/sources/memory/capture widgets, and the
        // compare hold's forced resume (see restoreCompareHold()'s own
        // comment on why that one is unconditional) — is the panel's own
        // job now.
        m_diversityPanel->setPresent(false);
    } else {
        m_status->setToolTip(QString());
        m_diversityPanel->setPresent(true);
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
            m_diversityPanel->applyDiversity({}, false);
            return;
        }
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        m_diversityPanel->applyDiversity(obj, json);

        // /diversity/map is heavier than the rest of this section, so it is
        // fetched on its own coarser cadence (kDiversityMapRefreshPolls)
        // rather than on every /diversity poll — same shape as /device's
        // refresh throttle. Counted ONLY here, off the timer-driven poll:
        // m_diversityPanel->applyDiversity() is also the read-back handler
        // for onDiversityRequestSet()/onDiversityRequestAlign(), and an
        // operator edit must not itself advance (or reset) this cadence.
        //
        // wantsMapPoll() is true only while the pop-out Diversity window is
        // on screen: since the sidebar was slimmed to a status line and a
        // door (docs/DIVERSITY-ROADMAP.md §3) the window's noise panel is
        // the only thing that draws the map, so a closed window costs no map
        // polling at all. Reset to zero whenever it goes false so the next
        // time it goes true the map is fetched immediately rather than
        // waiting out a stale count — see m_mapFetched's own header comment.
        if (m_diversityPanel->wantsMapPoll()) {
            if (!m_mapFetched || ++m_pollsSinceMap >= kDiversityMapRefreshPolls) {
                m_mapFetched = true;
                m_pollsSinceMap = 0;
                pollDiversityMap();
            }
        } else {
            m_mapFetched = false;
            m_pollsSinceMap = 0;
        }
    });
}

// Same non-critical-to-presence contract as pollDiversity() above: an
// {"error"} reply (no map yet) or a route an older gate never had both mean
// "nothing to draw", not a failed gate.
void AetherGateApplet::pollDiversityMap()
{
    const QString base = baseUrl();
    if (base.isEmpty())
        return;
    QNetworkRequest req{QUrl(base + QStringLiteral("/diversity/map"))};
    req.setTransferTimeout(2000);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        m_diversityPanel->applyMap(obj);
    });
}

// One path for every diversity write, same shape as sendDeviceSet(): the
// read-back arrives with the reply, so a control always ends up showing what
// the gate took rather than what we asked for.
void AetherGateApplet::onDiversityRequestSet(QUrlQuery query)
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
        m_diversityPanel->applyDiversity(obj, json);
    });
}

void AetherGateApplet::onDiversityRequestAlign()
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
        m_diversityPanel->applyDiversity(obj, json);
    });
}

// The "Hear A only" hold's forced resume — see
// AetherGateDiversityPanel::restoreCompareHold()'s own comment for why this
// is the one diversity write that is NOT gated on m_present: setPresent(false)
// itself needs this to still go out (the gate must not be left stuck in
// "off" just because THIS poll is what noticed it was gone), so this builds
// its own request, gated only on baseUrl() being non-empty.
void AetherGateApplet::onDiversityRequestCompareRestore(QUrlQuery query)
{
    const QString base = baseUrl();
    if (base.isEmpty())
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
        m_diversityPanel->applyDiversity(obj, json);
    });
}

// Unlike every other diversity write, the gate does not answer until the
// capture itself finishes — the response IS the result, not a read-back of
// state — so this bounds the timeout by the requested duration rather than
// reusing the fixed 4s every other /diversity/set write gets.
//
// The button-disable/"recording…" label swap that used to happen here
// happens on the panel's side now, before it ever emits requestCapture() —
// this slot is network-only, and reports back through
// AetherGateDiversityPanel::applyCaptureResult() (see its own header
// comment for the ok/pathOrError split, unchanged from this function's old
// three failure branches).
void AetherGateApplet::onDiversityRequestCapture(int seconds)
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("seconds"), QString::number(seconds));
    QUrl url(base + QStringLiteral("/diversity/capture"));
    url.setQuery(q);
    QNetworkRequest req{url};
    req.setTransferTimeout((seconds + 5) * 1000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_diversityPanel->applyCaptureResult(false, tr("capture failed"));
            return;
        }
        QJsonObject obj;
        const bool json = parseObject(reply->readAll(), &obj);
        if (!json) {
            m_diversityPanel->applyCaptureResult(false, tr("capture failed"));
            return;
        }
        if (obj.contains(QStringLiteral("error"))) {
            // An error this request itself reported must survive the very
            // next poll: capture.active is already back to false by then, and
            // its "path" is still whatever the LAST successful capture wrote
            // — without the flag, that poll (arriving within one tick of this
            // reply) silently replaces the error with that stale path. See
            // applyCaptureResult()'s own comment for how it protects that.
            m_diversityPanel->applyCaptureResult(
                false, obj.value(QStringLiteral("error")).toString());
        } else {
            m_diversityPanel->applyCaptureResult(
                true, obj.value(QStringLiteral("path")).toString());
        }
    });
}

// No read-back to apply: the next periodic /diversity poll shows the memory
// list emptied out, the same way sendResolution() lets /status carry the
// result instead of parsing this reply.
void AetherGateApplet::onDiversityRequestMemoryClear()
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QNetworkRequest req{QUrl(base + QStringLiteral("/diversity/memory/clear"))};
    req.setTransferTimeout(4000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

// The operator's own label for a remembered talker. Same no-read-back shape
// as onDiversityRequestMemoryClear() above: the gate echoes the stored name
// in the next periodic /diversity poll's memory[] entry, which is also what
// makes a rejected write visibly revert rather than silently stick in the
// table. `name` arrives as raw operator text -- percent-encoded here so a
// callsign with a space, an ampersand or an equals sign in it cannot inject a
// second query parameter.
void AetherGateApplet::onDiversityRequestMemoryName(int id, QString name)
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    q.addQueryItem(QStringLiteral("name"),
                   QString::fromLatin1(QUrl::toPercentEncoding(name)));
    QUrl url(base + QStringLiteral("/diversity/memory/name"));
    url.setQuery(q);
    QNetworkRequest req{url};
    req.setTransferTimeout(4000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

} // namespace AetherSDR
