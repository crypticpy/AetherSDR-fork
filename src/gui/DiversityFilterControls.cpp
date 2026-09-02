#include "gui/DiversityFilterControls.h"

#include "core/ThemeManager.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace AetherSDR {

namespace {

// The curve gets a fixed slab of the page. It is the instrument; the columns
// under it are the keypad, and a curve that shrank when the notch table grew a
// row would make a filter look as if it had changed shape.
constexpr int kPanelHeight = 240;
constexpr int kSpinWidth = 74;
constexpr int kRowHeight = 22;
constexpr int kNotchTableHeight = 92;

const char* kCheckStyle =
    "QCheckBox { color: {{color.text.primary}}; font-size: 11px; spacing: 5px;"
    " background: transparent; }"
    "QCheckBox::indicator { width: 12px; height: 12px; border-radius: 2px;"
    " border: 1px solid {{color.toggle.border}};"
    " background: {{color.toggle.background}}; }"
    "QCheckBox::indicator:checked {"
    " background: {{color.toggle.accent.background.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }";

const char* kSpinStyle =
    "QSpinBox { background: {{color.background.1}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }"
    "QSpinBox:disabled { color: {{color.text.disabled}}; }";

const char* kCaptionStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 12px; font-weight: bold;"
    " background: transparent; }";

const char* kForceLineStyle =
    "QLabel { color: {{color.text.primary}}; font-size: 11px;"
    " background: transparent; }";

const char* kStatusStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

const char* kNotchTableStyle =
    "QTableWidget { background: transparent; color: {{color.text.primary}};"
    " font-size: 11px; border: 1px solid {{color.background.1}};"
    " gridline-color: {{color.background.1}}; }"
    "QHeaderView::section { background: {{color.background.1}};"
    " color: {{color.text.secondary}}; font-size: 10px; font-weight: bold;"
    " border: none; padding: 1px 3px; }";

QString emDash()
{
    return QStringLiteral("—");
}

QLabel* fieldLabel(const QString& text, QWidget* parent)
{
    return DiversityWidgets::makeFieldLabel(text, parent);
}

QHBoxLayout* newRow(QVBoxLayout* body)
{
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    body->addLayout(row);
    return row;
}

} // namespace

