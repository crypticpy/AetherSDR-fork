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
//
// The PRESETS strip is built here for that second reason and no other. It
// belongs with the four columns; it lives beside applyStatus() because putting
// it there would take DiversityFilterControls.cpp past the cap.

#include "gui/DiversityWindow.h"

#include "gui/DiversityFilterControls.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityFlowStrip.h"
#include "gui/DiversityTalkerControls.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QButtonGroup>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
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
#include <QVBoxLayout>

#include <cmath>
#include <initializer_list>
#include <utility>

namespace AetherSDR {

namespace {

// How long a refusal from the gate stays on the status line. Long enough to
// read a sentence, short enough that it cannot be mistaken for a permanent
// state of the page.
constexpr int kTransientMs = 5000;

constexpr int kNotchClearButtonHeight = 18;

// The same height every other button on this page uses.
constexpr int kPresetHeight = 22;

// One preset's query, written in the order it goes on the wire so the string
// the gate sees is the string this file reads.
QUrlQuery filterQuery(std::initializer_list<std::pair<const char*, const char*>> items)
{
    QUrlQuery q;
    for (const auto& item : items)
        q.addQueryItem(QLatin1String(item.first), QLatin1String(item.second));
    return q;
}

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

// The read side of the write hold. See DiversityFilterControls.h for
// kWriteHoldMs and DiversityFilterControls.cpp's holdWrite() for how a hold
// gets set. `incoming` is what the poll or write-reply just said this control
// ought to be; if a hold is live and it disagrees, the write is dropped and
// the control keeps showing what the operator put there. Once the hold has
// either expired or been echoed, it is cleared here so the next call is a
// plain, unheld write.
bool holdBlocks(QObject* obj, const QVariant& incoming)
{
    const QVariant until = obj->property("pendingUntil");
    if (!until.isValid())
        return false;
    if (QDateTime::currentMSecsSinceEpoch() >= until.toLongLong()) {
        obj->setProperty("pendingUntil", QVariant());
        obj->setProperty("pendingValue", QVariant());
        return false;
    }
    if (obj->property("pendingValue") == incoming) {
        obj->setProperty("pendingUntil", QVariant());
        obj->setProperty("pendingValue", QVariant());
        return false;
    }
    return true;
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
    // The FLOW strip's last step states the passband in force, so it is fed
    // wherever a /filter answer arrives -- including the reply to a write made
    // from a different page -- rather than only while FILTER is up.
    if (m_flow)
        m_flow->applyFilter(filter);
}

void DiversityWindow::clearFilterReadouts()
{
    if (m_filter)
        m_filter->clear();
}

// --------------------------------------------------------------------------
// PRESETS: five whole filters, one click each
// --------------------------------------------------------------------------
//
// The four buttons in the WIDTH column are widths -- they move one edge and
// touch nothing else. These are the whole filter: edges, shape, and whichever
// of the tone controls the setting is actually about. That is the difference
// between "make it 2.4 kHz" and "put me back on SSB", and an operator who has
// been down in a CW filter with the APF up wants the second one.
//
// Each is exactly one /filter/set, so the gate's reply to it is the page's
// next state and there is no window in which half a preset is in force. RESET
// is the one exception and it is honest about being two: clearing the notches
// is a different route, because "remove them all" is not a setting.

QWidget* DiversityFilterControls::buildPresetStrip()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("PRESETS"), QStringLiteral("diversityWindowFilterPresetsBox"), body, this);
    frame->setToolTip(
        tr("A whole filter in one click -- edges, shape and the tone controls "
           "that go with them, rather than the one edge the width buttons "
           "move. Nothing here is remembered: each is a set of values sent to "
           "the gate, and the page then shows whatever the gate did with "
           "them."));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    body->addLayout(row);

    const auto addPreset = [&](const QString& suffix, const QString& label,
                               const QString& tip, const QUrlQuery& query,
                               bool clearNotches) {
        auto* button = new QPushButton(label, this);
        button->setObjectName(QStringLiteral("diversityWindowFilterPreset") + suffix);
        button->setAccessibleName(label);
        button->setToolTip(tip);
        button->setAccessibleDescription(tip);
        button->setFixedHeight(kPresetHeight);
        applyToggleButtonStyle(button);
        m_controls.append(button);
        connect(button, &QPushButton::clicked, this, [this, query, clearNotches] {
            set(query);
            if (clearNotches)
                notch(QStringLiteral("clear"), QStringLiteral("1"));
        });
        row->addWidget(button, 1);
    };

