// AetherGateApplet's diversity and CHAIN plumbing: every route in the section
// that is not /status or /device, and the two windows opened from it.
//
// The controls and the presentation moved out of the applet long ago --
// AetherGateDiversityPanel owns the sidebar section, AetherGateChainWindow the
// filter chain, DiversityBandPoller the two cadences neither /status nor
// /diversity fits. What stayed behind is the transport: the applet still owns
// every socket the section uses, so each of those objects asks for a write by
// emitting a signal and the GET that answers it lives here. /diversity and
// /diversity/map are polled here too, deliberately NOT through get(): an older
// gate 404s both, and that says nothing about whether the GATE answered, so a
// failure hides the section instead of counting toward presence.
//
// The one request in the file that never reaches the gate is the tune: the
// gate has no tune verb, so a click on the BAND page moves AetherSDR's own
// active slice (and, when it lands outside the span, its panadapter).
//
// Defined in its own file rather than in AetherGateApplet.cpp for the reason
// DiversityWindowBand.cpp and DiversityWindowPanels.cpp exist: these members
// belong to AetherGateApplet, and AetherGateApplet.cpp is at the file-size
// budget AGENTS.md asks for.

#include "gui/AetherGateApplet.h"

#include "gui/AetherGateAppletShared.h"
#include "gui/AetherGateChainWindow.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityBandPoller.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QDialog>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace AetherSDR {

// /diversity/map is heavier than the rest of the section (up to 256 floats
// twice over) and changes slowly compared to phase/ratio, so it is read on
// its own, coarser cadence rather than every /diversity poll.
static constexpr int kDiversityMapRefreshPolls = 2;

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
            m_diversityAvailable = false;
            updateBandPoll();
            return;
        }
        QJsonObject obj;
        const bool json = GateApplet::parseObject(reply->readAll(), &obj);
        m_diversityPanel->applyDiversity(obj, json);
        // The BAND page's background poll (DiversityBandPoller::
        // setBandAvailable()) reads this straight off the wire rather than
        // off the panel: the panel's own notion of "available" also folds in
        // whether it has anything to show, and this one has to keep polling
        // (once the window has been opened at least once and so exists to
        // apply a spatial/finder reply to -- AetherGateDiversityPanel::
        // applySpatial()/applyFinder() are no-ops before that) whether or
        // not the operator is looking at the window right now.
        m_diversityAvailable =
            json && obj.value(QStringLiteral("available")).toBool();
        updateBandPoll();

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

// The BAND page's FOREGROUND poll (4 Hz) runs only while that page is on
// screen: an open window showing SLICE, SITE or FILTER costs zero of it. The
// BACKGROUND poll (1 Hz, see the call to setBandAvailable() below) is looser
// on purpose -- it runs whenever the gate is present, dual-tuner, and the
// window has been opened at least once (so there is a waterfall and a FINDER
// table for it to feed; before that first open, m_diversityPanel->window() is
// null and every reply setBandAvailable() would provoke has nowhere to go) --
// whether or not the window is showing BAND, or showing anything at all right
// now. Called from setPresent()/setRadioAddress(), from the panel's
// bandPollChanged() (also emitted the moment the window is first built), and
// from pollDiversity() on every /diversity poll, so both rates react at once
// rather than up to a second later.
void AetherGateApplet::updateBandPoll()
{
    m_bandPoller->setBaseUrl(baseUrl());
    // /filter has one customer, the CHAIN window (the Diversity window's
    // FILTER tab, its other one, is retired): on screen is the reason to poll
    // it, and off screen the reason to stop.
    const bool wantFilter = m_chainWindow && m_chainWindow->isVisible();
    m_bandPoller->setPages(m_present && m_diversityPanel->wantsBandPoll(),
                           m_present && m_diversityPanel->wantsSitePoll(),
                           m_present && wantFilter);
    m_bandPoller->setBandAvailable(m_present && m_diversityAvailable
                                   && m_diversityPanel->window() != nullptr);
}

AetherGateChainWindow* AetherGateApplet::chainWindow() const
{
    return m_chainWindow.data();
}

void AetherGateApplet::setAudioEngine(AudioEngine* audio)
{
    m_audio = audio;
    if (m_chainWindow)
        m_chainWindow->setAudioEngine(audio);
}

// Built once and then kept, exactly as AetherGateDiversityPanel::toggleWindow()
// keeps the Diversity window: rebuilding it would throw away the selected
// stage, and the strip would flash empty every time the operator glanced at it.
void AetherGateApplet::toggleChainWindow()
{
    if (!m_chainWindow) {
        m_chainWindow = new AetherGateChainWindow(this);
        m_chainWindow->setPresent(m_present);
        m_chainWindow->setAudioEngine(m_audio);
        connect(m_chainWindow, &AetherGateChainWindow::requestWrite, this,
                &AetherGateApplet::onChainRequestWrite);
        // The window redraws from /filter and from nothing else -- the same
        // object the Diversity window's STATION card is fed, off the same poller, so the two views
        // can never disagree about what the receiver is doing.
        connect(m_bandPoller, &DiversityBandPoller::filterReceived, m_chainWindow,
                &AetherGateChainWindow::applyFilter);
        // Closing it with the title bar's own button has to stop the poll as
        // surely as pressing the door again does.
        connect(m_chainWindow, &QDialog::finished, this,
                &AetherGateApplet::updateBandPoll);
        // The door back: the FRONT END card's OPEN PANEL button asks for
        // this applet, the same way toggleChainWindow() itself brings the
        // chain window forward a few lines below -- show it, then raise and
        // activate whatever top-level window is hosting it.
        connect(m_chainWindow, &AetherGateChainWindow::openPanelRequested, this, [this] {
            setVisible(true);
            raise();
            if (QWidget* top = window()) {
                top->raise();
                top->activateWindow();
            }
        });
    }
    const bool wantVisible = !m_chainWindow->isVisible();
    if (!wantVisible) {
        m_chainWindow->hide();
        updateBandPoll();
        return;
    }
    m_chainWindow->show();
    m_chainWindow->raise();
    m_chainWindow->activateWindow();
    updateBandPoll();
}

