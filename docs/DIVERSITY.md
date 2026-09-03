# Diversity reception with an RSPduo and Aether-gate

This is the operator's guide to the two-antenna diversity combiner: what
it does, what it cannot do, how to drive it from AetherSDR, and how to read
the numbers it shows. The design roadmap lives in
[`DIVERSITY-ROADMAP.md`](DIVERSITY-ROADMAP.md).

## What it is

An SDRplay RSPduo has two coherent tuners. Aether-gate (the bridge that
presents the RSPduo to AetherSDR as a SmartSDR-protocol radio) runs both
tuners on the same frequency, aligns them sample-for-sample, and combines
them into one audio stream:

    y = (a + m·b) / sqrt(1 + |m|²)

where `a` and `b` are the two antennas and `m` is a complex weight (a phase
and a level ratio). Everything in the feature is about choosing `m`:

- **manual**: you set phase and ratio with two knobs.
- **null**: `m` minimises noise power. Kills one coherent noise source.
- **track**: `m` maximises the SNR of whoever is talking. The gate detects
  overs, fits a weight per over, remembers each talker's weight and recalls
  it in the block they key up in, and between overs spends the weight on
  nulling the noise instead.

Two antennas give **one degree of freedom**. The combiner can either steer
at one station or null one interferer, never both at once. That single
fact explains every limit below.

## What to expect, honestly

| Scene | What the combiner can do |
|---|---|
| Isotropic band noise (coherence between loops < 0.3) | Maximal-ratio gain only: about +1.5 dB when one loop is 4 dB down, +3 dB when they are equal. Measured live on 40 m: +0 to +1 dB over the better loop. |
| One coherent local noise source (coherence > 0.5) | A null of 10–25 dB on that source. This is the big win. |
| A station that fades differently on each loop | The fade guard switches to the surviving antenna within 0.2 s. |
| A steady carrier on frequency | Treated as noise after 1.5 s and nulled; speech over it is tracked normally. |
| Two stations in a QSO | Each gets its own remembered weight, applied on the first syllable. |
| A DX station under a pile-up | **Lock** on the DX: their overs get their beam, every other over is nulled. |

The two loops report a **phase difference**, not a bearing: the same
number covers two directions, and the window says "phase" for that reason.

## Hardware and launch

- RSPduo in dual-tuner mode (`mode=DT`), one antenna per tuner input.
  Two loops a few metres apart on the same band work well; the loops need
  not be matched.
- Start the gate with both tuners:

```
python -m aether_gate --adapter soapy --soapy-driver sdrplay \
    --soapy-args serial=<serial>,mode=DT --ae 127.0.0.1 --port 5992 \
    --gain 12 --samp-rate 125000 --bins 16384
```

- Connect AetherSDR to the gate as usual. The Aether-gate applet in the
  sidebar shows a **Diversity** section only when the gate reports
  `available: true`; single-tuner gates never show it.

Never probe the SDRplay service by opening devices on trial and error: the
SoapySDRPlay3 driver leaks a lock on failure and the service must then be
restarted.

## The sidebar

