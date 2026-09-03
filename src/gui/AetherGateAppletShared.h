#pragma once

// The handful of things AetherGateApplet's three translation units share.
//
// AetherGateApplet.cpp outgrew the file-size budget AGENTS.md asks for, so the
// ArgInfo-typed device controls moved to AetherGateAppletControls.cpp and the
// diversity/chain plumbing to AetherGateAppletDiversity.cpp -- both still
// definitions of AetherGateApplet's own members, the same way
// DiversityWindowBand.cpp is a second translation unit for DiversityWindow.
// Two of that file's private helpers were used by more than one of the three
// pieces, and a private helper kept in three copies is the one that drifts,
// so they moved here rather than being duplicated.

#include "core/ThemeManager.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QString>

namespace AetherSDR {
namespace GateApplet {

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
inline constexpr char kRowLabelStyle[] =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; }";

inline void styleRowLabel(QLabel* label)
{
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kRowLabelStyle));
}

// True when a body is a JSON object — the only shape any gate route answers
// with. An old gate falls through to its web panel (HTML, HTTP 200) on a route
// it does not know, and a stray web server on the port answers the same way,
// so "the request succeeded" on its own says nothing about what answered.
inline bool parseObject(const QByteArray& body, QJsonObject* out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

} // namespace GateApplet
} // namespace AetherSDR
