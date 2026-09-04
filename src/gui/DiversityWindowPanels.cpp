#include "gui/DiversityWindowPanels.h"

#include "gui/DiversityWindow.h"

#include "core/ThemeManager.h"
#include "gui/AetherGateDiversityFormat.h"
#include "gui/ClientCompKnob.h"
#include "gui/DiversityHelp.h"
#include "gui/DiversityMapStrip.h"
#include "gui/Theme.h"

#include <QAbstractItemView>
#include <QAccessible>
#include <QButtonGroup>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>

namespace AetherSDR {

namespace {

// The meter's fixed window, matching DiversityScope's own SNR bars so the two
// views of the same three numbers cannot disagree about what "half way up"
// means.
constexpr double kSnrLoDb = -10.0;
constexpr double kSnrHiDb = 30.0;

// Same ~150ms coalescing the sidebar panel uses: a knob drag fires
// valueChanged many times a second and each one becomes a real HTTP round
// trip once the applet turns it into a request.
constexpr int kDebounceMs = 150;

// The map strip is the noise panel's main readout here rather than the
// sidebar's 24px glance strip.
constexpr int kMapStripHeight = 58;

constexpr int kMeterWidth   = 46;
constexpr int kHeaderHeight = 16;
constexpr int kValueHeight  = 16;
constexpr int kTickColWidth = 22;

// Caption above each stage panel -- the same accent.bright / 11px / bold the
// gate applet's own collapsible section headers use, so the window's panels
// and the sidebar's sections read as one family.
const char* kGroupCaptionStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "background: transparent; }";

// A row-caption above a group of chain buttons ("MODE", "HEAR", "PAN"). 10px,
// not the 9px the first pass used: nothing in this window is allowed below
// 10px any more, which was the operator's second complaint about it.
const char* kCaptionStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; "
    "background: transparent; }";

// Fixed-width numeric readouts. The [live] property selector is how a value
// that has two meanings (realigning vs. steady, gate present vs. absent)
// changes colour without a per-poll setStyleSheet: set the property, re-polish
// once when it actually flips.
const char* kValueStyle =
    "QLabel { color: {{color.text.primary}}; font-size: 11px; font-weight: bold; "
    "background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

const char* kFieldLabelStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; "
    "background: transparent; }";

// A whole readout sentence rather than a bare number ("A - B: +3.4 dB"). The
// [live] selector is how the alignment line goes warning-coloured while the
// gate is realigning without a setStyleSheet() on every poll.
const char* kReadoutLineStyle =
    "QLabel { color: {{color.text.primary}}; font-size: 11px; "
    "background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

// The one-word BALANCE verdict, and the alignment line: both are conclusions
// rather than measurements, so they get the panel-caption weight.
const char* kVerdictStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "background: transparent; }";

const char* kSourcesListStyle =
    "QListWidget { background: transparent; color: {{color.text.primary}};"
    " font-size: 11px; border: 1px solid {{color.background.1}};"
    " border-radius: 3px; }";

} // namespace

DiversitySnrMeter::DiversitySnrMeter(const QString& header, QWidget* parent)
    : QWidget(parent), m_header(header)
{
    setFixedWidth(kMeterWidth);
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAccessibleName(tr("%1 signal to noise").arg(header));
    setAccessibleDescription(
        tr("Signal-to-noise on a fixed -10 to +30 dB scale. Read-only."));

    // Raw QPainter keyed off ThemeManager::color(), so applyStyleSheet's
    // reverse map never sees these: declare them so Inspect mode surfaces the
    // tokens actually read, and repaint on a live theme switch (the pattern
    // DiversityScope / DiversityMapStrip already follow).
    auto& tm = ThemeManager::instance();
    tm.declareWidgetTokens(this, QStringList{
        QStringLiteral("color.background.spectrum"),
        QStringLiteral("color.border.subtle"),
        QStringLiteral("color.spectrum.grid"),
        QStringLiteral("color.text.secondary"),
        QStringLiteral("color.text.primary"),
        QStringLiteral("color.meter.bar.fillGradient"),
    });
    connect(&tm, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void DiversitySnrMeter::setSnrDb(double db, bool valid)
{
    if (m_valid == valid && (!valid || qFuzzyCompare(m_db + 1.0, db + 1.0)))
        return;
    m_db = db;
    m_valid = valid;
    // /diversity polls at 1 Hz, so this is a settled value rather than a
    // high-rate stream -- announcing every one is information, not spam
    // (docs/a11y.md's throttling note is about the 30 Hz meters).
    QAccessibleValueChangeEvent ev(this, valid ? QVariant(db) : QVariant());
    QAccessible::updateAccessibility(&ev);
    update();
}

void DiversitySnrMeter::clearReading()
{
    setSnrDb(0.0, false);
}

void DiversitySnrMeter::paintEvent(QPaintEvent*)
{
    auto& tm = ThemeManager::instance();
    QPainter p(this);
    // Same split ClientLevelMeter uses: geometry is axis-aligned rectangles
    // that look wrong smeared across a half pixel, text does not.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor secondary = tm.color(this, QStringLiteral("color.text.secondary"));
    const QColor primary = tm.color(this, QStringLiteral("color.text.primary"));

    const int stripTop = kHeaderHeight + 4;
    const int stripBot = height() - kValueHeight - 4;
    const int stripH = std::max(1, stripBot - stripTop);
    const int barLeft = kTickColWidth + 2;
    const QRect barR(barLeft, stripTop, width() - barLeft - 2, stripH);

    QFont hf = p.font();
    hf.setPixelSize(10);
    hf.setBold(true);
    p.setFont(hf);
    p.setPen(secondary);
    p.drawText(QRect(0, 0, width(), kHeaderHeight), Qt::AlignCenter, m_header);

    p.fillRect(barR, tm.color(this, QStringLiteral("color.background.spectrum")));

    if (m_valid) {
        const double t = std::clamp((m_db - kSnrLoDb) / (kSnrHiDb - kSnrLoDb), 0.0, 1.0);
        const int fillH = int(t * stripH);
        if (fillH > 0) {
            // Gradient mapped to the FULL strip, not the partial fill, so the
            // colour at a given height always means the same dB -- the bar
            // grows up through a fixed gradient (ClientLevelMeter's rule).
            p.fillRect(QRect(barR.x(), barR.y() + stripH - fillH, barR.width(), fillH),
                       tm.brush(this, QStringLiteral("color.meter.bar.fillGradient"), barR));
        }
    }

    p.setPen(tm.color(this, QStringLiteral("color.border.subtle")));
    p.setBrush(Qt::NoBrush);
    p.drawRect(barR.adjusted(0, 0, -1, -1));

    QFont tf = p.font();
    tf.setPixelSize(8);
    tf.setBold(false);
    p.setFont(tf);
    const QFontMetrics fm(tf);

    struct Tick { double db; const char* label; };
    static constexpr Tick kTicks[] = {
        {  30.0, "+30" },
        {  20.0, "+20" },
        {  10.0, "+10" },
        {   0.0,   "0" },
        { -10.0, "-10" },
    };
    for (const Tick& t : kTicks) {
        const double norm = (t.db - kSnrLoDb) / (kSnrHiDb - kSnrLoDb);
        const int y = stripTop + int((1.0 - norm) * stripH);
        const QString s = QString::fromLatin1(t.label);
        const int ty = std::clamp(y + fm.ascent() / 2 - 1, stripTop + fm.ascent() - 1,
                                  stripTop + stripH - 1);
        p.setPen(secondary);
        p.drawText(kTickColWidth - 2 - fm.horizontalAdvance(s), ty, s);
        p.setPen(tm.color(this, QStringLiteral("color.spectrum.grid")));
        p.drawLine(kTickColWidth - 1, y, barLeft - 1, y);
    }

    // Fixed-width numeric field: right-aligned inside a reserved rectangle so
    // a digit-count change never moves anything beside it.
    QFont vf = p.font();
    vf.setPixelSize(10);
    vf.setBold(true);
    p.setFont(vf);
    p.setPen(primary);
    p.drawText(QRect(0, height() - kValueHeight, width(), kValueHeight), Qt::AlignCenter,
               m_valid ? QString::asprintf("%+.1f", m_db) : QStringLiteral("—"));
}

namespace DiversityWidgets {

QFrame* makeGroupBox(const QString& caption, const QString& objectName,
                     QVBoxLayout*& body, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    // "stripGroupBox" is the name DiversityWindow's own stylesheet rule
    // targets -- same wrapper the Aetherial Audio channel strip puts round
    // each of its stages.
    frame->setObjectName(QStringLiteral("stripGroupBox"));
    frame->setAccessibleName(caption);

    auto* outer = new QVBoxLayout(frame);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(6);

    auto* captionLabel = new QLabel(caption, frame);
    captionLabel->setObjectName(objectName + QStringLiteral("Caption"));
    ThemeManager::instance().applyStyleSheet(captionLabel,
                                             QString::fromLatin1(kGroupCaptionStyle));
    outer->addWidget(captionLabel);

    auto* content = new QWidget(frame);
    content->setObjectName(objectName);
    body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    outer->addWidget(content, 1);
    return frame;
}

void addHelpBesideCaption(QFrame* frame, DiversityHelp::Topic topic,
                          const QString& helpObjectName)
{
    // makeGroupBox() puts the caption QLabel directly into the frame's own
    // QVBoxLayout; it is the only direct-child QLabel there, so a caller
    // error surfaces as a missing button instead of a captionless header.
    auto* outer = qobject_cast<QVBoxLayout*>(frame->layout());
    const QList<QLabel*> captions =
        frame->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
    if (!outer || captions.isEmpty())
        return;
    QLabel* caption = captions.first();
    outer->removeWidget(caption);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(caption);
    row->addStretch(1);
    auto* help = DiversityHelp::button(frame, topic);
    if (!helpObjectName.isEmpty())
        help->setObjectName(helpObjectName);
    row->addWidget(help);
    outer->insertLayout(0, row);

    // DiversityHelp::button() is a fixed 18px square, taller than the
    // caption label's own 13px sizeHint -- stretching the row to fit it
    // costs 5px this box did not have in its budget before. `outer` is this
    // one frame's own QVBoxLayout (makeGroupBox() makes a fresh one per
    // call), so trimming its single caption-to-content gap here does not
    // touch any other page's group boxes.
    outer->setSpacing(1);
}

QLabel* makeCaption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kCaptionStyle));
    return label;
}

QLabel* makeFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kFieldLabelStyle));
    return label;
}