The sidebar entry is deliberately small: one status line (mode, the live
talker, the combiner's gain over the better loop), the mode selector, and
**Open Diversity window**. Everything else lives in the window, the same
way Aether Voice is hidden until opened. A compact scope can be kept in the
sidebar with the setting `AetherGateDiversityPanel_ShowScope`.

## The window

Open it from the sidebar button. Its geometry and visibility persist; it
reopens on the next run if it was open at exit.

Three strips sit above the pages, and they do three different jobs. They are
three rows rather than one because as one row they read as a single sentence
of controls, and half of them are not.

**Row 1 — the pages.** SLICE / BAND / SITE / FILTER, and nothing else;
`pages` is written at the right end of the row to say so. SLICE, described
below, is about the frequency you are tuned to. BAND is about the whole span
the gate can see, and is where you go to decide where to be tuned. SITE is
about neither: it is about your station — what kind of noise this address
makes, and what the world's beacon network measures your antennas to be
worth. FILTER is about what happens to the audio *after* the combiner: the
slice filter, drawn and driven.

**Row 2 — PAIR.** Everything on this row acts on the antenna pair itself and
applies whichever page you are looking at. MODE (off / manual / null /
track), HEAR (OUT — the combined output, A, B, STEREO — what goes to the
audio; the combiner keeps learning whichever you pick), *Hear A only*
(press-and-hold A/B comparison that restores the previous mode on release),
REALIGN, and CAPTURE with a duration: writes an aligned two-channel
recording on the gate.

REALIGN and CAPTURE both answer on their own faces, because both used to be
writes into silence. REALIGN reads `ALIGNING…` while the gate works and then
the result for three seconds — `LAG −63` if the alignment did not move,
`LAG −63 (was +4032)` if it did — and says the same thing on the footer strip
and in EVENTS. CAPTURE counts its own recording down (`REC 10 s`, `REC 9 s`,
…) and then reads `SAVED`, with the file's name on the footer strip for six
seconds. The footer strip is the one line visible from every page, which is
why both answers go there: on any page but SLICE there was previously no
sign either button had done anything at all.

**Row 3 — FLOW.** Five steps in the order that gets the best signal, each
showing what the gate currently says about it: `1 ALIGN`, `2 MODE`,
`3 HEAR`, `4 NOISE`, `5 FILTER`. The one to do next is lit, the ones behind
it are plain and the ones ahead are dim. Clicking a step goes to the page it
lives on, and where there is a single obvious action it also takes it —
step 1 realigns, step 2 sets track when the combiner is off, step 3 switches
HEAR to the combined output. Steps 4 and 5 only change the page, because
there is a choice to make on both and the strip should not make it for you.
Every state on the strip is a number or a word the gate said; nothing there
is computed by the window. See *A working session* below for what each step
is for.

**STEREO** puts loop A in the left channel and loop B in the right, with
one AGC gain for both so the loops keep their level difference. On two
speakers the loops become a soundstage: a station arriving from one side
sits off-centre, noise both loops see sits in the middle, and a fade on
one loop is a fade on one side. Give the app's own RX noise reduction a
miss while you listen this way — its chain works on a mono mix. In FM the
stereo monitor plays loop A (the discriminator keeps one phase state).

**Scope.** The polar dial shows the live weight as a filled dot (angle =
phase, radius = level ratio; inner ring equal level, rim B +20 dB) and each
remembered talker as a hollow ring with its id. The three lines under it
read: talking / weight moves / memory size; noise reference and coherence
and blanker activity; QRM state and passband flatness.

**A / B / OUT meters and gain.** SNR of each loop and of the output; `gain`
is OUT minus the better loop. On isotropic noise expect it to hover near
zero; that is the physics, not a fault.

**TALKERS.** The gate's memory: id, an editable name (double-click; stored
on the gate and persisted across restarts by the talker's spatial
signature), phase, level, hits, last heard, first heard, **Filter**, and
**TX** — the upper edge of the station's transmitted audio in kHz, which
is their rig's filter and stays put when the antennas move and the spatial
signature does not. Hover a row for the whole print: both audio edges,
where the voice is centred and how it tilts, syllables per second, how
long their overs run. The gate learns it from the combined audio over
their overs (a print needs an over of 1.5 s or more), so a talker heard
once briefly shows `—`. A station you have not named is shown by their
number (`#4`) rather than as a blank; type over it to name them, and clear
it back to nothing to un-name them.

The **Filter** column is what PER TALKER on the FILTER page will put back
when they key up: `300–2700 soft auto`, with a filled dot on whoever's is
in force right now and a dash where the gate has kept none for them. It is
the widest cell in the table, so it elides — hover it for the whole thing.
The row that is lit is whoever is talking. Buttons:

- **Lock on #N** — select a row first. Pins the combiner on that station.
  Their overs get the remembered beam (the weight is pre-steered there
  between overs); any other over is nulled from its own steering vector.
  The LOCKED banner shows who is pinned, who is being nulled right now,
  how many overs were steered and nulled, and the best output SNR reached.
  **Release lock** returns to normal tracking.
- **Clear memory** — forget every talker and weight. Do this after moving
  band or antennas.

**Timeline.** Two minutes of A, B and OUT SNR with talk and steady-QRM
bands beneath, so a fade or a switch is visible after the fact.

**ANTENNAS.** The three meters again, the manual phase/ratio knobs (only
active in manual mode), and BALANCE: A minus B, noise coherence, passband
flatness and slope, and a verdict line. A loop that has been several dB
down for ten minutes produces a warning from the gate here: check that
loop, its preamp and feedline before blaming the combiner.

**Per-bin weights** sits under that verdict, because it is a setting about
the weight rather than about a page. Two loops give one degree of freedom
*at one frequency*; across a 2.7 kHz channel the best answer differs from
one end to the other, and a single weight is the average of them. Tick the
box and the gate solves a weight per bin instead. The figure beside it —
`33 bins · +0.4 dB` — is how many bins the last solve refined and what that
earned over the single-weight answer; on one clean source it is worth about
nothing and says so, and a gate too old to report it shows `—`. The talker
is held distortionless either way.

