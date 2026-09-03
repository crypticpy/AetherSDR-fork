#include "gui/DiversityFilterControls.h"

#include "gui/DiversityWindowPanels.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

namespace AetherSDR {

namespace {

// The height every button on this page uses -- DiversityWindow's own
// MODE/HEAR/PAN rows (DiversityWindowChain.cpp) and this page's old PRESETS
// strip both used the same number.
constexpr int kRowHeight = 26;

QString emDash()
{
    return QStringLiteral("—");
}

// "+0.2"/"−1.9" with a real minus sign, because this is a number read as a
// sentence rather than a cell in a column. See DiversityBeaconControls.cpp's
// own copy of this helper for the same reasoning; each file that needs it
// keeps its own rather than sharing one across unrelated widgets.
QString signedDb(double v, int decimals)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', decimals);
    return QStringLiteral("+%1").arg(v, 0, 'f', decimals);
}

} // namespace

DiversityFilterControls::DiversityFilterControls(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFilterBody"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_openChainButton = new QPushButton(tr("OPEN CHAIN"), this);
    m_openChainButton->setObjectName(QStringLiteral("diversityWindowFilterOpenChain"));
    m_openChainButton->setAccessibleName(tr("Open the filter chain window"));
    m_openChainButton->setToolTip(
        tr("Everything a single receiver's filter offers -- roofing, the "
           "noise blanker, the passband and its shape, notches, the "
           "automatic notcher, contour, the audio peaking filter, auto EQ, "
           "per-talker recall and AGC -- drawn as a block diagram in its own "
           "window. It is the same window whichever gate control opens it, "
           "so a change made from there is the change in force here too."));
    m_openChainButton->setAccessibleDescription(m_openChainButton->toolTip());
    m_openChainButton->setFixedHeight(kRowHeight);
    applyToggleButtonStyle(m_openChainButton);
    connect(m_openChainButton, &QPushButton::clicked, this,
            &DiversityFilterControls::requestOpenChain);
    root->addWidget(m_openChainButton, 0, Qt::AlignLeft);

    m_movedLabel = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterMovedLabel"),
        tr("roofing, blanker, shape, notch, APF, AGC: in the CHAIN window"),
        tr("Every stage a single receiver has, whatever the mode, now lives "
           "in one window rather than on this page -- open it with the "
           "button above."),
        this);
    // makeReadoutLine() only uses its worstCase string to size the label; the
    // text itself starts as "-", same as any other readout. This one never
    // changes, so it is set once here rather than from an apply*() method.
    m_movedLabel->setText(
        tr("roofing, blanker, shape, notch, APF, AGC: in the CHAIN window"));
    m_movedLabel->setAccessibleName(tr("Where the generic filter stages went"));
    root->addWidget(m_movedLabel);

    root->addWidget(buildPairStagesBox());
    root->addStretch(1);
}

// --------------------------------------------------------------------------
// PAIR STAGES: the two /diversity/set stages a single receiver could never
// have, because both are about what to do with a SECOND loop's disagreement
// with the first.
// --------------------------------------------------------------------------

