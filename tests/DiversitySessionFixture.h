#pragma once

// Canned payloads for diversity_session_model_test, in the same spirit as
// tests/DiversityGateFixture.h (the diversity window's own fixture) but
// without any of that header's fake-transport machinery: DiversitySession
// Model::apply() takes five QJsonObjects directly and has no network of its
// own to fake, so this header is nothing but small, parameterised builders
// for those five shapes. Every builder defaults to "this tick is fine", so a
// test only has to name the one thing it is exercising.
//
// tests/DiversityGateFixture.h is at its own line budget and is not touched
// by this file or by anything that includes it.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace DiversitySessionFixture {

// --- shared small pieces ---------------------------------------------------

struct MemoryRow {
    int id;
    QString name; // empty -> the gate's own "unnamed" (JSON null)
};

inline QJsonObject makeMemoryRow(const MemoryRow& row)
{
    QJsonObject o;
    o["id"] = row.id;
    o["name"] = row.name.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(row.name);
    return o;
}

inline QJsonArray makeMemory(const QVector<MemoryRow>& rows)
{
    QJsonArray arr;
    for (const MemoryRow& row : rows)
        arr.append(makeMemoryRow(row));
    return arr;
}

// One noise_profile.kinds[] row. hasAction false is a finding with nothing
// to do about it (action: null); hasAction true always carries a route and
// query the gate composed, never one the window would have to build.
inline QJsonObject makeKind(const QString& kind, bool hasAction, bool active)
{
    QJsonObject o;
    o["kind"] = kind;
    o["label"] = kind;
    o["detail"] = QString();
    o["db"] = 10.0;
    o["window_s"] = 2.0;
    if (hasAction) {
        QJsonObject action;
        action["label"] = QStringLiteral("BLANK");
        action["route"] = QStringLiteral("/diversity/set");
        action["query"] = QStringLiteral("nb=on");
        o["action"] = action;
        o["why"] = QJsonValue(QJsonValue::Null);
    } else {
        o["action"] = QJsonValue(QJsonValue::Null);
        o["why"] = QStringLiteral("nothing to do");
    }
    o["active"] = active;
    return o;
}

inline QJsonObject makeGovernor(bool autoOn, const QString& holdTool = QString(),
                                const QString& holdKind = QString(),
                                const QString& holdWhy = QString())
{
    QJsonObject gov;
    gov["auto"] = autoOn;
    QJsonArray holding;
    if (!holdTool.isEmpty()) {
        QJsonObject h;
        h["tool"] = holdTool;
        h["kind"] = holdKind;
        h["why"] = holdWhy;
        holding.append(h);
    }
    gov["holding"] = holding;
    return gov;
}

// --- /diversity --------------------------------------------------------