**NOISE.** The two-channel impulse blanker (NB button, threshold knob), the
PAN selector (what the panadapter shows: A, B, combined, or the nulled
output), the spatial noise map (inter-loop coherence per bin across the
band: high bars are one local source, low is sky noise) with the current
passband marked, the list of coherent sources it found, and **Null
selected**, which parks a manual weight on a source's null.

**EVENTS.** One line per transition: talker started/ended, new talker
remembered, lock and release, steady carrier nulled, mode and hear
changes, realigns, captures.

One of those lines is worth knowing about before you see it. `voice split:
not Ted's voice → Ann` means the gate decided mid-over that the voice it
was hearing was no longer the one it had been hearing, and split the over
into two talkers. It is the one talker change you cannot hear happening:
nobody stopped transmitting, the frequency did not move, and the only
outward sign is that the number beside the marker on the dial quietly
became a different number — and, with PER TALKER on, that the filter
changed under you. A handful of these on a busy round-table is the gate
doing its job. A stream of them on one station usually means their audio
is changing faster than the print can follow (a fading path, or somebody
walking away from the microphone), and the cure is to name them and lock
on rather than to argue with the splitter.

## The BAND page

Everything on the SLICE page is about one channel, so none of it can answer
"where should I be listening?". The BAND page asks the gate two questions
about its whole 125 kHz instead. Both are click-to-tune: a click moves
AetherSDR's active slice and switches the combiner to **track**, so the
first over you hear is already being solved for.

**SPATIAL WATERFALL.** One row per poll (4 Hz) across the gate's span, with
colour meaning *direction* rather than strength: hue is the inter-loop
arrival phase, and the key under the picture is the scale itself — the hue
wheel from −180° to +180°, and the grey the waterfall paints where there
is no direction to report. A bin only gets a hue where the two loops agree
well enough to call it one: below 0.5 coherence it stays grey, and from
there the colour comes up to full by 0.9. Sky noise arrives from
everywhere at once, so it is grey by definition. Brightness is the level,
stretched between the row's own 20th and 98th percentiles rather than
between its floor and its peak — the old scale let a single strong carrier
set the top of the range and squashed everything else into a blur at the
bottom of it.

Two stations arriving from different places cannot share a colour. One
local noise source paints a single flat colour across everything it
touches, which is how you recognise it. The receiver's passband is
bracketed. Click a column to tune there; hover for the numbers.

**FINDER.** The activity strip is the share of the last ten minutes each
column carried voice; the table below it is the conversations the gate
found there, best first (at most twelve, ranked by the gate): frequency,
score, SNR, syllabic rate, how long it has been active, how long ago it was
last heard, arrival phase, coherence, and `gain` — the diversity gain the
pair can actually earn on that signal, which on plain sky noise is often
near zero and says so. The frequency is snapped to the 500 Hz grid you
actually tune on; hover a row to see the estimate it was rounded from
(`estimate 3860.37 kHz`), which is also how sure the gate is of it.
**Tune** (or a double-click on the row) goes there.

A gate that is too old to serve these two routes, or that has not built a
map yet, leaves the waterfall empty with *waiting for the gate*; missing
numbers render as `—` rather than as zeros.

## The SITE page

Both other pages are about signals that happen to be there right now, so
neither can answer the two questions you actually ask when the receiver
sounds worse than it did last week.

**NOISE PROFILE.** What kind of noise this address makes, as opposed to how
much of it there is. The gate measures the *shape* of the noise floor once
a second, and the top line is the verdict: `60 Hz grid: 120 Hz hum 13.7 dB,
2 harmonics`, or `no mains-locked hum`. A 100 or 120 Hz comb is a
rectifier — a supply, an LED driver, a dimmer, a charger — so walk the
house and unplug them one at a time. The impulse line (`15 /s at 12.5 dB`,
or `none`) is the blanker's own quarry: electric fences, vehicle ignition,
arcing insulators, power-line telecoms. The lines below that are the
strongest peaks that are *not* mains harmonics, which is usually a switching
supply or a cheap oscillator that you can hunt with a portable. The strip on
the right is the last 120 seconds: bars are impulses per second, the line is
hum in dB, so you can see an appliance switch on. A noise figure tells you
none of this; these are sentences you can act on.

