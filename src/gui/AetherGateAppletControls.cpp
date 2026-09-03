// AetherGateApplet's device controls: the rows the gate builds out of what the
// attached device actually reports, and the one write path behind them.
//
// A gate fronting an RSPdx offers an antenna port, a bias-T, two notches, HDR
// mode and an AGC setpoint; one fronting an RTL stick offers almost none of
// that. So there is no fixed set of widgets here -- /device answers with a
// list of Soapy ArgInfo descriptions and this file turns each one into the
// control its type asks for (BOOL a checkbox, INT a spin box, FLOAT a double
// spin box, an option list a combo, anything else a line edit), rebuilding
// only when the SHAPE of that list changes so a value arriving on the poll
// never yanks a widget out from under the operator's cursor.
//
// Defined in its own file rather than in AetherGateApplet.cpp for the reason
// DiversityWindowBand.cpp and DiversityWindowPanels.cpp exist: these members
// belong to AetherGateApplet, and AetherGateApplet.cpp is at the file-size
// budget AGENTS.md asks for.

#include "gui/AetherGateApplet.h"

#include "gui/AetherGateAppletShared.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

// Numeric bounds used only when the gate reports none for a setting. Wide on
// purpose: a guessed range clamps in BOTH directions — a write outside it is
// capped before it reaches the device, and a read-back outside it displays as
// the clamp instead of the value the device holds.
static constexpr int kUnboundedInt = 1000000;
static constexpr double kUnboundedDouble = 1.0e9;

namespace {

// Reads the gate's {"min","max","step"} for a setting. False when the gate
// reported none (an older gate, or a driver that gave Soapy's 0..0 default).
bool readRange(const QJsonObject& setting, double* lo, double* hi, double* step)
{
    const QJsonObject r = setting.value(QStringLiteral("range")).toObject();
    if (r.isEmpty())
        return false;
    *lo = r.value(QStringLiteral("min")).toDouble();
    *hi = r.value(QStringLiteral("max")).toDouble();
    *step = r.value(QStringLiteral("step")).toDouble();
    return *hi > *lo;
}

int decimalsForStep(double step)
{
    if (step <= 0.0)
        return 3;
    return std::clamp(int(std::ceil(-std::log10(step))), 0, 6);
}

} // namespace

// One path for every write. A radio drop mid-edit leaves the widgets alive
// under the operator's cursor for a moment; without the guard their next
// change would build a request from an empty base and send it nowhere useful.
void AetherGateApplet::sendDeviceSet(const QUrlQuery& query)
{
    const QString base = baseUrl();
    if (base.isEmpty() || !m_present)
        return;
    QUrl url(base + QStringLiteral("/device/set"));
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setTransferTimeout(4000);          // the gate settles 0.35 s before reading back
    QNetworkReply* reply = m_net->get(req);
    // The read-back arrives with the reply, so the control always ends up
    // showing what the DEVICE took rather than what we asked for.
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QJsonObject obj;
        const bool json = GateApplet::parseObject(reply->readAll(), &obj);
        applyDeviceControls(obj, json);
    });
}

void AetherGateApplet::applyDeviceControls(const QJsonObject& dev, bool isJson)
{
    m_deviceFetched = true;
    m_pollsSinceDevice = 0;
    if (!isJson) {
        // An older gate has no /device route and falls through to its web
        // panel: HTTP 200, HTML.  That is a gate without device controls, not
        // a device without settings — say which, rather than showing nothing.
        m_deviceBox->setVisible(false);
        m_deviceHint->setVisible(true);
        m_controlsFingerprint.clear();
        return;
    }
    m_deviceHint->setVisible(false);
    buildDeviceControls(dev);
}

