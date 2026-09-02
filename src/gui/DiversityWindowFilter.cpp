// The Diversity window's FILTER page: everything that happens to the audio
// AFTER the combiner has decided what to add to what.
//
// SLICE is about the weight, BAND about the span, SITE about the station. None
// of them can answer the question an operator asks more often than any of the
// three -- "why does this sound like that?" -- because the answer is almost
// never the combiner. It is a passband 300 Hz narrower than you think, a sharp
// filter ringing on a 49 Hz skirt, an automatic notch chewing at a vowel, an
// AGC decay short enough to breathe between syllables, or a blanker removing
// four percent of the audio to kill a fence that stopped an hour ago. Every one
// of those is visible in one status object and invisible in every other page.
//
// So this page is the gate's slice filter, drawn and driven: the response curve
// with the passband over it (DiversityFilterPanel), and four columns of
// controls under it (DiversityFilterControls). What lives HERE is only the two
// seams -- how the page is wired into the window's page stack, and how one
// /filter status object becomes what the widgets show.
//
// Two classes in one file for the reason DiversityWindowPanels.cpp already
// holds DiversitySnrMeter beside four DiversityWindow members: both halves are
// the same seam, and DiversityFilterControls.cpp -- which builds thirty
// controls and explains each of them to somebody who has never met an audio
// peaking filter -- is at the file-size budget AGENTS.md asks for on its own.

#include "gui/DiversityWindow.h"

#include "gui/DiversityFilterControls.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QButtonGroup>
#include <QAbstractButton>
#include <QCheckBox>
#include <QFrame>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolButton>

#include <cmath>

namespace AetherSDR {

namespace {

// How long a refusal from the gate stays on the status line. Long enough to
// read a sentence, short enough that it cannot be mistaken for a permanent
// state of the page.
constexpr int kTransientMs = 5000;

constexpr int kNotchClearButtonHeight = 18;

QString emDash()
{
    return QStringLiteral("—");
}

QString signedDb(double v, int decimals)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', decimals);
    return QStringLiteral("+%1").arg(v, 0, 'f', decimals);
}

bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

int jsonInt(const QJsonObject& obj, const char* key, int fallback)
{
    double v = 0.0;
    return jsonNumber(obj, key, &v) ? int(std::lround(v)) : fallback;
}

QString jsonHz(const QJsonObject& obj, const char* key)
{
    double v = 0.0;
    if (!jsonNumber(obj, key, &v))
        return emDash();
    return QString::number(qint64(std::llround(v)));
}

QString kiloHertz(const QJsonObject& obj, const char* key)
{
    double v = 0.0;
    if (!jsonNumber(obj, key, &v))
        return emDash();
    return QStringLiteral("%1 kHz").arg(v / 1000.0, 0, 'f', 0);
}

} // namespace

// --------------------------------------------------------------------------
// The window's half: one more page in the stack
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildFilterPage()
{
    m_filter = new DiversityFilterControls;
    // Signal-to-signal, exactly like every other write this window makes: the
    // page does not know there is a gate, only that it has asked for something.
    connect(m_filter, &DiversityFilterControls::requestFilter, this,
            &DiversityWindow::requestFilter);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("diversityWindowFilterScroll"));
    scroll->setWidget(m_filter);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

bool DiversityWindow::filterPageVisible() const
{
    return m_pages && m_pageFilterButton && m_pageFilterButton->isChecked();
}

void DiversityWindow::applyFilter(const QJsonObject& filter)
{
    if (m_filter)
        m_filter->applyStatus(filter);
}

void DiversityWindow::clearFilterReadouts()
{
    if (m_filter)
        m_filter->clear();
}

// --------------------------------------------------------------------------
// The page's half: one status object becomes what the widgets show
// --------------------------------------------------------------------------

void DiversityFilterControls::writeSpin(QSpinBox* spin, int value)
{
    // A spin box the operator is in the middle of is not written at all -- not
    // even its remembered gate value, because the value the gate last reported
    // is exactly what the pending edit is about to disagree with.
    if (!spin || spin->hasFocus())
        return;
    spin->setProperty("gateValue", value);
    const QSignalBlocker block(spin);
    spin->setValue(value);
}

void DiversityFilterControls::writeCheck(QCheckBox* check, bool on)
{
    if (!check)
        return;
    const QSignalBlocker block(check);
    check->setChecked(on);
}

void DiversityFilterControls::checkValue(QButtonGroup* group, const QString& value)
{
    if (!group)
        return;
    for (QAbstractButton* button : group->buttons()) {
        if (button->property("filterValue").toString() != value)
            continue;
        const QSignalBlocker block(button);
        button->setChecked(true);
        return;
    }
}

void DiversityFilterControls::setControlsEnabled(bool on)
{
    for (QWidget* control : m_controls)
        control->setEnabled(on);
    if (m_panel)
        m_panel->setEnabled(on);
    if (m_notchTable)
        m_notchTable->setEnabled(on);
}

void DiversityFilterControls::showTransient(const QString& text)
{
    m_status->setText(text);
    DiversityWidgets::setLive(m_status, true);
    m_statusTimer->start(kTransientMs);
}