**Acting on the noise profile.** The table under those lines is the gate's
own list of findings, one row per kind: `MAINS`, `IMPULSE`, `PERIODIC`,
`TONE`, or a single `FLOOR` row when nothing else was found. Each row
carries the gate's label and detail, the window it was measured over (the
impulse detector runs over a longer one than the rest, and the row says
so), its size in dB, and **one button — the action the gate itself
nominated**. `BLANK` turns the noise blanker on at the threshold the gate
chose; `UNBLANK` turns it off again and the row lights up while it is in
force; `NULLED` shows a direction already being nulled; `NOTCH` adds a
fixed notch on a tone. Pressing it sends the gate's own route and query
string back verbatim — this window composes none of them, so a gate that
grows a new kind of finding gets a working button here without a new
AetherSDR build.

A row whose button reads `—` is a finding with nothing to do about it, and
the gate's reason is on the button's hover: `not directional enough to null
(coherence 0.14)`, `nothing to notch; ANF handles tones in the passband`,
`already a fixed notch`. That is a finding too — an operator who is not
told it goes looking for a control that is not missing. If the gate refuses
an action, its own words appear on the line under the table for five
seconds and nothing moves: the row's state comes back on the next poll,
which is the gate's answer rather than the window's optimism.

**BEACONS.** The NCDXF/IARU International Beacon Project: eighteen known
transmitters sharing one frequency on a three-minute rota, listed here in
transmission order with the one on the air now lit. Because the
transmitters and the paths are known, what you read is a measurement of
*your* station — the antennas, the feedline, the noise floor — rather than
a report about somebody else's. Each beacon sends its call and then one
second of dashes at 100, 10, 1 and 0.1 W; the **Steps** cell fills one dot
per step you heard, and the lowest step heard is the band's real reach.
Beside it: SNR in a 500 Hz bandwidth, each loop on its own, the arrival
phase and coherence, the diversity gain the pair earned, and how long ago
it was heard.

The page shows only the beacons for the frequency in the gate's span; the
other bands' results are kept but not drawn, so switching bands does not
lose them. With no beacon frequency in the span it says so and names the
five: 14.100, 18.110, 21.150, 24.930 and 28.200 MHz. Beacon phase is worth
more than it looks — it is the only phase in this window whose right answer
is already known, which is what a future geometry solve has to calibrate
itself against.

**Station grid.** Type your Maidenhead locator into the field at the top of
BEACONS and press `SET` — four characters (`EM10`) or six (`EM10bk`), case
does not matter. The gate already knows where every beacon is; your locator
is the second point every bearing needs. With it the table gains **Brg**
(degrees true) and **km** columns and the pattern plot beside it comes
alive. Without it both columns are dashes and the plot says `needs the
station grid` rather than drawing a dial with nothing on it. `FORGET` drops
it again and keeps every result. A locator that will not parse comes back
as the gate's own refusal on the status line.

**Heard.** The **Heard** column is `3/7`: how many of that beacon's passes
on this band you actually heard, out of how many the gate sampled. One in
seven is a path that opens; seven in seven is a path that is simply there.
The **SNR** cell shows the latest pass and carries the mean over every pass
on its hover — one number answers "is it open now", the other answers "is
this path any good".

**Propagation.** Results now survive a gate restart, so the log is a
night's work rather than a snapshot and eighteen rows of one band is no
longer the whole of it. The lines under the pattern plot are the rest,
collapsed to one sentence per band the gate has sampled:
`20 m · 3 of 18 heard · weakest 1 W · median −3.3 dB · 4 min ago`. The
weakest step is the point of it — hearing the 0.1 W dash is thirty
decibels of margin over hearing only the 100 W one.

**The pattern plot.** A compass dial: one dot per beacon heard on *both*
loops, at the bearing it actually lies on, with loop B minus loop A as the
radius. The ring at half radius is the two loops equal; a dot outside it is
a direction where B hears better, one inside is a direction where A does,
and the scale is clipped at ±10 dB because a pair that differs by more than
that on a beacon is not a pattern, it is a broken feedline. Because both
loops heard the same signal at the same instant, propagation cancels and
what is left is your antennas. Hover a dot for its call, band, bearing,
distance, B−A, phase and SNR. With a locator but nothing heard on both
loops yet it says so; that one is fixed by time on the air rather than by
typing.

**BEACON CHECK.** The five beacon frequencies are the only place in the
hobby where "go and listen for three minutes" is a complete measurement
procedure, and the row of band buttons is that procedure. Pressing `20 m`
tunes the *active slice* to 14.100 MHz, leaves it there for one full
eighteen-slot cycle plus ten seconds of slack, and then puts the radio back
exactly where it was; the countdown reads `CHECK 20 m · 2:41 left` and
`CANCEL` brings it home at once. It moves the frequency and **nothing
else** — not the mode, not the combiner, not the filter — because a check
that changed the receiver would not be measuring the receiver you use.
Closing the window ends a running check and tunes back; switching pages
does not. With no slice for it to remember it refuses to start rather than
tuning away with no way home.

