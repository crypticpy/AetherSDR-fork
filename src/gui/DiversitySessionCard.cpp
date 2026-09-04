// One step's card on the START page. See DiversitySessionPage.h for why the
// workflow is a page of cards rather than a line of words.
//
// The card is a fixed frame: 86 px tall whatever it is saying, a header row
// of exactly three things (which step, what the gate says about it, the one
// button that fixes it) and three fixed lines of copy under them that are set
// once and never change again. Nothing on it is height-for-width and nothing
// on it moves when a poll lands -- the START page is five of these stacked,
// and a card that grew a line would push the OFFERS row off the bottom of a
// 1120x860 window.
//
// FOUR TONES, NO NEW COLOURS. "lit" is the step to do with AUTO CLEAN on (the
// accent the whole window already uses for its verdict lines), "state" the
// same step with AUTO CLEAN off (shown, not nudged -- the operator has said
// they are driving), "plain" a step behind you and "dim" one still ahead.
// They resolve to color.accent.bright, color.text.primary, color.text.primary
// and color.text.disabled: the three tokens the FLOW strip already keyed off,
// and not one token more.

#include "gui/DiversitySessionPage.h"

#include "core/ThemeManager.h"
#include "gui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// The card's own frame. Constant: the tone lives in the text colours, not in
// the box, because five boxes changing colour under each other reads as an
// alarm panel rather than as a checklist.
const char* kCardStyle =
    "QWidget#%1 { background: {{color.background.1}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px; }";

const char* kTitleStyleTemplate =
    "QLabel { color: %1; font-size: 11px; font-weight: bold;"
    " background: transparent; }";
const char* kStateStyleTemplate =
    "QLabel { color: %1; font-size: 10px; background: transparent; }";
const char* kBodyStyleTemplate =
    "QLabel { color: %1; font-size: 10px; background: transparent; }";

// Every card is this tall in every state -- see the header comment.
constexpr int kCardHeight = 86;
constexpr int kHeaderHeight = 18;
constexpr int kCureHeight = 18;

// The glyph in front of the title. A checklist reads as a checklist before a
// single word of it has been read; the same three glyphs the FLOW strip used,
// kept so the two never disagreed about what "done" looks like.
QString glyphForTone(const QString& tone)
{
    if (tone == QLatin1String("plain"))
        return QStringLiteral("✓");
    if (tone == QLatin1String("lit") || tone == QLatin1String("state"))
        return QStringLiteral("●");
    return QStringLiteral("○");
}

} // namespace

DiversitySessionCard::DiversitySessionCard(int index, QWidget* parent)
    : QWidget(parent)
    , m_index(index)
{
    setObjectName(QStringLiteral("diversityWindowSessionCard%1").arg(index));
    // A plain QWidget draws no background from a style sheet without this;
    // the alternative is a QFrame whose own frame we would then have to turn
    // off (WaveformsDialog.cpp makes the same call for the same reason).
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(kCardHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ThemeManager::instance().applyStyleSheet(
        this, QString::fromLatin1(kCardStyle).arg(objectName()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 6, 10, 6);
    root->setSpacing(4);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("diversityWindowSessionCard%1Title").arg(index));
    m_title->setWordWrap(false);
    m_title->setFixedHeight(kHeaderHeight);
    header->addWidget(m_title);

    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("diversityWindowSessionCard%1State").arg(index));
    m_state->setWordWrap(false);
    m_state->setFixedHeight(kHeaderHeight);
    // Ignored horizontally so the gate's longest state sentence clips rather
    // than dragging the window's minimum width out past the 1120 it opens at.
    m_state->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_state->setMinimumWidth(0);
    header->addWidget(m_state, 1);

    m_cure = new QPushButton(this);
    m_cure->setObjectName(QStringLiteral("diversityWindowSessionCard%1Cure").arg(index));
    m_cure->setCursor(Qt::PointingHandCursor);
    m_cure->setFixedHeight(kCureHeight);
    applyToggleButtonStyle(m_cure);
    m_cure->hide();
    connect(m_cure, &QPushButton::clicked, this,
            [this] { emit cureActivated(m_stepId); });
    header->addWidget(m_cure);

    root->addLayout(header);

    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("diversityWindowSessionCard%1Body").arg(index));
    // Three fixed lines joined with "\n" -- never wrapped. See the header.
    m_body->setWordWrap(false);
    m_body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_body->setMinimumWidth(0);
    m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(m_body, 1);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this] { applyTone(); });

    m_tone = QStringLiteral("dim");
    applyTone();
}

