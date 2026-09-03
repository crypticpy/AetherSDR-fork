// DiversityEventLog -- the poll-to-poll diff itself.
//
// Split out of DiversityWindowEvents.cpp, which builds the TALKERS table and
// the EVENTS list this feeds. The two have nothing in common but a header: one
// is widgets and the other is a pure function of two structs, and keeping them
// in one translation unit was only ever the accident of having written them on
// the same night. The class comment in DiversityWindowEvents.h has the rules
// this file implements.

#include "gui/DiversityWindowEvents.h"

#include <QCoreApplication>
#include <QLatin1String>
#include <QString>
#include <QStringList>

#include <cmath>

namespace AetherSDR {

namespace {

// Which leg the operator is hearing, in the words the chain row's own buttons
// use -- "A", not "a", so the line and the button agree.
QString hearWord(const QString& wire)
{
    if (wire == QLatin1String("a"))
        return QStringLiteral("A");
    if (wire == QLatin1String("b"))
        return QStringLiteral("B");
    return wire;
}



// A talker in the middle of a sentence: their name if the gate has one, their
// number if not, and "somebody" when there is no talker at all -- the split
// line has to read as English on both sides of the arrow even when one side
// was nobody.
QString talkerWord(bool have, int id, const QString& name)
{
    if (!have)
        return QCoreApplication::translate("DiversityEventLog", "somebody");
    return name.isEmpty() ? QStringLiteral("#%1").arg(id) : name;
}

} // namespace

QString DiversityEventLog::talkerTag(int id, const QString& name)
{
    if (name.isEmpty())
        return QStringLiteral("#%1").arg(id);
    return QStringLiteral("#%1 \"%2\"").arg(QString::number(id), name);
}

QString DiversityEventLog::shortDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return QStringLiteral("—");
    if (seconds < 60.0)
        return QStringLiteral("%1 s").arg(qint64(std::llround(seconds)));
    if (seconds < 3600.0)
        return QStringLiteral("%1 m").arg(qint64(seconds / 60.0));
    return QStringLiteral("%1 h").arg(qint64(seconds / 3600.0));
}

QString DiversityEventLog::memoryClearedLine()
{
    return QCoreApplication::translate("DiversityEventLog", "memory cleared");
}

QString DiversityEventLog::captureSavedLine(const QString& basename)
{
    return QCoreApplication::translate("DiversityEventLog", "capture saved %1")
        .arg(basename);
}

void DiversityEventLog::reset()
{
    m_have = false;
    m_prev = DiversitySnapshot{};
}

QStringList DiversityEventLog::apply(const DiversitySnapshot& s)
{
    QStringList lines;
    const DiversitySnapshot prev = m_prev;
    const bool had = m_have;
    m_have = true;
    m_prev = s;

    if (!had)
        return lines;

    // Presence is a barrier -- see this class's header comment.
    if (s.present != prev.present) {
        lines << (s.present
                      ? QCoreApplication::translate("DiversityEventLog", "gate back")
                      : QCoreApplication::translate("DiversityEventLog", "gate lost"));
        return lines;
    }
    if (!s.present || !s.available || !prev.available)
        return lines;

    // --- who is talking ---------------------------------------------------
    const bool talkerChanged = (s.haveTalker != prev.haveTalker)
                               || (s.haveTalker && s.talkerId != prev.talkerId);
    if (talkerChanged && prev.haveTalker) {
        lines << QCoreApplication::translate("DiversityEventLog", "%1 ended after %2")
                     .arg(talkerTag(prev.talkerId, prev.talkerName),
                          shortDuration(prev.talkerSinceS));
    }
    if (talkerChanged && s.haveTalker) {
        if (s.haveTalkerWeight) {
            lines << QCoreApplication::translate("DiversityEventLog",
                                                 "%1 started (phase %2°, %3 dB)")
                         .arg(talkerTag(s.talkerId, s.talkerName),
                              QString::asprintf("%.0f", s.talkerPhaseDeg),
                              QString::asprintf("%+.1f", s.talkerRatioDb));
        } else {
            lines << QCoreApplication::translate("DiversityEventLog", "%1 started")
                         .arg(talkerTag(s.talkerId, s.talkerName));
        }
    }

    // --- the gate split one over into two voices --------------------------
    // Said out loud because it is the one talker change the operator cannot
    // hear happening: the frequency did not move, nobody stopped talking, and
    // the number beside the marker quietly became a different number.
    if (s.haveVoiceSplits && prev.haveVoiceSplits && s.voiceSplits > prev.voiceSplits) {
        lines << QCoreApplication::translate("DiversityEventLog",
                                             "voice split: not %1's voice → %2")
                     .arg(talkerWord(prev.haveTalker, prev.talkerId, prev.talkerName),
                          talkerWord(s.haveTalker, s.talkerId, s.talkerName));
    }

    // --- new entries in memory -------------------------------------------
    for (int id : s.memoryIds) {
        if (!prev.memoryIds.contains(id)) {
            lines << QCoreApplication::translate("DiversityEventLog",
                                                 "new talker #%1 remembered")
                         .arg(id);
        }
    }

    // --- station focus ----------------------------------------------------
    const bool focusChanged = (s.haveFocus != prev.haveFocus)
                              || (s.haveFocus && s.focusId != prev.focusId);
    if (focusChanged) {
        lines << (s.haveFocus
                      ? QCoreApplication::translate("DiversityEventLog", "locked on %1")
                            .arg(talkerTag(s.focusId, s.focusName))
                      : QCoreApplication::translate("DiversityEventLog", "lock released"));
    }
    if (s.haveFocus && s.focusNulling && !(prev.haveFocus && prev.focusNulling)
            && s.haveTalker) {
        lines << QCoreApplication::translate("DiversityEventLog",
                                             "nulling %1 (not the locked station)")
                     .arg(talkerTag(s.talkerId, s.talkerName));
    }

    // --- steady QRM -------------------------------------------------------
    if (s.haveSteadyQrm && prev.haveSteadyQrm && s.steadyQrm != prev.steadyQrm) {
        lines << (s.steadyQrm
                      ? QCoreApplication::translate("DiversityEventLog",
                                                    "steady carrier nulled")
                      : QCoreApplication::translate("DiversityEventLog",
                                                    "steady carrier gone"));
    }

    // --- chain ------------------------------------------------------------
    if (!s.mode.isEmpty() && s.mode != prev.mode) {
        lines << QCoreApplication::translate("DiversityEventLog", "mode → %1")
                     .arg(s.mode);
    }
    if (!s.hear.isEmpty() && s.hear != prev.hear) {
        lines << QCoreApplication::translate("DiversityEventLog", "hear → %1")
                     .arg(hearWord(s.hear));
    }

    // --- alignment --------------------------------------------------------
    if (s.realigning && !prev.realigning)
        lines << QCoreApplication::translate("DiversityEventLog", "realigning…");
    else if (!s.realigning && prev.realigning && s.aligned) {
        lines << (s.haveLag
                      ? QCoreApplication::translate("DiversityEventLog", "aligned, lag %1")
                            .arg(qint64(std::llround(s.lagSamples)))
                      : QCoreApplication::translate("DiversityEventLog", "aligned"));
    }

    return lines;
}

} // namespace AetherSDR