void AetherGateApplet::buildDeviceControls(const QJsonObject& dev)
{
    // Fingerprint the SHAPE (which controls exist, and what kind each is), not
    // the values — rebuilding widgets under the operator's cursor every time a
    // value changed would make the panel unusable.
    QStringList shape;
    const QJsonObject ant = dev.value(QStringLiteral("antenna")).toObject();
    if (!ant.isEmpty())
        shape << QStringLiteral("antenna");
    const QJsonArray settings = dev.value(QStringLiteral("settings")).toArray();
    for (const QJsonValue& v : settings) {
        const QJsonObject so = v.toObject();
        shape << QStringLiteral("%1:%2:%3:%4")
                     .arg(so.value(QStringLiteral("key")).toString(),
                          so.value(QStringLiteral("type")).toString())
                     .arg(so.value(QStringLiteral("options")).toArray().size())
                     .arg(QString::fromUtf8(QJsonDocument(
                              so.value(QStringLiteral("range")).toObject())
                              .toJson(QJsonDocument::Compact)));
    }
    const QString fingerprint = shape.join(QLatin1Char('|'));

    if (fingerprint != m_controlsFingerprint) {
        m_controlsFingerprint = fingerprint;
        m_settingWidgets.clear();
        m_antenna = nullptr;
        while (m_deviceForm->count() > 0) {
            QLayoutItem* item = m_deviceForm->takeAt(0);
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }

        if (!ant.isEmpty()) {
            m_antenna = new QComboBox(m_deviceBox);
            m_antenna->setObjectName(QStringLiteral("gateAntennaCombo"));
            for (const QJsonValue& o : ant.value(QStringLiteral("options")).toArray())
                m_antenna->addItem(o.toString());
            connect(m_antenna, &QComboBox::currentTextChanged, this,
                    [this](const QString& text) {
                        QUrlQuery q;
                        q.addQueryItem(QStringLiteral("antenna"), text);
                        sendDeviceSet(q);
                    });
            auto* label = new QLabel(tr("Antenna"), m_deviceBox);
            GateApplet::styleRowLabel(label);
            m_deviceForm->addRow(label, m_antenna);
        }

        for (const QJsonValue& v : settings) {
            const QJsonObject so = v.toObject();
            const QString key = so.value(QStringLiteral("key")).toString();
            const QString name = so.value(QStringLiteral("name")).toString(key);
            const QString type = so.value(QStringLiteral("type")).toString();
            const QJsonArray options = so.value(QStringLiteral("options")).toArray();

            auto push = [this, key](const QString& value) {
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("key"), key);
                q.addQueryItem(QStringLiteral("value"), value);
                sendDeviceSet(q);
            };

            // The widget follows the Soapy ArgInfo type the gate relays:
            // "0" BOOL, "1" INT, "2" FLOAT, anything else a string. A setting
            // with an option list is a choice whatever its type says.
            double lo = 0.0, hi = 0.0, step = 0.0;
            const bool bounded = readRange(so, &lo, &hi, &step);
            QWidget* w = nullptr;
            if (!options.isEmpty()) {
                auto* combo = new QComboBox(m_deviceBox);
                for (const QJsonValue& o : options)
                    combo->addItem(o.toString());
                connect(combo, &QComboBox::currentTextChanged, this, push);
                w = combo;
            } else if (type == QLatin1String("0")) {
                auto* check = new QCheckBox(m_deviceBox);
                connect(check, &QCheckBox::toggled, this, [push](bool on) {
                    push(on ? QStringLiteral("true") : QStringLiteral("false"));
                });
                w = check;
            } else if (type == QLatin1String("1")) {
                auto* spin = new QSpinBox(m_deviceBox);
                if (bounded) {
                    spin->setRange(int(std::lround(lo)), int(std::lround(hi)));
                    if (step >= 1.0)
                        spin->setSingleStep(int(std::lround(step)));
                } else {
                    spin->setRange(-kUnboundedInt, kUnboundedInt);
                }
                spin->setKeyboardTracking(false);   // one write per committed edit
                connect(spin, &QSpinBox::valueChanged, this, [push](int v) {
                    push(QString::number(v));
                });
                w = spin;
            } else if (type == QLatin1String("2")) {
                auto* spin = new QDoubleSpinBox(m_deviceBox);
                spin->setDecimals(decimalsForStep(bounded ? step : 0.0));
                if (bounded) {
                    spin->setRange(lo, hi);
                    if (step > 0.0)
                        spin->setSingleStep(step);
                } else {
                    spin->setRange(-kUnboundedDouble, kUnboundedDouble);
                }
                spin->setKeyboardTracking(false);
                connect(spin, &QDoubleSpinBox::valueChanged, this, [push](double v) {
                    push(QString::number(v, 'g', 10));
                });
                w = spin;
            } else {
                auto* edit = new QLineEdit(m_deviceBox);
                connect(edit, &QLineEdit::editingFinished, this, [push, edit] {
                    push(edit->text());
                });
                w = edit;
            }
            w->setObjectName(QStringLiteral("gateSetting:") + key);
            m_settingWidgets.insert(key, w);
            auto* label = new QLabel(name, m_deviceBox);
            GateApplet::styleRowLabel(label);
            m_deviceForm->addRow(label, w);
        }
        m_deviceBox->setVisible(!shape.isEmpty());
    }

    // Values, every time — blocked so a refresh never re-sends what it reads,
    // and skipped on a control the operator is in the middle of editing.
    if (m_antenna && !ant.isEmpty()) {
        const QSignalBlocker block(m_antenna);
        const int idx = m_antenna->findText(ant.value(QStringLiteral("value")).toString());
        if (idx >= 0)
            m_antenna->setCurrentIndex(idx);
    }
    for (const QJsonValue& v : settings) {
        const QJsonObject so = v.toObject();
        QWidget* w = m_settingWidgets.value(so.value(QStringLiteral("key")).toString());
        if (!w || w->hasFocus())
            continue;
        const QString value = so.value(QStringLiteral("value")).toString();
        const QSignalBlocker block(w);
        if (auto* combo = qobject_cast<QComboBox*>(w)) {
            const int idx = combo->findText(value);
            if (idx >= 0)
                combo->setCurrentIndex(idx);
        } else if (auto* check = qobject_cast<QCheckBox*>(w)) {
            check->setChecked(value == QLatin1String("true"));
        } else if (auto* spin = qobject_cast<QSpinBox*>(w)) {
            spin->setValue(value.toInt());
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(w)) {
            dspin->setValue(value.toDouble());
        } else if (auto* edit = qobject_cast<QLineEdit*>(w)) {
            edit->setText(value);
        }
    }
}

} // namespace AetherSDR
