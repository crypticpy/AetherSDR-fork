#include "gui/AetherGateChainBypass.h"

#include "gui/Theme.h"

#include <QJsonObject>

namespace AetherSDR {

AetherGateChainHearRawButton::AetherGateChainHearRawButton(QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("aetherGateChainHearRawButton"));
    setAccessibleName(tr("Hear the raw slice while held"));
    setCursor(Qt::PointingHandCursor);
    setAutoRepeat(false);
    setFixedHeight(22);
    // Same family the SQUEEZE controls on the VISUAL tab use
    // (AetherGateChainVisual.cpp) -- a caution/armed action, the tribe
    // Theme.h's own convention comment names for exactly this kind of
    // control.
    applyToggleButtonStyle(this, ToggleTribe::Warning);
    connect(this, &QPushButton::pressed, this, &AetherGateChainHearRawButton::onPressed);
    connect(this, &QPushButton::released, this, &AetherGateChainHearRawButton::onReleased);
    refreshFace();
}

void AetherGateChainHearRawButton::onPressed()
{
    if (m_held)
        return;
    m_held = true;
    sendBypass(true);
    refreshFace();
}

void AetherGateChainHearRawButton::onReleased()
{
    if (!m_held)
        return;
    m_held = false;
    sendBypass(false);
    refreshFace();
}

void AetherGateChainHearRawButton::sendBypass(bool on)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("bypass"), on ? QStringLiteral("on") : QStringLiteral("off"));
    emit requestWrite(QStringLiteral("/filter/set"), q);
}

void AetherGateChainHearRawButton::releaseIfHeld()
{
    if (!m_held)
        return;
    m_held = false;
    sendBypass(false);
    refreshFace();
}

void AetherGateChainHearRawButton::applyFilter(const QJsonObject& filter)
{
    m_gateHasBypass = filter.contains(QStringLiteral("bypass"));
    m_gateBypassed = filter.value(QStringLiteral("bypass")).toBool();
    refreshFace();
}

void AetherGateChainHearRawButton::setPresent(bool present)
{
    if (m_present == present)
        return;
    m_present = present;
    if (!present) {
        releaseIfHeld();
        m_gateHasBypass = false;
        m_gateBypassed = false;
    }
    refreshFace();
}

// The four states, in the order a caller would ask about them: is it on
// screen at all, is the operator holding it down right now, has something
// else (the SLICE FILTER row's own IN/BYPASS toggle) already bypassed the
// chain, and otherwise the ordinary face.
void AetherGateChainHearRawButton::refreshFace()
{
    setVisible(m_present && m_gateHasBypass);
    const QString holdTip =
        tr("Hold to hear the slice with the whole chain out of circuit.");
    if (m_held) {
        setText(tr("RAW · release to hear the chain"));
        setToolTip(holdTip);
        setAccessibleDescription(holdTip);
        setEnabled(true);
        return;
    }
    if (m_gateBypassed) {
        const QString tip = tr("Press IN on the SLICE FILTER row to put the chain back.");
        setText(tr("CHAIN IS BYPASSED"));
        setToolTip(tip);
        setAccessibleDescription(tip);
        setEnabled(false);
        return;
    }
    setText(tr("HEAR RAW"));
    setToolTip(holdTip);
    setAccessibleDescription(holdTip);
    setEnabled(m_present);
}

} // namespace AetherSDR
