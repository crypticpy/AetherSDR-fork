// The NEXT strip's drawing and its one carried-over control. See
// DiversityNextStrip.h for why the footer says one step instead of five.
//
// The one rule this file keeps, inherited from the strip it replaces: it
// never invents a fact. Every word on the line is either the title
// DiversitySessionModel gave the step, the state sentence that model built
// out of the gate's own fields, or a number the dig payload itself reported.
// "NEXT" and "listening" are the only two words this file adds, and both are
// about the strip rather than about the radio.

#include "gui/DiversityNextStrip.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// The AppSettings key the collapse choice lives under. "True"/"False"
// strings, the shape every other boolean key in this window uses (see
// AetherGateDiversityPanel.cpp's kWindowVisibleKey).
const char* kCollapsedKey = "DiversityNextStripCollapsed";

// One line, one sheet. Same token and size the FLOW line wore, so the footer
// did not change weight when it changed shape; the two per-part colours are
// inlined into the rich text because a colour that changes with the state
// cannot come from a selector.
const char* kLineStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px;"
    " background: transparent; }";

// The whole strip is one line of the window's height budget and nothing on
// it may make it taller: 22 px for the row, 20 for the buttons in it.
constexpr int kStripHeight = 22;
constexpr int kButtonHeight = 20;

QString dash()
{
    return QStringLiteral("—");
}

// "+4.1" / "−0.6" -- a real minus sign, because this is a number in a
// sentence rather than a cell in a column.
QString signedDb(double v)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', 1);
    return QStringLiteral("+%1").arg(v, 0, 'f', 1);
}