DiversityFilterControls::DiversityFilterControls(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFilterBody"));
    setAccessibleName(tr("Filter page"));

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        m_status->setText(m_baseStatus);
        DiversityWidgets::setLive(m_status, false);
    });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    m_caption = new QLabel(emDash(), this);
    m_caption->setObjectName(QStringLiteral("diversityWindowFilterCaptionLabel"));
    m_caption->setAccessibleName(tr("Filter summary"));
    m_caption->setToolTip(
        tr("What the gate has in force right now: the sideband, the two "
           "passband edges in audio hertz, the shape and how many taps it "
           "costs, and how many hertz the skirt takes to fall away. A sharp "
           "filter's skirt is measured in tens of hertz and a soft one's in "
           "hundreds -- that difference, not the width, is what you hear as "
           "ringing on one and warmth on the other."));
    m_caption->setAccessibleDescription(m_caption->toolTip());
    ThemeManager::instance().applyStyleSheet(m_caption,
                                             QString::fromLatin1(kCaptionStyle));
    root->addWidget(m_caption);

    m_panel = new DiversityFilterPanel(this);
    m_panel->setFixedHeight(kPanelHeight);
    connect(m_panel, &DiversityFilterPanel::edgesDragged, this,
            [this](int low, int high) {
                // Only the edge that actually moved. Re-asserting the other one
                // would hand the auto-width tracker's answer back to it as an
                // operator setting, which is a different claim.
                QUrlQuery q;
                if (low != m_lowHz)
                    q.addQueryItem(QStringLiteral("low"), QString::number(low));
                if (high != m_highHz)
                    q.addQueryItem(QStringLiteral("high"), QString::number(high));
                if (!q.isEmpty())
                    set(q);
            });
    connect(m_panel, &DiversityFilterPanel::notchRequested, this, [this](double hz) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("add"),
                       QString::number(qint64(std::llround(hz))));
        q.addQueryItem(QStringLiteral("width"),
                       QString::number(m_notchWidthSpin->value()));
        emit requestFilter(QStringLiteral("/filter/notch"), q);
    });
    root->addWidget(m_panel);

    m_forceLine = new QLabel(emDash(), this);
    m_forceLine->setObjectName(QStringLiteral("diversityWindowFilterForceLabel"));
    m_forceLine->setAccessibleName(tr("Filter state"));
    m_forceLine->setToolTip(
        tr("Everything that is switched on, in force, in one line: the edges "
           "the gate is actually using and the ones you asked for, what AUTO "
           "has chosen, how many tones the automatic notcher is holding, how "
           "many notches you have placed, the AGC mode and the gain it is "
           "taking off, and what the blanker is removing. When the two pairs "
           "of edges disagree, something else is moving them."));
    m_forceLine->setAccessibleDescription(m_forceLine->toolTip());
    ThemeManager::instance().applyStyleSheet(m_forceLine,
                                             QString::fromLatin1(kForceLineStyle));
    // Fixed height and an ignored width, exactly like the status line below:
    // it is one line whatever it says, and a sentence that grew the page's
    // minimum width would put the four columns behind a scrollbar.
    m_forceLine->setFixedHeight(m_forceLine->sizeHint().height());
    m_forceLine->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    root->addWidget(m_forceLine);

    m_status = new QLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("diversityWindowFilterStatusLabel"));
    m_status->setAccessibleName(tr("Filter status"));
    ThemeManager::instance().applyStyleSheet(m_status,
                                             QString::fromLatin1(kStatusStyle));
    // A fixed height whether or not it is saying anything: a line that appeared
    // only when the gate refused something would shift every control under it
    // at the moment the operator most needs them to stay still.
    m_status->setText(tr("Filter is not available for this mode"));
    m_status->setFixedHeight(m_status->sizeHint().height());
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_status->setText(QString());
    root->addWidget(m_status);

    auto* columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(8);
    columns->addWidget(buildWidthColumn(), 1);
    columns->addWidget(buildNotchColumn(), 1);
    columns->addWidget(buildToneColumn(), 1);
    columns->addWidget(buildAgcColumn(), 1);
    // Stretch 0: the four boxes are as tall as what is in them and no taller.
    // They used to take every pixel to the bottom of the window and hold a
    // control in the top third of each, which drew four boxes mostly full of
    // nothing and read as four things half-built.
    root->addLayout(columns, 0);
    root->addWidget(buildPresetStrip(), 0);
    // What is left over is left over. There is no honest control to put in it
    // and stretching something into it would be decoration.
    root->addStretch(1);

    setControlsEnabled(false);
}

// --------------------------------------------------------------------------
// Writing to the gate
// --------------------------------------------------------------------------

void DiversityFilterControls::set(const QString& key, const QString& value)
{
    QUrlQuery q;
    q.addQueryItem(key, value);
    set(q);
}

void DiversityFilterControls::set(const QUrlQuery& query)
{
    emit requestFilter(QStringLiteral("/filter/set"), query);
}

void DiversityFilterControls::notch(const QString& key, const QString& value)
{
    QUrlQuery q;
    q.addQueryItem(key, value);
    emit requestFilter(QStringLiteral("/filter/notch"), q);
}

// --------------------------------------------------------------------------
// Small builders
// --------------------------------------------------------------------------

QButtonGroup* DiversityFilterControls::buildValueButtons(QVBoxLayout* body,
                                                         const QString& key,
                                                         const QString& objectPrefix,
                                                         const QStringList& labels,
                                                         const QStringList& values,
                                                         const QString& tip)
{
    QHBoxLayout* row = newRow(body);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(labels[i], this);
        button->setObjectName(objectPrefix + values[i]);
        button->setAccessibleName(tr("%1 %2").arg(key, labels[i]));
        button->setToolTip(tip);
        button->setAccessibleDescription(tip);
        button->setCheckable(true);
        button->setFixedHeight(kRowHeight);
        button->setProperty("filterValue", values[i]);
        applyToggleButtonStyle(button);
        group->addButton(button);
        row->addWidget(button, 1);
        m_controls.append(button);
        // clicked(), not toggled(): applyStatus() checks a button back from the
        // poll and must not turn that read-back into another write.
        connect(button, &QPushButton::clicked, this,
                [this, key, value = values[i]] { set(key, value); });
    }
    row->addStretch(0);
    return group;
}

