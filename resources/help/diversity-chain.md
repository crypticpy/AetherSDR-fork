# Chain

## The CHAIN window

`OPEN CHAIN` at the right-hand end of the page-tab row opens it, from
whichever page you are on. It is a separate window rather than a fifth
page — there used to be a FILTER tab and this is what replaced it —
because the receive chain is not a pair feature: an RSPdx has a
passband, a blanker and an AGC just as an RSPduo does, and the window
renders whatever `/filter` reports on either.

It is read left to right, as a block diagram, in four labelled groups
with an arrow between them.

**FRONT END** is one summary card, not seven boxes. Antenna port,
broadcast traps, PRE/ATT, RF GAIN, RF AGC, the analogue roofing filter
and the sample rate: one line each, and one hint under all of them —
`antenna, gain and rate are set in the GATE panel` — with an `OPEN
PANEL` button under it that brings the Aether-gate applet's own panel
to the front. Nothing in that card is changed from this window except
the roofing filter, on a receiver whose driver offers its IF
bandwidths, and GUARD below.

HEADROOM and GUARD are two more rows on the same card, when
`/frontend` says a guard is fitted. HEADROOM is a measured line —
how far the last second's strongest sample sat below full scale,
and how many samples clipped — in the warning tone the instant it
drops under 3 dB or anything clips. GUARD is the one control the
card carries: a switch, ON or OFF, and beneath it a floor menu that
limits how far down the switch is allowed to take the LNA. The
pane's GUARD entry names the last thing the guard actually
did — `stepped 0 → 1 at 11:42, clipping` — rather than repeat the
card's own on/off sentence. If the receiver refuses a GUARD write the
row that asked shows the reason, not just the inspector below. When
the gate's dBm scale no longer matches the LNA state GUARD
moved it to, a one-line note says so under the hint — the numbers
on every other card are still true, but relative rather than
calibrated until the scale is re-trimmed. A receiver with no guard
fitted (`available: false`) shows neither row; the card is not
padded out with dashes for a stage that does not exist.

**PAIR** is what the two loops do together: ALIGN, NB, COMBINER,
SUB-BAND NULL, POST-FILTER and SUB-BAND MRC. On a single-tuner device
the gate does not send those rows and the column is simply not there.
The last two are the stages that only exist because there are two
receivers — a post-filter that works on the *coherence* between them,
and a per-bin weight that only makes sense with a spatial map behind
it. Neither has an equivalent on a single antenna, which is why they
outlived the FILTER page every other stage on it left before them.

**POST-FILTER** is OFF / V1 / V2, following the gate's own
`post.enabled`/`post.version`. V1 is the older, always-on-style
suppressor; V2 learns the noise floor between words and subtracts it,
worth trying on faint SSB the combiner has already improved but not
fully cleaned up. The card's measured line shows what V2 is actually
doing when the gate reports numbers for it — `in 7.7 dB → out 10.8 dB,
pauses 10 %` — and just `v1` or a dash otherwise.

**SUB-BAND MRC** is one switch. On, the gate gives every bin in the
passband its own weight from the spatial map instead of one weight for
the whole channel — a small, situational gain, and a lab switch more
than a daily one. The measured line shows the gate's own numbers when
it has them, `+0.2 dB over broadband, 120 bins`, and a dash otherwise.

Both write immediately through `/diversity/set` (`post=off|on|v2`,
`mrc=on|off`) rather than through `/filter/set`, because both belong to
the combiner rather than to the slice filter. There is no Apply button
and no write-hold: the gate's reply is the next `/diversity` poll's
answer, and a switch simply follows whatever that poll reports.

**PASSBAND** is the filter you actually tune: the digital roof, the
slice filter, the passband itself, AUTO WIDTH, SHAPE, IF NOTCH,
ANF/DNF, CONTOUR, APF and the RX EQ tilt. It is the long group, so past
seven stages it draws in two columns rather than growing a scrollbar.

**OUT** is the detector, the AGC and the hand-off to the app's own noise
reduction.

Every live stage is one card: the stage's NAME, one measured line, and
one control — a switch, a menu, or a button where the gate offered a
verb (`REALIGN`). The measured line is the part worth knowing about. The
gate writes a whole sentence per stage (`track · φ 157.3° · -4.6 dB ·
SNR a 15.2 / b 12.9 → 16.4 dB`), and a card that printed it would have
to cut it off mid-word. So each stage names which of that sentence's
fields it keeps, and the card drops whole fields and then whole *words*
until what is left fits. **A card never ends in an ellipsis.** The whole
sentence is on the hover and in the pane, always.

