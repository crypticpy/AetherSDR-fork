#pragma once

// Shared fixtures for the DiversitySessionPage test binaries
// (diversity_session_page_test.cpp -- the tab row and the five cards -- and
// diversity_session_page_next_test.cpp -- the NEXT strip and the frame
// budget): both drive the same START page through the same socket-free
// FakeGate, so the fixture below (constants, the governor/dig payload
// builders, the Bench struct that opens one applet/window per case, and
// the finder helpers for cards/strip/requests) lives here once rather than
// twice. diversity_session_page_test.cpp was 809 lines, over the 800-line
// budget AGENTS.md asks for.

#include "DiversityGateFixture.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityNextStrip.h"
#include "gui/DiversitySessionModel.h"
#include "gui/DiversitySessionPage.h"
#include "gui/DiversityWindow.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>

namespace {

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversityNextStrip;
using AetherSDR::DiversitySessionCard;
using AetherSDR::DiversitySessionModel;
using AetherSDR::DiversityWindow;
using AetherSDR::SessionCopy;
using AetherSDR::sessionStepCopy;

using namespace DiversityGateFixture;

const char* const kPageKey = "DiversityWindowPage";
const char* const kCollapsedKey = "DiversityNextStripCollapsed";
const char* const kPageButtons[] = {
    "diversityWindowPageStart", "diversityWindowPageSlice", "diversityWindowPageBand",
    "diversityWindowPageSite"};

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// What a station that has never opened this window has. Written rather than
// assumed: these cases share one AppSettings and two are about what it keeps.
void forgetEverything()
{
    closedToStart();
    AppSettings::instance().setValue(QLatin1String(kPageKey), QString());
    AppSettings::instance().setValue(QLatin1String(kCollapsedKey), QStringLiteral("True"));
}

// A copy of a fixture with one wire value swapped: what each case needs is the
// SAME site with one thing different, and a fixture that differed in more than
// the field under test could not prove which field the page read.
QByteArray with(const QByteArray& body, const char* from, const char* to)
{
    QByteArray out = body;
    out.replace(from, to);
    return out;
}

// The governor block, docs/DIVERSITY.md's own shape. Every cure below depends
// on it: the model clears every cure model-wide while AUTO CLEAN is off.
QJsonObject governor(bool autoOn, const QString& state = QStringLiteral("watching"),
                     const QString& why = QString())
{
    QJsonObject g;
    g.insert(QStringLiteral("available"), true);
    g.insert(QStringLiteral("auto"), autoOn);
    g.insert(QStringLiteral("state"), state);
    g.insert(QStringLiteral("why"), why);
    g.insert(QStringLiteral("settle_s"), 5.0);
    g.insert(QStringLiteral("margin_db"), 1.0);
    g.insert(QStringLiteral("spread_db"), 2.0);
    g.insert(QStringLiteral("holding"), QJsonArray());
    g.insert(QStringLiteral("pending"), QJsonValue());
    g.insert(QStringLiteral("events"), QJsonArray());
    g.insert(QStringLiteral("backoff"), QJsonArray());
    g.insert(QStringLiteral("error"), QString());
    return g;
}

QByteArray withGovernor(const QByteArray& body, const QJsonObject& gov)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    root.insert(QStringLiteral("governor"), gov);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// The site every case not about the governor runs on: two findings with a
// button, AUTO CLEAN on so the cures exist at all.
QByteArray kindsAuto(bool autoOn = true)
{
    return withGovernor(kDiversityStatusWithKinds, governor(autoOn));
}

// /filter with a talker locked in -- the one thing that makes the STATION step
// done, and the only field of that payload this page reads.
QJsonObject filterWithTalker(int id)
{
    QJsonObject root = QJsonDocument::fromJson(kDiversityFilterStatus).object();
    QJsonObject talker;
    talker.insert(QStringLiteral("enabled"), true);
    talker.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("talker"), talker);
    return root;
}

// Every chore behind us: aligned and tracking, a clean site, no beacon
// frequency in the span (the tuned Hz stays 0), and a talker to be given a
// filter. What the collapsed footer is for.
const QByteArray kAllChoresDone = R"JSON({"available": true, "channels": 2,
    "mode": "track", "source": "combined", "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 1.2}, "updates": 42,
    "talking": true, "talker": {"id": 7, "since_s": 30.0},
    "memory": [{"id": 7, "name": "Ann"}, {"id": 8, "name": "Bo"},
               {"id": 9, "name": "Cy"}, {"id": 10, "name": "Di"}],
    "noise_profile": {"mains_hz": 60.0, "hum_db": 3.0, "harmonics": 0,
                      "impulses_per_s": 0.0, "impulse_db": 0.0, "periodic": [],
                      "seconds": 2.0, "window_s": 2.0, "impulse_window_s": 4.0,
                      "kinds": []},
    "capture": {"active": false, "path": null}})JSON";