QSpinBox* DiversityFilterControls::buildSpin(const QString& objectName,
                                             const QString& key, int lo, int hi,
                                             const QString& suffix,
                                             const QString& accessibleName,
                                             const QString& tip)
{
    auto* spin = new QSpinBox(this);
    spin->setObjectName(objectName);
    spin->setAccessibleName(accessibleName);
    spin->setToolTip(tip);
    spin->setAccessibleDescription(tip);
    spin->setRange(lo, hi);
    spin->setSingleStep(10);
    spin->setSuffix(suffix);
    spin->setFixedWidth(kSpinWidth);
    spin->setFixedHeight(kRowHeight);
    spin->setKeyboardTracking(false);
    ThemeManager::instance().applyStyleSheet(spin, QString::fromLatin1(kSpinStyle));
    m_controls.append(spin);
    if (key.isEmpty())
        return spin;
    // editingFinished, not valueChanged: an operator typing "2400" would
    // otherwise write 2, 24, 240 and 2400 on the way past. The gateValue
    // property is what the last status put here, so leaving a spin box
    // untouched does not write it back on every focus change.
    connect(spin, &QSpinBox::editingFinished, this, [this, spin, key] {
        if (spin->property("gateValue").isValid()
            && spin->property("gateValue").toInt() == spin->value()) {
            return;
        }
        spin->setProperty("gateValue", spin->value());
        set(key, QString::number(spin->value()));
    });
    return spin;
}

QCheckBox* DiversityFilterControls::buildCheck(const QString& objectName,
                                               const QString& key,
                                               const QString& text, const QString& tip)
{
    auto* check = new QCheckBox(text, this);
    check->setObjectName(objectName);
    check->setAccessibleName(text);
    check->setToolTip(tip);
    check->setAccessibleDescription(tip);
    ThemeManager::instance().applyStyleSheet(check, QString::fromLatin1(kCheckStyle));
    m_controls.append(check);
    connect(check, &QCheckBox::clicked, this, [this, key](bool on) {
        set(key, on ? QStringLiteral("1") : QStringLiteral("0"));
    });
    return check;
}

// --------------------------------------------------------------------------
// The four columns
// --------------------------------------------------------------------------

