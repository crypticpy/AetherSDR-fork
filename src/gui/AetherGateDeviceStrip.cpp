#include "AetherGateDeviceStrip.h"

#include "core/ThemeManager.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// Character for character AetherGateApplet's own kRowLabelStyle: this line
// sits directly under the connection line and has to read as the next row of
// the same applet, not as a strip bolted underneath it. Tokens, not literals,
// so a theme switch repaints it with the rest.
const char* kDeviceLabelStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; }";

// The buttons are the applet's, so they take the canonical toggle dressing
// (gui/Theme.h) rather than a style of their own -- same helper the Diversity
// window's own two-way choices use.
constexpr int kButtonHeight = 20;

const char* kDiversityTip =
    QT_TR_NOOP("Run both tuners as one receiver - off falls back to a single tuner.");
const char* kDiversityLong =
    QT_TR_NOOP("Both tuners, summed. Off drops back to one tuner feeding the "
               "audio and the panadapter -- the way out of diversity without "
               "opening the gate's own page.");

const char* kTunerTip =
    QT_TR_NOOP("Which tuner feeds the receiver while diversity is off.");
const char* kTunerLong =
    QT_TR_NOOP("Which tuner feeds the receiver when diversity is off. Pick one "
               "while the pair is stopped; with both running there is nothing "
               "to choose.");

} // namespace

AetherGateDeviceStrip::AetherGateDeviceStrip(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("gateDeviceStrip"));
    setAccessibleName(tr("Gate device"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    // One line, never wrapped: the gate's label is already a sentence
    // ("RSPduo 2405055D34 - diversity (track)") and a wrapped one would push
    // every row below it down by a line each time the mode changed.
    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("gateDeviceLabel"));
    m_label->setAccessibleName(tr("Device the gate is fronting"));
    m_label->setWordWrap(false);
    ThemeManager::instance().applyStyleSheet(m_label,
                                             QString::fromLatin1(kDeviceLabelStyle));
    root->addWidget(m_label);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    root->addLayout(row);

    m_diversity = new QPushButton(tr("DIVERSITY"), this);
    m_diversity->setObjectName(QStringLiteral("gateDiversityToggle"));
    m_diversity->setAccessibleName(tr("Run both tuners in diversity"));
    m_diversity->setToolTip(tr(kDiversityTip));
    m_diversity->setAccessibleDescription(tr(kDiversityLong));
    m_diversity->setCheckable(true);
    m_diversity->setFixedHeight(kButtonHeight);
    m_diversity->setCursor(Qt::PointingHandCursor);
    applyToggleButtonStyle(m_diversity);
    // clicked(), not toggled(): render() below checks the button back from the
    // gate's last answer, and that must not fire another write.
    connect(m_diversity, &QPushButton::clicked, this, [this] {
        if (m_running)
            sendSet(QStringLiteral("off"), feedTuner());
        else
            sendSet(QStringLiteral("track"), QStringLiteral("combined"));
        render();
    });
    row->addWidget(m_diversity);

    const QStringList labels{tr("A"), tr("B")};
    const QStringList feeds{QStringLiteral("a"), QStringLiteral("b")};
    const QStringList names{QStringLiteral("gateTunerA"), QStringLiteral("gateTunerB")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(labels.at(i), this);
        button->setObjectName(names.at(i));
        button->setAccessibleName(tr("Feed the receiver from tuner %1").arg(labels.at(i)));
        button->setToolTip(tr(kTunerTip));
        button->setAccessibleDescription(tr(kTunerLong));
        button->setCheckable(true);
        button->setFixedHeight(kButtonHeight);
        button->setCursor(Qt::PointingHandCursor);
        applyToggleButtonStyle(button);
        const QString feed = feeds.at(i);
        // Picking a tuner IS leaving diversity: one write says both which mode
        // the gate should be in and which tuner it should be on, so the two can
        // never disagree about what "off" meant.
        connect(button, &QPushButton::clicked, this, [this, feed] {
            sendSet(QStringLiteral("off"), feed);
            render();
        });
        if (i == 0)
            m_tunerA = button;
        else
            m_tunerB = button;
        row->addWidget(button);
    }
    row->addStretch(1);

    render();
}

void AetherGateDeviceStrip::applyDevice(const QJsonValue& device)
{
    const QJsonObject dev = device.toObject();
    m_text = dev.value(QStringLiteral("label")).toString();
    if (m_text.isEmpty()) {
        // A gate that reports a device but no label of its own: the model is
        // the part an operator recognises, so use it rather than the driver.
        m_text = dev.value(QStringLiteral("model")).toString();
    }

    const QJsonObject div = dev.value(QStringLiteral("diversity")).toObject();
    m_capable = div.value(QStringLiteral("capable")).toBool();
    m_running = div.value(QStringLiteral("running")).toBool();
    m_tuner = div.value(QStringLiteral("tuner")).toString().toLower();
    render();
}

void AetherGateDeviceStrip::sendSet(const QString& mode, const QString& feed)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("mode"), mode);
    q.addQueryItem(QStringLiteral("source"), feed);
    q.addQueryItem(QStringLiteral("pan"), feed);
    emit requestDiversitySet(q);
}

QString AetherGateDeviceStrip::feedTuner() const
{
    return m_tuner == QStringLiteral("b") ? QStringLiteral("b") : QStringLiteral("a");
}

void AetherGateDeviceStrip::render()
{
    m_label->setText(tr("device: %1").arg(m_text.isEmpty() ? QStringLiteral("-") : m_text));

    // A single-tuner device has no diversity to switch, and a gate that never
    // said what it has cannot be asked to stop: hide the controls rather than
    // showing a switch that would write to a device with nothing to combine.
    m_diversity->setVisible(m_capable);
    m_tunerA->setVisible(m_capable);
    m_tunerB->setVisible(m_capable);
    if (!m_capable)
        return;

    m_diversity->setChecked(m_running);
    // With both tuners in the sum there is no single tuner selected, so neither
    // button is checked -- and neither is live, because the choice only exists
    // once the pair has stopped.
    m_tunerA->setChecked(!m_running && m_tuner == QStringLiteral("a"));
    m_tunerB->setChecked(!m_running && m_tuner == QStringLiteral("b"));
    m_tunerA->setEnabled(!m_running);
    m_tunerB->setEnabled(!m_running);
}

} // namespace AetherSDR