QLabel* makeValue(const QString& objectName, const QString& worstCase, QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(objectName);
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kValueStyle));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(worstCase) + 8);
    return label;
}

QLabel* makeReadoutLine(const QString& objectName, const QString& worstCase,
                        const QString& tip, QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(objectName);
    ThemeManager::instance().applyStyleSheet(label,
                                             QString::fromLatin1(kReadoutLineStyle));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(worstCase) + 8);
    // H1's 90-char tooltip rule, applied once here for every readout line in
    // the app that goes through this factory: the full sentence still
    // reaches a screen reader as the accessible description below.
    label->setToolTip(tip.length() > 90 ? tip.left(87) + QStringLiteral("…") : tip);
    label->setAccessibleDescription(tip);
    return label;
}

void setLive(QWidget* w, bool live)
{
    if (w->property("live").isValid() && w->property("live").toBool() == live)
        return;
    w->setProperty("live", live);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void applySources(QListWidget* list, const QJsonArray& sources)
{
    if (!list)
        return;

    QStringList rows;
    QStringList tips;
    rows.reserve(sources.size());
    tips.reserve(sources.size());
    for (const QJsonValue& v : sources) {
        const QJsonObject so = v.toObject();
        rows << DiversityFormat::sourceListText(so);
        tips << DiversityFormat::sourceTooltip(so);
    }

    QStringList have;
    QStringList haveTips;
    have.reserve(list->count());
    haveTips.reserve(list->count());
    for (int i = 0; i < list->count(); ++i) {
        have << list->item(i)->text();
        haveTips << list->item(i)->toolTip();
    }

    // Compared on text AND tooltip: the short row text alone (freq + coh) can
    // stay identical between two polls while phase/ratio -- visible only in
    // the tooltip -- moved, and that must still trigger a rebuild.
    if (have == rows && haveTips == tips)
        return;

    const QVariant prevKey =
        list->currentItem() ? list->currentItem()->data(Qt::UserRole) : QVariant();
    const QSignalBlocker block(list);
    list->clear();
    for (int i = 0; i < sources.size(); ++i) {
        const QJsonObject so = sources[i].toObject();
        auto* item = new QListWidgetItem(rows[i], list);
        item->setToolTip(tips[i]);
        item->setData(Qt::UserRole,
                      QVariantList{so.value(QStringLiteral("lo_hz")).toDouble(),
                                   so.value(QStringLiteral("hi_hz")).toDouble()});
    }
    if (!prevKey.isValid())
        return;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole) == prevKey) {
            list->setCurrentRow(i);
            break;
        }
    }
}

} // namespace DiversityWidgets