QWidget* DiversityFilterControls::buildWidthColumn()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("WIDTH"), QStringLiteral("diversityWindowFilterWidthBox"), body, this);
    frame->setToolTip(
        tr("How much of the audio band reaches you, and how abruptly it stops. "
           "The presets are the four widths a voice filter is normally set to; "
           "the spin boxes are for anything else. AUTO hands both edges to the "
           "gate, which fits them to the station you are listening to."));

    m_shapeGroup = buildValueButtons(
        body, QStringLiteral("shape"), QStringLiteral("diversityWindowFilterShape"),
        {tr("SOFT"), tr("SHARP")},
        {QStringLiteral("soft"), QStringLiteral("sharp")},
        tr("SHARP spends taps on a near-vertical skirt: it rejects the "
           "adjacent station and rings a little on transients. SOFT rolls off "
           "over a few hundred hertz, which sounds warmer and lets more of the "
           "neighbour in. Neither is better; they are the two ends of the same "
           "trade."));

    QHBoxLayout* presets = newRow(body);
    const QList<QPair<QString, int>> widths = {{QStringLiteral("1.8k"), 1800},
                                              {QStringLiteral("2.4k"), 2400},
                                              {QStringLiteral("2.7k"), 2700},
                                              {QStringLiteral("3.0k"), 3000}};
    for (const auto& entry : widths) {
        auto* button = new QPushButton(entry.first, this);
        button->setObjectName(QStringLiteral("diversityWindowFilterPreset%1")
                                  .arg(entry.second));
        button->setAccessibleName(tr("%1 hertz wide").arg(entry.second));
        button->setToolTip(tr("Keep the low edge where it is and put the high "
                              "edge %1 Hz above it. The low edge is the one "
                              "that decides how much rumble and hum you hear, "
                              "so a width preset should not move it.")
                               .arg(entry.second));
        button->setAccessibleDescription(button->toolTip());
        button->setFixedHeight(kRowHeight);
        applyToggleButtonStyle(button);
        m_controls.append(button);
        connect(button, &QPushButton::clicked, this, [this, span = entry.second] {
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("low"), QString::number(m_lowHz));
            q.addQueryItem(QStringLiteral("high"), QString::number(m_lowHz + span));
            set(q);
        });
        presets->addWidget(button, 1);
    }

    QHBoxLayout* edges = newRow(body);
    edges->addWidget(fieldLabel(tr("Low"), this));
    m_lowSpin = buildSpin(QStringLiteral("diversityWindowFilterLowSpin"),
                          QStringLiteral("low"), 0, 20000, tr(" Hz"),
                          tr("Low passband edge"),
                          tr("The bottom of the passband, in audio hertz. "
                             "Raising it is the cheapest cure for mains hum and "
                             "for the rumble a poorly sited antenna picks up."));
    edges->addWidget(m_lowSpin);
    edges->addWidget(fieldLabel(tr("High"), this));
    m_highSpin = buildSpin(QStringLiteral("diversityWindowFilterHighSpin"),
                           QStringLiteral("high"), 0, 20000, tr(" Hz"),
                           tr("High passband edge"),
                           tr("The top of the passband, in audio hertz. This is "
                              "the edge that decides intelligibility: below "
                              "about 2.4 kHz consonants start going."));
    edges->addWidget(m_highSpin);
    edges->addStretch(1);

    QHBoxLayout* autoRow = newRow(body);
    m_autoButton = new QPushButton(tr("AUTO"), this);
    m_autoButton->setObjectName(QStringLiteral("diversityWindowFilterAutoButton"));
    m_autoButton->setAccessibleName(tr("Automatic passband width"));
    m_autoButton->setToolTip(
        tr("Let the gate choose both edges. It fits them to the station's own "
           "voice print when it has heard enough overs to have one, and to the "
           "spectrum in the channel when it has not -- the readout beside this "
           "says which. Your own edges are remembered and come back the moment "
           "you switch it off."));
    m_autoButton->setAccessibleDescription(m_autoButton->toolTip());
    m_autoButton->setCheckable(true);
    m_autoButton->setFixedHeight(kRowHeight);
    applyToggleButtonStyle(m_autoButton);
    m_controls.append(m_autoButton);
    connect(m_autoButton, &QPushButton::clicked, this, [this](bool on) {
        set(QStringLiteral("auto"), on ? QStringLiteral("1") : QStringLiteral("0"));
    });
    autoRow->addWidget(m_autoButton);
    m_autoLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterAutoLabel"),
        QStringLiteral("AUTO · spectrum 9999–9999"),
        tr("Where the automatic width has put the edges, and what it worked "
           "them out from: the talker's own print, or the spectrum in the "
           "channel. \"Warming up\" means it has neither yet."),
        this);
    autoRow->addWidget(m_autoLine, 1);

    m_roofingLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterRoofingLabel"),
        QStringLiteral("Roof 9999 kHz RF · 999 kHz digital"),
        tr("The two filters upstream of this one: the analogue roofing filter "
           "in the tuner, and the digital channel filter the gate applies "
           "before the audio filter runs. Neither is adjustable from here; "
           "they are stated because they, not the passband, are what decides "
           "whether a strong neighbour can desensitise you."),
        this);
    body->addWidget(m_roofingLine);
    body->addStretch(1);
    return frame;
}

