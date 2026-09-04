#include "gui/DiversityBandPoller.h"

#include "gui/AetherGateDiversityPanel.h"

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

// 2 Hz on the CHAIN window. The response curve, the AGC gain and the noise
// blanker's blanked-percentage all move while the operator listens, and a
// second between frames is slow enough to read as a stalled instrument; four
// times a second would be spending requests on a 1023-tap response that only
// changes when somebody moves a control.
constexpr int kFilterTickMs = 500;

// One in four BAND-cadence ticks (4 Hz -> 1 Hz) is how /diversity/finder,
// which wants a once-a-second summary rather than a waterfall row, rides the
// same timer as /diversity/spatial without its own clock. At any other
// cadence every tick already is the slow tick.
constexpr int kSlowEveryTicks = 4;

// 1 Hz, with no page on screen at all -- setBandAvailable(true) and nothing
// else. The same rate SITE already ticks at, kept as its own name because the
// two are picked for different reasons: SITE's is "no faster route exists on
// this page", this one is "nobody is watching, so the waterfall row rate
// backs off from 4 Hz to the slowest rate still worth keeping history at".
constexpr int kBackgroundTickMs = 1000;

// How often the background timer also primes /diversity/beacons and
// /diversity/compass: a beacon slot is ten seconds long and the schedule
// turns once every three minutes (see the SITE cadence comment at the top of
// the file), so once every half minute is already generous for a page that
// is not even on screen -- it exists only so a relaunch, or a window left
// open on BAND/FILTER/SLICE, does not show a blank beacon table until the
// operator forces a check.
constexpr int kBackgroundBeaconEveryTicks = 30;

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
    // Independent of m_timer above on purpose -- see setBandAvailable() and
    // restartBackground(). A page cadence (250/500/1000 ms, changing as the
    // operator switches pages) and a "nobody is watching" cadence sharing one
    // clock is exactly the bug this second timer avoids: the CHAIN window's 2 Hz
    // or a SITE page's 1 Hz would otherwise double one of its own ticks into
    // an extra background fetch every time the interval lined up.
    m_backgroundTimer = new QTimer(this);
    m_backgroundTimer->setObjectName(QStringLiteral("gateDiversityBackgroundTimer"));
    connect(m_backgroundTimer, &QTimer::timeout, this, &DiversityBandPoller::backgroundPoll);
}

bool DiversityBandPoller::isPolling() const
{
    return m_timer->isActive() || m_backgroundTimer->isActive();
}

void DiversityBandPoller::setBaseUrl(const QString& base)
{
    if (m_base == base)
        return;
    m_base = base;
    restart();
    restartBackground();
}

void DiversityBandPoller::setPages(bool bandVisible, bool siteVisible, bool filterVisible)
{
    if (m_bandEnabled == bandVisible && m_siteEnabled == siteVisible
        && m_filterEnabled == filterVisible) {
        return;
    }
    m_bandEnabled = bandVisible;
    m_siteEnabled = siteVisible;
    m_filterEnabled = filterVisible;
    restart();
}

void DiversityBandPoller::attachFilter(AetherGateDiversityPanel* panel)
{
    if (!panel)
        return;
    connect(this, &DiversityBandPoller::filterReceived, panel,
            &AetherGateDiversityPanel::applyFilter);
    connect(panel, &AetherGateDiversityPanel::requestFilter, this,
            &DiversityBandPoller::sendFilter);
    // The SITE page's write channel, wired here for the same reason: the
    // applet's constructor keeps the one attachFilter() line it already has.
    connect(this, &DiversityBandPoller::siteReceived, panel,
            &AetherGateDiversityPanel::applySiteReply);
    connect(panel, &AetherGateDiversityPanel::requestSite, this,
            &DiversityBandPoller::sendSite);
    // The SITE page's second read-only route and the FLOW strip's own route,
    // wired here for the same reason: the applet's constructor keeps its one
    // attachFilter() line whatever this poller grows.
    connect(this, &DiversityBandPoller::compassReceived, panel,
            &AetherGateDiversityPanel::applyCompass);
    connect(this, &DiversityBandPoller::digReceived, panel,
            &AetherGateDiversityPanel::applyDig);
    connect(panel, &AetherGateDiversityPanel::requestDig, this,
            &DiversityBandPoller::sendDig);
}

void DiversityBandPoller::restart()
{
    if ((!m_bandEnabled && !m_siteEnabled && !m_filterEnabled) || m_base.isEmpty()
        || !m_net) {
        m_timer->stop();
        return;
    }
    // Start from tick zero so the first poll fetches EVERY route the visible
    // page needs: it has just become visible and has nothing on it. Only one
    // page of a window is ever on screen, so this picks an interval rather
    // than compromising between two.
    m_tick = 0;
    m_timer->start(m_bandEnabled ? kTickMs : (m_filterEnabled ? kFilterTickMs : kSiteTickMs));
    poll();
}