// --------------------------------------------------------------------------
// DiversityWindow stage-panel builders, defined here rather than in
// DiversityWindow.cpp -- see this file's header comment.
//
// Every control built below gets a tooltip written for a ham who has never
// seen a diversity combiner, because "no hover help anywhere" was the single
// most-repeated thing wrong with the first pass. The tooltip is also set as
// the accessible description, so the same explanation reaches a screen reader
// rather than only a mouse.
// --------------------------------------------------------------------------

QWidget* DiversityWindow::buildAntennasPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("ANTENNAS"), QStringLiteral("diversityWindowAntennas"), body, this);

    auto* meters = new QHBoxLayout;
    meters->setSpacing(8);
    const auto addMeter = [&](DiversitySnrMeter*& meter, const QString& header,
                              const QString& objectName, const QString& tip,
                              const QString& description) {
        meter = new DiversitySnrMeter(header, frame);
        meter->setObjectName(objectName);
        meter->setToolTip(tip);
        meter->setAccessibleDescription(description);
        meters->addWidget(meter);
    };
    addMeter(m_meterA, tr("A"), QStringLiteral("diversityWindowMeterA"),
             tr("Loop A alone, -10 to +30 dB. The reference the combiner "
                "must beat."),
             tr("Signal-to-noise on loop A alone, on a fixed -10 to +30 dB "
                "scale. This is the reference: whatever the combiner does has "
                "to beat the better of A and B to be worth having."));
    addMeter(m_meterB, tr("B"), QStringLiteral("diversityWindowMeterB"),
             tr("Loop B alone, same scale. Far below A means mis-terminated "
                "or nulled."),
             tr("Signal-to-noise on loop B alone, same fixed scale. A loop that "
                "reads far below the other is either mis-terminated or pointed "
                "into a null."));
    addMeter(m_meterOut, tr("OUT"), QStringLiteral("diversityWindowMeterOut"),
             tr("What you are hearing. Should stand above both A and B."),
             tr("Signal-to-noise on what you are actually hearing. If it does "
                "not stand above both A and B, the combiner is not buying you "
                "anything on this signal."));
    meters->addSpacing(12);

    // Phase and ratio are a MANUAL-mode setpoint: Null and Track solve for
    // their own weight and Off applies none, so outside manual these are
    // disabled and never written by a poll. The caption greys with them --
    // a knob that merely stops responding, with no visible reason, is the
    // kind of dead control Principle XI is about.
    auto* manual = new QVBoxLayout;
    manual->setSpacing(4);
    m_manualCaption = DiversityWidgets::makeCaption(tr("MANUAL WEIGHT"), frame);
    m_manualCaption->setObjectName(QStringLiteral("diversityWindowManualCaption"));
    m_manualCaption->setToolTip(
        tr("The weight applied to loop B before it is added to loop A. Live "
           "only in MANUAL mode."));
    m_manualCaption->setAccessibleDescription(
        tr("The weight applied to loop B before it is added to loop A. Live "
           "only in MANUAL mode -- NULL and TRACK solve for their own weight, "
           "and OFF applies none, so the knobs grey out in those modes rather "
           "than sitting there looking adjustable."));
    manual->addWidget(m_manualCaption);

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(8);
    m_phaseKnob = new ClientCompKnob(frame);
    m_phaseKnob->setObjectName(QStringLiteral("diversityWindowPhaseKnob"));
    m_phaseKnob->setAccessibleName(tr("Manual phase"));
    m_phaseKnob->setToolTip(
        tr("How far loop B is rotated in phase before adding to loop A. "
           "MANUAL mode only."));
    m_phaseKnob->setAccessibleDescription(
        tr("How far loop B is rotated in phase before being added to loop A. "
           "Sweep it and a local noise source will null sharply at one "
           "setting; the wanted signal, arriving from a different direction, "
           "will not. MANUAL mode only."));
    m_phaseKnob->setFixedSize(72, 88);
    m_phaseKnob->setLabel(tr("PHASE"));
    m_phaseKnob->setRange(0.0f, 360.0f);
    m_phaseKnob->setDefault(0.0f);
    m_phaseKnob->setLabelFormat([](float v) { return QString::asprintf("%.0f°", double(v)); });
    connect(m_phaseKnob, &ClientCompKnob::valueChanged, this,
            [this](float) { m_phaseDebounce->start(kDebounceMs); });
    knobs->addWidget(m_phaseKnob);

    m_ratioKnob = new ClientCompKnob(frame);
    m_ratioKnob->setObjectName(QStringLiteral("diversityWindowRatioKnob"));
    m_ratioKnob->setAccessibleName(tr("Manual ratio"));
    m_ratioKnob->setToolTip(
        tr("How much louder loop B is made before adding to loop A. MANUAL "
           "mode only."));
    m_ratioKnob->setAccessibleDescription(
        tr("How much louder loop B is made before being added to loop A. Get "
           "this within a decibel of the level the noise arrives at on both "
           "loops or the null will be shallow no matter what the phase is. "
           "MANUAL mode only."));
    m_ratioKnob->setFixedSize(72, 88);
    m_ratioKnob->setLabel(tr("RATIO"));
    m_ratioKnob->setRange(-20.0f, 20.0f);
    m_ratioKnob->setDefault(0.0f);
    m_ratioKnob->setLabelFormat([](float v) { return QString::asprintf("%+.1f dB", double(v)); });
    connect(m_ratioKnob, &ClientCompKnob::valueChanged, this,
            [this](float) { m_ratioDebounce->start(kDebounceMs); });
    knobs->addWidget(m_ratioKnob);
    knobs->addStretch(1);
    manual->addLayout(knobs);
    manual->addStretch(1);
    meters->addLayout(manual, 1);
    body->addLayout(meters);

    // BALANCE: the three numbers that decide whether a second loop can do
    // anything here at all, and the sentence they add up to. Without it the
    // meters say "A is louder than B" and leave the operator to work out what
    // that implies, which is exactly the gap this panel was reported as
    // having.
    body->addWidget(DiversityWidgets::makeCaption(tr("BALANCE"), frame));
    m_balanceDelta = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBalanceDeltaLabel"),
        QStringLiteral("A - B: +99.9 dB"),
        tr("How much better loop A's SNR is than loop B's right now."),
        frame);
    m_balanceDelta->setAccessibleName(tr("Loop balance"));
    m_balanceDelta->setAccessibleDescription(
        tr("How much better loop A's signal-to-noise is than loop B's right "
           "now. Near zero means both loops are contributing; a large number "
           "means one of them is doing all the work and the combiner has "
           "little to add."));
    body->addWidget(m_balanceDelta);

    m_balanceCoherence = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBalanceCoherenceLabel"),
        QStringLiteral("noise coherence 0.00"),
        tr("How alike the noise looks on the two loops, from 0 to 1."),
        frame);
    m_balanceCoherence->setAccessibleName(tr("Noise coherence"));
    m_balanceCoherence->setAccessibleDescription(
        tr("How alike the noise looks on the two loops, from 0 to 1. Near zero "
           "is sky noise arriving from every direction at once, which no "
           "combiner can cancel -- the second antenna can only add gain. Near "
           "one means a single dominant local source, which can be nulled."));
    body->addWidget(m_balanceCoherence);

    m_balancePassband = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowBalancePassbandLabel"),
        QStringLiteral("passband flat 0.00 · slope -99.9°/kHz"),
        tr("How uniform the combined passband is, and how fast phase drifts "
           "with frequency."),
        frame);
    m_balancePassband->setAccessibleName(tr("Passband balance"));
    m_balancePassband->setAccessibleDescription(
        tr("How uniform the combined passband is across its width, and how "
           "fast the phase between the loops drifts with frequency. A steep "
           "slope means one weight cannot null the whole channel at once, so "
           "the null will be deep at one edge and shallow at the other."));
    body->addWidget(m_balancePassband);

    m_balanceVerdict = new QLabel(QStringLiteral("—"), frame);
    m_balanceVerdict->setObjectName(QStringLiteral("diversityWindowBalanceVerdictLabel"));
    m_balanceVerdict->setAccessibleName(tr("Balance verdict"));
    m_balanceVerdict->setToolTip(
        tr("Verdict from the balance numbers: gain-only, or a null this pair can "
           "actually use."));
    m_balanceVerdict->setAccessibleDescription(
        tr("What the three numbers above add up to. \"Gain only\" means the "
           "noise is isotropic and there is nothing to null -- the second loop "
           "can still help by adding signal. \"Null available\" means one "
           "source dominates and NULL or TRACK has something to bite on."));
    ThemeManager::instance().applyStyleSheet(m_balanceVerdict,
                                             QString::fromLatin1(kVerdictStyle));
    body->addWidget(m_balanceVerdict);

    // The per-bin refinement switch belongs with the weight, not with the
    // noise map: it changes what "the weight" MEANS -- one answer for the
    // channel, or one per bin of it -- which is what every readout above it is
    // about. Built in DiversityWindowSite.cpp beside the rest of the subband
    // story; this panel is only where it is shown.
    body->addWidget(buildSubbandRow(frame));
    body->addStretch(1);
    return frame;
}