QWidget* DiversityFilterControls::buildNotchColumn()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("NOTCH"), QStringLiteral("diversityWindowFilterNotchBox"), body, this);
    frame->setToolTip(
        tr("Narrow cuts inside the passband, for carriers and tones that sit on "
           "top of the station you want. The automatic notcher finds them for "
           "you; the table is the ones you placed yourself, which stay where "
           "you put them."));

    m_anfCheck = buildCheck(QStringLiteral("diversityWindowFilterAnfCheck"),
                            QStringLiteral("anf"), tr("ANF"),
                            tr("Hunt for steady tones inside the passband and "
                               "notch each one as it appears. It works on "
                               "carriers and heterodynes and leaves speech "
                               "alone; on a very weak signal it can chew at the "
                               "voice, which is when you turn it off and place "
                               "the notches yourself."));
    body->addWidget(m_anfCheck);
    m_anfLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterAnfLabel"),
        QStringLiteral("1240 Hz −34.0 dB, 2010 Hz"),
        tr("The tones the automatic notcher currently has hold of, and how deep "
           "it is cutting each. \"none\" means it is running and has found "
           "nothing, which is a different fact from being switched off."),
        this);
    body->addWidget(m_anfLine);

    m_notchTable = new QTableWidget(0, 4, this);
    m_notchTable->setObjectName(QStringLiteral("diversityWindowFilterNotchTable"));
    m_notchTable->setAccessibleName(tr("Manual notches"));
    m_notchTable->setToolTip(tr("Every notch you have placed by hand: where it "
                                "is, how wide it is, and how deep the gate is "
                                "actually cutting there."));
    m_notchTable->setHorizontalHeaderLabels(
        {tr("Hz"), tr("W"), tr("dB"), QString()});
    m_notchTable->verticalHeader()->setVisible(false);
    m_notchTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_notchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_notchTable->setFixedHeight(kNotchTableHeight);
    m_notchTable->horizontalHeader()->setStretchLastSection(true);
    m_notchTable->setColumnWidth(0, 52);
    m_notchTable->setColumnWidth(1, 40);
    m_notchTable->setColumnWidth(2, 48);
    ThemeManager::instance().applyStyleSheet(m_notchTable,
                                             QString::fromLatin1(kNotchTableStyle));
    body->addWidget(m_notchTable);

    QHBoxLayout* add = newRow(body);
    m_notchHzSpin = buildSpin(QStringLiteral("diversityWindowFilterNotchHzSpin"),
                              QString(), 0, 20000, tr(" Hz"), tr("Notch frequency"),
                              tr("Where to put the next notch. Double-clicking "
                                 "the curve above does the same thing at the "
                                 "frequency under the pointer, which is quicker "
                                 "when you can see the carrier."));
    add->addWidget(m_notchHzSpin);
    m_notchWidthSpin = buildSpin(QStringLiteral("diversityWindowFilterNotchWidthSpin"),
                                 QString(), 10, 2000, tr(" Hz"), tr("Notch width"),
                                 tr("How wide the next notch is. Narrow enough "
                                    "and a notch removes a carrier without "
                                    "being audible on speech; too wide and it "
                                    "takes a syllable with it."));
    m_notchWidthSpin->setValue(140);
    add->addWidget(m_notchWidthSpin);
    m_notchAddButton = new QPushButton(tr("ADD"), this);
    m_notchAddButton->setObjectName(
        QStringLiteral("diversityWindowFilterNotchAddButton"));
    m_notchAddButton->setAccessibleName(tr("Add notch"));
    m_notchAddButton->setToolTip(tr("Place a notch at the frequency and width "
                                    "beside this button."));
    m_notchAddButton->setAccessibleDescription(m_notchAddButton->toolTip());
    m_notchAddButton->setFixedHeight(kRowHeight);
    applyToggleButtonStyle(m_notchAddButton);
    m_controls.append(m_notchAddButton);
    connect(m_notchAddButton, &QPushButton::clicked, this, [this] {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("add"), QString::number(m_notchHzSpin->value()));
        q.addQueryItem(QStringLiteral("width"),
                       QString::number(m_notchWidthSpin->value()));
        emit requestFilter(QStringLiteral("/filter/notch"), q);
    });
    add->addWidget(m_notchAddButton, 1);

    m_notchClearAllButton = new QPushButton(tr("CLEAR ALL"), this);
    m_notchClearAllButton->setObjectName(
        QStringLiteral("diversityWindowFilterNotchClearAllButton"));
    m_notchClearAllButton->setAccessibleName(tr("Clear every manual notch"));
    m_notchClearAllButton->setToolTip(
        tr("Remove every notch in the table at once. The automatic notcher's "
           "own tones are not in the table and are not touched."));
    m_notchClearAllButton->setAccessibleDescription(m_notchClearAllButton->toolTip());
    m_notchClearAllButton->setFixedHeight(kRowHeight);
    applyToggleButtonStyle(m_notchClearAllButton);
    m_controls.append(m_notchClearAllButton);
    connect(m_notchClearAllButton, &QPushButton::clicked, this,
            [this] { notch(QStringLiteral("clear"), QStringLiteral("1")); });
    body->addWidget(m_notchClearAllButton);
    body->addStretch(1);
    return frame;
}

