#pragma once

// Shared setup helpers for the DiversityWindow FLOW test binaries
// (diversity_flow_test.cpp -- REALIGN/spectrum/CAPTURE/no-scroll -- and
// diversity_flow_dig_test.cpp -- DIG): both bring an applet up through the
// same socket-free FakeGate and open the window the same way, so
// closedToStart()/connectGate()/openButton()/child<T>()/openWindow()/
// fire()/lastRequest() live here once rather than twice.
// diversity_flow_test.cpp was 838 lines, over the 800-line budget
// AGENTS.md asks for.

#include "DiversityGateFixture.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"
#include "gui/AetherGateDiversityPanel.h"
#include "gui/DiversityWindow.h"

#include <QByteArray>
#include <QNetworkReply>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QToolButton>

namespace {

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using AetherSDR::DiversityWindow;

using namespace DiversityGateFixture;

void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

void connectGate(AetherGateApplet& a, FakeGate& net,
                 const QByteArray& status = kDiversityStatusWithKinds)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/diversity/align")] = {QNetworkReply::NoError, status};
    net.routes[QStringLiteral("/filter")] = {QNetworkReply::NoError,
                                             kDiversityFilterStatus};
    net.routes[QStringLiteral("/filter/set")] = {QNetworkReply::NoError,
                                                 kDiversityFilterStatus};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

template <typename T>
T* child(DiversityWindow* w, const char* name)
{
    return w->findChild<T*>(QString::fromLatin1(name));
}

DiversityWindow* openWindow(AetherGateApplet& a)
{
    openButton(a)->click();
    settle();
    DiversityWindow* w = a.diversityPanel()->window();
    if (w)
        tick(a);   // /diversity is fetched once before there is a window to feed
    return w;
}

// Makes a timer go off now. QTimer::timeout carries a QPrivateSignal, so it
// cannot be emitted from outside the class -- but moc strips that from the
// meta-method, and the point of every use below is to skip real seconds rather
// than to wait them out in a test.
void fire(QTimer* timer)
{
    if (!timer)
        return;
    const bool once = timer->isSingleShot();
    if (once)
        timer->stop();
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
}

QString lastRequest(const FakeGate& net, const QString& prefix)
{
    for (int i = net.log.size() - 1; i >= 0; --i) {
        if (net.log.at(i).startsWith(prefix))
            return net.log.at(i);
    }
    return QString();
}

} // namespace