QWidget* DiversityWindow::buildNoisePanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* frame = DiversityWidgets::makeGroupBox(
        tr("NOISE"), QStringLiteral("diversityWindowNoise"), body, this);

    auto* topRow = new QWidget(frame);
    auto* top = new QHBoxLayout(topRow);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    top->addWidget(DiversityWidgets::makeCaption(tr("BLANKER"), topRow));
    m_nbButton = new QPushButton(tr("NB"), topRow);
    m_nbButton->setObjectName(QStringLiteral("diversityWindowNbButton"));
    m_nbButton->setAccessibleName(tr("Noise blanker"));
    m_nbButton->setToolTip(
        tr("Mutes short, loud spikes -- ignition, arcing -- before the rest "
           "of the chain hears them."));
    m_nbButton->setAccessibleDescription(
        tr("A noise blanker watches for short, loud spikes -- power-line "
           "arcing, ignition noise, some switching supplies -- and mutes the "
           "receiver for the microseconds each spike lasts, before anything "
           "else in the chain hears it. It does nothing at all for a steady "
           "hiss or a carrier; that is what the null is for."));
    m_nbButton->setCheckable(true);
    m_nbButton->setFixedHeight(26);
    applyToggleButtonStyle(m_nbButton, ToggleTribe::Warning);
    connect(m_nbButton, &QPushButton::clicked, this, [this](bool on) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("nb"), on ? QStringLiteral("on") : QStringLiteral("off"));
        emit requestSet(q);
    });
    top->addWidget(m_nbButton);

    m_nbKnob = new ClientCompKnob(topRow);
    m_nbKnob->setObjectName(QStringLiteral("diversityWindowNbKnob"));
    m_nbKnob->setAccessibleName(tr("Noise blanker threshold"));
    m_nbKnob->setToolTip(
        tr("How far above average a sample must jump before the blanker "
           "mutes it."));
    m_nbKnob->setAccessibleDescription(
        tr("How far above the running average a sample has to jump before the "
           "blanker calls it an impulse and mutes it. Too low and it chews "
           "holes in speech and strong signals; too high and it never fires. "
           "Start high and wind it down until the crackle stops."));
    m_nbKnob->setFixedSize(72, 80);
    m_nbKnob->setLabel(tr("THRESH"));
    m_nbKnob->setRange(0.0f, 40.0f);
    m_nbKnob->setDefault(0.0f);
    m_nbKnob->setLabelFormat([](float v) { return QString::asprintf("%.1f dB", double(v)); });
    connect(m_nbKnob, &ClientCompKnob::valueChanged, this,
            [this](float) { m_nbDebounce->start(kDebounceMs); });
    top->addWidget(m_nbKnob);
    top->addStretch(1);
    body->addWidget(topRow);

    // Which leg the panadapter draws, on its own row: with the blanker knob
    // beside it the two would not fit the column at the window's minimum.
    auto* panRow = new QWidget(frame);
    auto* pan = new QHBoxLayout(panRow);
    pan->setContentsMargins(0, 0, 0, 0);
    pan->setSpacing(6);
    m_panGroup = addButtonRow(
        panRow, tr("PAN"), QStringLiteral("pan"), QStringLiteral("diversityWindowPan"),
        {tr("A"), tr("B"), tr("COMBINED"), tr("NULLED")},
        {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("combined"),
         QStringLiteral("nulled")},
        {tr("Draw loop A's raw spectrum on the panadapter. What one antenna "
            "sees, uncombined."),
         tr("Draw loop B's raw spectrum on the panadapter."),
         tr("Draw the combiner's output -- the two loops added with the "
            "current weight."),
         tr("Draw what the null removed -- confirms it's cancelling the "
            "right signal.")});
    // addButtonRow() (DiversityWindow.cpp, not owned here) sets the
    // accessible description to the same short tip it puts on the button --
    // these two buttons had the load-bearing longer sentence before the H1
    // migration, so it is restored here rather than lost.
    for (QAbstractButton* button : m_panGroup->buttons()) {
        if (button->objectName() == QLatin1String("diversityWindowPancombined")) {
            button->setAccessibleDescription(
                tr("Draw the combiner's output -- the two loops added with the "
                   "current weight. This is what you are hearing."));
        } else if (button->objectName() == QLatin1String("diversityWindowPannulled")) {
            button->setAccessibleDescription(
                tr("Draw the difference the null is removing: what the combiner "
                   "threw away. A tall peak here is the interference being "
                   "cancelled, which is the quickest confirmation that the null "
                   "is on the right thing."));
        }
    }
    pan->addStretch(1);
    body->addWidget(panRow);

    m_mapStrip = new DiversityMapStrip(frame);
    m_mapStrip->setObjectName(QStringLiteral("diversityWindowMapStrip"));
    m_mapStrip->setStripHeight(kMapStripHeight);
    m_mapStrip->setAxisMode(true);
    m_mapStrip->setToolTip(
        tr("Per-bin loop similarity - tall bars mark a local source worth "
           "nulling."));
    m_mapStrip->setAccessibleDescription(
        tr("How alike the two loops look at each frequency across the mapped "
           "span. A tall bar means one local source dominates that patch and "
           "the combiner has something it can null; short bars mean noise "
           "arriving from everywhere at once, which nothing can cancel. The "
           "shaded band is the passband you are actually listening through, "
           "and the brackets underneath mark the sources listed below."));
    body->addWidget(m_mapStrip);

    // Broken over two lines by hand rather than word-wrapped: it is the
    // widest string in the panel, and pinning the column to it would cost the
    // talkers table two columns -- but a wrapped label makes the whole grid
    // height-for-width, and the scroll area then sizes the grid to its
    // preferred height instead of its minimum, which is what puts a
    // scrollbar on a window that fits.
    auto* mapCaption = new QLabel(
        tr("inter-loop coherence per bin\nhigh: one local source · low: sky noise"),
        frame);
    ThemeManager::instance().applyStyleSheet(mapCaption,
                                             QString::fromLatin1(kCaptionStyle));
    mapCaption->setObjectName(QStringLiteral("diversityWindowMapCaptionLabel"));
    mapCaption->setToolTip(m_mapStrip->toolTip());
    body->addWidget(mapCaption);

    m_sourcesList = new QListWidget(frame);
    m_sourcesList->setObjectName(QStringLiteral("diversityWindowSourcesList"));
    m_sourcesList->setAccessibleName(tr("Diversity sources"));
    m_sourcesList->setToolTip(
        tr("A patch where both loops see the same source. Select, then "
           "Null selected."));
    m_sourcesList->setAccessibleDescription(
        tr("Each row is a patch of the band where both loops see the same "
           "thing at a steady phase -- the signature of one interfering "
           "source rather than sky noise. Select the one that is bothering "
           "you and press Null selected: the gate solves for the weight that "
           "cancels it and applies it. Hover a row for its phase and level."));
    ThemeManager::instance().applyStyleSheet(m_sourcesList,
                                             QString::fromLatin1(kSourcesListStyle));
    m_sourcesList->setFixedHeight(3 * (fontMetrics().height() + 6));
    m_sourcesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sourcesList->setTextElideMode(Qt::ElideRight);
    body->addWidget(m_sourcesList);

    auto* bottomRow = new QWidget(frame);
    auto* bottom = new QHBoxLayout(bottomRow);
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(6);
    m_nullSourceButton = new QPushButton(tr("Null selected"), bottomRow);
    m_nullSourceButton->setObjectName(QStringLiteral("diversityWindowNullSourceButton"));
    m_nullSourceButton->setAccessibleName(tr("Null the selected noise source"));
    m_nullSourceButton->setToolTip(
        tr("Point the combiner's null at the source selected above."));
    m_nullSourceButton->setAccessibleDescription(
        tr("Point the combiner's null at the source selected above. The gate "
           "solves for the phase and level that cancel it and switches to that "
           "weight; if the source moves or stops, use TRACK instead so the "
           "null follows it."));
    m_nullSourceButton->setEnabled(false);
    connect(m_sourcesList, &QListWidget::currentRowChanged, this,
            [this](int row) { m_nullSourceButton->setEnabled(row >= 0); });
    connect(m_nullSourceButton, &QPushButton::clicked, this, [this] {
        // The index sent is the SELECTED ITEM's current position, never a row
        // cached earlier: the list is rebuilt from every poll and "sources" can
        // shrink or reorder between the click and this handler running.
        QListWidgetItem* item = m_sourcesList->currentItem();
        if (!item)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("null_source"),
                       QString::number(m_sourcesList->row(item)));
        emit requestSet(q);
    });
    bottom->addWidget(m_nullSourceButton);
    body->addWidget(bottomRow);

    m_noiseStatus = DiversityWidgets::makeReadoutLine(
        QStringLiteral("diversityWindowNoiseStatusLabel"),
        tr("noise reference: guard band · coherence 0.00"),
        tr("Where the gate measures \"noise\" from for the SNR figure."),
        frame);
    m_noiseStatus->setAccessibleName(tr("Noise reference"));
    m_noiseStatus->setAccessibleDescription(
        tr("Where the gate measures \"noise\" from when it works out a "
           "signal-to-noise figure. A GUARD BAND is a slice just outside the "
           "passband, which is honest while the band is quiet but wrong if "
           "something is sitting in the guard. IN-BAND means it is estimating "
           "the noise floor underneath the signal itself. The coherence figure "
           "is the same one the BALANCE block explains."));
    body->addWidget(m_noiseStatus);
    body->addStretch(1);
    return frame;
}

} // namespace AetherSDR
