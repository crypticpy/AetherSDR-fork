#pragma once

// Shared fixture for the B25 AUTO CLEAN test binaries
// (aether_gate_chain_auto_test.cpp -- the "held by AUTO" note, name
// hygiene, the toggle write, and the render/no-scroll check -- and
// aether_gate_chain_auto_inspector_test.cpp -- the AUTO CLEAN card's own
// state+events inspector): both build the same auto_clean/squeeze chain
// row and governor block, so bringUp()/feedDevice()/autoCleanRow()/
// squeezeRow()/withAutoCleanChain()/held()/event()/backoff()/ruledOut()/
// governor()/localEpoch() live here once rather than twice.
// aether_gate_chain_auto_test.cpp was 816 lines, over the 800-line budget
// AGENTS.md asks for.
//
// The auto_clean chain row and the "squeeze" row it needs to show a held
// squeeze note are built locally here, the same way squeeze's own
// withSqueezeChainRow() is -- B25 landed after AetherGateChainFixture.h was
// written, and the task that adds it may not change a helper an earlier
// test already depends on.

#include "AetherGateChainFixture.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTime>

namespace {

using namespace AetherGateChainFixture;

constexpr int kTabChain = 0;

void bringUp(AetherGateChainWindow* w)
{
    w->resize(1120, 820);
    w->setCurrentTab(kTabChain);
    settle();
    w->grab();
    settle();
}

// One /device body with a "frontend" key, applied the way
// aether_gate_chain_frontend_test.cpp's own feedDevice() is -- only the
// render test below needs it, to put the FRONT END card's synthetic GUARD
// row on screen so a held "guard" tool has a row to hold.
void feedDevice(AetherGateChainWindow* w, const QJsonObject& fe)
{
    w->applyDevice(QJsonDocument::fromJson(deviceWithFrontend(fe)).object());
    settle();
}

// The auto_clean row, at the head of chain[] the way chainstatus.py puts it
// -- kind "toggle", action /diversity/set?auto=<the OTHER state>, because the
// action is always the write that would flip it.
QJsonObject autoCleanRow(bool on)
{
    QJsonObject row;
    row.insert(QStringLiteral("id"), QStringLiteral("auto_clean"));
    row.insert(QStringLiteral("name"), QStringLiteral("AUTO CLEAN"));
    row.insert(QStringLiteral("kind"), QStringLiteral("toggle"));
    row.insert(QStringLiteral("enabled"), on);
    row.insert(QStringLiteral("detail"), on ? QStringLiteral("holding 1")
                                            : QStringLiteral("off"));
    QJsonObject action;
    action.insert(QStringLiteral("label"), on ? QStringLiteral("OFF") : QStringLiteral("ON"));
    action.insert(QStringLiteral("route"), QStringLiteral("/diversity/set"));
    action.insert(QStringLiteral("query"),
                 on ? QStringLiteral("auto=off") : QStringLiteral("auto=on"));
    row.insert(QStringLiteral("action"), action);
    return row;
}

// One more chain[] row, shaped the way chainstatus.py's own _squeeze_row()
// shapes it -- the same row aether_gate_chain_squeeze_test.cpp's
// withSqueezeChainRow() builds, copied rather than shared because that
// helper is local to that file for the same reason this one is local here.
QJsonObject squeezeRow()
{
    QJsonObject row;
    row.insert(QStringLiteral("id"), QStringLiteral("squeeze"));
    row.insert(QStringLiteral("name"), QStringLiteral("SQUEEZE"));
    row.insert(QStringLiteral("kind"), QStringLiteral("value"));
    row.insert(QStringLiteral("enabled"), true);
    row.insert(QStringLiteral("detail"), QStringLiteral("null · 1450 Hz"));
    QJsonObject action;
    action.insert(QStringLiteral("label"), QStringLiteral("RELEASE"));
    action.insert(QStringLiteral("route"), QStringLiteral("/diversity/set"));
    action.insert(QStringLiteral("query"), QStringLiteral("squeeze=off"));
    row.insert(QStringLiteral("action"), action);
    return row;
}

// kChainFullFilter with auto_clean prepended and squeeze appended -- so a
// held squeeze note has a card to land on and the toggle test has its row.
QByteArray withAutoCleanChain(bool autoOn, const QJsonObject& governor = QJsonObject())
{
    QJsonObject root = QJsonDocument::fromJson(kChainFullFilter).object();
    QJsonArray chain = root.value(QStringLiteral("chain")).toArray();
    chain.insert(0, autoCleanRow(autoOn));
    chain.append(squeezeRow());
    root.insert(QStringLiteral("chain"), chain);
    if (!governor.isEmpty())
        root.insert(QStringLiteral("governor"), governor);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// One row of governor.holding[]/pending -- {tool, kind, why, since, delta_db}.
// `hasDelta` false omits delta_db (JSON null), the "measuring, no score yet"
// case chainAutoNoteForStage() must not print a "0.0 dB" for.
QJsonObject held(const QString& tool, const QString& kind, const QString& why,
                 double since, bool hasDelta, double deltaDb = 0.0)
{
    QJsonObject h;
    h.insert(QStringLiteral("tool"), tool);
    h.insert(QStringLiteral("kind"), kind);
    h.insert(QStringLiteral("why"), why);
    h.insert(QStringLiteral("since"), since);
    h.insert(QStringLiteral("delta_db"), hasDelta ? QJsonValue(deltaDb) : QJsonValue());
    return h;
}

// One row of governor.events[] -- held's shape plus `t` and `result`. `wall`
// is the real epoch-seconds companion core/governor.py's events now carry
// alongside `t`'s monotonic one; omitted (hasWall false) reproduces an older
// gate that never sends it.
QJsonObject event(double t, const QString& tool, const QString& kind, const QString& why,
                  const QString& result, bool hasDelta, double deltaDb = 0.0,
                  bool hasWall = false, double wall = 0.0)
{
    QJsonObject e = held(tool, kind, why, 0.0, hasDelta, deltaDb);
    e.insert(QStringLiteral("t"), t);
    e.insert(QStringLiteral("result"), result);
    if (hasWall)
        e.insert(QStringLiteral("wall"), wall);
    return e;
}

// `until_wall` is core/governor.py's real epoch-seconds companion to
// `until`'s monotonic one; omitted (hasUntilWall false) reproduces an older
// gate that never sends it.
QJsonObject backoff(const QString& kind, const QString& tool, double until,
                    bool hasUntilWall = false, double untilWall = 0.0)
{
    QJsonObject b;
    b.insert(QStringLiteral("kind"), kind);
    b.insert(QStringLiteral("tool"), tool);
    b.insert(QStringLiteral("until"), until);
    if (hasUntilWall)
        b.insert(QStringLiteral("until_wall"), untilWall);
    return b;
}

// One row of governor.ruled_out[].
QJsonObject ruledOut(const QString& tool, const QString& why)
{
    QJsonObject r;
    r.insert(QStringLiteral("tool"), tool);
    r.insert(QStringLiteral("why"), why);
    return r;
}

// The whole governor block, /diversity/governor's own shape (docs/DIVERSITY
// .md "AUTO CLEAN: the chain decides").
QJsonObject governor(bool autoOn, const QString& state, const QString& why,
                     const QJsonArray& holding = {}, const QJsonValue& pending = QJsonValue(),
                     const QJsonArray& events = {}, const QJsonArray& backoffs = {},
                     const QJsonArray& ruledOutRows = {},
                     const QString& objectiveSource = QString(),
                     const QString& error = QString())
{
    QJsonObject g;
    g.insert(QStringLiteral("available"), true);
    g.insert(QStringLiteral("auto"), autoOn);
    g.insert(QStringLiteral("state"), state);
    g.insert(QStringLiteral("why"), why);
    g.insert(QStringLiteral("settle_s"), 5.0);
    g.insert(QStringLiteral("margin_db"), 1.0);
    g.insert(QStringLiteral("spread_db"), 2.0);
    g.insert(QStringLiteral("objective_source"), objectiveSource);
    g.insert(QStringLiteral("holding"), holding);
    g.insert(QStringLiteral("pending"), pending);
    g.insert(QStringLiteral("events"), events);
    g.insert(QStringLiteral("ruled_out"), ruledOutRows);
    g.insert(QStringLiteral("backoff"), backoffs);
    g.insert(QStringLiteral("error"), error);
    return g;
}

// 12:41:07 UTC-relative epoch seconds so the event-line tests' expected
// "HH:mm:ss" text is deterministic regardless of the machine's zone: built
// from QDateTime::currentDateTime()'s own date at a fixed local time, the
// same trick a local-time round-trip needs.
double localEpoch(int h, int m, int s)
{
    QDateTime dt = QDateTime::currentDateTime();
    dt.setTime(QTime(h, m, s));
    return dt.toMSecsSinceEpoch() / 1000.0;
}

} // namespace
