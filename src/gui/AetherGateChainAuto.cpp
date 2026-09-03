#include "gui/AetherGateChainAuto.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateChainStage.h"
#include "gui/AetherGateChainStrip.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLayout>

namespace AetherSDR {

namespace {

double num(const QJsonValue& v, double fallback = 0.0)
{
    return v.isDouble() ? v.toDouble() : fallback;
}

ChainAutoHeld parseHeld(const QJsonObject& h)
{
    ChainAutoHeld out;
    out.tool = h.value(QStringLiteral("tool")).toString();
    out.kind = h.value(QStringLiteral("kind")).toString();
    out.why = h.value(QStringLiteral("why")).toString();
    out.since = num(h.value(QStringLiteral("since")));
    const QJsonValue delta = h.value(QStringLiteral("delta_db"));
    out.hasDelta = delta.isDouble();
    out.deltaDb = out.hasDelta ? delta.toDouble() : 0.0;
    return out;
}

ChainAutoEvent parseEvent(const QJsonObject& e)
{
    ChainAutoEvent out;
    out.t = num(e.value(QStringLiteral("t")));
    out.tool = e.value(QStringLiteral("tool")).toString();
    out.kind = e.value(QStringLiteral("kind")).toString();
    out.why = e.value(QStringLiteral("why")).toString();
    out.result = e.value(QStringLiteral("result")).toString();
    const QJsonValue delta = e.value(QStringLiteral("delta_db"));
    out.hasDelta = delta.isDouble();
    out.deltaDb = out.hasDelta ? delta.toDouble() : 0.0;
    return out;
}

QString suffixFor(const QString& id)
{
    QString clean = id;
    clean.replace(QLatin1Char(' '), QLatin1Char('_'));
    return clean;
}

// "+1.8", "-0.9" -- one decimal, always signed, the same convention the rest
// of this window's family uses for a delta.
QString signedDb(double db)
{
    return (db >= 0.0 ? QStringLiteral("+") : QString()) + QString::number(db, 'f', 1);
}

QString timeOf(double epochSeconds, const char* fmt)
{
    return QDateTime::fromMSecsSinceEpoch(qint64(epochSeconds * 1000.0))
        .time()
        .toString(QString::fromLatin1(fmt));
}

// "12:41:07 · squeeze · carrier · kept +1.8 dB · <why>". An error event's
// `why` is already the gate's own error text (governor.py's failed()), so it
// is not repeated after "error: ".
QString eventLine(const ChainAutoEvent& e)
{
    const QString time = timeOf(e.t, "HH:mm:ss");
    if (e.result == QLatin1String("error")) {
        return QStringLiteral("%1 · %2 · %3 · error: %4")
            .arg(time, e.tool, e.kind, e.why);
    }
    QString outcome = e.result;
    if (e.result == QLatin1String("kept") || e.result == QLatin1String("undone")) {
        outcome += QLatin1Char(' ');
        outcome += e.hasDelta ? signedDb(e.deltaDb) + QStringLiteral(" dB")
                              : QStringLiteral("—");
    }
    return QStringLiteral("%1 · %2 · %3 · %4 · %5")
        .arg(time, e.tool, e.kind, outcome, e.why);
}

// "backing off: mains/squeeze until 12:46".
QString backoffLine(const ChainAutoBackoff& b)
{
    return QStringLiteral("backing off: %1/%2 until %3")
        .arg(b.kind, b.tool, timeOf(b.until, "HH:mm"));
}

} // namespace

ChainAutoGovernor chainAutoParseGovernor(const QJsonObject& filter)
{
    ChainAutoGovernor g;
    const QJsonValue v = filter.value(QStringLiteral("governor"));
    if (!v.isObject())
        return g;
    const QJsonObject o = v.toObject();
    g.available = o.value(QStringLiteral("available")).toBool(false);
    g.autoOn = o.value(QStringLiteral("auto")).toBool(false);
    g.state = o.value(QStringLiteral("state")).toString();
    g.why = o.value(QStringLiteral("why")).toString();
    g.settleS = num(o.value(QStringLiteral("settle_s")));
    g.marginDb = num(o.value(QStringLiteral("margin_db")));
    g.spreadDb = num(o.value(QStringLiteral("spread_db")));
    g.error = o.value(QStringLiteral("error")).toString();
    for (const QJsonValue& hv : o.value(QStringLiteral("holding")).toArray())
        g.holding << parseHeld(hv.toObject());
    const QJsonValue pv = o.value(QStringLiteral("pending"));
    if (pv.isObject()) {
        g.hasPending = true;
        g.pending = parseHeld(pv.toObject());
    }
    for (const QJsonValue& ev : o.value(QStringLiteral("events")).toArray())
        g.events << parseEvent(ev.toObject());
    for (const QJsonValue& bv : o.value(QStringLiteral("backoff")).toArray()) {
        const QJsonObject bo = bv.toObject();
        g.backoff << ChainAutoBackoff{bo.value(QStringLiteral("kind")).toString(),
                                      bo.value(QStringLiteral("tool")).toString(),
                                      num(bo.value(QStringLiteral("until")))};
    }
    return g;
}

QString chainAutoRowIdForTool(const QString& tool)
{
    if (tool == QLatin1String("guard"))
        return QStringLiteral("frontend_guard");
    if (tool == QLatin1String("nb"))
        return QStringLiteral("nb");
    if (tool == QLatin1String("mode"))
        return QStringLiteral("combiner");
    if (tool == QLatin1String("squeeze"))
        return QStringLiteral("squeeze");
    return QString();   // "dig": the AUTO CLEAN card only
}

QString chainAutoNoteForStage(const QString& stageId, const ChainAutoGovernor& gov)
{
    if (!gov.available || stageId.isEmpty())
        return QString();
    if (gov.hasPending && chainAutoRowIdForTool(gov.pending.tool) == stageId)
        return QStringLiteral("AUTO · trying · %1").arg(gov.pending.why);
    for (const ChainAutoHeld& h : gov.holding) {
        if (chainAutoRowIdForTool(h.tool) != stageId)
            continue;
        QString note = QStringLiteral("AUTO · %1 · %2").arg(h.kind, h.why);
        if (h.hasDelta)
            note += QStringLiteral(", %1 dB").arg(signedDb(h.deltaDb));
        return note;
    }
    return QString();
}

void chainAutoApplyNotes(AetherGateChainStrip* strip, const ChainAutoGovernor& gov)
{
    if (!strip)
        return;
    static const QStringList kRowIds = {
        QStringLiteral("frontend_guard"), QStringLiteral("nb"),
        QStringLiteral("combiner"), QStringLiteral("squeeze")};
    for (const QString& id : kRowIds) {
        AetherGateChainTile* tile = strip->tile(id);
        if (!tile)
            continue;
        const QString text = chainAutoNoteForStage(id, gov);
        const QString name = QStringLiteral("gateChainAutoNote_") + suffixFor(id);
        auto* note = tile->findChild<QLabel*>(name, Qt::FindDirectChildrenOnly);
        if (!note) {
            if (text.isEmpty())
                continue;
            note = new QLabel(tile);
            note->setObjectName(name);
            note->setAccessibleName(QStringLiteral("Held by AUTO CLEAN"));
            note->setWordWrap(false);
            ThemeManager::instance().applyStyleSheet(note, chainUnderlineStyleSheet());
            tile->layout()->addWidget(note);
        }
        note->setVisible(!text.isEmpty());
        if (text.isEmpty())
            continue;
        const int width = tile->shape() == ChainTileShape::Card ? kChainCardTextWidth
                                                                 : kChainLineTextWidth;
        note->setText(chainFitToWidth(note, text, width));
        note->setToolTip(text);
        note->setAccessibleDescription(text);
    }
}

QStringList chainAutoEventLines(const ChainAutoGovernor& gov, int maxLines)
{
    QStringList lines;
    for (int i = gov.events.size() - 1; i >= 0 && lines.size() < maxLines; --i)
        lines << eventLine(gov.events.at(i));
    for (int i = 0; i < gov.backoff.size() && lines.size() < maxLines; ++i)
        lines << backoffLine(gov.backoff.at(i));
    return lines;
}

QString chainAutoStateLine(const ChainAutoGovernor& gov)
{
    if (!gov.available)
        return QString();
    return QStringLiteral("%1 · %2").arg(gov.state, gov.why);
}

} // namespace AetherSDR