QWidget* DiversityFilterControls::buildToneColumn()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("TONE"), QStringLiteral("diversityWindowFilterToneBox"), body, this);
    frame->setToolTip(
        tr("Shaping inside the passband rather than cutting at its edges: a "
           "broad tilt, a narrow peak, and an automatic tilt that matches the "
           "station's own audio to yours."));

    m_contourCheck = buildCheck(QStringLiteral("diversityWindowFilterContourCheck"),
                                QStringLiteral("contour"), tr("CONTOUR"),
                                tr("A broad lift or cut centred where you put "
                                   "it. Pulling a few decibels out around 500 Hz "
                                   "clears the mud off a bass-heavy station; "
                                   "adding a couple around 1.8 kHz sharpens "
                                   "consonants without making it shrill."));
    body->addWidget(m_contourCheck);
    QHBoxLayout* contourRow = newRow(body);
    contourRow->addWidget(fieldLabel(tr("Hz"), this));
    m_contourHzSpin = buildSpin(QStringLiteral("diversityWindowFilterContourHzSpin"),
                                QStringLiteral("contour_hz"), 0, 20000, tr(" Hz"),
                                tr("Contour centre"),
                                tr("The frequency the contour is centred on."));
    contourRow->addWidget(m_contourHzSpin);
    contourRow->addWidget(fieldLabel(tr("dB"), this));
    m_contourDbSpin = buildSpin(QStringLiteral("diversityWindowFilterContourDbSpin"),
                                QStringLiteral("contour_db"), -20, 20, tr(" dB"),
                                tr("Contour gain"),
                                tr("How much to lift (positive) or cut "
                                   "(negative) at the contour centre."));
    m_contourDbSpin->setSingleStep(1);
    contourRow->addWidget(m_contourDbSpin);
    contourRow->addStretch(1);

    QHBoxLayout* contourWidth = newRow(body);
    contourWidth->addWidget(fieldLabel(tr("Width"), this));
    m_contourWidthSpin =
        buildSpin(QStringLiteral("diversityWindowFilterContourWidthSpin"),
                  QStringLiteral("contour_width"), 10, 5000, tr(" Hz"),
                  tr("Contour width"),
                  tr("How broad the lift or cut is. Wide enough and it reads as "
                     "tone; narrow enough and it reads as a resonance."));
    contourWidth->addWidget(m_contourWidthSpin);
    contourWidth->addStretch(1);

    m_apfCheck = buildCheck(QStringLiteral("diversityWindowFilterApfCheck"),
                            QStringLiteral("apf"), tr("APF"),
                            tr("The audio peaking filter: a very narrow "
                               "resonance for digging one CW note out of noise. "
                               "It is the wrong tool on speech, where it makes "
                               "everything sound like a telephone."));
    body->addWidget(m_apfCheck);
    QHBoxLayout* apfRow = newRow(body);
    apfRow->addWidget(fieldLabel(tr("Hz"), this));
    m_apfHzSpin = buildSpin(QStringLiteral("diversityWindowFilterApfHzSpin"),
                            QStringLiteral("apf_hz"), 0, 20000, tr(" Hz"),
                            tr("Audio peak centre"),
                            tr("The note the peak sits on -- normally your own "
                               "sidetone pitch, so a signal you tune to zero "
                               "beat lands in it."));
    apfRow->addWidget(m_apfHzSpin);
    apfRow->addWidget(fieldLabel(tr("W"), this));
    m_apfWidthSpin = buildSpin(QStringLiteral("diversityWindowFilterApfWidthSpin"),
                               QStringLiteral("apf_width"), 10, 2000, tr(" Hz"),
                               tr("Audio peak width"),
                               tr("How narrow the peak is. Narrower rings more "
                                  "and hears less either side of the note."));
    apfRow->addWidget(m_apfWidthSpin);
    apfRow->addStretch(1);

    m_autoEqCheck = buildCheck(QStringLiteral("diversityWindowFilterAutoEqCheck"),
                               QStringLiteral("auto_eq"), tr("AUTO EQ"),
                               tr("Measure the tilt of the station's own audio "
                                  "and take it back out, so a dark rig and a "
                                  "bright one arrive sounding alike. The "
                                  "readout is how much tilt it is currently "
                                  "correcting."));
    body->addWidget(m_autoEqCheck);
    m_tiltLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterTiltLabel"), QStringLiteral("tilt −99.9 dB"),
        tr("The spectral tilt the automatic equaliser is undoing. A large "
           "number means the station you are listening to is a long way from "
           "flat, not that anything is wrong here."),
        this);
    body->addWidget(m_tiltLine);
    body->addStretch(1);
    return frame;
}

