#include "gui/DiversityBandPoller.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace AetherSDR {

namespace {

// 4 Hz: one waterfall row per tick.
constexpr int kTickMs = 250;

// /diversity/finder summarises ten minutes and is answered from a ring the
// gate updates slowly -- 1 Hz is already faster than it can change.
constexpr int kFinderEveryTicks = 4;

// Bounded like every other gate request (tools/check_network_timeouts.py): a
// half-open socket must become an error, not a reply that never arrives. Well
// inside the in-flight guard's tolerance -- a request that has not answered in
// two seconds has missed eight ticks and is not worth waiting for.
constexpr int kTransferTimeoutMs = 2000;

bool parseObject(const QByteArray& body, QJsonObject* out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

} // namespace

DiversityBandPoller::DiversityBandPoller(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent), m_net(net)
{
    setObjectName(QStringLiteral("gateDiversityBandPoller"));
    m_timer = new QTimer(this);
    m_timer->setObjectName(QStringLiteral("gateDiversityBandTimer"));
    connect(m_timer, &QTimer::timeout, this, &DiversityBandPoller::poll);
}

bool DiversityBandPoller::isPolling() const
{
    return m_timer->isActive();
}

void DiversityBandPoller::setBaseUrl(const QString& base)
{
    if (m_base == base)
        return;
    m_base = base;
    restart();
}

void DiversityBandPoller::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    restart();
}

void DiversityBandPoller::restart()
{
    if (!m_enabled || m_base.isEmpty() || !m_net) {
        m_timer->stop();
        return;
    }
    // Start from tick zero so the first poll fetches BOTH routes: the page has
    // just become visible and has nothing on it.
    m_tick = 0;
    m_timer->start(kTickMs);
    poll();
}

void DiversityBandPoller::poll()
{
    if (!m_enabled || m_base.isEmpty() || !m_net)
        return;
    const bool wantFinder = (m_tick % kFinderEveryTicks) == 0;
    ++m_tick;
    fetchSpatial();
    if (wantFinder)
        fetchFinder();
}

void DiversityBandPoller::fetchSpatial()
{
    if (m_spatialInFlight)
        return;
    m_spatialInFlight = true;
    QNetworkRequest req{QUrl(m_base + QStringLiteral("/diversity/spatial"))};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_spatialInFlight = false;
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        emit spatialReceived(obj);
    });
}

void DiversityBandPoller::fetchFinder()
{
    if (m_finderInFlight)
        return;
    m_finderInFlight = true;
    QNetworkRequest req{QUrl(m_base + QStringLiteral("/diversity/finder"))};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_finderInFlight = false;
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        emit finderReceived(obj);
    });
}

} // namespace AetherSDR