The whole page is polled only while it is on screen, and a gate too old to
serve the beacon route says `beacon watch: not available from this gate`.
Missing numbers render as `—` rather than as zeros.

## The FILTER page

The question an operator asks more often than any other is "why does this
sound like that?", and the answer is almost never the combiner. It is a
passband 300 Hz narrower than you thought, a sharp filter ringing on a
49 Hz skirt, an automatic notch chewing at a vowel, an AGC decay short
enough to breathe between syllables, or a blanker removing four percent of
the audio to kill a fence that stopped an hour ago. Every one of those is
visible on this page and invisible on the other three.

**The curve.** The gate's own measured response, 0 dB at the top and −60 dB
at the bottom, with the passband shaded over it. The caption above it reads
the whole filter in one line — `LSB · 100–2900 Hz · SHARP 1023 taps ·
61 Hz transition`. On a lower sideband the numbers are positive audio hertz
and the sideband is in the caption, because a filter drawn in negative
frequencies is a picture nobody reads.

Both edges are draggable and snap to 10 Hz; only the edge you moved is sent,
so dragging the low edge leaves an AUTO-chosen high edge alone. The keyboard
reaches them too: Up and Down pick an edge, Left and Right move it by 10 Hz,
Shift by 50. Double-click anywhere on the curve to put a notch there at the
width beside the ADD button. Vertical marks are notches, labelled with the
depth the gate measured; dashed lines are tones the automatic notcher has
found on its own; short ticks off the bottom axis are the contour and audio
peak centres. The hertz under the pointer is in the corner. Depth labels sit
just under the 0 dB line beside their mark rather than on it, because the
response runs along the top edge across the whole passband.

**What is actually arriving.** Filled in behind the curve is the gate's
one-second pre-filter spectrum — the same measurement the AUTO width and the
ANF read — on the same hertz axis. It is *not* drawn on the filter's own
0…−60 dB scale: the gate reports it in dB below its own peak, so a dead
channel drawn that way would paint a full-scale slab under the curve. The
gate's median is pinned to the tick marked `floor` at −45 dB instead, and
everything keeps its distance from that, so the area's HEIGHT is decibels
over the noise and a station 30 dB out of the noise rises from −45 to −15.
Before the gate has heard a block there is no area at all and the corner says
`no audio yet`. While AUTO is on, the two edges it has chosen are marked with
their own thin dash-dotted lines (labelled `auto` at the low one), which is
how you see whether the tracker put them on the energy or beside it.

**The state line.** Under the curve, one line, always the same height:
`in force 100–2900 Hz (asked 100–2900) · AUTO print 300–2700 · ANF 2 tones ·
notches 2 · AGC med −1.9 dB · NB 0.4 %`. Everything on it is also in one of
the four columns below — the point is that "what is switched on?" costs one
glance rather than four columns of reading, and that the two pairs of edges
sit next to each other where a disagreement between them is impossible to
miss. `ANF off` and `ANF no tones` are different sentences on purpose.

**WIDTH.** SOFT and SHARP are the two ends of one trade: SHARP spends taps
on a near-vertical skirt and rings a little on transients, SOFT rolls off
over a few hundred hertz and lets more of the neighbour in. The four preset
buttons are widths, not passbands — they keep the low edge where it is and
put the high edge 1.8, 2.4, 2.7 or 3.0 kHz above it, because the low edge is
what decides how much rumble and hum you hear. AUTO hands both edges to the
gate: `AUTO · print 300–2700` means it is fitting them to the voice print of
the station you are listening to, `AUTO · spectrum 210–2840` that it is
fitting them to the occupied spectrum instead, and `AUTO · warming up` that
it has not decided yet. While AUTO is on, the spin boxes still show what
*you* asked for and the caption shows what is in force. The roofing line
(`Roof 200 kHz RF · 25 kHz digital`) is the two filters upstream of all of
this, which nothing on this page can move.

**NOTCH.** ANF is the automatic notcher; beside it are the tones it has
found and how deep it is cutting them, to the same decimal the notch table's
own dB column uses (`1240 Hz −34.0 dB, 2010 Hz −31.0 dB`, or `none`). The table below is your own notches, each with its measured depth
and its own CLEAR; CLEAR ALL empties the table and leaves the automatic
notcher alone.

