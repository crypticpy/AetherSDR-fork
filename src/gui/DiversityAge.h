#pragma once

// Shared "how long ago" wording for every remembered measurement on the
// Diversity window (AGENTS.md, "Keep what the station learned"): the home
// station does not move, so a value the gate measured once stays useful and
// is drawn with its AGE rather than blanked. Rule for every surface that
// uses this: a stored value is drawn in the ordinary tone with its age; only
// a value that was never measured draws an em dash -- that dash is this
// header's caller's job, not this header's, since only the caller knows
// whether the gate sent the key at all.
//
// Four bands, no seconds and no colour at any age (design decision: a stale
// value is stale, not wrong, and the operator decides):
//
//   diversityAgeText(seconds)      -> "just now" / "N min ago" /
//                                      "N h ago" / "N d ago"
//   diversityAgeSince(epoch, now)  -> the same, from two wall stamps
//
// Replaces the three private ageText()/ageSince()/sinceText() copies this
// window grew independently -- DiversityBeaconPanel.cpp, DiversityBeacon
// Controls.cpp and DiversitySessionModel.cpp all had their own, each a
// slightly different set of bands. This is the one copy.

#include <QCoreApplication>
#include <QString>

#include <algorithm>

namespace AetherSDR {

// Floor, not round: the boundary is where the band actually changes, not
// where a fractional value rounds early. 59 s reads "just now", 60 reads
// "1 min ago"; 3599 s reads "59 min ago", 3600 reads "1 h ago"; 86399 s
// reads "23 h ago", 86400 reads "1 d ago".
inline QString diversityAgeText(qint64 secs)
{
    secs = std::max<qint64>(0, secs);
    if (secs < 60)
        return QCoreApplication::translate("DiversityAge", "just now");
    if (secs < 3600)
        return QCoreApplication::translate("DiversityAge", "%1 min ago").arg(secs / 60);
    if (secs < 86400)
        return QCoreApplication::translate("DiversityAge", "%1 h ago").arg(secs / 3600);
    return QCoreApplication::translate("DiversityAge", "%1 d ago").arg(secs / 86400);
}

// From an absolute epoch stamp and the wall clock to read it against. A
// stamp in the future -- clock skew, a payload built a moment before it was
// sent -- reads as "just now" rather than as a negative number nothing
// measured.
inline QString diversityAgeSince(qint64 epochSecs, qint64 nowSecs)
{
    return diversityAgeText(nowSecs - epochSecs);
}

} // namespace AetherSDR