// The gate's own route and the gate's own query, sent verbatim on the applet's
// one transport. sendFilter()'s reply is the status object the window redraws
// from, so the write and the read-back after it are one request rather than
// two -- and the window changes only when that reply says it should.
void AetherGateApplet::onChainRequestWrite(QString route, QUrlQuery query)
{
    if (route.isEmpty())
        return;
    m_bandPoller->sendFilter(route, query);
}

// The one request in this section that never reaches the gate. The gate has no
// tune verb (docs/DIVERSITY.md, "Limits and known gaps"), so a click on the
// BAND page tunes AetherSDR's own active slice -- the same slice a click on the
// panadapter would move, through the same slice frequency write, so the radio
// cannot end up disagreeing with the app about where it is.
//
// "Active" is defined exactly as MainWindow::activeSlice() defines it, minus
// its cache: the first slice flagged active, falling back to the first slice
// there is. With no model wired (the applet can be driven address-first) there
// is nothing to tune and the click is dropped rather than guessed at.
SliceModel* AetherGateApplet::activeSlice() const
{
    if (!m_model)
        return nullptr;
    const QList<SliceModel*> slices = m_model->slices();
    for (SliceModel* slice : slices) {
        if (slice && slice->isActive())
            return slice;
    }
    return slices.isEmpty() ? nullptr : slices.first();
}

void AetherGateApplet::onDiversityRequestTune(double hz)
{
    SliceModel* target = (hz > 0.0) ? activeSlice() : nullptr;
    if (!target)
        return;
    const double mhz = hz / 1.0e6;
    // Asked BEFORE the tune: the pan has moved by the time it returns.
    const PanadapterModel* pan = m_model->panadapter(target->panId());
    const bool outOfSpan = !pan || !pan->spanContainsMhz(mhz);
    // A refusal (locked slice, implausible target) must not move the display.
    if (!m_model->tuneSliceForCat(target, mhz))
        return;
    // Then move the pan OURSELVES. tuneSliceForCat's out-of-span arm sends
    // `slice tune <id> <mhz>` without autopan=0, asking the RADIO to recentre
    // -- and Aether-gate has no autopan: it took the tune, the slice read 14.1,
    // and the pan model kept the 80 m centre and span labels with new FFT rows
    // painted under the old scale (B22, 2026-09-03). The GUI's cross-band tune
    // does not rely on autopan either -- applyTuneRequest ->
    // revealFrequencyIfNeeded -> applyTuneCenteringWrite ends in this same call,
    // which is what puts `display pan set <pan> center=` on the wire and
    // re-labels the waterfall rows. Centre only: the span is not ours to move.
    if (outOfSpan)
        m_model->requestPanCenter(target->panId(), mhz);
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
            GateApplet::parseObject(reply->readAll(), &obj);
        m_diversityPanel->applyMap(obj);
    });
}

// One path for every diversity write, same shape as sendDeviceSet(): the
// read-back arrives with the reply, so a control always ends up showing what
// the gate took rather than what we asked for. An empty query is a route that
// takes none (/diversity/align), not a write with nothing in it.
void AetherGateApplet::sendDiversityWrite(const QString& path,
                                          const QUrlQuery& query,
                                          bool requirePresent)
{
    const QString base = baseUrl();
    if (base.isEmpty() || (requirePresent && !m_present))
        return;
    QUrl url(base + path);
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(4000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QJsonObject obj;
        const bool json = GateApplet::parseObject(reply->readAll(), &obj);
        m_diversityPanel->applyDiversity(obj, json);
    });
}

void AetherGateApplet::onDiversityRequestSet(QUrlQuery query)
{
    sendDiversityWrite(QStringLiteral("/diversity/set"), query, true);
}

void AetherGateApplet::onDiversityRequestAlign()
{
    sendDiversityWrite(QStringLiteral("/diversity/align"), {}, true);
}

// The "Hear A only" hold's forced resume — see
// AetherGateDiversityPanel::restoreCompareHold()'s own comment for why this
// is the one diversity write that is NOT gated on m_present: setPresent(false)
// itself needs this to still go out (the gate must not be left stuck in
// "off" just because THIS poll is what noticed it was gone), so this builds
// its own request, gated only on baseUrl() being non-empty.
void AetherGateApplet::onDiversityRequestCompareRestore(QUrlQuery query)
{
    sendDiversityWrite(QStringLiteral("/diversity/set"), query, false);
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
        const bool json = GateApplet::parseObject(reply->readAll(), &obj);
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
    sendFireAndForget(QStringLiteral("/diversity/memory/clear"), {}, 4000);
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
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    q.addQueryItem(QStringLiteral("name"),
                   QString::fromLatin1(QUrl::toPercentEncoding(name)));
    sendFireAndForget(QStringLiteral("/diversity/memory/name"), q, 4000);
}

} // namespace AetherSDR