// The full /diversity payload. Every argument defaults to "this tick is
// fine": aligned, tracking, hearing the combined output, no talker yet, an
// empty (clean) noise profile, AUTO CLEAN on with nothing held.
// The trailing five params are the gate's new align_held/align_note/
// align_retry_s keys (haveAlignExtras defaults to false, which omits all
// three from the payload -- an old gate that has never heard of them).
// haveAlignRetry gates align_retry_s alone, since the gate sends align_held
// and align_note together but align_retry_s only while nothing is locked.
inline QJsonObject makeDiversity(bool available = true, const QString& mode = QStringLiteral("track"),
                                 const QString& source = QStringLiteral("combined"),
                                 bool aligned = true, bool realigning = false, bool haveTalker = false,
                                 int talkerId = -1, const QVector<MemoryRow>& memory = QVector<MemoryRow>(),
                                 bool haveNoiseProfile = true,
                                 const QVector<QJsonObject>& kinds = QVector<QJsonObject>(),
                                 bool governorAuto = true, const QString& holdTool = QString(),
                                 const QString& holdKind = QString(), const QString& holdWhy = QString(),
                                 bool talking = false, double outDb = 0.0,
                                 bool haveAlignExtras = false, bool alignHeld = false,
                                 const QString& alignNote = QString(), bool haveAlignRetry = false,
                                 double alignRetryS = 0.0)
{
    QJsonObject o;
    o["available"] = available;
    if (!available)
        return o;

    o["mode"] = mode;
    o["source"] = source;
    o["aligned"] = aligned;
    o["realigning"] = realigning;
    if (haveAlignExtras) {
        o["align_held"] = alignHeld;
        o["align_note"] = alignNote;
        if (haveAlignRetry)
            o["align_retry_s"] = alignRetryS;
    }

    if (haveTalker) {
        QJsonObject t;
        t["id"] = talkerId;
        t["since_s"] = 4.0;
        o["talker"] = t;
    }

    o["memory"] = makeMemory(memory);

    if (haveNoiseProfile) {
        QJsonObject profile;
        QJsonArray arr;
        for (const QJsonObject& k : kinds)
            arr.append(k);
        profile["kinds"] = arr;
        o["noise_profile"] = profile;
    }

    o["governor"] = makeGovernor(governorAuto, holdTool, holdKind, holdWhy);

    o["talking"] = talking;
    QJsonObject snr;
    snr["a"] = 0.0;
    snr["b"] = 0.0;
    snr["out"] = outDb;
    o["snr_db"] = snr;
    return o;
}

// --- /filter -------------------------------------------------------------

inline QJsonObject makeFilter(bool haveTalkerMatch = false, int talkerId = -1, double lowHz = 300.0,
                              double highHz = 2700.0)
{
    QJsonObject o;
    o["low_hz"] = lowHz;
    o["high_hz"] = highHz;
    if (haveTalkerMatch) {
        QJsonObject t;
        t["enabled"] = true;
        t["id"] = talkerId;
        o["talker"] = t;
    }
    return o;
}

// --- /diversity/dig --------------------------------------------------------

// objective_before/after are deliberately far from gain_db's own value:
// digStateQuotesTheGateOnly checks digSummary() never recomputes a gain from
// them.
inline QJsonObject makeDig(bool available, bool running, const QString& phase, double gainDb,
                           const QJsonObject& changed = QJsonObject(), bool cancelled = false)
{
    QJsonObject o;
    o["available"] = available;
    if (!available)
        return o;
    o["running"] = running;
    o["phase"] = phase;
    o["gain_db"] = gainDb;
    o["objective_before"] = 0.538;
    o["objective_after"] = 0.286;
    o["changed"] = changed;
    o["cancelled"] = cancelled;
    return o;
}

// --- /diversity/beacons ------------------------------------------------

inline QJsonObject makePropagationRow(double bandHz, int heard, int of, double bestW, double updated)
{
    QJsonObject o;
    o["band_hz"] = bandHz;
    o["heard"] = heard;
    o["of"] = of;
    o["best_w"] = bestW;
    o["updated"] = updated;
    return o;
}

inline QJsonObject makeBeacons(bool available, const QVector<QJsonObject>& propagation = QVector<QJsonObject>())
{
    QJsonObject o;
    o["available"] = available;
    QJsonArray arr;
    for (const QJsonObject& row : propagation)
        arr.append(row);
    o["propagation"] = arr;
    return o;
}

// --- /diversity/compass ------------------------------------------------

// DiversitySessionModel stores this payload but does not yet derive
// anything from it (no step reads a bearing); an empty object is every
// test's whole need of it.
inline QJsonObject makeCompass()
{
    return QJsonObject();
}

// Amateur-band frequencies the tests tune to, matching
// DiversitySessionModel's own private band table.
constexpr double kHz20m = 14100000.0;  // has a beacon frequency
constexpr double kHz15m = 21150000.0;  // has a beacon frequency
constexpr double kHz40m = 7100000.0;   // no beacon frequency -- BAND is n/a

} // namespace DiversitySessionFixture