**TONE.** CONTOUR is a broad tilt at a frequency you choose — a few dB down
at 700 Hz takes the boxiness out of a close-miked voice without touching
the consonants. `APF (CW)` is a narrow peak, and the parenthesis is a
warning: it is a CW tool and nothing else. Leave it on through a voice
contact and everything comes through one note, which sounds exactly like
the other station is talking into a cup. AUTO EQ matches the station's
audio to yours and reports what it is doing as a tilt in dB.

**AUTO CONTOUR.** Ticked, the gate places the contour itself: it fits the
bell to the talker's own voice print — the frequency their audio piles up
at and how much of it to take back out — so a boomy station is flattened
and a thin one is left alone. While it is fitting, the three numbers under
it are the gate's rather than yours: they show the bell it has settled on
and they are not typeable, with `from print` beneath them. `no print yet`
means it has not heard enough of anybody to fit anything and there is no
contour in force at all — which is not the same thing as a contour at
0 Hz, and the page never draws one. Typing any of the three numbers by
hand takes the fit off, because the gate cannot fit and obey at the same
time; the tick comes off as you commit the value and the caption reads
`manual` from then on.

**PER TALKER.** Under the four columns, its own box. The gate already
remembers a combiner weight per station; with this on it remembers a
filter per station too, and puts it back the block they key up. `FAST`
snaps to it on the block boundary, `SMOOTH` glides over about a second —
FAST if you are working a pile-up and want the change done before the
first syllable, SMOOTH on a round-table where the switching itself would
be more distracting than the mismatch. Off, one filter serves everybody.

Whose filter is in force is on the state line: `filter: Ted's (#3)`, or
`filter: #3` when the gate has heard them enough to keep a filter but you
have not given them a name. The same filter is in the TALKERS table's
Filter column on the SLICE page, where you can see everyone's at once.

**AGC & NB.** The five AGC modes, and the three times behind them in
milliseconds: attack, decay and hang. `gain −1.9 dB` is what the AGC is
actually taking off right now, which is the number that tells you whether a
short decay is pumping. NB is the noise blanker, with its threshold and —
the readout that matters — `blanked 0.4 %`, the share of the audio it is
throwing away. Anything above a percent or two on a quiet band means it is
eating signal, not noise.

**PRESETS.** Under the four columns, five whole filters. The width buttons in
the WIDTH column move one edge and touch nothing else; these set the lot.
`SSB WIDE` is 100–2900 soft, `SSB NARROW` 300–2400 sharp, `CW-ISH` 400–1000
sharp with the audio peak on at 700 Hz, `NET` 200–2700 sharp with AUTO EQ on
for a round-table where every station arrives with a different tilt. `RESET`
is 100–2900 soft with the automatic notcher, contour, audio peak, automatic
width, automatic equaliser and blanker all off, AGC medium, and every notch
you placed cleared — the state to come back to when you can no longer tell
which of six things is making it sound wrong. Each preset is one
`/filter/set`; RESET is that plus one `/filter/notch?clear=1`, because
removing every notch is a route rather than a setting.

Every control writes immediately; there is no Apply button. The gate's reply
to a write is the same status object a poll returns, so what you see a
moment later is the radio's answer rather than the page's optimism. A value
the gate refuses appears on the status line under the curve for a few
seconds and nothing moves. The page is polled twice a second while it is on
screen and not at all otherwise, and a mode with no slice filter behind it
says `Filter is not available for this mode` and greys the lot.

## A working session

This is the order the FLOW strip encodes, and the order to work in. Each step
makes the next one mean something: a mode set before the tuners are aligned
is solving on two signals that are not the same signal, and a filter set
before you are hearing the combined output is a filter on the wrong audio.

1. **Align the tuners.** Start the gate, connect, open the window, and wait
   for step 1 to read a lag. The gate cross-correlates the two tuners and
   holds them sample-aligned; the lag is usually a few tens of samples and
   stays put. Until it is aligned there is no pair, only two receivers, and
   everything below is meaningless. If it does not settle, or you have just
   restarted a stream, press REALIGN and read what it answers.

2. **Pick a mode.** **track** is the default answer: the combiner follows
   whoever is talking and nulls the rest between overs. **null** is for a
   noise source with a direction and no station you are protecting.
   **manual** hands you the phase and ratio knobs on ANTENNAS. **off** is
   one antenna and no combining. Watch `gain` — OUT minus the better loop —
   while it settles; near zero on a quiet band is the physics, not a fault.