**MODE** at the top is `PHONE` / `CW` / `DATA`, and it does two things.
It decides which stages are drawn — the rest collapse into `STAGES THIS
MODE DOES NOT USE (n)`, which is a disclosure, not a switch: expanding
it turns nothing on. And it offers one button, `SET UP FOR PHONE` or
`SET UP FOR CW`, which applies that mode's ordered list of writes one at
a time, waiting for the receiver after each. The line under the button
says what that does to the sound. `DATA` is deliberately dead, with `No
set for data yet.` on its hover: the gate has no data-specific stage and
a button that wrote nothing would be a lie.

**Two tabs.** The window has two tabs at the top, `CHAIN` and `VISUAL`, and they are
two views of the same `/filter` poll. `CHAIN` is the block diagram
above. `VISUAL` is the passband drawn as a curve at the full width of the
window, over what is actually arriving: the measured response, the
spectrum filled in under it with the noise floor as a dotted line, each
notch as a vertical line labelled with its depth, each tone the ANF is
holding as a dashed line, the CONTOUR centre and the APF centre as ticks
along the bottom, and the AUTO WIDTH edges as faint lines when AUTO is
on. The row of coloured squares under the picture is the key to all of
it, and it names only the marks that are actually on the plot.

**The axis is the passband's.** The gate answers on a fixed audio array,
but the picture spans the passband in force plus a margin either side,
so a 250 Hz CW filter is not four pixels wide in the middle of 3 kHz of
empty air. The span is recomputed when a drag ENDS, never during one —
an axis that moved under the handle you were holding would be
unpointable — and any mark that falls outside it is clamped to the edge
rather than dropped, so it is still there to be clicked.

**A second trace** shows what you are actually *hearing*: the FFT of this
application's own receive audio, after the gate's chain and after the
client chain both. It only runs while `VISUAL` is the tab in front. The
gate's spectrum is in dB below its own peak and this one is dBFS after a
volume control, so the two are pinned to the same floor tick and read
against their own floors rather than laid on one another — a comparison
of *shape*, which is the honest one: if the response curve says 40 dB
down at 3 kHz and this trace is not, something downstream is not doing
what the curve claims.

The caption over the picture says what it is of — `PASSBAND · LSB ·
CW-R` — and adds `NOT ANSWERING` when the last poll failed, keeping the
old curve on screen rather than blanking something that was true a
second ago. The line under the picture reads what the gate said:
`350–2400 Hz · floor -70.0 dB · 2 notches · SHARP · 1023 taps · 49 Hz
skirt`. Hover anywhere and the top-right corner reads the frequency and
how far the band stands over the noise floor there, `+34.0 dB over
floor` — a measurement of the signal, not of where your pointer is.

Only the tab in front is fed. With `CHAIN` in front the picture is not
walked, fingerprinted or painted at all; turn to `VISUAL` and it catches
up at once from the last body, without waiting for a poll. A chain
window left open on `CHAIN` behind the main window costs nothing.

**Presets.** `PRESETS` sits over the picture on the `VISUAL` tab: a menu, `LOAD`,
`SAVE AS...`, `DELETE`, and a line that says which preset is in force.

A preset is the **whole chain as you left it** — every stage on the
current chain that can be written from this window, with its on/off or
its value: the roof, the blanker, the shape, both notches switches, the
contour, the APF, the EQ, the AGC, and on a dual-tuner pair the
combiner, SUB-BAND NULL and POST-FILTER too. What is *not* in one is
anything this window has never been able to move (the antenna port, the
LNA, the sample rate — those are set in the GATE panel), and the
passband edges themselves, which belong to the band you are on rather
than to a way of listening.

`SAVE AS...` opens a field where the menu was; type a name and press
Enter (Escape leaves everything as it was). `LOAD` applies the chosen
preset the same way `SET UP FOR PHONE` does — one write at a time, each
waited for, in the gate's own signal order, with the progress line
counting them off — and a stage the preset names that this receiver
does not have is skipped and said out loud: `this receiver has no
POST-FILTER, SUB-BAND NULL — the rest was set`. A preset saved on the
pair loads on one tuner without complaint. `DELETE` does not ask first;
it leaves an `UNDO` for eight seconds instead.

**Where they live.** One JSON file per preset, under the app's data
folder in `chain-presets/` — on macOS
`~/Library/Application Support/AetherSDR/chain-presets/net-night-80m.json`.
That is the import and the export: a preset is a file you can copy to
another machine, mail to a friend, or drop into that folder by hand, and
it is in the menu the next time the row rereads the folder. The file is
plain enough to edit — a name, the mode it was saved in, and the stages
with their values in order.