    addPreset(QStringLiteral("SsbWide"), tr("SSB WIDE"),
              tr("100-2900 Hz, soft. The widest a voice filter is normally set "
                 "to: everything the other station's microphone sent, with a "
                 "roll-off gentle enough that the band either side arrives as "
                 "background rather than as an edge."),
              filterQuery({{"low", "100"}, {"high", "2900"}, {"shape", "soft"}}),
              false);
    addPreset(QStringLiteral("SsbNarrow"), tr("SSB NARROW"),
              tr("300-2400 Hz, sharp. What you drop to when the channel is "
                 "crowded: the bottom 200 Hz is rumble and the top 500 is "
                 "mostly the neighbour, and a sharp skirt keeps them both "
                 "out at the cost of a little ring on transients."),
              filterQuery({{"low", "300"}, {"high", "2400"}, {"shape", "sharp"}}),
              false);
    addPreset(QStringLiteral("Cw"), tr("CW-ISH"),
              tr("400-1000 Hz, sharp, with the audio peak switched on at "
                 "700 Hz. Not a CW filter -- the gate's roofing filter is "
                 "still where it was -- but it is what makes a note audible "
                 "in a passband that was cut for speech."),
              filterQuery({{"low", "400"},
                           {"high", "1000"},
                           {"shape", "sharp"},
                           {"apf", "1"},
                           {"apf_hz", "700"}}),
              false);
    addPreset(QStringLiteral("Net"), tr("NET"),
              tr("200-2700 Hz, sharp, with AUTO EQ on. For a net or a "
                 "round-table, where every station arrives with a different "
                 "audio tilt: the equaliser takes each one's tilt back out so "
                 "you are not reaching for the tone control every over."),
              filterQuery({{"low", "200"},
                           {"high", "2700"},
                           {"shape", "sharp"},
                           {"auto_eq", "1"}}),
              false);
    addPreset(QStringLiteral("Reset"), tr("RESET"),
              tr("Everything off and 100-2900 Hz soft: no automatic notcher, "
                 "no contour, no audio peak, no automatic width, no automatic "
                 "equaliser, no blanker, AGC medium, and every notch you "
                 "placed cleared. The state to go back to when you can no "
                 "longer tell which of six things is making it sound wrong."),
              filterQuery({{"low", "100"},
                           {"high", "2900"},
                           {"shape", "soft"},
                           {"anf", "0"},
                           {"contour", "0"},
                           {"apf", "0"},
                           {"auto", "0"},
                           {"auto_eq", "0"},
                           {"nb", "0"},
                           {"agc", "med"}}),
              true);
    return frame;
}

// --------------------------------------------------------------------------
// APF: one container so applyStatus() can hide it with one call
// --------------------------------------------------------------------------
//
// Built here rather than inline in buildToneColumn() for the file-size reason
// buildPresetStrip() already is: DiversityFilterControls.cpp is at the budget.
// APF is a CW tool -- see the tooltip -- and applyStatus() below hides this
// whole block outside CW rather than leaving a control on the page that does
// nothing for the mode the operator is actually in.

QWidget* DiversityFilterControls::buildApfBlock()
{
    m_apfBlock = new QWidget(this);
    m_apfBlock->setObjectName(QStringLiteral("diversityWindowFilterApfBlock"));
    auto* body = new QVBoxLayout(m_apfBlock);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);

    m_apfCheck = buildCheck(QStringLiteral("diversityWindowFilterApfCheck"),
                            QStringLiteral("apf"), tr("APF (CW)"),
                            tr("The audio peaking filter: a very narrow "
                               "resonance for digging one CW note out of noise. "
                               "It is a CW tool and nothing else -- on speech "
                               "it rings everything through one note, which "
                               "sounds like the other station is talking into a "
                               "tiny cup."));
    body->addWidget(m_apfCheck);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    body->addLayout(row);
    row->addWidget(DiversityWidgets::makeFieldLabel(tr("Hz"), this));
    m_apfHzSpin = buildSpin(QStringLiteral("diversityWindowFilterApfHzSpin"),
                            QStringLiteral("apf_hz"), 0, 20000, tr(" Hz"),
                            tr("Audio peak centre"),
                            tr("The note the peak sits on -- normally your own "
                               "sidetone pitch, so a signal you tune to zero "
                               "beat lands in it."));
    row->addWidget(m_apfHzSpin);
    row->addWidget(DiversityWidgets::makeFieldLabel(tr("W"), this));
    m_apfWidthSpin = buildSpin(QStringLiteral("diversityWindowFilterApfWidthSpin"),
                               QStringLiteral("apf_width"), 10, 2000, tr(" Hz"),
                               tr("Audio peak width"),
                               tr("How narrow the peak is. Narrower rings more "
                                  "and hears less either side of the note."));
    row->addWidget(m_apfWidthSpin);
    row->addStretch(1);
    return m_apfBlock;
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
    // Nor is one the operator just committed, until the hold either sees its
    // own value echoed back or times out. See holdBlocks() above.
    if (holdBlocks(spin, value))
        return;
    spin->setProperty("gateValue", value);
    const QSignalBlocker block(spin);
    spin->setValue(value);
}

