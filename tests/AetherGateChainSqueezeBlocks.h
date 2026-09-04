// Squeeze blocks off /filter, shared by the CHAIN/VISUAL squeeze tests: the
// builders used to live at the top of aether_gate_chain_squeeze_test.cpp and
// moved here when that file reached its 800-line budget. Same wording, same
// fields -- see the comments on each for what core/squeeze.py's status() sends.
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace AetherGateChainFixture {

// The "squeeze" object off /filter, built field by field rather than as a raw
// JSON literal so each test states exactly what it is asking for. Every field
// core/squeeze.py's status() answers is here, even the ones this app does not
// read yet (`reason`, `phase_deg`, `ratio_db`, `scope`) -- a body missing a
// field the gate does send would be a fixture bug, not a smaller one.
inline QJsonObject squeezeOffBlock()
{
    QJsonObject sq;
    sq.insert(QStringLiteral("hz"), QJsonValue());
    sq.insert(QStringLiteral("width_hz"), QJsonValue());
    sq.insert(QStringLiteral("held"), false);
    sq.insert(QStringLiteral("reason"), QJsonValue());
    sq.insert(QStringLiteral("tool"), QJsonValue());
    sq.insert(QStringLiteral("why"), QString());
    sq.insert(QStringLiteral("phase_deg"), QJsonValue());
    sq.insert(QStringLiteral("ratio_db"), QJsonValue());
    sq.insert(QStringLiteral("coherence"), QJsonValue());
    sq.insert(QStringLiteral("depth_db"), QJsonValue());
    sq.insert(QStringLiteral("scope"), QStringLiteral("slice"));
    sq.insert(QStringLiteral("target"), QJsonValue());
    sq.insert(QStringLiteral("comb"), QJsonValue());
    sq.insert(QStringLiteral("since"), QJsonValue());
    return sq;
}

// Armed: a target is configured, the gate is still listening for enough
// coherence to commit to a tool. `why` is the gate's own sentence for that --
// see core/squeeze.py's set_squeeze()/observe() for the wording this copies.
inline QJsonObject squeezeArmedSignalBlock(double hz, double widthHz)
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("hz"), hz);
    sq.insert(QStringLiteral("width_hz"), widthHz);
    sq.insert(QStringLiteral("held"), false);
    sq.insert(QStringLiteral("why"), QStringLiteral("listening for 0.50 coherence"));
    sq.insert(QStringLiteral("coherence"), 0.22);
    sq.insert(QStringLiteral("target"), QStringLiteral("signal"));
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

// Held, one signal, the NULL tool. `why`'s wording is core/squeeze.py's own,
// verbatim: f"coherence {coh:.2f} — nulled in {n} bins".
inline QJsonObject squeezeHeldSignalBlock(double hz, double widthHz)
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("hz"), hz);
    sq.insert(QStringLiteral("width_hz"), widthHz);
    sq.insert(QStringLiteral("held"), true);
    sq.insert(QStringLiteral("tool"), QStringLiteral("null"));
    sq.insert(QStringLiteral("why"), QStringLiteral("coherence 0.62 — nulled in 5 bins"));
    sq.insert(QStringLiteral("coherence"), 0.62);
    sq.insert(QStringLiteral("depth_db"), -18.4);
    sq.insert(QStringLiteral("target"), QStringLiteral("signal"));
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

// Held, a comb, the NOTCH tool -- the case the gate falls back to when the
// coherence is real but not one direction. `teeth_in_band` is SIGNED, and
// negative here on purpose: this is the one test in the file that would pass
// with the abs() in DiversityFilterPanelSqueeze.cpp missing or backwards if
// it did not check the SIGNED input against the POSITIVE panel axis.
inline QJsonObject squeezeHeldCombBlock()
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("held"), true);
    sq.insert(QStringLiteral("tool"), QStringLiteral("notch"));
    sq.insert(QStringLiteral("why"),
             QStringLiteral("coherence 0.40 — not one direction; notched 4 teeth"));
    sq.insert(QStringLiteral("coherence"), 0.40);
    sq.insert(QStringLiteral("depth_db"), -12.0);
    sq.insert(QStringLiteral("target"), QStringLiteral("comb"));
    QJsonObject comb;
    comb.insert(QStringLiteral("spacing_hz"), 200.0);
    comb.insert(QStringLiteral("offset_hz"), 30.0);
    comb.insert(QStringLiteral("teeth_seen"), 4);
    comb.insert(QStringLiteral("teeth_in_band"),
               QJsonArray{-230.0, -430.0, -630.0, -830.0});
    sq.insert(QStringLiteral("comb"), comb);
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

// visualFilter()'s body with `squeeze` inserted (and `mode` overridden, when
// asked) -- the one shape none of the fixture's existing builders make, added
// here rather than in AetherGateChainFixture.h per the task's own file list.
inline QByteArray withSqueeze(const QByteArray& body, const QJsonObject& squeeze,
                       const QString& mode = QString())
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    root.insert(QStringLiteral("squeeze"), squeeze);
    if (!mode.isEmpty())
        root.insert(QStringLiteral("mode"), mode);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// One more chain[] row, shaped the way chainstatus.py's own _squeeze_row()
// shapes it -- id "squeeze", kind "value" -- so a click on the bracket has a
// card to jump to. AetherGateChainWindow::jumpToStage() returns without
// moving if the strip has no tile for the id, so the door test needs this.
inline QByteArray withSqueezeChainRow(const QByteArray& body)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    QJsonArray chain = root.value(QStringLiteral("chain")).toArray();
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
    chain.append(row);
    root.insert(QStringLiteral("chain"), chain);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Armed on a comb the gate has not found yet: squeeze=comb was asked for,
// core/squeeze.py's set_comb_auto() is feeding its CombDetector, `reason` is
// its "no comb found" and every comb number is still null.
inline QJsonObject squeezeArmedCombBlock()
{
    QJsonObject sq = squeezeOffBlock();
    sq.insert(QStringLiteral("held"), false);
    sq.insert(QStringLiteral("reason"), QStringLiteral("no comb found"));
    sq.insert(QStringLiteral("target"), QStringLiteral("comb"));
    QJsonObject comb;
    comb.insert(QStringLiteral("spacing_hz"), QJsonValue());
    comb.insert(QStringLiteral("offset_hz"), QJsonValue());
    comb.insert(QStringLiteral("teeth_seen"), 0);
    comb.insert(QStringLiteral("teeth_in_band"), QJsonArray());
    comb.insert(QStringLiteral("coherence"), QJsonValue());
    sq.insert(QStringLiteral("comb"), comb);
    sq.insert(QStringLiteral("since"), 1234.5);
    return sq;
}

} // namespace AetherGateChainFixture