void DiversitySessionCard::setStep(const DiversitySessionModel::Step& step)
{
    m_stepId = step.id;

    m_title->setText(QStringLiteral("%1 %2 · %3")
                         .arg(glyphForTone(step.tone), QString::number(m_index),
                              step.title));
    m_title->setAccessibleName(step.title);

    m_state->setText(step.state);
    m_state->setAccessibleName(tr("%1 state").arg(step.title));
    // The whole sentence, for a screen reader and for a hover, because the
    // label itself clips rather than wraps.
    m_state->setToolTip(step.state);
    m_state->setAccessibleDescription(step.state);

    // The three copy lines. Set every call rather than once in the
    // constructor: a step this model has no copy for returns empty strings,
    // and a card that kept last call's prose would be describing a different
    // step. They do not change in practice -- this only makes that a fact
    // rather than an assumption.
    const QStringList lines{step.gives[0], step.gives[1], step.when};
    m_body->setText(lines.join(QStringLiteral("\n")));
    m_body->setAccessibleName(tr("%1, what it gives you").arg(step.title));
    m_body->setAccessibleDescription(lines.join(QStringLiteral(" ")));

    const bool offer = !step.cure.kind.isEmpty() && !step.cure.label.isEmpty();
    m_cure->setText(step.cure.label);
    m_cure->setAccessibleName(offer ? tr("%1: %2").arg(step.title, step.cure.label)
                                    : tr("%1 cure").arg(step.title));
    m_cure->setToolTip(offer ? tr("%1 — %2").arg(step.title, step.state) : QString());
    m_cure->setAccessibleDescription(m_cure->toolTip());
    m_cure->setVisible(offer);

    if (m_tone != step.tone) {
        m_tone = step.tone;
        applyTone();
    }
}

// One re-style per tone change, never per poll: setStyleSheet() reparses the
// sheet and throws away the widget's cached style, which is not a thing to do
// once a second on five cards.
void DiversitySessionCard::applyTone()
{
    const ThemeManager& tm = ThemeManager::instance();
    const QString accent = tm.cssFragment(QStringLiteral("color.accent.bright"));
    const QString normal = tm.cssFragment(QStringLiteral("color.text.primary"));
    const QString quiet = tm.cssFragment(QStringLiteral("color.text.secondary"));
    const QString dim = tm.cssFragment(QStringLiteral("color.text.disabled"));

    QString titleColour = dim;
    QString stateColour = dim;
    QString bodyColour = dim;
    if (m_tone == QLatin1String("lit")) {
        titleColour = accent;
        stateColour = accent;
        bodyColour = quiet;
    } else if (m_tone == QLatin1String("state")) {
        titleColour = normal;
        stateColour = normal;
        bodyColour = quiet;
    } else if (m_tone == QLatin1String("plain")) {
        titleColour = normal;
        stateColour = quiet;
        bodyColour = quiet;
    }

    ThemeManager::instance().applyStyleSheet(
        m_title, QString::fromLatin1(kTitleStyleTemplate).arg(titleColour));
    ThemeManager::instance().applyStyleSheet(
        m_state, QString::fromLatin1(kStateStyleTemplate).arg(stateColour));
    ThemeManager::instance().applyStyleSheet(
        m_body, QString::fromLatin1(kBodyStyleTemplate).arg(bodyColour));
}

} // namespace AetherSDR
