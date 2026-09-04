// AetherGateChainStagePrivate.h -- the constants, stylesheet strings and
// small helpers AetherGateChainStage.cpp and AetherGateChainTile.cpp share.
// Private to those two units; nothing else includes it.
#pragma once

#include "gui/AetherGateChainStage.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QString>

namespace AetherSDR::chainstage {


// The tile's rows. A block diagram whose blocks change size when a number
// gains a digit is not a diagram, so the width is fixed and the height has a
// floor -- 220 x 84 is the operator's own number (design §0.3 item 8), and a
// tile carrying a refusal is allowed to grow past the floor rather than elide
// the one sentence that says what went wrong.
inline constexpr int kRowHeight = 20;
inline constexpr int kLargeRowHeight = 26;
inline constexpr int kDetailControlWidth = 300;

// A switch is a small thing on a card, not the card's headline. The first
// build gave it the whole width and the operator read the strip as a wall of
// ON buttons with dim words over them; the NAME leads now and the switch is
// the size of the word on it.
inline constexpr int kSwitchWidth = 54;
inline constexpr int kMenuWidth = 116;

// What the one measured line has to fit inside, on each of the two shapes.
inline constexpr int kCardTextWidth = kChainCardWidth - 4 - 14;
inline constexpr int kLineTextWidth =
    kChainSummaryWidth - 14 - kChainSummaryNameWidth - 6;

// The lowest and highest a synthesised digital roofing filter can be asked
// for. 100 Hz is narrower than any radio's narrowest CW roofing filter and
// 25 kHz is the gate's own decimated rate -- past either end there is nothing
// to design.
inline constexpr int kFreeEntryMinHz = 100;
inline constexpr int kFreeEntryMaxHz = 25000;

// The border is 2 px in BOTH states so that selecting a tile cannot move the
// text inside it by a pixel; only the colour changes. The accent is
// color.accent.bright, the same token the detail pane's title uses, which is
// how the tile and the pane say they are about the same stage (design §0.3
// items 3 and 7).
//
// A fixed row gets the dashed frame and no hover: nothing about it responds,
// and a border that looked like the others would be inviting a click that does
// nothing.
inline const char* const kTileStyle =
    "QFrame { border: 2px solid {{color.background.1}};"
    " border-radius: 4px; background: transparent; }"
    "QFrame[fixed=\"true\"] { border: 2px dashed {{color.background.1}}; }"
    "QFrame[line=\"true\"] { border: 2px solid transparent;"
    " border-radius: 3px; background: transparent; }"
    "QFrame[selected=\"true\"] { border: 2px solid {{color.accent.bright}}; }"
    "QFrame[line=\"true\"][selected=\"true\"] {"
    " border: 2px solid {{color.accent.bright}}; }";

// makeFieldLabel()/makeValue() with the colour taken down to the disabled
// token, for a row nothing in the product can move. Same size and weight, so
// a dimmed tile still reads as the same kind of object.
inline const char* const kDimNameStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 10px; font-weight: bold;"
    " background: transparent; }";

inline const char* const kDimValueStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 11px; font-weight: bold;"
    " background: transparent; }";

// The line under the value: why a fixed stage cannot move, or -- when the gate
// has just refused a write -- what it said. Two meanings, one line, told apart
// by colour rather than by position, so the tile's height does not change when
// a refusal arrives.
inline const char* const kUnderStyle =
    "QLabel { color: {{color.text.disabled}}; font-size: 10px;"
    " background: transparent; }"
    "QLabel[live=\"true\"] { color: {{color.accent.warning}}; }";

inline const char* const kSelectStyle =
    "QComboBox { background: {{color.background.1}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }"
    "QComboBox::drop-down { width: 14px; border: none; }"
    "QComboBox:disabled { color: {{color.text.disabled}}; }";

inline const char* const kFreeStyle =
    "QLineEdit { background: {{color.background.0}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px;"
    " font-size: 11px; padding: 1px 3px; }";

// A checks[] row's own box. Same three tokens DiversityWindowSite.cpp's
// kSubbandCheckStyle already wears for the per-bin-weight check -- the
// colour ratchet tracks call sites and never-before-seen colours, not a
// second stylesheet string that reuses tokens already in production, so this
// is not a new colour, it is the same role on a second checkbox.
inline const char* const kCheckStyle =
    "QCheckBox { color: {{color.text.primary}}; font-size: 10px; spacing: 5px;"
    " background: transparent; }"
    "QCheckBox::indicator { width: 11px; height: 11px; border-radius: 2px;"
    " border: 1px solid {{color.toggle.border}};"
    " background: {{color.toggle.background}}; }"
    "QCheckBox::indicator:checked {"
    " background: {{color.toggle.accent.background.checked}};"
    " border: 1px solid {{color.toggle.accent.border.checked}}; }"
    "QCheckBox:disabled { color: {{color.text.disabled}}; }";

inline QString emDash()
{
    return QStringLiteral("—");
}

inline QString suffixFor(const QString& id)
{
    QString clean = id;
    clean.replace(QLatin1Char(' '), QLatin1Char('_'));
    return clean;
}

// Elide rather than wrap. Every detail cell in this window has a fixed field
// width (DiversityWidgets::makeReadoutLine reserves its worst case), and a
// gate sentence longer than the field lives on the hover instead of reflowing
// the row -- there is exactly one setWordWrap() in this codebase and it is
// false.
inline void setElided(QLabel* label, const QString& text, int width)
{
    const QFontMetrics fm(label->font());
    label->setText(fm.elidedText(text, Qt::ElideRight, width));
    label->setToolTip(text);
    label->setAccessibleDescription(text);
}

// Tabular figures: every digit the same width, so a number that changes twice
// a second does not shuffle the characters beside it. Purely a font feature --
// the face, the size and the colour token are all unchanged.
inline void applyTabularFigures(QLabel* label)
{
    QFont f = label->font();
    f.setFeature(QFont::Tag("tnum"), 1);
    label->setFont(f);
}

// The one reason the FRONT END card prints once, under all of its rows,
// instead of once per row.
inline QString frontEndSharedWhy()
{
    return QCoreApplication::translate("AetherGateChainStage",
                                       "set in the GATE panel");
}


} // namespace AetherSDR::chainstage