void DiversityFilterControls::writeCheck(QCheckBox* check, bool on)
{
    if (!check || holdBlocks(check, on))
        return;
    const QSignalBlocker block(check);
    check->setChecked(on);
}

void DiversityFilterControls::checkValue(QButtonGroup* group, const QString& value)
{
    if (!group || holdBlocks(group, value))
        return;
    for (QAbstractButton* button : group->buttons()) {
        if (button->property("filterValue").toString() != value)
            continue;
        const QSignalBlocker block(button);
        button->setChecked(true);
        return;
    }
}

void DiversityFilterControls::clearWriteHolds()
{
    for (QWidget* control : m_controls) {
        control->setProperty("pendingValue", QVariant());
        control->setProperty("pendingUntil", QVariant());
    }
    for (QButtonGroup* group : {m_shapeGroup, m_agcGroup}) {
        if (!group)
            continue;
        group->setProperty("pendingValue", QVariant());
        group->setProperty("pendingUntil", QVariant());
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
    // A refused write never took, so its hold must not survive it: the next
    // poll is what puts the refused control back where the gate actually has
    // it, and a hold still open would keep that poll from doing so.
    clearWriteHolds();
    m_status->setText(text);
    DiversityWidgets::setLive(m_status, true);
    m_statusTimer->start(kTransientMs);
}

void DiversityFilterControls::setTalkerNames(const QJsonArray& memory)
{
    if (m_talker)
        m_talker->setTalkerNames(memory);
}

void DiversityFilterControls::clear()
{
    m_available = false;
    m_statusTimer->stop();
    if (m_talker)
        m_talker->clear();
    m_baseStatus.clear();
    m_status->setText(QString());
    DiversityWidgets::setLive(m_status, false);
    m_panel->clear();
    setControlsEnabled(false);
    m_caption->setText(emDash());
    m_notchRows.clear();
    m_notchTable->setRowCount(0);
    for (QLabel* line : {m_forceLine, m_autoLine, m_roofingLine, m_anfLine, m_tiltLine,
                         m_contourSourceLine, m_gainLine, m_blankedLine}) {
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
    // Same hold as writeCheck(), by hand: m_autoButton is a QPushButton, not a
    // QCheckBox, so it cannot go through that helper.
    if (!holdBlocks(m_autoButton, autoOn)) {
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
        // One decimal, the same as the notch table's dB column: the two lists
        // are the same measurement made by two different notchers, and reading
        // one to the hertz and the other to the decibel invited the reader to
        // think they were different quantities.
        tones << tr("%1 Hz %2 dB")
                     .arg(QString::number(qint64(std::llround(found.at(i).toDouble()))),
                          i < depths.size() ? signedDb(depths.at(i).toDouble(), 1)
                                            : emDash());
    }
    m_anfLine->setText(tones.isEmpty() ? tr("none")
                                       : tones.join(QStringLiteral(", ")));
    const QJsonArray notchArray = f.value(QStringLiteral("notches")).toArray();
    applyNotchTable(notchArray);

    // --- TONE --------------------------------------------------------------
    const QJsonObject contour = f.value(QStringLiteral("contour")).toObject();
    writeCheck(m_contourCheck, contour.value(QStringLiteral("enabled")).toBool());
    const bool autoContour = contour.value(QStringLiteral("auto")).toBool();
    writeCheck(m_autoContourCheck, autoContour);
    // While the gate is fitting the bell itself, the three numbers are ITS
    // answer: they are shown, because seeing where the fit landed is the whole
    // point, and they are dead, because a control the next poll overwrites is
    // not a control. hz null with auto on is "nothing fitted yet" -- the spin
    // keeps its last value rather than snapping to zero, which would draw a
    // contour at DC that nobody has.
    const QJsonValue contourHz = contour.value(QStringLiteral("hz"));
    writeSpin(m_contourHzSpin, jsonInt(contour, "hz", m_contourHzSpin->value()));
    writeSpin(m_contourDbSpin, jsonInt(contour, "db", m_contourDbSpin->value()));
    writeSpin(m_contourWidthSpin,
              jsonInt(contour, "width_hz", m_contourWidthSpin->value()));
    for (QSpinBox* spin : {m_contourHzSpin, m_contourDbSpin, m_contourWidthSpin})
        spin->setEnabled(!autoContour);
    const QJsonValue contourSource = contour.value(QStringLiteral("source"));
    if (!autoContour) {
        m_contourSourceLine->setText(tr("manual"));
    } else {
        m_contourSourceLine->setText(
            (contourSource.isString() && contourHz.isDouble())
                ? tr("from %1").arg(contourSource.toString())
                : tr("no print yet"));
    }

    // APF is a CW tool -- see its tooltip -- and has nothing to say on a voice
    // slice, so the whole block is hidden outside CW rather than left on the
    // page showing a control that does nothing for the mode in force. Hidden
    // means untouched: nothing about it is written or read while it is not
    // CW, so a stale hold or a stray value cannot survive a mode change and
    // reappear the moment the operator switches back.
    const bool cwMode = mode.compare(QStringLiteral("cw"), Qt::CaseInsensitive) == 0;
    m_apfBlock->setVisible(cwMode);
    if (cwMode) {
        const QJsonObject apf = f.value(QStringLiteral("apf")).toObject();
        writeCheck(m_apfCheck, apf.value(QStringLiteral("enabled")).toBool());
        writeSpin(m_apfHzSpin, jsonInt(apf, "hz", m_apfHzSpin->value()));
        writeSpin(m_apfWidthSpin, jsonInt(apf, "width_hz", m_apfWidthSpin->value()));
    }

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
    // threshold_db is newer than the rest of the agc object. Absent means the
    // gate would ignore a write, so the spin goes dead rather than sitting
    // there showing a number nothing is honouring -- the same "a dash means the
    // gate is too old" rule the subband readout keeps.
    const QJsonValue threshold = agc.value(QStringLiteral("threshold_db"));
    m_thresholdSpin->setEnabled(threshold.isDouble());
    if (threshold.isDouble())
        writeSpin(m_thresholdSpin, int(std::llround(threshold.toDouble())));
    double gain = 0.0;
    const bool haveGain = jsonNumber(agc, "gain_db", &gain);
    m_gainLine->setText(haveGain ? tr("gain %1 dB").arg(signedDb(gain, 1))
                                 : tr("gain %1").arg(emDash()));

    // --- PER TALKER --------------------------------------------------------
    m_talker->applyTalker(f.value(QStringLiteral("talker")));

    const QJsonObject nb = f.value(QStringLiteral("nb")).toObject();
    writeCheck(m_nbCheck, nb.value(QStringLiteral("enabled")).toBool());
    writeSpin(m_nbSpin, jsonInt(nb, "threshold_db", m_nbSpin->value()));
    double blanked = 0.0;
    const bool haveBlanked = jsonNumber(nb, "blanked_pct", &blanked);
    m_blankedLine->setText(haveBlanked ? tr("blanked %1 %").arg(blanked, 0, 'f', 1)
                                       : tr("blanked %1").arg(emDash()));

    // --- the one line between the curve and the columns --------------------
    // Assembled last because it is a summary of everything above it and of
    // nothing else: every clause here has already been written into a control
    // or a readout, from the same object, in this same call.
    QStringList state;
    state << tr("in force %1–%2 Hz (asked %3–%4)")
                 .arg(jsonHz(f, "low_hz"), jsonHz(f, "high_hz"),
                      jsonHz(f, "set_low_hz"), jsonHz(f, "set_high_hz"));
    if (!autoOn) {
        state << tr("AUTO off");
    } else if (!source.isString()) {
        state << tr("AUTO warming up");
    } else {
        state << tr("AUTO %1 %2–%3")
                     .arg(source.toString(), jsonHz(autoObj, "low_hz"),
                          jsonHz(autoObj, "high_hz"));
    }
    if (!anf.value(QStringLiteral("enabled")).toBool()) {
        state << tr("ANF off");
    } else if (tones.isEmpty()) {
        // Running and holding nothing is a different fact from switched off,
        // and the whole value of this line is that it does not blur the two.
        state << tr("ANF no tones");
    } else {
        state << (tones.size() == 1 ? tr("ANF 1 tone")
                                    : tr("ANF %1 tones").arg(tones.size()));
    }
    state << tr("notches %1").arg(notchArray.size());
    // Whose filter this is. Only when the recall is on AND the gate names a
    // talker -- see DiversityTalkerControls::stateClause(); an empty clause is
    // dropped rather than dashed, because "one filter for everybody" is the
    // ordinary state and not a missing reading.
    const QString talkerClause = m_talker->stateClause();
    if (!talkerClause.isEmpty())
        state << talkerClause;
    const QString agcMode = agc.value(QStringLiteral("mode")).toString();
    if (agcMode == QLatin1String("off")) {
        state << tr("AGC off");
    } else {
        state << tr("AGC %1 %2")
                     .arg(agcMode.isEmpty() ? emDash() : agcMode,
                          haveGain ? tr("%1 dB").arg(signedDb(gain, 1)) : emDash());
    }
    if (!nb.value(QStringLiteral("enabled")).toBool()) {
        state << tr("NB off");
    } else {
        state << (haveBlanked ? tr("NB %1 %").arg(blanked, 0, 'f', 1) : tr("NB on"));
    }
    m_forceLine->setText(state.join(QStringLiteral(" · ")));
}

} // namespace AetherSDR