3. **Hear the combined output.** HEAR decides what reaches the audio, and it
   is separate from the mode: the combiner goes on learning whether you are
   listening to it or not. Set it to **OUT** to hear the result. **STEREO**
   is the other honest answer — both loops, one AGC, the difference between
   them as a soundstage. A or B alone is a diagnostic, not a listening
   state, and step 3 says so while you are in one.

4. **Act on the noise the gate found.** The SITE page's profile runs by
   itself and lists what this address is actually making — mains hum,
   impulses, periodic modulation, tones — with the gate's verdict on each
   and one button where there is something to do about it. Step 4 counts
   the findings that still offer an unused button. Work down them; the ones
   with no button carry the reason there is none.

5. **Set the filter.** Last, because it acts on the audio the four steps
   above produced. Drag the passband edges on the curve or type them, pick a
   shape, add a notch, or leave AUTO on and let the gate track the voice it
   is hearing. Step 5 shows the edges *in force* — which is not necessarily
   the pair you asked for, if AUTO has moved them.

Then, working a specific station: wait for them to be remembered (one over
of a few seconds), double-click the name cell and label them, select the row
and **Lock**. Callers are nulled while the lock holds. Use *Hear A only* at
any point to hear the difference the combiner is making.

Beacons (SITE) are a separate errand from the five steps and only pay on the
higher bands — see below.

## Questions this window gets asked

**Do I turn AetherSDR's own noise reduction off?** No. Leave it on. Nothing
on the gate is a noise reducer. The combiner is *spatial* — it steers a null
at where the noise is coming from — and the gate's filter is passband, AGC
and notches. The app's NR works on what a single audio stream looks like
over time. They are three different attacks on three different problems and
they stack. The one exception is STEREO: the app's NR chain works on a mono
mix, so give it a miss while you are listening to the loops in stereo.

**What is CAPTURE for?** The replay lab, not listening. It writes an aligned
two-channel raw recording on the gate, which `python -m aether_gate.replay`
reads back so an alignment or combiner change can be tried against a real
recorded site instead of against whatever the band is doing this evening. It
is not a recording of what you are hearing and it is no part of a listening
session. Ten seconds is the useful default.

**Why is the beacon table empty?** Two reasons, and both are about where you
are tuned and who you are. The NCDXF/IARU beacon network transmits on five
frequencies only — 14.100, 18.110, 21.150, 24.930 and 28.200 MHz — so the
table fills on 20, 17, 15, 12 and 10 m and nowhere else. On 40 m or 80 m
there is nothing to hear and the page says so rather than showing an empty
log. It also needs your grid square to work out a bearing and a distance for
each beacon; Liberty Hill, Texas is **EM10**. That is set on the gate now and
persists across restarts, so this is a one-time answer.

**Does the noise profile need a capture first?** No. It runs by itself, on
the live streams, as soon as the tuners are aligned — a couple of seconds of
audio is enough for the first verdict and it keeps refining. The rows on the
SITE page *are* its findings, each with the gate's own reasoning and, where
one exists, the single button that acts on it. There is nothing to start and
nothing to record.

## For scripts: the control port

The gate's HTTP control port (default 8731) serves:

- `GET /diversity` — the status object (mode, weight, SNRs, talker,
  memory (each entry with `voice` {centroid_hz, low_hz, high_hz, tilt_db,
  syllabic_hz, over_s, overs} or null), focus, passband, noise coherence, blanker, sources, loops,
  capture, `subband` {enabled, bins, extra_db}: the per-bin refinement of
  the tracked weight, `noise_profile` {mains_hz, hum_db, harmonics,
  impulses_per_s, impulse_db, periodic[], seconds, window_s,
  impulse_window_s, `kinds[]`}: what kind of noise this is). Each `kinds`
  row is {kind: "mains"|"impulse"|"periodic"|"tone"|"floor", label, detail,
  db (or null), window_s, action (or null), why (or null), active}, and an
  `action` is {label, route, query} — the gate's own nomination, meant to
  be sent back as `GET <route>?<query>` unchanged.
- `GET /diversity/set?mode=&source=&phase=&ratio=&nb=&nb_db=&pan=&null_source=&focus=&subband=`
  — any subset; `source=combined|a|b|stereo` is what reaches the audio
  (stereo: A left, B right) and `pan=` what the panadapter draws;
  `focus=<id>` pins, `focus=off` releases; `subband=on|off`
  (default on: in null/track every passband bin gets its own weight
  wherever the learned noise has a direction, the talker held
  distortionless).
