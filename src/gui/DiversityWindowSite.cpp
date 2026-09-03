// The Diversity window's SITE page: everything that is about the STATION
// rather than about the slice or the span.
//
// SLICE answers "what is the combiner doing with what I am tuned to". BAND
// answers "where should I be tuned". Neither can answer the two questions an
// operator actually asks when the receiver sounds worse than it did last week,
// because both are about signals that happen to be there right now:
//
//   * WHAT IS MY NOISE? Not how much -- what kind. The gate profiles the noise
//     floor's shape rather than its size, so it can say "there is a 120 Hz comb
//     with two harmonics" (a rectifier: a supply, an LED driver, a dimmer, a
//     charger) or "fifteen impulses a second at 12 dB" (a fence, ignition,
//     arcing, power-line telecoms). Those are sentences you can act on by
//     walking around the house; a noise figure is not.
//
//   * ARE MY ANTENNAS ANY GOOD? The NCDXF/IARU beacon project answers that and
//     nothing else does: eighteen known transmitters, on a known schedule, at
//     four calibrated power steps. The lowest step you can hear is the path's
//     real margin in decibels, and the phase the two loops measure on a beacon
//     is the only phase in this whole window whose right answer is already
//     known -- which is exactly what a future geometry solve has to calibrate
//     itself against.
//
// The noise profile rides on the /diversity status object every page already
// polls; the beacon watch is its own 1 Hz route, fetched only while this page
// is on screen (DiversityBandPoller). Its own file for the reason
// DiversityWindowBand.cpp and DiversityWindowPanels.cpp are: these are members
// of DiversityWindow, and DiversityWindow.cpp is at the file-size budget
// AGENTS.md asks for.

#include "gui/DiversityWindow.h"

#include "core/ThemeManager.h"
#include "gui/DiversityBeaconPanel.h"
#include "gui/DiversityNoiseProfilePanel.h"
#include "gui/DiversityWindowPanels.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cmath>

namespace AetherSDR {

namespace {

const char* kSubbandCheckStyle =
    "QCheckBox { color: {{color.text.primary}}; font-size: 11px; spacing: 5px;"
    " background: transparent; }"
    "QCheckBox::indicator { width: 12px; height: 12px; border-radius: 2px;"
    " border: 1px solid {{color.toggle.border}};"
    " background: {{color.toggle.background}}; }"
    "QCheckBox::indicator:checked {"
    " background: {{color.toggle.accent.background.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }";

// Same three tokens the station-locator field beside it wears -- one more
// QLineEdit on the same page should not look like a different kind of control.
const char* kNoteEditStyle =
    "QLineEdit { background: {{color.background.0}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 4px; }";

// A free-text note the gate stores verbatim and hands back verbatim. 80 is the
// gate's own limit; the field enforces it here so the refusal never has to
// happen.
constexpr int kAntennaMaxChars = 80;

QString emDash()
{
    return QStringLiteral("—");
}

// "212" from 212.0, for a bearing that is read as a compass heading rather
// than as a measurement -- nobody steers an antenna to a tenth of a degree.
QString degreesText(double deg)
{
    return QString::number(qint64(std::llround(deg)));
}

} // namespace

QWidget* DiversityWindow::buildSubbandRow(QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("diversityWindowSubbandRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_subbandCheck = new QCheckBox(tr("Per-bin weights"), row);
    m_subbandCheck->setObjectName(QStringLiteral("diversityWindowSubbandCheck"));
    m_subbandCheck->setAccessibleName(tr("Per-bin weight refinement"));
    m_subbandCheck->setToolTip(
        tr("Solve a separate weight for every bin of the passband instead of "
           "one weight for the whole channel. Two loops give one degree of "
           "freedom AT ONE FREQUENCY -- across a 2.7 kHz channel there is a "
           "different best answer at each end, and a single weight is the "
           "average of them. Refining per bin buys most where the noise "
           "arrives from several directions at once; on one clean source it is "
           "worth about nothing, and the figure beside it says so. The talker "
           "is held distortionless either way."));
    m_subbandCheck->setAccessibleDescription(m_subbandCheck->toolTip());
    ThemeManager::instance().applyStyleSheet(m_subbandCheck,
                                             QString::fromLatin1(kSubbandCheckStyle));
    // clicked(), not toggled(): applySite() checks the box back from the poll
    // and must not turn that read-back into another write.
    connect(m_subbandCheck, &QCheckBox::clicked, this, [this](bool on) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subband"),
                       on ? QStringLiteral("on") : QStringLiteral("off"));
        emit requestSet(q);
    });
    layout->addWidget(m_subbandCheck);

