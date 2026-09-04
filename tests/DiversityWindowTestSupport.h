#pragma once

// Shared setup helpers for the DiversityWindow test binaries
// (diversity_window_test.cpp and diversity_window_talkers_test.cpp): both
// bring an applet up to "gate present, diversity live" through the same
// socket-free FakeGate and open the window from the same sidebar button, so
// the three functions below live here once rather than twice.
//
// diversity_window_test.cpp was 850 lines, over the 800-line budget
// AGENTS.md asks for; talker-table/lock/timeline/event/old-gate cases moved
// out to diversity_window_talkers_test.cpp, and this header is what keeps
// them from duplicating closedToStart()/connectGate()/openButton().

#include "DiversityGateFixture.h"
#include "core/AppSettings.h"
#include "gui/AetherGateApplet.h"

#include <QByteArray>
#include <QNetworkReply>
#include <QPushButton>
#include <QString>

namespace {

using AetherSDR::AetherGateApplet;
using AetherSDR::AppSettings;
using namespace DiversityGateFixture;

// AppSettings is one process-wide cache, and the window's own visibility is
// persisted in it -- so every case starts from a known closed state rather
// than from whatever the previous case left behind.
void closedToStart()
{
    AppSettings::instance().setValue(QStringLiteral("DiversityWindowVisible"),
                                     QStringLiteral("False"));
}

// Brings an applet up to "gate present, diversity live" with `diversity` as
// the /diversity body.
void connectGate(AetherGateApplet& a, FakeGate& net, const QByteArray& diversity)
{
    net.routes[QStringLiteral("/status")] = {QNetworkReply::NoError, kStatus};
    net.routes[QStringLiteral("/device")] = {QNetworkReply::NoError, kDevice};
    net.routes[QStringLiteral("/diversity")] = {QNetworkReply::NoError, diversity};
    net.routes[QStringLiteral("/diversity/set")] = {QNetworkReply::NoError, diversity};
    a.setRadioAddress(QStringLiteral("10.0.0.5"));
    settle();
    settle();
    settle();
}

QPushButton* openButton(AetherGateApplet& a)
{
    return a.findChild<QPushButton*>(QStringLiteral("gateDiversityOpenWindowButton"));
}

} // namespace
