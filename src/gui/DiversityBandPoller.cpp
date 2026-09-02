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

// The SITE page has no 4 Hz route on it. Ticking four times a second to make
// one request would be a wakeup budget spent on nothing.
constexpr int kSiteTickMs = 1000;

// /diversity/finder summarises ten minutes and is answered from a ring the
// gate updates slowly -- 1 Hz is already faster than it can change, and so is
// /diversity/beacons, whose schedule turns once every three minutes.
constexpr int kSlowEveryTicks = 4;

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

void DiversityBandPoller::setPages(bool bandVisible, bool siteVisible)
{
    if (m_bandEnabled == bandVisible && m_siteEnabled == siteVisible)
        return;
    m_bandEnabled = bandVisible;
    m_siteEnabled = siteVisible;
    restart();
}

void DiversityBandPoller::restart()
{
    if ((!m_bandEnabled && !m_siteEnabled) || m_base.isEmpty() || !m_net) {
        m_timer->stop();
        return;
    }
    // Start from tick zero so the first poll fetches EVERY route the visible
    // page needs: it has just become visible and has nothing on it.
    m_tick = 0;
    m_timer->start(m_bandEnabled ? kTickMs : kSiteTickMs);
    poll();
}

void DiversityBandPoller::poll()
{
    if ((!m_bandEnabled && !m_siteEnabled) || m_base.isEmpty() || !m_net)
        return;
    // While only SITE is up the timer is already at 1 Hz, so every tick is a
    // slow tick; while BAND is up it runs at 4 Hz and one in four is.
    const bool slowTick = !m_bandEnabled || (m_tick % kSlowEveryTicks) == 0;
    ++m_tick;
    if (m_bandEnabled) {
        fetchSpatial();
        if (slowTick)
            fetchFinder();
    }
    if (m_siteEnabled && slowTick)
        fetchBeacons();
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

void DiversityBandPoller::fetchBeacons()
{
    if (m_beaconsInFlight)
        return;
    m_beaconsInFlight = true;
    QNetworkRequest req{QUrl(m_base + QStringLiteral("/diversity/beacons"))};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_beaconsInFlight = false;
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        emit beaconsReceived(obj);
    });
}

} // namespace AetherSDR