    m_subbandValue = DiversityWidgets::makeValue(
        QStringLiteral("diversityWindowSubbandValueLabel"),
        QStringLiteral("9999 bins · +99.9 dB"), row);
    m_subbandValue->setAccessibleName(tr("Per-bin refinement gain"));
    m_subbandValue->setToolTip(
        tr("How many passband bins got their own weight on the last solve, and "
           "what that earned over the single-weight answer. A dash means the "
           "gate is too old to report it."));
    layout->addWidget(m_subbandValue);
    layout->addStretch(1);
    return row;
}

// --------------------------------------------------------------------------
// The two station notes
// --------------------------------------------------------------------------
//
// The locator beside this one is a fact the gate can check. This one is not:
// it is whatever the operator wants to be able to read back later, and the
// reason it exists is that an active loop's control box has a band selector
// and a gain knob that nothing on the wire can see. A beacon sweep taken with
// the boxes on SW and the gain at noon is a different measurement from the
// same sweep taken with them anywhere else, and without a note saying which,
// last week's numbers cannot be compared with tonight's.

QWidget* DiversityWindow::buildAntennaRow(QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("diversityWindowSiteAntennaRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel* caption = DiversityWidgets::makeFieldLabel(tr("ANTENNA"), row);
    caption->setObjectName(QStringLiteral("diversityWindowSiteAntennaCaption"));
    caption->setAccessibleName(tr("Antenna note caption"));
    layout->addWidget(caption);

    m_antennaEdit = new QLineEdit(row);
    m_antennaEdit->setObjectName(QStringLiteral("diversityWindowSiteAntennaEdit"));
    m_antennaEdit->setAccessibleName(tr("Antenna note"));
    m_antennaEdit->setMaxLength(kAntennaMaxChars);
    m_antennaEdit->setPlaceholderText(tr("which loops, box band + gain"));
    m_antennaEdit->setToolTip(
        tr("What is on the end of the coax tonight, in your own words: which "
           "loops, and where their control boxes are set. \"SW both, gain 12 "
           "o'clock\" is the whole idea -- the box's band switch and gain knob "
           "do not reach the gate, so a beacon sweep taken a week ago can only "
           "be compared with tonight's if you wrote down what they were on. It "
           "is filed with every line of the site log. Press Enter or click "
           "away to save; empty it to clear the note."));
    m_antennaEdit->setAccessibleDescription(m_antennaEdit->toolTip());
    ThemeManager::instance().applyStyleSheet(m_antennaEdit,
                                             QString::fromLatin1(kNoteEditStyle));
    // returnPressed and editingFinished both, because either is how somebody
    // finishes typing -- and the write is guarded on the value having actually
    // changed, so Enter (which raises both) still asks the gate exactly once,
    // and so a poll's check-back can never turn into a write of its own.
    const auto commit = [this] {
        if (!m_antennaEdit)
            return;
        const QString typed = m_antennaEdit->text().trimmed();
        if (typed == m_antennaSent)
            return;
        m_antennaSent = typed;
        QUrlQuery q;
        // The gate's own word for "forget it": an empty box is a cleared note
        // rather than a note that is the empty string.
        q.addQueryItem(QStringLiteral("antenna"),
                       typed.isEmpty() ? QStringLiteral("off") : typed);
        emit requestSite(QStringLiteral("/diversity/set"), q);
    };
    connect(m_antennaEdit, &QLineEdit::returnPressed, this, commit);
    connect(m_antennaEdit, &QLineEdit::editingFinished, this, commit);
    layout->addWidget(m_antennaEdit, 1);
    return row;
}

QWidget* DiversityWindow::buildSitePage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("diversityWindowSitePage"));
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    QVBoxLayout* noiseBody = nullptr;
    QFrame* noiseFrame = DiversityWidgets::makeGroupBox(
        tr("NOISE PROFILE"), QStringLiteral("diversityWindowNoiseProfileBox"),
        noiseBody, page);
    noiseFrame->setToolTip(
        tr("What KIND of noise this address makes, as opposed to how much. The "
           "gate measures the shape of the noise floor -- a mains-locked comb, "
           "an impulse rate, the strongest lines that are not mains harmonics "
           "-- because the shape is what tells you which appliance to go and "
           "unplug."));
    m_noiseProfile = new DiversityNoiseProfilePanel(noiseFrame);
    // The row buttons quote the gate's own route and query back at it. Nothing
    // between here and the wire inspects either -- a gate that grows a new kind
    // of finding gets a working button in this window without a new build.
    connect(m_noiseProfile, &DiversityNoiseProfilePanel::actionRequested, this,
            [this](const QString& route, const QUrlQuery& query) {
                emit requestSite(route, query);
            });
    noiseBody->addWidget(m_noiseProfile);
    root->addWidget(noiseFrame);

    QVBoxLayout* beaconBody = nullptr;
    QFrame* beaconFrame = DiversityWidgets::makeGroupBox(
        tr("BEACONS"), QStringLiteral("diversityWindowBeaconBox"), beaconBody, page);
    beaconFrame->setToolTip(
        tr("The NCDXF/IARU International Beacon Project: eighteen known "
           "transmitters sharing one frequency on a three-minute rota. Because "
           "the transmitters and the paths are known, what you read here is a "
           "measurement of YOUR station -- the antennas, the feedline, the "
           "noise floor -- rather than a report about somebody else's."));
    m_beacons = new DiversityBeaconPanel(beaconFrame);
    // The antenna note sits beside the station locator, on the locator's own
    // row: both are facts about THIS station that the operator tells the gate,
    // the sweep below them is only readable a week later against the two of
    // them together, and the page has no spare height for a row of its own.
    m_beacons->setGridRowExtra(buildAntennaRow(m_beacons));
    connect(m_beacons, &DiversityBeaconPanel::actionRequested, this,
            [this](const QString& route, const QUrlQuery& query) {
                emit requestSite(route, query);
            });
    // A BEACON CHECK leaves by the same door a click on the BAND waterfall
    // does: the gate has no tune route, so the frequency crosses to
    // AetherGateApplet and becomes a real slice tune there.
    connect(m_beacons, &DiversityBeaconPanel::tuneRequested, this,
            &DiversityWindow::requestTune);
    // A check starting or coming home changes what the site poll wants; the
    // same signal a page switch sends, since the handler re-reads every page.
    connect(m_beacons, &DiversityBeaconPanel::checkStateChanged, this,
            [this] { emit bandPageChanged(bandPageVisible() && isVisible()); });
    beaconBody->addWidget(m_beacons);
    // Neither panel stretches: the beacon table is a fixed eighteen rows and
    // the noise profile is a fixed set of lines, so surplus height collects at
    // the bottom rather than being shared out between a header line, a table
    // and a caption that would each drift apart from the others.
    root->addWidget(beaconFrame);
    root->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("diversityWindowSiteScroll"));
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