- `GET /diversity/map` — the spatial noise map with `passband_hz`.
- `GET /diversity/spatial` — live per-bin phase/coherence/level rows (the
  BAND page's waterfall); `GET /diversity/finder` — ranked conversations
  across the span with activity per point (the BAND page's table).
- `GET /diversity/beacons` — the NCDXF/IARU beacon watch: which beacon is
  on the band's beacon frequency now (from UTC), and per beacon heard the
  SNR in 500 Hz, each loop's SNR, the pair's phase and coherence, the
  power steps heard (100/10/1/0.1 W) and the MRC gain. Idle unless a beacon
  frequency (14.100, 18.110, 21.150, 24.930, 28.200) is inside the span.
  With a station locator set it also carries `station_grid` and, per
  result, `grid`, `bearing_deg` (null without one), `distance_km`,
  `samples`, `heard_n`, `snr_mean_db` and `last_heard`; plus
  `propagation[]` {band_hz, sampled, heard, of, best_w, median_snr_db,
  updated} — one row per band the gate has sampled, kept across restarts —
  and `pattern[]` {call, band_hz, bearing_deg, distance_km, b_minus_a_db,
  phase_deg, snr_db}, sorted by bearing, for beacons heard on both loops.
- `GET /diversity/set?grid=<locator>` sets the station's Maidenhead
  locator (four or six characters); `grid=off` forgets it. Replies with the
  diversity status, or `{"error": "not a Maidenhead locator: 'ZZ99'"}`.
- `python -m aether_gate.replay CAPTURE.npz` (in the gate) — the replay
  lab: a capture through the live combiner path as A / B / wideband /
  per-bin WAVs at one gain, plus `summary.json`.
- `GET /filter` — the slice filter's status (the FILTER page): `available`,
  `mode`, `sideband`, `low_hz`/`high_hz` (in force) and
  `set_low_hz`/`set_high_hz` (asked for — they differ while AUTO is on),
  `width_hz`, `shape`, `taps`, `transition_hz`, `notches[]`
  {hz, width_hz, depth_db}, `anf` {enabled, found_hz[], depth_db[]},
  `contour` {enabled, hz, db, width_hz}, `apf` {enabled, hz, width_hz},
  `auto` {enabled, source: "print" | "spectrum" | null, low_hz, high_hz},
  `auto_eq` {enabled, tilt_db}, `nb` {enabled, threshold_db, blanked_pct},
  `agc` {mode, attack_ms, decay_ms, hang_ms, gain_db}, `roofing`
  {analogue_hz, digital_hz}, `response` {hz[], db[]} — the measured curve —
  and `spectrum` {hz[], db[], floor_db} or null: the one-second pre-filter
  spectrum on the SAME grid as `response`, in dB below its own peak (so its
  maximum is 0.0) with `floor_db` the median of those points on that same
  scale. Null until the gate has heard a block. Numbers may be ints or
  floats.
- `GET /filter/set?low=&high=&shape=&anf=&contour=&contour_hz=&contour_db=&contour_width=&apf=&apf_hz=&apf_width=&auto=&auto_eq=&nb=&nb_db=&agc=&attack_ms=&decay_ms=&hang_ms=`
  — any subset; `shape=soft|sharp`, `agc=fast|med|slow|long|off`,
  0 ≤ low < high ≤ 20000. Replies with the status object above, or with
  `{"error": "..."}` if a value is refused.
- `GET /filter/notch?add=<hz>&width=<hz>` places a notch;
  `GET /filter/notch?clear=<hz>` removes one; `GET /filter/notch?clear=1`
  removes them all. The automatic notcher's own tones are not in this list
  and are not touched.
- `GET /diversity/memory/name?id=&name=` — label a talker (empty clears).
- `GET /diversity/memory/clear`, `GET /diversity/align`,
  `GET /diversity/capture?seconds=`.

`tools/diversity_recorder.py` polls the status to a CSV and
`tools/diversity_report.py` summarises a session (hours, talk share, gain
over the better loop, best-loop share, talkers, passband flatness).

## Limits and known gaps

- Two antennas: one null or one beam. Four-channel coherent HF hardware is
  the next step; the code is written to be N-ready but is 2×2 today.
- Band changes come from AetherSDR; the gate has no tune route of its own.
  The BAND page's click-to-tune therefore moves AetherSDR's active slice,
  the same write a click on the panadapter makes.
- Talker matching is spatial. Two stations at the same phase and level are
  one talker to the memory.
- The audio blanker and the diversity blanker are separate; use one.