void DiversityBandPoller::poll()
{
    if ((!m_bandEnabled && !m_siteEnabled && !m_filterEnabled) || m_base.isEmpty()
        || !m_net) {
        return;
    }
    // While only SITE is up the timer is already at 1 Hz, so every tick is a
    // slow tick; while BAND is up it runs at 4 Hz and one in four is.
    const bool slowTick = !m_bandEnabled || (m_tick % kSlowEveryTicks) == 0;
    ++m_tick;
    if (m_bandEnabled) {
        fetchSpatial();
        if (slowTick)
            fetchFinder();
    }
    if (m_siteEnabled && slowTick) {
        fetchBeacons();
        fetchCompass();
    }
    if (m_filterEnabled)
        fetchFilter();
}

void DiversityBandPoller::setBandAvailable(bool available)
{
    if (m_bandAvailable == available)
        return;
    m_bandAvailable = available;
    restartBackground();
}

void DiversityBandPoller::restartBackground()
{
    if (!m_bandAvailable || m_base.isEmpty() || !m_net) {
        m_backgroundTimer->stop();
        return;
    }
    // Start from tick zero, same reasoning as restart() above: the first
    // background poll fires at once rather than waiting out a second, so a
    // relaunch (or a window that just got built) does not sit on a blank
    // waterfall and an empty beacon table for a whole tick before either one
    // is asked for.
    m_backgroundTick = 0;
    m_backgroundTimer->start(kBackgroundTickMs);
    backgroundPoll();
}

// The independent half of setBandAvailable(): unlike poll() above, this never
// shares a timer with a page cadence, so the CHAIN window's 2 Hz or a SITE
// page's 1 Hz can never accidentally double one of its ticks into an extra
// background fetch.
void DiversityBandPoller::backgroundPoll()
{
    if (m_base.isEmpty() || !m_net)
        return;
    // /diversity/spatial and /diversity/finder: skip while BAND is actually
    // the page on screen -- poll() already has both routes at 4 Hz/1 Hz then,
    // and firing this too would just be a second request for the same row.
    if (!m_bandEnabled) {
        fetchSpatial();
        fetchFinder();
    }
    // /diversity/beacons and /diversity/compass, once every half minute,
    // regardless of page: this is the B-SITE-1 fix. poll()'s own fetch of the
    // same two routes only runs while SITE is the page actually on screen, so
    // a window that restored onto BAND, FILTER or SLICE -- or is not even
    // open yet -- used to leave the beacon table exactly as blank as it was
    // at the last relaunch until the operator switched to SITE and forced a
    // check. SITE being up already covers this ten times over, so skip it
    // there too rather than doubling the request.
    const bool onHalfMinute = (m_backgroundTick % kBackgroundBeaconEveryTicks) == 0;
    ++m_backgroundTick;
    if (!m_siteEnabled && onHalfMinute) {
        fetchBeacons();
        fetchCompass();
    }
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

// The beacons' other half. Same cadence, same in-flight guard: the fit is
// recomputed from the same results the table is drawn from, so asking faster
// than the table is redrawn could not show anything new.
void DiversityBandPoller::fetchCompass()
{
    if (m_compassInFlight)
        return;
    m_compassInFlight = true;
    QNetworkRequest req{QUrl(m_base + QStringLiteral("/diversity/compass"))};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_compassInFlight = false;
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        emit compassReceived(obj);
    });
}

void DiversityBandPoller::fetchFilter()
{
    if (m_filterInFlight)
        return;
    m_filterInFlight = true;
    QNetworkRequest req{QUrl(m_base + QStringLiteral("/filter"))};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_filterInFlight = false;
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        emit filterReceived(obj);
    });
}

// Deliberately NOT guarded by m_filterInFlight: that guard exists so a slow
// gate cannot have four polls queued against it, and a write the operator just
// made is not a poll. Dropping one would leave a control showing a value the
// gate was never told about.
void DiversityBandPoller::sendFilter(const QString& path, const QUrlQuery& query)
{
    sendWrite(path, query, false);
}

void DiversityBandPoller::sendSite(const QString& path, const QUrlQuery& query)
{
    sendWrite(path, query, true);
}

// No in-flight guard, and no page gate: every call is either a write the
// operator just made or the one status read the window asked for at its own
// cadence, and both are already rate-limited by the thing that called them.
void DiversityBandPoller::sendDig(const QUrlQuery& query)
{
    if (m_base.isEmpty() || !m_net)
        return;
    QUrl url(m_base + QStringLiteral("/diversity/dig"));
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        emit digReceived(obj);
    });
}

void DiversityBandPoller::sendWrite(const QString& path, const QUrlQuery& query,
                                    bool asSite)
{
    if (m_base.isEmpty() || !m_net)
        return;
    QUrl url(m_base + path);
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, asSite] {
        reply->deleteLater();
        QJsonObject obj;
        if (reply->error() == QNetworkReply::NoError)
            parseObject(reply->readAll(), &obj);
        if (asSite)
            emit siteReceived(obj);
        else
            emit filterReceived(obj);
    });
}

} // namespace AetherSDR
