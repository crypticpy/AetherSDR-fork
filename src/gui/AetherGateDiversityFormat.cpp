#include "AetherGateDiversityFormat.h"

#include <QJsonObject>
#include <QObject>

#include <algorithm>
#include <cmath>

namespace AetherSDR {
namespace DiversityFormat {

namespace {

// -20.0 reads as "-20.0", which is the ASCII hyphen the rest of this file's
// numeric formatting already uses -- except the per-source row and the
// status label's lag, which are short and dense enough on screen that the
// true minus sign earns its keep the way it already does in
// AetherClockApplet/ClientCompKnob's dB strings.
QString formatSignedDb(double v)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', 1);
    return QString::number(v, 'f', 1);
}

// Same true-minus-sign convention as formatSignedDb, for an integer (the
// status label's lag_samples).
QString formatSignedInt(int v)
{
    if (v < 0)
        return QStringLiteral("−%1").arg(-v);
    return QString::number(v);
}

} // namespace

QString status(const QJsonObject& d)
{
    if (d.value(QStringLiteral("realigning")).toBool())
        return QObject::tr("realigning…");
    if (!d.value(QStringLiteral("aligned")).toBool())
        return QObject::tr("not aligned");
    const int lag = int(std::lround(d.value(QStringLiteral("lag_samples")).toDouble()));
    return QObject::tr("aligned · lag %1").arg(formatSignedInt(lag));
}

QString statusWorstCasePhrase()
{
    return QStringLiteral("aligned · lag −99999");
}

QString sourceListText(const QJsonObject& s)
{
    const double lo = s.value(QStringLiteral("lo_hz")).toDouble();
    const double hi = s.value(QStringLiteral("hi_hz")).toDouble();
    const double coh = s.value(QStringLiteral("coherence")).toDouble();
    const QString freqPart = std::abs(hi - lo) > 500.0
        ? QStringLiteral("%1–%2").arg(lo / 1.0e6, 0, 'f', 3).arg(hi / 1.0e6, 0, 'f', 3)
        : QString::number((lo + hi) / 2.0 / 1.0e6, 'f', 3);
    return QStringLiteral("%1 MHz   coh %2").arg(freqPart).arg(coh, 0, 'f', 2);
}

QString sourceTooltip(const QJsonObject& s)
{
    const double lo = s.value(QStringLiteral("lo_hz")).toDouble();
    const double hi = s.value(QStringLiteral("hi_hz")).toDouble();
    const int phase = int(std::lround(s.value(QStringLiteral("phase_deg")).toDouble()));
    const double ratio = s.value(QStringLiteral("ratio_db")).toDouble();
    const double coh = s.value(QStringLiteral("coherence")).toDouble();
    return QStringLiteral("%1–%2 MHz · coh %3 · %4° · %5 dB")
        .arg(lo / 1.0e6, 0, 'f', 3)
        .arg(hi / 1.0e6, 0, 'f', 3)
        .arg(coh, 0, 'f', 2)
        .arg(phase)
        .arg(formatSignedDb(ratio));
}

} // namespace DiversityFormat
} // namespace AetherSDR