void DiversityFilterControls::clear()
{
    m_available = false;
    m_statusTimer->stop();
    m_baseStatus.clear();
    m_status->setText(QString());
    DiversityWidgets::setLive(m_status, false);
    m_panel->clear();
    setControlsEnabled(false);
    m_caption->setText(emDash());
    m_notchRows.clear();
    m_notchTable->setRowCount(0);
    for (QLabel* line : {m_autoLine, m_roofingLine, m_anfLine, m_tiltLine, m_gainLine,
                         m_blankedLine}) {
        line->setText(emDash());
    }
}

void DiversityFilterControls::applyNotchTable(const QJsonArray& notches)
{
    // Rebuild only on change: at 2 Hz an unchanged list would otherwise destroy
    // and re-create one button per notch twice a second, and the CLEAR button
    // under the pointer would be a different object on every frame.
    QStringList rows;
    rows.reserve(notches.size());
    for (const QJsonValue& entry : notches) {
        const QJsonObject notch = entry.toObject();
        double depth = 0.0;
        rows << QStringLiteral("%1\x1f%2\x1f%3")
                    .arg(jsonHz(notch, "hz"), jsonHz(notch, "width_hz"),
                         jsonNumber(notch, "depth_db", &depth) ? signedDb(depth, 1)
                                                               : emDash());
    }
    if (rows == m_notchRows)
        return;
    m_notchRows = rows;

    m_notchTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const QStringList cells = rows.at(row).split(QChar(0x1f));
        for (int col = 0; col < 3; ++col)
            m_notchTable->setItem(row, col, new QTableWidgetItem(cells.at(col)));
        const QString hz = cells.at(0);
        auto* clearButton = new QPushButton(tr("CLEAR"), m_notchTable);
        clearButton->setObjectName(
            QStringLiteral("diversityWindowFilterNotchClear%1").arg(hz));
        clearButton->setAccessibleName(tr("Clear the notch at %1 hertz").arg(hz));
        clearButton->setToolTip(tr("Remove this notch and leave the others where "
                                   "they are."));
        clearButton->setAccessibleDescription(clearButton->toolTip());
        clearButton->setFixedHeight(kNotchClearButtonHeight);
        applyToggleButtonStyle(clearButton);
        connect(clearButton, &QPushButton::clicked, this,
                [this, hz] { notch(QStringLiteral("clear"), hz); });
        m_notchTable->setCellWidget(row, 3, clearButton);
    }
}