bool DiversityWindow::sitePageVisible() const
{
    return m_pages && m_pageSiteButton && m_pageSiteButton->isChecked();
}

bool DiversityWindow::beaconPollWanted() const
{
    return m_beacons && m_beacons->pollWanted();
}

void DiversityWindow::applyBeacons(const QJsonObject& beacons)
{
    if (m_beacons)
        m_beacons->applyBeacons(beacons);
}

void DiversityWindow::applySiteReply(const QJsonObject& reply)
{
    // Both panels are offered it and each ignores it unless it is the one that
    // asked, so a refusal is shown by the control that caused it and by no
    // other. The gate's own state comes back on the next poll either way --
    // nothing here writes a readout from a reply.
    if (m_noiseProfile)
        m_noiseProfile->applyActionReply(reply);
    if (m_beacons)
        m_beacons->applyActionReply(reply);
}

void DiversityWindow::endBeaconCheck()
{
    if (m_beacons)
        m_beacons->cancelCheck();
}

void DiversityWindow::setActiveSliceHz(double hz)
{
    if (m_beacons)
        m_beacons->setActiveSliceHz(hz);
}

// --------------------------------------------------------------------------
// /diversity/compass -> the noise bearing line
// --------------------------------------------------------------------------

void DiversityWindow::applyCompass(const QJsonObject& compass)
{
    if (!m_noiseProfile)
        return;
    // A gate that predates the route, a dropped request, or a compass with no
    // noise block at all. None of those is "no direction" -- they are "nothing
    // said", and a dash is how the rest of this window says that.
    const QJsonValue value = compass.value(QStringLiteral("noise"));
    if (!value.isObject()) {
        m_noiseProfile->setBearing(emDash(), QString());
        return;
    }
    const QJsonObject noise = value.toObject();
    // The gate names the noise the bearing is FOR. Without a name the sentence
    // is still true, just less specific -- "noise" is what it is.
    const QString kindValue = noise.value(QStringLiteral("kind")).toString();
    const QString kind = kindValue.isEmpty() ? tr("noise") : kindValue;
    // Why there is no bearing belongs in the tooltip and nowhere else: it is a
    // sentence about beacon counts, and the line has to stay one line.
    const QString reason = noise.value(QStringLiteral("reason")).toString();

    const QJsonValue bearing = noise.value(QStringLiteral("bearing_deg"));
    if (!bearing.isDouble()) {
        m_noiseProfile->setBearing(
            tr("%1: direction unknown — no compass fit yet").arg(kind), reason);
        return;
    }
    QString text = tr("%1 from %2°").arg(kind, degreesText(bearing.toDouble()));
    // Two elements in a line cannot tell a bearing from its reflection about
    // the baseline. The gate gives both when it has them, and both are printed:
    // dropping one would be picking an answer by coin toss.
    const QJsonValue mirror = noise.value(QStringLiteral("mirror_deg"));
    if (mirror.isDouble())
        text += tr(" (or %1°)").arg(degreesText(mirror.toDouble()));
    const QJsonValue coherence = noise.value(QStringLiteral("coherence"));
    if (coherence.isDouble())
        text += tr(" · coh %1").arg(coherence.toDouble(), 0, 'f', 2);
    const QJsonValue since = noise.value(QStringLiteral("since"));
    if (since.isDouble()) {
        text += tr(" · since %1")
                    .arg(QDateTime::fromSecsSinceEpoch(qint64(since.toDouble()))
                             .toString(QStringLiteral("HH:mm")));
    }
    m_noiseProfile->setBearing(text, reason);
}