// "1:12" from 72 seconds -- comparable with the button that started the run,
// which offers one, three and five minutes.
QString clockText(double seconds)
{
    const qint64 total = qint64(std::llround(std::max(0.0, seconds)));
    return QStringLiteral("%1:%2")
        .arg(total / 60)
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

DiversityNextStrip::DiversityNextStrip(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("diversityWindowNextStrip"));
    setAccessibleName(tr("Next step"));
    setFixedHeight(kStripHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_collapsePref = AppSettings::instance()
                         .value(QLatin1String(kCollapsedKey), QStringLiteral("True"))
                         .toString()
                     == QStringLiteral("True");

    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(6);

    m_line = new QLabel(this);
    m_line->setObjectName(QStringLiteral("diversityWindowNextLine"));
    m_line->setAccessibleName(tr("Next step"));
    m_line->setToolTip(
        tr("The one thing left to do, and the gate's own words for why. The "
           "whole order -- RECEIVER, SITE NOISE, BAND, STATION, then LISTEN "
           "-- is on the START page with what each step buys you. Once all "
           "four are behind you this line collapses to who is talking; click "
           "it to open it again."));
    m_line->setTextFormat(Qt::RichText);
    // The collapse toggle is a link in this label rather than a sixth button:
    // it is the line itself that opens and closes, and a control only a mouse
    // can reach is not one.
    m_line->setTextInteractionFlags(Qt::LinksAccessibleByMouse
                                    | Qt::LinksAccessibleByKeyboard);
    m_line->setWordWrap(false);
    m_line->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_line->setMinimumWidth(0);
    ThemeManager::instance().applyStyleSheet(m_line, QString::fromLatin1(kLineStyle));
    connect(m_line, &QLabel::linkActivated, this, [this](const QString& href) {
        if (href != QLatin1String("toggle"))
            return;
        m_collapsePref = !m_collapsePref;
        AppSettings::instance().setValue(QLatin1String(kCollapsedKey),
                                         m_collapsePref ? QStringLiteral("True")
                                                        : QStringLiteral("False"));
        rebuild();
    });
    m_row->addWidget(m_line, 1);

    m_button = new QPushButton(this);
    m_button->setObjectName(QStringLiteral("diversityWindowNextButton"));
    m_button->setAccessibleName(tr("Do the next step"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFixedHeight(kButtonHeight);
    applyToggleButtonStyle(m_button);
    m_button->hide();
    connect(m_button, &QPushButton::clicked, this,
            [this] { emit cureActivated(m_nextId); });
    m_row->addWidget(m_button);

    // Re-render on a theme change: the two per-part colours are inlined into
    // the rich text, so unlike a stylesheet they are not re-resolved for us.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this] { rebuild(); });

    updateAutoCleanButton();
    rebuild();
}

void DiversityNextStrip::setDigControls(QWidget* controls)
{
    if (!controls || !m_row)
        return;
    controls->setParent(this);
    m_row->addWidget(controls);
}

bool DiversityNextStrip::collapsed() const
{
    return m_allDone && m_collapsePref;
}

// --------------------------------------------------------------------------
// Incoming state
// --------------------------------------------------------------------------

void DiversityNextStrip::setNext(const DiversitySessionModel::Step& next, bool haveNext,
                                 const QString& listenState, bool allDone)
{
    m_haveNext = haveNext;
    m_nextId = next.id;
    m_title = next.title;
    m_state = next.state;
    m_cureLabel = next.cure.kind.isEmpty() ? QString() : next.cure.label;
    m_listenState = listenState;
    m_allDone = allDone;
    rebuild();
}

void DiversityNextStrip::applyDiversity(const QJsonObject& d, bool available)
{
    m_governor = available ? chainAutoParseGovernor(d) : ChainAutoGovernor();
    updateAutoCleanButton();
    rebuild();
}

void DiversityNextStrip::applyDig(const QJsonObject& dig)
{
    m_digAvailable = dig.value(QStringLiteral("available")).toBool();
    m_digRunning = m_digAvailable && dig.value(QStringLiteral("running")).toBool();
    m_digPhase = dig.value(QStringLiteral("phase")).toString();
    m_digVerdict = dig.value(QStringLiteral("verdict")).toString();
    m_digError = dig.value(QStringLiteral("error")).toString();
    m_digCancelled = dig.value(QStringLiteral("cancelled")).toBool();
    m_digGainDb = dig.value(QStringLiteral("gain_db")).toDouble();
    m_digElapsedS = dig.value(QStringLiteral("elapsed_s")).toDouble();
    m_digSeconds = dig.value(QStringLiteral("seconds")).toDouble();
    rebuild();
}

bool DiversityNextStrip::digAwaitingVerdict() const
{
    return m_digAvailable && !m_digRunning && m_digPhase == QLatin1String("done")
           && m_digVerdict.isEmpty() && !m_digCancelled && m_digError.isEmpty();
}

void DiversityNextStrip::clear()
{
    m_haveNext = false;
    m_title.clear();
    m_state.clear();
    m_cureLabel.clear();
    m_listenState.clear();
    m_allDone = false;
    // The dig goes away with the gate: a run whose status nothing is
    // answering for is not a run this strip can say anything true about.
    m_digAvailable = false;
    m_digRunning = false;
    m_digPhase.clear();
    m_digVerdict.clear();
    m_digError.clear();
    m_digCancelled = false;
    m_digGainDb = 0.0;
    m_digElapsedS = 0.0;
    m_digSeconds = 0.0;
    m_governor = ChainAutoGovernor();
    updateAutoCleanButton();
    rebuild();
}

// --------------------------------------------------------------------------
// The one carried-over control
// --------------------------------------------------------------------------
//
// AUTO CLEAN's switch, moved here whole from DiversityFlowStripAuto.cpp when
// the strip under it was replaced: same object name, same face, same fixed
// short tooltip, same not-optimistic discipline (a click never toggles the
// button -- it emits, the window writes, and the next poll's governor block
// is what moves it). The operator was explicit that nothing else goes on
// this switch, so nothing else does.

void DiversityNextStrip::updateAutoCleanButton()
{
    if (!m_autoCleanButton) {
        m_autoCleanButton = new QPushButton(tr("AUTO CLEAN"), this);
        m_autoCleanButton->setObjectName(
            QStringLiteral("diversityWindowFlowAutoCleanButton"));
        m_autoCleanButton->setAccessibleName(tr("AUTO CLEAN switch"));
        m_autoCleanButton->setCheckable(true);
        m_autoCleanButton->setCursor(Qt::PointingHandCursor);
        m_autoCleanButton->setFixedHeight(kButtonHeight);
        applyToggleButtonStyle(m_autoCleanButton);
        // NOT Ignored horizontally, which is what the FLOW strip used: a
        // stretch-0 Ignored item beside a stretch-1 sibling -- which m_line
        // is -- is allotted zero width, and QWidget::setGeometry then clamps
        // it back up to its own minimum WITHOUT moving anything after it, so
        // the switch painted straight over the first 140 px of the line. The
        // face is one of two short fixed strings, so there is nothing here
        // that wants to shrink: a fixed width off the widest of them is both
        // the honest policy and the one that leaves the line its own room.
        m_autoCleanButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_autoCleanButton->setMinimumWidth(
            m_autoCleanButton->fontMetrics().horizontalAdvance(
                QStringLiteral("AUTO CLEAN ON"))
            + 40);
        connect(m_autoCleanButton, &QPushButton::clicked, this,
                [this](bool checked) { emit requestAutoCleanToggle(checked); });
        // Leftmost: the "really visible" the operator asked for, on the one
        // line every page of this window keeps on screen.
        if (m_row)
            m_row->insertWidget(0, m_autoCleanButton);
    }

    m_autoCleanButton->setVisible(m_governor.available);
    const QSignalBlocker block(m_autoCleanButton);
    m_autoCleanButton->setChecked(m_governor.available && m_governor.autoOn);
    chainAutoSetButtonIndicator(m_autoCleanButton, chainAutoIndicatorLine(m_governor),
                                chainAutoStateWord(m_governor));
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

// What the dig adds to the line. A run is the thing happening now on
// whatever page the operator wandered to, so it is quoted from all five --
// the same reasoning the FLOW strip quoted its own DIG step from every page.
QString DiversityNextStrip::digTail() const
{
    if (!m_digAvailable)
        return QString();
    if (m_digRunning) {
        return tr(" · DIG %1 of %2 · %3 dB · %4")
            .arg(clockText(m_digElapsedS), clockText(m_digSeconds),
                 signedDb(m_digGainDb),
                 chainAutoDigStartedByAuto(m_governor) ? tr("started by AUTO")
                                                       : tr("started by you"));
    }
    if (!m_digError.isEmpty())
        return QStringLiteral(" · ") + m_digError;
    if (m_digCancelled)
        return tr(" · DIG found %1 dB (put back)").arg(signedDb(m_digGainDb));
    if (digAwaitingVerdict())
        return tr(" · DIG done · %1 dB — better or worse?").arg(signedDb(m_digGainDb));
    return QString();
}

void DiversityNextStrip::rebuild()
{
    if (!m_line)
        return;

    const ThemeManager& tm = ThemeManager::instance();
    const QString normal = tm.cssFragment(QStringLiteral("color.text.primary"));
    const QString dim = tm.cssFragment(QStringLiteral("color.text.disabled"));
    const QString accent = tm.cssFragment(QStringLiteral("color.accent.bright"));

    QString head;
    QString body;
    QString colour = normal;
    if (collapsed()) {
        // Every chore behind us: the one fact worth a row of the window is
        // who is talking and what the pair is buying on them, which is the
        // LISTEN step's own state sentence.
        head = tr("● listening");
        body = m_listenState.isEmpty() ? dash() : m_listenState;
    } else if (!m_haveNext) {
        // nextStep() answers -1 across a dead gate: nothing is next because
        // nothing can be answered.
        head = tr("NEXT");
        body = QStringLiteral("%1 · %2").arg(dash(), tr("gate not answering"));
        colour = dim;
    } else {
        head = tr("NEXT");
        body = QStringLiteral("%1 · %2")
                   .arg(m_title, m_state.isEmpty() ? dash() : m_state);
        // A cure to press is the model's own "lit": the accent goes on the
        // line as well as on the button so the two read as one offer.
        if (!m_cureLabel.isEmpty())
            colour = accent;
    }
    body += digTail();

    m_plain = QStringLiteral("%1 · %2").arg(head, body);
    // Escaped: talker names, mode words and the gate's own error strings are
    // all in `body`, and a station called "<b" must not write markup here.
    const QString escaped = m_plain.toHtmlEscaped();
    // The whole line is the collapse toggle, but only once there is something
    // to collapse -- before that a click would have nothing to hide.
    if (m_allDone) {
        m_line->setText(QStringLiteral("<a href=\"toggle\" style=\"color:%1;"
                                       "text-decoration:none;\">%2</a>")
                            .arg(colour, escaped));
    } else {
        m_line->setText(
            QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(colour, escaped));
    }
    m_line->setAccessibleDescription(m_plain);

    if (m_button) {
        const bool offer = !collapsed() && m_haveNext && !m_cureLabel.isEmpty();
        m_button->setText(m_cureLabel);
        m_button->setToolTip(offer ? tr("%1 — %2").arg(m_title, m_state) : QString());
        m_button->setAccessibleDescription(m_button->toolTip());
        m_button->setVisible(offer);
    }
}

} // namespace AetherSDR
