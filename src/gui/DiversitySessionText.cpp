// The fixed operator copy for DiversitySessionModel -- every "gives"/"when"
// line for the five steps in one file, split out of DiversitySessionModel.cpp
// for the same reason DiversityFlowStripAuto.cpp is split out of
// DiversityFlowStrip.cpp: a reviewer auditing what the window SAYS should
// not have to read how it decides what to say.
//
// RECEIVER, BAND, SITE NOISE and LISTEN carry their prose forward almost
// verbatim from the Phase 3a plan's §1 (PAIR READY / BEACONS / NOISE
// PROFILE / LISTEN), retitled to R2.1's names -- the physics did not change,
// only the grouping into loops. STATION is new in R2.1 and has no §1
// precedent; its lines are written to the same register, drawing on
// docs/DIVERSITY.md's TALKERS and FILTER PAGE sections for the facts.
//
// Every line here is asserted <= 92 characters by
// diversity_session_model_test's everyCopyLineFitsNinetyTwoChars -- the
// budget the Phase 3a plan measured the 1120 px window against (see its
// "No-scroll / 1120 width" risk list). No line below wraps with "\n"; a
// wrapped QLabel is the #1 way this window has grown a scrollbar before.

#include "gui/DiversitySessionModel.h"

namespace AetherSDR {

SessionCopy sessionStepCopy(int stepId)
{
    switch (stepId) {
    case DiversitySessionModel::StepReceiver:
        return SessionCopy{
            QStringLiteral("RECEIVER"),
            {QStringLiteral("Two loops lined up sample-for-sample, one weight solved on "
                            "whoever is talking,"),
             QStringLiteral("and that combined output in your ears. Nothing below this "
                            "reads true until it is.")},
            QStringLiteral("Once when you sit down; again after any gate restart — "
                           "the gate comes back mode off.")};
    case DiversitySessionModel::StepSiteNoise:
        return SessionCopy{
            QStringLiteral("SITE NOISE"),
            {QStringLiteral("The shape of your own noise floor named: mains hum and "
                            "harmonics, impulses per"),
             QStringLiteral("second, periodic modulation, tones — each with the one "
                            "control that acts on it.")},
            QStringLiteral("It runs by itself. Read it when you sit down and whenever the "
                           "receiver sounds worse.")};
    case DiversitySessionModel::StepBand:
        return SessionCopy{
            QStringLiteral("BAND"),
            {QStringLiteral("Eighteen transmitters of known power and known bearing on a "
                            "three-minute rota:"),
             QStringLiteral("which way the band is open, how far down the power steps you "
                            "hear, and A against B.")},
            QStringLiteral("When you sit down and when the band changes: 20-10 m, 3 min "
                           "off the station per band.")};
    case DiversitySessionModel::StepStation:
        return SessionCopy{
            QStringLiteral("STATION"),
            {QStringLiteral("Who is on frequency, remembered by voice, and the filter that "
                            "fits their signal —"),
             QStringLiteral("so DIG OUT and AUTO CLEAN have someone to measure against "
                            "instead of silence.")},
            QStringLiteral("Once a voice is locked in; resets when the talker changes or "
                           "you retune off them.")};
    case DiversitySessionModel::StepListen:
        return SessionCopy{
            QStringLiteral("LISTEN"),
            {QStringLiteral("BAND says where to be — direction-coloured waterfall, "
                            "FINDER's ranked conversations."),
             QStringLiteral("SLICE says who you have — talker memory, the live "
                            "weight, two minutes of A/B/OUT.")},
            QStringLiteral("Now. The four steps above were so that this one means "
                           "something.")};
    default:
        break;
    }
    return SessionCopy{};
}

QString sessionPairModeExplain()
{
    return QStringLiteral(
        "track follows the talker — null steers away from the noise");
}

QStringList sessionAllFixedCopyLines()
{
    QStringList lines;
    for (int id = 0; id < DiversitySessionModel::StepCount; ++id) {
        const SessionCopy copy = sessionStepCopy(id);
        lines << copy.gives[0] << copy.gives[1] << copy.when;
    }
    lines << sessionPairModeExplain();
    return lines;
}

} // namespace AetherSDR