**What "edited" means.** The line under the menu reads `in force: Net
night` after a load or a save. The moment any stage the preset names
reads differently from what the preset says — you switched the blanker
off from the diagram, or another client turned the shape sharp — it
reads `in force: Net night (edited)`, and the menu entry gains the same
word. Put the stage back and the word goes away. It is a comparison
against the receiver's own answers, not a memory of what you pressed: a
write the gate refused never makes a preset edited, and a knob turned
from somewhere else always does.

**Working on the picture.** Everything on `VISUAL` that is drawn can be pointed at.

- **Drag an edge** of the passband and that edge alone is written
  (`low=` or `high=`) when you let go; the other edge is left to the
  auto-width tracker. A poll that lands mid-drag does not snatch the
  handle back.
- **Double-click** inside the passband to place a notch there;
  **right-click** a notch to take it away.
- **Drag the roof handle**, the small triangle in the labelled strip
  along the top of the plot (`ROOF 3.0 kHz · offset −120 Hz`), and
  release to write `roof_offset_hz=`. The strip has its own axis,
  `-offset_max_hz` to `+offset_max_hz`, so a negative offset is exactly
  as reachable as a positive one. The roof itself is a faint wash under
  the curve, `digital_hz` wide, shown where it sits even with
  `PEAK OFFSET` off (dashed, to say where it would sit if switched on)
  and reaching the gutter with no line pretending an edge is there when
  the true one falls outside the plot. Neither the wash nor the handle
  is drawn at all once the gate's own `digital_active` says the roof is
  not actually in circuit; no handle is drawn once `offset_max_hz`
  reaches 0 even when it is.
- **Drag a notch** to move it. That is genuinely two writes, a clear
  and an add, and they go through the same one-at-a-time sequencer as a
  preset — the add waits for the clear's answer.
- **Click any mark** — an edge, a notch, an ANF tone, the CONTOUR or
  APF tick, an AUTO edge — and the window turns to the `CHAIN` tab with
  that stage's card selected, scrolled into view, and its pane
  filled. The pointer becomes a hand over a mark that is a door rather
  than a handle. A click on open curve does nothing.

**WHAT THIS DOES**, the pane along the bottom, is titled with the
selected stage's own name — `AUTO WIDTH — what it does` — and answers
four questions in order: what the stage does to what you hear, what it
is doing now (the card's line spelled out whole), what you would hear
with it off (`with it off: ...`), or, on a row nothing here can switch,
the reason it is fixed, then the levels the gate measured through it —
`in -97.4 · out -101.2 dB`, with a dash for a leg nothing measures — but
only when it measured at least one leg; a stage the gate never scores at
all shows no levels line rather than a dashed `in — · out — dB`. Last,
the control at full size. With nothing selected the pane's title reads
`WHAT THIS DOES` and the first line says `Click a stage.`

Arrow keys walk the diagram in signal order and stop at both ends; space
presses the selected stage's switch.

Two things about writes are worth knowing. **Nothing is optimistic.** A
switch moves when the receiver says the stage changed, never when you
press it, and while a write is out the control is greyed. **A refusal is
quoted.** When the receiver says no, its own words appear in the
pane and on the card that asked, and the row does not move — a
refused value never happened. The status line in the corner says only
one of three things: `live`, `applying...`, or `no connection`.

### ROOFING · DIGITAL — PEAK OFFSET

The digital roof's centre can be dragged off the slice centre, the Icom
VC-Tune / Kenwood Digi-Sel move: park a strong neighbour on the roof's
skirt instead of inside it, so the AGC and the combiner never see it. A
PEAK OFFSET check mark sits on the ROOFING · DIGITAL card itself: on
applies the remembered offset, off holds it without discarding it. The
card's detail line appends `· offset +800 Hz` while it is in force. The
offset is clamped so the roof still covers the whole passband; a roof too
narrow for the passband it is meant to carry holds the offset at 0 and
reports `offset_max_hz: 0`. The clamp is applied when the offset is
written; narrowing the passband afterwards does not re-clamp it.

**Checks.** A card can carry check marks of its own, one per entry in the
gate's `checks[]`, labelled and wired exactly as the gate wrote them; no
route or query is built in the app. ROOFING · DIGITAL's `PEAK OFFSET` is
the first. A check greys while its own write is out and moves only on
read-back, the same as every other control; a refusal is quoted on the
card and in the pane.

