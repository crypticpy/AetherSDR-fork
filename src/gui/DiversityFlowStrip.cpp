// The FLOW strip's derivations. See DiversityFlowStrip.h for why the five
// steps are in this order; what follows is only how each one reads its state
// off a payload the window already had.
//
// Two rules run through all five and are the reason this is a widget of its own
// rather than five more members on DiversityWindow:
//
//   * a step never invents a fact. Every state string is either the gate's own
//     number, the gate's own word, or a dash. "not aligned" is a thing the gate
//     said; "probably fine" is not a thing anything could say.
//   * a step's DONE-ness and its wording are the same derivation. The state
//     that reads "off -> pick TRACK" is the state that is not done, by
//     construction, so the lit button and the sentence beside it can never
//     disagree about whether there is anything left to do.

#include "gui/DiversityFlowStrip.h"

#include "core/ThemeManager.h"
#include "gui/DiversityWindowPanels.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

#include <cmath>

namespace AetherSDR {

namespace {

// Three states, one sheet, no new colours: "next" borrows the accent the
// checked toggle buttons already wear, "later" the secondary text token the
// status strip's dead state uses, and "done" is simply the default.
const char* kStepStyle =
    "QPushButton { background: transparent;"
    " border: 1px solid {{color.background.1}}; border-radius: 3px;"
    " padding: 1px 6px; font-size: 10px; text-align: left;"
    " color: {{color.text.primary}}; }"
    "QPushButton:hover { background: {{color.background.2}}; }"
    "QPushButton[flowState=\"later\"] { color: {{color.text.secondary}}; }"
    "QPushButton[flowState=\"next\"] {"
    " background: {{color.toggle.accent.background.checked}};"
    " color: {{color.toggle.accent.foreground.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }";

constexpr int kStepHeight = 22;
// A floor rather than the widest label: five steps at their natural widths
// would set the window's minimum width on their own, and the window has to
// stay openable at 1120 with nothing behind a scrollbar. Every step carries the
// same stretch, so a state string changing length moves no button -- it is the
// text inside a fixed box that changes, which is the rule the rest of this
// window is built to.
constexpr int kStepMinWidth = 150;

QString dash()
{
    return QStringLiteral("—");
}

} // namespace

DiversityFlowStrip::DiversityFlowStrip(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowFlowStrip"));
    setAccessibleName(tr("Flow"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_caption = DiversityWidgets::makeCaption(tr("FLOW"), this);
    m_caption->setObjectName(QStringLiteral("diversityWindowFlowCaption"));
    m_caption->setAccessibleName(tr("Flow"));
    m_caption->setToolTip(
        tr("The order that gets the best signal: align the tuners, pick a "
           "mode, hear the combined output, act on the noise the gate found, "
           "then set the filter. Beacons (SITE) only matter on 20–10 m; "
           "CAPTURE is for the replay lab, not for listening."));
    m_caption->setAccessibleDescription(m_caption->toolTip());
    layout->addWidget(m_caption);

    struct StepSpec {
        const char* label;
        const char* accessible;
        const char* tip;
    };
    // One sentence each on what the step is for and what "done" means -- the
    // second half is the part an operator cannot guess, and it is why the
    // tooltips are not just the labels again.
    const StepSpec specs[StepCount] = {
        {QT_TR_NOOP("ALIGN"), QT_TR_NOOP("Flow step 1, align"),
         QT_TR_NOOP("Line the two tuners' sample streams up. Nothing can be "
                    "combined until they are, so every number on every page is "
                    "meaningless first. Done when the gate reports aligned and "
                    "a lag; click to run REALIGN now.")},
        {QT_TR_NOOP("MODE"), QT_TR_NOOP("Flow step 2, mode"),
         QT_TR_NOOP("Choose how the weight on the second loop is arrived at. "
                    "Done when the mode is anything but OFF -- OFF is loop A "
                    "on its own, which is an ordinary single-tuner receiver. "
                    "Click to switch to TRACK.")},
        {QT_TR_NOOP("HEAR"), QT_TR_NOOP("Flow step 3, hear"),
         QT_TR_NOOP("Send the combiner's output to the audio. Done when you "
                    "are on OUT or STEREO; on A or B the weight is still being "
                    "solved but you are not listening to it. Click to go back "
                    "to the combined output.")},
        {QT_TR_NOOP("NOISE"), QT_TR_NOOP("Flow step 4, noise"),
         QT_TR_NOOP("Act on what the gate found this address doing. It "
                    "profiles the noise floor by itself once the tuners are "
                    "aligned -- no capture is needed -- and nominates one "
                    "control per finding. Done when no finding is still "
                    "offering an unused button. Click for the SITE page.")},
        {QT_TR_NOOP("FILTER"), QT_TR_NOOP("Flow step 5, filter"),
         QT_TR_NOOP("Set how the station sounds: the passband in force, its "
                    "shape, and whether the gate is fitting the edges itself. "
                    "This step is never unfinished -- there is always a filter "
                    "-- it is the last stop rather than a chore. Click for the "
                    "FILTER page.")}};

    m_steps.reserve(StepCount);
    for (int i = 0; i < StepCount; ++i) {
        auto* button = new QPushButton(this);
        button->setObjectName(QStringLiteral("diversityWindowFlowStep%1").arg(i + 1));
        button->setAccessibleName(tr(specs[i].accessible));
        button->setToolTip(tr(specs[i].tip));
        button->setAccessibleDescription(button->toolTip());
        button->setProperty("flowLabel", tr(specs[i].label));
        button->setFlat(true);
        button->setFixedHeight(kStepHeight);
        button->setMinimumWidth(kStepMinWidth);
        button->setCursor(Qt::PointingHandCursor);
        ThemeManager::instance().applyStyleSheet(button, QString::fromLatin1(kStepStyle));
        connect(button, &QPushButton::clicked, this,
                [this, i] { emit stepActivated(i); });
        layout->addWidget(button, 1);
        m_steps.append(button);
    }

    rebuild();
}

// --------------------------------------------------------------------------
// Incoming state
// --------------------------------------------------------------------------

void DiversityFlowStrip::applyDiversity(const QJsonObject& d, bool available)
{
    m_available = available;
    if (!available) {
        m_aligned = false;
        m_realigning = false;
        m_haveLag = false;
        m_mode.clear();
        m_source.clear();
        m_haveProfile = false;
        m_offered = 0;
        m_activeKinds.clear();
        rebuild();
        return;
    }

    m_aligned = d.value(QStringLiteral("aligned")).toBool();
    m_realigning = d.value(QStringLiteral("realigning")).toBool();
    const QJsonValue lag = d.value(QStringLiteral("lag_samples"));
    m_haveLag = lag.isDouble();
    m_lagSamples = m_haveLag ? lag.toDouble() : 0.0;
    m_mode = d.value(QStringLiteral("mode")).toString();
    m_source = d.value(QStringLiteral("source")).toString();

    // Same isObject() guard the rest of the window keeps on optional blocks: a
    // null noise_profile is "not measured yet", which is a different sentence
    // from "measured and clean", and toObject()'s silent {} would blur them.
    const QJsonValue profile = d.value(QStringLiteral("noise_profile"));
    m_haveProfile = profile.isObject();
    m_offered = 0;
    m_activeKinds.clear();
    if (m_haveProfile) {
        const QJsonArray kinds =
            profile.toObject().value(QStringLiteral("kinds")).toArray();
        for (const QJsonValue& v : kinds) {
            const QJsonObject row = v.toObject();
            if (!row.value(QStringLiteral("action")).isObject())
                continue;   // the gate said why there is nothing to do
            if (row.value(QStringLiteral("active")).toBool())
                m_activeKinds << row.value(QStringLiteral("kind")).toString();
            else
                ++m_offered;
        }
    }
    rebuild();
}

void DiversityFlowStrip::applyFilter(const QJsonObject& f)
{
    if (f.isEmpty() || f.contains(QStringLiteral("error")))
        return;
    m_haveFilter = true;
    m_filterAvailable = f.value(QStringLiteral("available")).toBool();
    if (!m_filterAvailable) {
        rebuild();
        return;
    }
    const QJsonValue low = f.value(QStringLiteral("low_hz"));
    const QJsonValue high = f.value(QStringLiteral("high_hz"));
    m_filterEdges =
        (low.isDouble() && high.isDouble())
            ? QStringLiteral("%1–%2").arg(QString::number(qint64(std::llround(low.toDouble()))),
                              QString::number(qint64(std::llround(high.toDouble()))))
            : dash();
    m_filterShape = f.value(QStringLiteral("shape")).toString();
    m_filterAuto = f.value(QStringLiteral("auto"))
                       .toObject()
                       .value(QStringLiteral("enabled"))
                       .toBool();
    rebuild();
}

void DiversityFlowStrip::clear()
{
    m_available = false;
    m_aligned = false;
    m_realigning = false;
    m_haveLag = false;
    m_mode.clear();
    m_source.clear();
    m_haveProfile = false;
    m_offered = 0;
    m_activeKinds.clear();
    m_haveFilter = false;
    m_filterAvailable = false;
    m_filterEdges.clear();
    m_filterShape.clear();
    m_filterAuto = false;
    rebuild();
}

// --------------------------------------------------------------------------
// The five derivations
// --------------------------------------------------------------------------

DiversityFlowStrip::State DiversityFlowStrip::alignState() const
{
    if (!m_available)
        return {dash(), false};
    if (m_realigning)
        return {tr("aligning…"), false};
    if (!m_aligned)
        return {tr("not aligned → REALIGN"), false};
    return {m_haveLag
                ? tr("lag %1").arg(QString::number(qint64(std::llround(m_lagSamples))))
                : tr("aligned"),
            true};
}

DiversityFlowStrip::State DiversityFlowStrip::modeState() const
{
    if (!m_available || m_mode.isEmpty())
        return {dash(), false};
    if (m_mode == QLatin1String("off"))
        return {tr("off → pick TRACK"), false};
    return {m_mode, true};
}

DiversityFlowStrip::State DiversityFlowStrip::hearState() const
{
    if (!m_available || m_source.isEmpty())
        return {dash(), false};
    if (m_source == QLatin1String("combined"))
        return {tr("OUT"), true};
    if (m_source == QLatin1String("stereo"))
        return {tr("STEREO"), true};
    // A or B, or a wire value a newer gate grew: whatever it is, it is not the
    // combined output, so the offer is the same one.
    return {tr("%1 only → hear OUT").arg(m_source.toUpper()), false};
}

DiversityFlowStrip::State DiversityFlowStrip::noiseState() const
{
    if (!m_available)
        return {dash(), false};
    // Not measured yet is not a finding and not a chore: the gate does this by
    // itself and the operator has nothing to press.
    if (!m_haveProfile)
        return {tr("profiling…"), true};
    if (m_offered > 0) {
        return {m_offered == 1 ? tr("1 finding → SITE")
                               : tr("%1 findings → SITE").arg(m_offered),
                false};
    }
    if (!m_activeKinds.isEmpty()) {
        // The gate's own kind tokens, joined. Not a re-sentenced summary: the
        // SITE page states each finding in full and this line only has to say
        // which of them are already being handled.
        return {tr("acting on %1").arg(m_activeKinds.join(QStringLiteral(", "))), true};
    }
    return {tr("clean"), true};
}

DiversityFlowStrip::State DiversityFlowStrip::filterState() const
{
    // FILTER is never "not done" (see the header): the bool below is always
    // true, and what changes is only what the line says.
    if (!m_haveFilter)
        return {tr("%1 → FILTER").arg(dash()), true};
    if (!m_filterAvailable)
        return {tr("no filter for this mode"), true};
    QString text = m_filterEdges;
    if (!m_filterShape.isEmpty())
        text += QStringLiteral(" ") + m_filterShape;
    if (m_filterAuto)
        text += QStringLiteral(" · ") + tr("AUTO");
    return {text, true};
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

void DiversityFlowStrip::setStepState(QPushButton* button, const QString& state)
{
    if (button->property("flowState").toString() == state)
        return;
    button->setProperty("flowState", state);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void DiversityFlowStrip::rebuild()
{
    if (m_steps.size() != StepCount)
        return;

    const State states[StepCount] = {alignState(), modeState(), hearState(),
                                     noiseState(), filterState()};

    // The one rule the whole widget is: next is the first step that is not
    // done. FILTER is done by construction, so this always lands somewhere.
    m_next = StepFilter;
    for (int i = 0; i < StepCount; ++i) {
        if (!states[i].done) {
            m_next = i;
            break;
        }
    }

    for (int i = 0; i < StepCount; ++i) {
        QPushButton* button = m_steps.at(i);
        button->setText(tr("%1 %2 · %3")
                            .arg(QString::number(i + 1),
                                 button->property("flowLabel").toString(),
                                 states[i].text));
        setStepState(button, i == m_next   ? QStringLiteral("next")
                             : i < m_next  ? QStringLiteral("done")
                                           : QStringLiteral("later"));
    }
}

} // namespace AetherSDR