QWidget* DiversityFilterControls::buildAgcColumn()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("AGC & NB"), QStringLiteral("diversityWindowFilterAgcBox"), body, this);
    frame->setToolTip(
        tr("What happens to the level after the filter: the automatic gain "
           "control that keeps a fading station at a constant volume, and the "
           "noise blanker that punches impulses out before it."));

    m_agcGroup = buildValueButtons(
        body, QStringLiteral("agc"), QStringLiteral("diversityWindowFilterAgc"),
        {tr("FAST"), tr("MED"), tr("SLOW"), tr("LONG"), tr("OFF")},
        {QStringLiteral("fast"), QStringLiteral("med"), QStringLiteral("slow"),
         QStringLiteral("long"), QStringLiteral("off")},
        tr("How quickly the gain recovers after a loud passage. FAST follows "
           "every syllable and pumps the noise up between words; SLOW and LONG "
           "hold the level through pauses, which is what you want on a fading "
           "signal. OFF leaves the level where the RF gain puts it."));

    QHBoxLayout* timing = newRow(body);
    timing->addWidget(fieldLabel(tr("Atk"), this));
    m_attackSpin = buildSpin(QStringLiteral("diversityWindowFilterAttackSpin"),
                             QStringLiteral("attack_ms"), 0, 500, tr(" ms"),
                             tr("AGC attack"),
                             tr("How fast the gain comes down on a sudden loud "
                                "signal. Too fast and transients click; too "
                                "slow and a static crash gets through at full "
                                "volume."));
    m_attackSpin->setSingleStep(1);
    timing->addWidget(m_attackSpin);
    timing->addWidget(fieldLabel(tr("Dec"), this));
    m_decaySpin = buildSpin(QStringLiteral("diversityWindowFilterDecaySpin"),
                            QStringLiteral("decay_ms"), 0, 5000, tr(" ms"),
                            tr("AGC decay"),
                            tr("How fast the gain comes back up once the signal "
                               "drops. This is the control that decides whether "
                               "the noise floor breathes between words."));
    timing->addWidget(m_decaySpin);
    timing->addStretch(1);

    QHBoxLayout* hangRow = newRow(body);
    hangRow->addWidget(fieldLabel(tr("Hang"), this));
    m_hangSpin = buildSpin(QStringLiteral("diversityWindowFilterHangSpin"),
                           QStringLiteral("hang_ms"), 0, 5000, tr(" ms"),
                           tr("AGC hang"),
                           tr("How long the gain is held still before it starts "
                              "recovering at all. A hang longer than the gaps "
                              "in speech is what keeps a whole over at one "
                              "level."));
    hangRow->addWidget(m_hangSpin);
    m_gainLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterGainLabel"), QStringLiteral("gain −99.9 dB"),
        tr("How much gain the AGC is applying right now. A large negative "
           "number is a strong signal being held down, not a fault."),
        this);
    hangRow->addWidget(m_gainLine, 1);

    m_nbCheck = buildCheck(QStringLiteral("diversityWindowFilterNbCheck"),
                           QStringLiteral("nb"), tr("NB"),
                           tr("Blank the samples an impulse arrives in -- "
                              "ignition noise, an electric fence, power-line "
                              "arcing. It cannot help with the steady buzz those "
                              "same sources also make, and blanking too much "
                              "audibly chops the speech."));
    body->addWidget(m_nbCheck);
    QHBoxLayout* nbRow = newRow(body);
    nbRow->addWidget(fieldLabel(tr("Level"), this));
    m_nbSpin = buildSpin(QStringLiteral("diversityWindowFilterNbSpin"),
                         QStringLiteral("nb_db"), 0, 60, tr(" dB"),
                         tr("Noise blanker threshold"),
                         tr("How far above the running average a sample has to "
                            "be before it is treated as an impulse. Lower "
                            "blanks more; the percentage beside it is how much "
                            "of the audio is actually being removed."));
    m_nbSpin->setSingleStep(1);
    nbRow->addWidget(m_nbSpin);
    m_blankedLine = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterBlankedLabel"),
        QStringLiteral("blanked 99.9 %"),
        tr("How much of the audio the blanker is currently removing. Above a "
           "few percent you are hearing the blanker rather than the band."),
        this);
    nbRow->addWidget(m_blankedLine, 1);
    body->addStretch(1);
    return frame;
}

} // namespace AetherSDR