void DiversityFilterControls::applyStatus(const QJsonObject& f)
{
    // A failed request, a non-JSON body or a gate with no /filter route at all:
    // nothing to say, and emptying a page that was working a moment ago would
    // be saying something.
    if (f.isEmpty())
        return;

    const QString error = f.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        // The gate refused a value. No control moves: the next poll is what
        // puts the refused one back where the gate actually has it, so the page
        // never shows a setting the radio does not have.
        showTransient(error);
        return;
    }

    if (!f.value(QStringLiteral("available")).toBool()) {
        clear();
        m_baseStatus = tr("Filter is not available for this mode");
        m_status->setText(m_baseStatus);
        return;
    }

    if (!m_available) {
        m_available = true;
        setControlsEnabled(true);
    }
    if (!m_baseStatus.isEmpty()) {
        m_baseStatus.clear();
        if (!m_statusTimer->isActive())
            m_status->setText(QString());
    }

    // --- the curve and its caption ----------------------------------------
    // A drag in progress owns the handles. A poll landing mid-drag would snatch
    // them back to where the gate still has them, which is the one moment the
    // gate is NOT the best available answer.
    if (!m_panel->dragging())
        m_panel->applyStatus(f);

    m_lowHz = jsonInt(f, "low_hz", m_lowHz);
    m_highHz = jsonInt(f, "high_hz", m_highHz);

    const QString sideband = f.value(QStringLiteral("sideband")).toString();
    const QString mode = f.value(QStringLiteral("mode")).toString();
    const QString shape = f.value(QStringLiteral("shape")).toString();
    const int taps = jsonInt(f, "taps", -1);
    double transition = 0.0;
    m_caption->setText(
        tr("%1 · %2–%3 Hz · %4 %5 taps · %6 Hz transition")
            .arg(sideband.isEmpty() ? (mode.isEmpty() ? emDash() : mode.toUpper())
                                    : sideband.toUpper(),
                 jsonHz(f, "low_hz"), jsonHz(f, "high_hz"),
                 shape.isEmpty() ? emDash() : shape.toUpper(),
                 taps >= 0 ? QString::number(taps) : emDash(),
                 jsonNumber(f, "transition_hz", &transition)
                     ? QString::number(qint64(std::llround(transition)))
                     : emDash()));

    // --- WIDTH -------------------------------------------------------------
    checkValue(m_shapeGroup, shape);
    // The spin boxes show what was ASKED for, not what is in force: with AUTO
    // on those differ, and rewriting the operator's own numbers with the
    // tracker's would lose them the moment they switched AUTO off.
    writeSpin(m_lowSpin, jsonInt(f, "set_low_hz", m_lowHz));
    writeSpin(m_highSpin, jsonInt(f, "set_high_hz", m_highHz));

    const QJsonObject autoObj = f.value(QStringLiteral("auto")).toObject();
    const bool autoOn = autoObj.value(QStringLiteral("enabled")).toBool();
    {
        const QSignalBlocker block(m_autoButton);
        m_autoButton->setChecked(autoOn);
    }
    const QJsonValue source = autoObj.value(QStringLiteral("source"));
    if (!autoOn) {
        m_autoLine->setText(tr("AUTO · off"));
    } else if (!source.isString()) {
        // Enabled with no source is the tracker's honest "I have not decided
        // yet" -- not a width of zero, and not the last one it had.
        m_autoLine->setText(tr("AUTO · warming up"));
    } else {
        m_autoLine->setText(tr("AUTO · %1 %2–%3")
                                .arg(source.toString(), jsonHz(autoObj, "low_hz"),
                                     jsonHz(autoObj, "high_hz")));
    }

    const QJsonObject roofing = f.value(QStringLiteral("roofing")).toObject();
    m_roofingLine->setText(tr("Roof %1 RF · %2 digital")
                               .arg(kiloHertz(roofing, "analogue_hz"),
                                    kiloHertz(roofing, "digital_hz")));

    // --- NOTCH -------------------------------------------------------------
    const QJsonObject anf = f.value(QStringLiteral("anf")).toObject();
    writeCheck(m_anfCheck, anf.value(QStringLiteral("enabled")).toBool());
    const QJsonArray found = anf.value(QStringLiteral("found_hz")).toArray();
    const QJsonArray depths = anf.value(QStringLiteral("depth_db")).toArray();
    QStringList tones;
    for (int i = 0; i < found.size(); ++i) {
        tones << tr("%1 Hz %2 dB")
                     .arg(QString::number(qint64(std::llround(found.at(i).toDouble()))),
                          i < depths.size() ? signedDb(depths.at(i).toDouble(), 0)
                                            : emDash());
    }
    m_anfLine->setText(tones.isEmpty() ? tr("none")
                                       : tones.join(QStringLiteral(", ")));
    applyNotchTable(f.value(QStringLiteral("notches")).toArray());

    // --- TONE --------------------------------------------------------------
    const QJsonObject contour = f.value(QStringLiteral("contour")).toObject();
    writeCheck(m_contourCheck, contour.value(QStringLiteral("enabled")).toBool());
    writeSpin(m_contourHzSpin, jsonInt(contour, "hz", m_contourHzSpin->value()));
    writeSpin(m_contourDbSpin, jsonInt(contour, "db", m_contourDbSpin->value()));
    writeSpin(m_contourWidthSpin,
              jsonInt(contour, "width_hz", m_contourWidthSpin->value()));

    const QJsonObject apf = f.value(QStringLiteral("apf")).toObject();
    writeCheck(m_apfCheck, apf.value(QStringLiteral("enabled")).toBool());
    writeSpin(m_apfHzSpin, jsonInt(apf, "hz", m_apfHzSpin->value()));
    writeSpin(m_apfWidthSpin, jsonInt(apf, "width_hz", m_apfWidthSpin->value()));

    const QJsonObject autoEq = f.value(QStringLiteral("auto_eq")).toObject();
    writeCheck(m_autoEqCheck, autoEq.value(QStringLiteral("enabled")).toBool());
    double tilt = 0.0;
    m_tiltLine->setText(jsonNumber(autoEq, "tilt_db", &tilt)
                            ? tr("tilt %1 dB").arg(signedDb(tilt, 1))
                            : tr("tilt %1").arg(emDash()));

    // --- AGC & NB ----------------------------------------------------------
    const QJsonObject agc = f.value(QStringLiteral("agc")).toObject();
    checkValue(m_agcGroup, agc.value(QStringLiteral("mode")).toString());
    writeSpin(m_attackSpin, jsonInt(agc, "attack_ms", m_attackSpin->value()));
    writeSpin(m_decaySpin, jsonInt(agc, "decay_ms", m_decaySpin->value()));
    writeSpin(m_hangSpin, jsonInt(agc, "hang_ms", m_hangSpin->value()));
    double gain = 0.0;
    m_gainLine->setText(jsonNumber(agc, "gain_db", &gain)
                            ? tr("gain %1 dB").arg(signedDb(gain, 1))
                            : tr("gain %1").arg(emDash()));

    const QJsonObject nb = f.value(QStringLiteral("nb")).toObject();
    writeCheck(m_nbCheck, nb.value(QStringLiteral("enabled")).toBool());
    writeSpin(m_nbSpin, jsonInt(nb, "threshold_db", m_nbSpin->value()));
    double blanked = 0.0;
    m_blankedLine->setText(jsonNumber(nb, "blanked_pct", &blanked)
                               ? tr("blanked %1 %").arg(blanked, 0, 'f', 1)
                               : tr("blanked %1").arg(emDash()));
}

} // namespace AetherSDR