const QByteArray kDigIdle = R"({"available": true, "running": false,
    "phase": "idle", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 0.0, "steps": [], "best": {}, "changed": {}})";

const QByteArray kDigRunning = R"({"available": true, "running": true,
    "phase": "searching", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 2.1, "elapsed_s": 72.0, "seconds": 180.0, "remaining_s": 108.0,
    "trials_planned": 24, "trials_done": 9, "steps": [],
    "best": {"post": "v2"}, "changed": {"post": "v2"}})";

const QByteArray kDigDone = R"({"available": true, "running": false,
    "phase": "done", "verdict": "", "error": "", "cancelled": false,
    "gain_db": 4.1, "objective_before": -3.2, "objective_after": 0.9,
    "steps": [{"knob": "nb", "kept": true}],
    "best": {"nb_db": 11.0}, "changed": {"nb_db": 11.0}})";

void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& status)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/align")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError, kDiversityFilterStatus};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

// One applet, one fake gate, one window, opened and torn down the same way
// every time. `fresh` is false only where the case is ABOUT what the window
// remembers across a session -- which forgetEverything() would throw away.
struct Bench {
    FakeGate net;
    AetherGateApplet a{nullptr, &net};
    DiversityWindow* w{nullptr};

    explicit Bench(const QByteArray& status, bool fresh = true)
    {
        if (fresh) {
            forgetEverything();
        } else {
            closedToStart();
        }
        connectGate(a, net, status);
        a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"))->click();
        settle();
        w = a.diversityPanel()->window();
        if (w)
            tick(a);   // /diversity is fetched once before there is a window to feed
    }
    ~Bench()
    {
        if (w)
            w->close();
        settle();
        closedToStart();
    }
};

// Makes a timer go off now: QTimer::timeout carries a QPrivateSignal, but moc
// strips that from the meta-method, and the point is to skip real seconds.
void fire(QTimer* timer)
{
    if (!timer)
        return;
    if (timer->isSingleShot())
        timer->stop();
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
}

// One /diversity/dig poll, without waiting out the window's own cadence.
void digTick(Bench& b, const QByteArray& body)
{
    b.net.routes[QStringLiteral("/diversity/dig")] = {QNetworkReply::NoError, body};
    fire(child<QTimer>(b.w, "diversityWindowDigTimer"));
    settle();
}

DiversitySessionCard* card(DiversityWindow* w, int index)
{
    return w->findChild<DiversitySessionCard*>(
        QStringLiteral("diversityWindowSessionCard%1").arg(index));
}

QLabel* cardBody(DiversityWindow* w, int index)
{
    return w->findChild<QLabel*>(
        QStringLiteral("diversityWindowSessionCard%1Body").arg(index));
}

QLabel* cardState(DiversityWindow* w, int index)
{
    return w->findChild<QLabel*>(
        QStringLiteral("diversityWindowSessionCard%1State").arg(index));
}

QPushButton* cardCure(DiversityWindow* w, int index)
{
    return w->findChild<QPushButton*>(
        QStringLiteral("diversityWindowSessionCard%1Cure").arg(index));
}

DiversityNextStrip* strip(DiversityWindow* w)
{
    return w->findChild<DiversityNextStrip*>(QStringLiteral("diversityWindowNextStrip"));
}

QString nextLine(DiversityWindow* w)
{
    DiversityNextStrip* s = strip(w);
    return s ? s->lineText() : QString();
}

// Every request to one route since `from`, in the order the gate saw them.
QStringList requestsTo(const FakeGate& net, const QString& prefix, int from)
{
    QStringList out;
    for (int i = from; i < net.log.size(); ++i) {
        if (net.log.at(i).startsWith(prefix))
            out << net.log.at(i);
    }
    return out;
}

// The three write routes. A page change is not one of them.
int writes(const FakeGate& net)
{
    return net.count(QStringLiteral("/diversity/set"))
           + net.count(QStringLiteral("/diversity/align"))
           + net.count(QStringLiteral("/diversity/dig"));
}

} // namespace
