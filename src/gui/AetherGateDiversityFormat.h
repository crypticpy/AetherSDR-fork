#pragma once

// Diversity section text formatting, split out of AetherGateDiversityPanel so
// that file stays under its own size budget (see AetherGateDiversityPanel's
// header comment for the extraction this came from -- AGENTS.md's "files
// must not grow like this" is the rule being followed). Pure string
// formatting only: no widgets, no network, no state. Each function takes the
// same QJsonObject shape AetherGateDiversityPanel::applyDiversity() already
// reads defensively and returns exactly the text that ends up on screen.

#include <QString>

class QJsonObject;

namespace AetherSDR {
namespace DiversityFormat {

// The status line USED to carry lag/aligned/peak/SNR/rn/mod -- every one of
// those changes on nearly every poll, which is what the operator's "the
// little phase thing is bouncing all around ... it glitches out the
// interface" complaint was actually about. What is left is a SHORT, FIXED
// set of phrases -- "aligned · lag <n>", "not aligned", "realigning…" -- so
// the label's width still varies a little between those three, but never
// continuously the way a live dB or sample count did.
QString status(const QJsonObject& diversity);

// The worst-case width status() can produce, used only to size
// gateDiversityStatusLabel's minimum width -- not itself ever shown. A
// 5-digit lag (plus its sign) is generous: 4158 samples is what a real
// RSPduo misalignment looks like (see rspduo-diversity-design's ring-offset
// note), so this is headroom, not a realistic value.
QString statusWorstCasePhrase();

// "7.111–7.114 MHz   coh 0.57" (or, when lo/hi are within 500 Hz of each
// other, "7.111 MHz   coh 0.57" at their centre) -- one /diversity "sources"
// entry as a gateDiversitySourcesList row's visible text. Phase/ratio moved
// to the item's tooltip (sourceTooltip below): the sidebar's 250px width has
// no room for them on the row itself without either a horizontal scrollbar
// or mid-word truncation.
QString sourceListText(const QJsonObject& source);

// "3.512–3.560 MHz · coh 0.82 · 141° · −2.1 dB" -- the same source entry's
// full detail, for the list item's tooltip rather than its row text.
QString sourceTooltip(const QJsonObject& source);

} // namespace DiversityFormat
} // namespace AetherSDR