QWidget* DiversityFilterControls::buildPairStagesBox()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("PAIR STAGES"), QStringLiteral("diversityWindowFilterPairStagesBox"), body,
        this);
    frame->setToolTip(
        tr("What the combiner itself does to the two loops' disagreement "
           "before handing the receiver a single audio stream -- as opposed "
           "to everything in the CHAIN window, which happens to that stream "
           "afterwards and would do the same thing with one antenna."));

    body->addWidget(DiversityWidgets::makeCaption(tr("POST-FILTER"), this));

    auto* postRow = new QHBoxLayout;
    postRow->setContentsMargins(0, 0, 0, 0);
    postRow->setSpacing(6);
    body->addLayout(postRow);

    m_postGroup = new QButtonGroup(this);
    m_postGroup->setExclusive(true);
    const auto addPostButton = [&](const QString& objectName, const QString& label,
                                   const QString& value, const QString& tip) {
        auto* button = new QPushButton(label, this);
        button->setObjectName(objectName);
        button->setAccessibleName(tr("Post-filter %1").arg(label));
        button->setToolTip(tip);
        button->setAccessibleDescription(tip);
        button->setCheckable(true);
        button->setFixedHeight(kRowHeight);
        button->setProperty("filterValue", value);
        applyToggleButtonStyle(button);
        m_postGroup->addButton(button);
        // clicked(), not toggled(): applyPost() checks a button back from a
        // poll and must not turn that read-back into another write.
        connect(button, &QPushButton::clicked, this, [this, value] {
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("post"), value);
            emit requestSet(q);
        });
        postRow->addWidget(button, 1);
    };
    addPostButton(QStringLiteral("diversityWindowFilterPostOff"), tr("OFF"),
                  QStringLiteral("off"),
                  tr("No coherence post-filter. The combiner hands the "
                     "receiver exactly what the weight it already has "
                     "produces, nothing more."));
    addPostButton(QStringLiteral("diversityWindowFilterPostV1"), tr("V1"),
                  QStringLiteral("on"),
                  tr("The coherence post-filter's first version, folded into "
                     "the sub-band combiner: extra reduction wherever the two "
                     "loops disagree, with no measurement of what it did."));
    addPostButton(QStringLiteral("diversityWindowFilterPostV2"), tr("V2"),
                  QStringLiteral("v2"),
                  tr("Learns what the noise between words sounds like and "
                     "subtracts it from the words themselves, with a pause "
                     "gate so it only listens when nobody is talking. Newer "
                     "than V1 and the one worth trying on a faint SSB "
                     "signal that still hisses under sub-band alone."));

    m_postReadout = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterPostReadout"),
        QStringLiteral("in −99.9 dB -> out −99.9 dB, pauses 100 %"),
        tr("V2's own measurement of what it is doing: the signal-to-noise "
           "it sees coming in and what it hands onward, and how much of the "
           "audio it judged silent enough to learn from. V1 has no such "
           "measurement to show."),
        this);
    body->addWidget(m_postReadout);

    body->addWidget(DiversityWidgets::makeCaption(tr("SUB-BAND MRC"), this));

    auto* mrcRow = new QHBoxLayout;
    mrcRow->setContentsMargins(0, 0, 0, 0);
    mrcRow->setSpacing(6);
    body->addLayout(mrcRow);

    m_mrcButton = new QPushButton(tr("MRC"), this);
    m_mrcButton->setObjectName(QStringLiteral("diversityWindowFilterMrc"));
    m_mrcButton->setAccessibleName(tr("Sub-band MRC"));
    m_mrcButton->setToolTip(
        tr("One weight per frequency bin, taken from the spatial map, on top "
           "of the single broadband weight the combiner already applies. "
           "Usually a small gain over broadband -- a lab switch to try more "
           "than an everyday one."));
    m_mrcButton->setAccessibleDescription(m_mrcButton->toolTip());
    m_mrcButton->setCheckable(true);
    m_mrcButton->setFixedHeight(kRowHeight);
    applyToggleButtonStyle(m_mrcButton);
    connect(m_mrcButton, &QPushButton::clicked, this, [this](bool checked) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mrc"),
                       checked ? QStringLiteral("on") : QStringLiteral("off"));
        emit requestSet(q);
    });
    mrcRow->addWidget(m_mrcButton, 0);
    mrcRow->addStretch(1);

    m_mrcReadout = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowFilterMrcReadout"),
        QStringLiteral("−9.9 dB over broadband, 9999 bins"),
        tr("How much MRC is adding over the broadband weight alone, and how "
           "many bins the map had enough signal to score. A dash means the "
           "gate has not measured it, whether or not MRC is switched on."),
        this);
    body->addWidget(m_mrcReadout);

    return frame;
}

// --------------------------------------------------------------------------
// /diversity -> the two readouts
// --------------------------------------------------------------------------

void DiversityFilterControls::checkValue(QButtonGroup* group, const QString& value)
{
    if (!group)
        return;
    for (QAbstractButton* button : group->buttons()) {
        const QSignalBlocker block(button);
        button->setChecked(button->property("filterValue").toString() == value);
    }
}

void DiversityFilterControls::applyPost(const QJsonObject& post)
{
    const bool enabled = post.value(QStringLiteral("enabled")).toBool();
    const int version = post.value(QStringLiteral("version")).toInt(1);
    checkValue(m_postGroup, !enabled ? QStringLiteral("off")
                            : version == 2 ? QStringLiteral("v2")
                                           : QStringLiteral("on"));

    if (!enabled) {
        m_postReadout->setText(emDash());
        return;
    }

    if (version == 2) {
        const QJsonValue snrIn = post.value(QStringLiteral("snr_in_db"));
        const QJsonValue snrOut = post.value(QStringLiteral("snr_out_db"));
        const QJsonValue pause = post.value(QStringLiteral("pause_fraction"));
        if (snrIn.isDouble() && snrOut.isDouble() && pause.isDouble()) {
            m_postReadout->setText(
                tr("in %1 dB -> out %2 dB, pauses %3 %")
                    .arg(signedDb(snrIn.toDouble(), 1), signedDb(snrOut.toDouble(), 1))
                    .arg(qint64(std::lround(pause.toDouble() * 100.0))));
            return;
        }
    }
    m_postReadout->setText(tr("v1"));
}

void DiversityFilterControls::applyMrc(const QJsonObject& mrc)
{
    const bool enabled = mrc.value(QStringLiteral("enabled")).toBool();
    {
        const QSignalBlocker block(m_mrcButton);
        m_mrcButton->setChecked(enabled);
    }

    const QJsonValue gain = mrc.value(QStringLiteral("gain_over_broadband_db"));
    const QJsonValue bins = mrc.value(QStringLiteral("bins_used"));
    if (gain.isDouble() && bins.isDouble()) {
        m_mrcReadout->setText(tr("%1 dB over broadband, %2 bins")
                                  .arg(signedDb(gain.toDouble(), 1))
                                  .arg(qint64(std::llround(bins.toDouble()))));
    } else {
        m_mrcReadout->setText(emDash());
    }
}

void DiversityFilterControls::clear()
{
    checkValue(m_postGroup, QString());
    m_postReadout->setText(emDash());
    {
        const QSignalBlocker block(m_mrcButton);
        m_mrcButton->setChecked(false);
    }
    m_mrcReadout->setText(emDash());
}

} // namespace AetherSDR