void DiversityWindow::applySite(const QJsonObject& d)
{
    applyAntennaNote(d.value(QStringLiteral("sitelog")));
    if (m_noiseProfile) {
        m_noiseProfile->applyProfile(d.value(QStringLiteral("noise_profile")));
        m_noiseProfile->applySubband(d.value(QStringLiteral("subband")));
    }
    if (!m_subbandCheck || !m_subbandValue)
        return;

    // Same isObject() guard the "nb" block keeps: a malformed or absent
    // "subband" must not read as "off, 0 bins, 0 dB" through toObject()'s
    // silent {}. An older gate has no key at all, and says so with a dash
    // rather than with a control that looks measured.
    const QJsonValue value = d.value(QStringLiteral("subband"));
    if (!value.isObject()) {
        m_subbandCheck->setEnabled(false);
        const QSignalBlocker block(m_subbandCheck);
        m_subbandCheck->setChecked(false);
        m_subbandValue->setText(emDash());
        return;
    }
    const QJsonObject subband = value.toObject();
    m_subbandCheck->setEnabled(true);
    {
        const QSignalBlocker block(m_subbandCheck);
        m_subbandCheck->setChecked(subband.value(QStringLiteral("enabled")).toBool());
    }
    const QJsonValue bins = subband.value(QStringLiteral("bins"));
    const QJsonValue extra = subband.value(QStringLiteral("extra_db"));
    m_subbandValue->setText(
        tr("%1 bins · %2 dB")
            .arg(bins.isDouble() ? QString::number(qint64(std::llround(bins.toDouble())))
                                 : emDash(),
                 extra.isDouble() ? QString::asprintf("%+.1f", extra.toDouble())
                                  : emDash()));
}

// The check-back half of the hold rule: a poll may fill the field in and may
// correct it, but it may never write. The operator's own typing outranks the
// poll while the cursor is in the box -- a note being typed must not be
// replaced mid-word by the note the gate still has.
void DiversityWindow::applyAntennaNote(const QJsonValue& sitelog)
{
    if (!m_antennaEdit || !sitelog.isObject())
        return;
    const QJsonValue note = sitelog.toObject().value(QStringLiteral("antenna"));
    // A null note is a cleared one; anything that is not a string at all is an
    // older gate, and that is a reason to leave the field alone rather than to
    // empty it.
    if (!note.isString() && !note.isNull())
        return;
    const QString text = note.toString();
    m_antennaSent = text;
    // Never while the operator is typing into it: a poll that overwrote a
    // half-written note once a second would make the field unusable. The
    // blocker is the belt to that brace -- nothing on this path may write.
    if (m_antennaEdit->hasFocus() || m_antennaEdit->text() == text)
        return;
    const QSignalBlocker block(m_antennaEdit);
    m_antennaEdit->setText(text);
}

void DiversityWindow::clearSiteReadouts()
{
    if (m_antennaEdit && !m_antennaEdit->hasFocus()) {
        m_antennaSent.clear();
        const QSignalBlocker block(m_antennaEdit);
        m_antennaEdit->clear();
    }
    if (m_noiseProfile)
        m_noiseProfile->clear();
    if (m_beacons)
        m_beacons->clear();
    if (m_subbandCheck) {
        m_subbandCheck->setEnabled(false);
        const QSignalBlocker block(m_subbandCheck);
        m_subbandCheck->setChecked(false);
    }
    if (m_subbandValue)
        m_subbandValue->setText(emDash());
}

} // namespace AetherSDR
