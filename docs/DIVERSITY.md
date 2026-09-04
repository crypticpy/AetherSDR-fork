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

**This stacks with the client's own noise reduction.** DFNR (AetherSDR's
speech-model denoiser, see the DSP applet's DFNR tab) is worth trying on
faint SSB voice the combiner has already improved but not fully cleaned up —
it runs on the demodulated audio after the gate's combining, so the two are
independent stages, not competing ones. Below roughly 3 dB SNR, expect
DFNR to occasionally invent speech-like artefacts rather than recover real
words; treat anything you can't otherwise confirm as unverified.

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

**Active loops with a control box (K-480WLA and its relatives).** These
loops put a band selector and a 0-30 dB gain control in the box at the
receiver end, with an indicator per band (MW, SW, FM, AIR, 6M, UV and a
full-band position on the K-480WLA; the SW position covers 2-30 MHz and
switches the medium-wave and FM band-stop filters in). Everything this
window measures is measured *through* those switches: a beacon slot that
hears nothing on 12 m may mean the band is closed, or that one box was
left on MW or full-band with the broadcast filters out, and a gain knob
set 10 dB apart on the two boxes shows up as a permanent B-minus-A
offset in the pattern dial. So keep both boxes on the same band position
and near the same gain, note what they are set to whenever you change
them (`/diversity/set?antenna=<free text>` stamps the note on every site
log line from then on, so a sweep can be read against the switch
positions it was made with), and treat the beacon sweep as valid only
for the band the boxes were set to. The loop head has no controls; the
box is where the mistakes are made.

## The sidebar

The sidebar entry is deliberately small: one status line (mode, the live
talker, the combiner's gain over the better loop), the mode selector, and
**Open Diversity window**. Everything else lives in the window, the same
way Aether Voice is hidden until opened. A compact scope can be kept in the
sidebar with the setting `AetherGateDiversityPanel_ShowScope`.

Under the door sits one more line: the sidebar's own answer to "where do I
start?", without opening the window at all. It reads `next: realign`,
`next: beacons`, `next: 2 findings`, `next: tune a voice`, `next: listen`, or
`next: —` once the gate is gone — the same DiversitySessionModel the
window's own START page reads, fed from the same five payloads the sidebar
already polls whether or not the window has ever been opened. Beside it, a
**QUICK START** button sends the three writes the START page's own QUICK
START does — `mode=track`, `source=combined`, `auto=on` — in that order, and
hides along with the line the moment there is no diversity block to answer
for.

## The window

Open it from the sidebar button. Its geometry and visibility persist; it
reopens on the next run if it was open at exit.

Two strips sit above the pages and one line sits under them. They do three
different jobs. They are three rows rather than one because as one row they
read as a single sentence of controls, and half of them are not.

**Row 1 — the pages.** START / SLICE / BAND / SITE / FILTER, and nothing
else; `pages` is written near the right end of the row to say so, and one
**?** at the end of the row opens the help for whichever page is showing.
The window opens on the page you left it on; the first time, on START.
START, described below, is the session itself written out. SLICE, described
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

A `pan:` readout sits at the right of the same row, display only. HEAR
is audio and is always the combiner's live output; the main panadapter's FFT
is a separate choice (the PAN buttons on the noise tools page) that normally
mirrors it — loop A and loop B added with the active slice's own weight — but
before the two tuners are aligned there is no weight yet, so a panadapter set
to `combined` reads `pan: A, not aligned` and shows loop A alone until REALIGN
settles; once aligned it reads `pan: A+B`. Pointed at one leg or the
null on purpose it reads `pan: A only`, `B only` or `nulled`; an older
gate that has never sent `pan` reads `pan: —`. Kept to a few words
because the row is already at the window's own 1120 px opening width, with
the fuller explanation in the readout's tooltip instead.

**The START page — the session, written out.** Five cards, in the order
that gets the best signal, each with the reason it is in that order beside
it:

```
✓ 1 · RECEIVER    aligned, track · hearing COMBINED                        
    Two loops lined up sample-for-sample, one weight solved on whoever is talking,
    and that combined output in your ears. Nothing below this reads true until it is.
    Once when you sit down; again after any gate restart — the gate comes back mode off.

● 2 · SITE NOISE  2 findings with a button                            [GO]
    The shape of your own noise floor named: mains hum and harmonics, impulses per
    second, periodic modulation, tones — each with the one control that acts on it.
    It runs by itself. Read it when you sit down and whenever the receiver sounds worse.
```

A tick is a step behind you, a filled dot is the one to do next, a hollow
circle is one still ahead. The three lines under each title never change:
a step you have finished still says what it bought you, which is what makes
this a station display rather than a wizard that blanks each page as you
leave it. The state beside the title is the gate's own words — nothing on
the card is computed by the window.

Where a step has a single obvious action the card carries one button for
it, on the right of its title: `REALIGN` when the loops are not aligned,
`TRACK` when the combiner is in `off`, `HEAR OUT` when you are listening to
one leg, `GO` to the page a choice has to be made on, `DIG 1 MIN` when the
talker has no filter yet. **`GO` on the BAND card only changes the page**;
a beacon check costs three minutes off the station and nothing here starts
one for you.

Under the cards, an OFFERS row: **QUICK START**, which sends the three
writes that make a cold gate listenable (`mode=track`, `source=combined`,
`auto=on`) in that order, and the three DIG durations with the last run's
one-line report beside them.

It was a line of five steps at the foot of the window until an operator
asked it the question one line cannot answer: not *which* step is next, but
why that order is the order. Five cards have the room for the answer.

**The NEXT line — at the foot of the window, on every page.** One step, and
the gate's own words for its state:

```
[AUTO CLEAN ON]  NEXT · SITE NOISE · 2 findings with a button        [GO]
```

The step is the first one on START that is not done, and the button is that
card's own cure — the same click, on the row that is visible from all five
pages. With AUTO CLEAN off the line is status only: no button, no lit card,
because a station under manual control is not to be nudged. Across a gate
that has stopped answering it reads `NEXT · — · gate not answering`.

Once all four chores are behind you the line collapses to the one fact left
worth a row of the window —

```
● listening · Ann talking · OUT +1.2 dB · 4 remembered
```

— and a click on it opens it back up. That choice is remembered.

See *A working session* below for what each step is for.

**DIG — an offer, not a step.** The five cards are an order of operations;
this is not in it, which is why the three durations sit on START's OFFERS
row rather than among them. Press `1 MIN`, `3 MIN` or `5 MIN` and the gate
spends that long moving one knob of the chain at a time against a live
objective, keeps whatever measurably helped, and puts back whatever did
not. A run goes on whichever page you wander to, so the NEXT line carries
it: `NEXT · STATION · Ann has no filter yet · DIG 1:12 of 3:00 · +2.1 dB ·
started by you`, with a `STOP` beside it that ends the run and puts your
chain back exactly as you had it. START's own offer line says the same fact
the way an offer says it — `digging · +2.1 dB so far`.

When it lands, one sentence naming only what it *moved* —
`+4.1 dB: nb_db, post, width`, or `nothing beat your settings` — and three
words to answer it with: `BETTER` keeps the changes and tells the gate they
worked, `KEEP` keeps them without judging, and `WORSE` puts the chain back
on your own settings and tells the gate the measurement was wrong. Your
ears win that argument and the gate learns from it. Until you say one of
them the NEXT line asks: `DIG done · +4.1 dB — better or worse?`. A run you
stopped is not a question — `DIG found +4.1 dB (put back)` — and neither is
one the gate refused, which is quoted verbatim. A gate that cannot dig has
no offer and no buttons: nothing greyed out to wonder about.

A trial has to beat the chain you had by a margin before it is kept, and
the margin comes from how much the band itself swung while the gate was
sampling the baseline: half that swing, never less than 0.5 dB and never
more than 2 dB, because 2 dB is about the smallest change you reliably
hear on a weak signal and a run that demands more than that cannot
conclude anything. A run that kept nothing still tells you the best thing
it measured — `nothing cleared the 2.0 dB margin · post v2 measured
+4.8 dB` — so "your settings are right" and "the band was too jumpy to
tell" read differently. When the baseline swung more than 3 dB the line
ends `· tentative, band swung 7.7 dB`: the gate went ahead anyway, but
that is its word on how much to trust the result. The gain on the line is
the sum of what was *kept*; the band drifting upward during the run is
not credited to knobs that never moved.

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

The **Filter** column is what PER TALKER on the CHAIN window will put back
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

Both of the gate routes this page draws from keep running in the
background, once a second, from the moment the Diversity window exists and
the gate reports a dual-tuner pair — whether BAND is the page on screen,
another page is, or the window is not even open yet. Opening onto BAND is
therefore not opening onto an empty picture and an empty table: the
waterfall already has a minute or more of history in it and the FINDER
table already has whatever it found while you were on another page,
because those two routes were never waiting for you to look.

**SPATIAL WATERFALL.** One row per poll (4 Hz while the page is actually on
screen, 1 Hz in the background otherwise) across the gate's span, with
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
bracketed. Click a column to tune there. The key under the picture says
what each of the three things a pixel can be means, in words — *colour =
arrival phase* on the hue scale, *bright = stronger*, *grey = noise, no
direction* — and the frequency axis is labelled on round kilohertz with a
faint grid up from each label, so a streak can be read to a kilohertz
without counting pixels. Moving the pointer over the picture shows a
crosshair and one line for the bin under it (`3 840.50 kHz · phase 112° ·
coherence 0.81 · level −97.3 dB`), from whichever row the pointer is on,
not only the newest.

**FINDER.** The activity strip is the share of the last ten minutes each
column carried *anything*; the payload also sends `voice_share`, the
share it carried somebody talking, which is what the strip used to show
on its own and why it stayed near-black on a busy band. The table below
it is what the gate found there, best first: every window that stood at
least 3 dB over its own **local** floor (measured per 10 kHz, so a tilted
band or a dense digital block no longer hides the quiet stretch beside
it) for two seconds or more, up to forty of them, ranked by score.

**kind** is the gate's verdict with how sure it is (`voice 0.91`): voice,
CW, data, RTTY, carrier, noise, and now `ft8`, `ft4` and `psk31`, named
from the band plan, which is the only thing at 244 Hz a point that can
tell an FT8 block from a conversation, plus `signal`, which means
*something is here and the gate will not guess what*. A CW column or an
unknown mode is listed as what it is instead of being dropped for not
sounding like speech.

The row you are tuned to is always in the list, flagged **tuned**, even
when it scored nothing: what the gate thinks of what you are actually
listening to is the one row you can check by ear. Digital rows carry the
allocation's own dial (`14074.0`, USB); phone rows are snapped to the
500 Hz grid you tune on and CW to 100 Hz, with the estimate they were
rounded from on hover (`estimate 3860.37 kHz`). The hover also says
score, SNR, syllabic rate, occupied width, how long the signal has been
active, how long ago it was last heard, arrival phase, coherence, and
`gain`, the diversity gain the pair can earn there, which on plain sky
noise is near zero and says so. **Tune** (or a double-click) goes there.
The strip colours each found signal's stretch by its kind, in the same
colours the table uses.

When the gate has nothing to find it says why in the legend's place:
*nothing to find yet: the loops are not aligned* while the pair is still
being lined up (which is where a span change lands you for a few seconds),
or *no frames yet after the tune* — the finder starts over on every tune
and needs a few polls before it has anything to rank.

A gate that is too old to serve these two routes, or that has not built a
map yet, leaves the waterfall empty with *waiting for the gate*; missing
numbers render as `—` rather than as zeros.

## The SITE page

Both other pages are about signals that happen to be there right now, so
neither can answer the two questions you actually ask when the receiver
sounds worse than it did last week.

**NOISE PROFILE** and **BEACONS** each carry a small `i` HELP button beside
their title, opening this page's help topic without leaving the window.

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

The third line beside them is which way it is arriving:
`hum from 212° (or 32°) · coh 0.42 · since 03:14`. Two elements in a line
cannot tell a bearing from its reflection about the baseline, so you get
both and break the tie by turning one loop off. Until the beacon compass has
fitted your loops' geometry — which needs beacons of known bearing on two
bands — the phase between the loops is a number and not a direction, and the
line says `hum: direction unknown — no compass fit yet` rather than printing
one; the gate's own sentence about what it is still short of is on the
line's hover. `since` is when this direction first held, which is how you
tell a neighbour's charger from something of your own.

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

A small **✕** beside a row's action button dismisses it — the operator's
own "I've seen this, stop asking." A dismissed row reads `dismissed` with
an `UNDO` beside it instead of the button, and stays that way until the
finding's own dB moves more than 3 dB from where it was dismissed, or the
finding drops out of the gate's list altogether; either way the row comes
back with its action, undismissed rather than remembered as still handled.
DISMISS only ever appears on a row that both has an action *and* is not
already active — an active row is already handled in the sense DISMISS
exists for, and a why-only row has nothing to dismiss. The dismissed set
survives a restart (it is filed the same way the antenna note is), so a
known, unfixable finding — a neighbour's LED strip that is not coming out
tonight — stops repeating itself every poll without turning off the
blanker or notch it would otherwise have nominated.

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
lose them. With no beacon frequency in the span it does not go blank: the
table follows whichever band was checked most recently, and the header says
so — `showing 20 m · checked 1 min ago · no beacon frequency in the span —
tune 14.100/18.110/21.150/24.930/28.200`. Only when nothing has ever been
checked does it fall back to naming the five frequencies with no band shown.
A relaunch that leaves the slice off a beacon frequency reads as the last
CHECK or SWEEP's log, not as the night's results having vanished. Beacon
phase is worth more than it looks — it is the only phase in this window
whose right answer is already known, which is what a future geometry solve
has to calibrate itself against.

This table, like the BAND page's, is kept warm in the background — once
every half minute, from the moment the Diversity window exists, whichever
page it happens to be showing. A restart used to leave it exactly as blank
as it was at the last relaunch until you opened SITE and forced a check; now
opening onto SITE reads back whatever the last background pass already
heard.

**Station grid.** Type your Maidenhead locator into the field at the top of
BEACONS and press `SET` — four characters (`EM10`) or six (`EM10bk`), case
does not matter. The gate already knows where every beacon is; your locator
is the second point every bearing needs. With it the table gains **Brg**
(degrees true) and **km** columns and the pattern plot beside it comes
alive. Without it both columns are dashes and the plot says `needs the
station grid` rather than drawing a dial with nothing on it. `FORGET` drops
it again and keeps every result. A locator that will not parse comes back
as the gate's own refusal on the status line.

**ANTENNA — your own note, beside the locator.** The locator is a fact the
gate can check; this one it cannot. An active loop's control box has a band
selector and a gain knob, and nothing on the wire can see either of them, so
a beacon sweep taken with the boxes on SW and the gain at noon is a
different measurement from the same sweep taken anywhere else. Type what
they were on — `K-480WLA pair, SW both, gain 12 o'clock` — and press Enter
or click away. Up to eighty characters; empty the box to clear it. The note
is filed with every line the site log writes from then on, so last week's
numbers can be read against the switch positions they were taken with, and
a changed note is itself a new noise verdict.

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
tuning away with no way home. The tune goes through the same recentre
policy a CAT tune does, so the panadapter follows the check to 20 m and
back rather than the slice going off the edge of an 80 m display.

`SWEEP ALL` is the five checks nose to tail — 20 m through 10 m, about
sixteen minutes — with one trip home at the end; the countdown reads
`SWEEP 2/5 · 17 m · 3:10 left` and `CANCEL` comes straight home from
wherever it is. **The report** is what the countdown line turns into
once the run is home: `home at 01:26 · 20 m: 2 of 18 heard — 4U1UN 10 W,
KH6RS 100 W` for a single check, band counts for a sweep, and every
band's calls on the line's hover. It is built from the results the gate
scored *since the run left* (an older day's results are not this run's),
and it follows the poll for a few seconds after the trip home because the
last beacon's slot is scored at the slot boundary, up to ten seconds after
the countdown ends. A five-band sweep's report can run wider than the
countdown row has to give it, and rather than clipping mid-word into
whatever sits beside it, the line elides and the rest goes on a second line
under the pattern dial — narrower there than the countdown row, so a report
long enough can still run past *that* and elide a second time, but between
the two lines more of the report is on screen than the countdown row alone
could ever hold, and the full detail, every band, is always on the
countdown line's hover regardless. **The feeds line** under the propagation
lines answers the other question a check leaves behind — what is done with
the numbers: today they feed the loop pattern dial (one point per beacon
heard on both loops, which needs your grid for a bearing) and the
propagation lines; nothing else reads them yet.

The page is polled while it is on screen and while a check is out — a
sweep started from SITE keeps reporting from BAND or SLICE — and a gate
too old to serve the beacon route says `beacon watch: not available from
this gate`. Missing numbers render as `—` rather than as zeros.

## The FILTER page

This page used to hold the whole slice filter — the response curve,
WIDTH, NOTCH, TONE, AUTO CONTOUR, PER TALKER, AGC & NB, PRESETS. None of
that is specific to a pair: a single-antenna receiver has a passband, an
AGC and a notch too. It has all moved to the gate's own CHAIN window,
reachable from the OPEN CHAIN button at the top of this page — the note
underneath it says so in as many words: `roofing, blanker, shape, notch,
APF, AGC: in the CHAIN window`. The passband as a picture — the curve, the notches, the tones the ANF is
holding — is on the CHAIN window's VISUAL tab, not here.

What is left on FILTER, in the PAIR STAGES box, is the two stages that
only exist because there are two receivers: a post-filter that works on
the *coherence* between them, and a per-bin weight that only makes sense
with a spatial map behind it. Neither has an equivalent on a single
antenna.

**POST-FILTER.** Three buttons, OFF/V1/V2, checked state follows the
gate's own `post.enabled`/`post.version`. V1 is the older, always-on-style
suppressor; V2 learns the noise floor between words and subtracts it,
worth trying on faint SSB the combiner has already improved but not fully
cleaned up. The readout beside the buttons shows what V2 is actually
doing when the gate reports numbers for it — `in 7.7 dB → out 10.8 dB,
pauses 10 %` — and just `v1` or a dash otherwise.

**SUB-BAND MRC.** One checkable button. Ticked, the gate gives every bin
in the passband its own weight from the spatial map instead of one
weight for the whole channel — a small, situational gain, and a lab
switch more than a daily one. The readout shows the gate's own numbers
when it has them, `+0.2 dB over broadband, 120 bins`, and a dash
otherwise.

Both write immediately through `/diversity/set` (`post=off|on|v2`,
`mrc=on|off`) — there is no Apply button and no write-hold: the gate's
reply is the next `/diversity` poll's answer, and a button's checked
state simply follows whatever that poll reports.

## The CHAIN window

`OPEN CHAIN` at the top of the FILTER page opens it. It is a separate
window rather than a fifth page because the receive chain is not a pair
feature: an RSPdx has a passband, a blanker and an AGC just as an RSPduo
does, and the window renders whatever `/filter` reports on either.

It is read left to right, as a block diagram, in four labelled groups
with an arrow between them.

**FRONT END** is one summary card, not seven boxes. Antenna port,
broadcast traps, PRE/ATT, RF GAIN, RF AGC, the analogue roofing filter
and the sample rate: one line each, and one hint under all of them —
`ALL SET ON THE SETUP PAGE`. Nothing in that card is changed from this
window except the roofing filter, on a receiver whose driver offers its
IF bandwidths.

HEADROOM and GUARD are two more rows on the same card, when
`/frontend` says a guard is fitted. HEADROOM is a measured line —
how far the last second's strongest sample sat below full scale,
and how many samples clipped — in the warning tone the instant it
drops under 3 dB or anything clips. GUARD is the one control the
card carries: a switch, ON or OFF, and beneath it a floor menu that
limits how far down the switch is allowed to take the LNA. The
inspector's GUARD entry names the last thing the guard actually
did — `stepped 0 → 1 at 11:42, clipping` — rather than repeat the
card's own on/off sentence. The hint under the card changes to
`THE REST IS SET ON THE SETUP PAGE` once GUARD is live from here.
When the gate's dBm scale no longer matches the LNA state GUARD
moved it to, a one-line note says so under the hint — the numbers
on every other card are still true, but relative rather than
calibrated until the scale is re-trimmed. A receiver with no guard
fitted (`available: false`) shows neither row; the card is not
padded out with dashes for a stage that does not exist.

**PAIR** is what the two loops do together: ALIGN, NB, COMBINER,
SUB-BAND NULL, POST-FILTER. On a single-tuner device the gate does not
send those rows and the column is simply not there.

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
sentence is on the hover and in the inspector, always.

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
window, over what is actually arriving: the measured response in blue,
the spectrum filled in under it with the noise floor as a dotted line,
each notch as a vertical line labelled with its depth, each tone the ANF
is holding as a dashed line, the CONTOUR centre and the APF centre as
ticks along the bottom, and the AUTO WIDTH edges as faint lines when
AUTO is on. The one line under the picture reads what the gate said:
`350–2400 Hz · floor -70.0 dB · 2 notches`. Hover anywhere and the
top-right corner reads the frequency and the response there.

Beside that caption sits a small **?** — a `DiversityHelp` button on the
`Chain` topic. The caption's own tooltip stays short now, so the full
walkthrough of what is drawn (edges, notches, ANF tones, the roof band,
CONTOUR and APF ticks, AUTO's edges) lives behind that button instead.

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
LNA, the sample rate — those are set on the setup page), and the
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
- **Drag the roof mark**, the small diamond at the digital roof's centre,
  and release to write `roof_offset_hz=`. The roof itself is a faint
  band, `digital_hz` wide, drawn where it sits even with `PEAK OFFSET`
  off, dashed, to say where it would sit if switched on. No handle is
  drawn once `offset_max_hz` reaches 0.
- **Drag a notch** to move it. That is genuinely two writes, a clear
  and an add, and they go through the same one-at-a-time sequencer as a
  preset — the add waits for the clear's answer.
- **Click any mark** — an edge, a notch, an ANF tone, the CONTOUR or
  APF tick, an AUTO edge — and the window turns to the `CHAIN` tab with
  that stage's card selected, scrolled into view, and its inspector
  filled. The pointer becomes a hand over a mark that is a door rather
  than a handle. A click on open curve does nothing.

**THIS STAGE**, the pane along the bottom, is the inspector. Click any
card and it answers four questions in order: what the stage does to what
you hear, what it is doing now (the card's line spelled out whole), the
control at full size, and what you would hear with it off. Underneath,
the levels the gate measured through it — `in -97.4 · out -101.2 dB`,
with a dash for a leg nothing measures, never a zero. With nothing
selected it says `Click a stage.`

Arrow keys walk the diagram in signal order and stop at both ends; space
presses the selected stage's switch.

Two things about writes are worth knowing. **Nothing is optimistic.** A
switch moves when the receiver says the stage changed, never when you
press it, and while a write is out the control is greyed. **A refusal is
quoted.** When the receiver says no, its own words appear in the
inspector and on the card that asked, and the row does not move — a
refused value never happened. The status line in the corner says only
one of three things: `live`, `applying...`, or `no connection`.

At the right of the header, above the diagram, `HEAR RAW` holds the
whole chain out of circuit for as long as you hold the button — a plain
A/B for how much of what you hear the chain is actually doing — and
puts it back the moment you let go, on hide, on Escape or on losing
focus; it disables itself, reading `CHAIN IS BYPASSED`, when the SLICE
FILTER row's own toggle already has the chain out, and it is not there
at all on a gate that has never mentioned `bypass`.

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
card and in the inspector.

## SQUEEZE

SQUEEZE is one target, one tool, chosen by measurement. Name a carrier
(`squeeze=<hz>`, slice-relative, signed) or the mains comb the noise
profile found (`squeeze=comb`) and the gate measures the target's
coherence between the two loops: at or above 0.5 it earns a spatial
null, steered at that one frequency while the rest of the passband keeps
the combiner's own weights; below it a spectral notch, since two
antennas cannot null what they do not both hear. `squeeze=off` releases.
The status block (`squeeze` on `/diversity` and `/filter`) says which tool
it chose and why (`tool`, `why`, `coherence`, `depth_db`), what it cost the
talker (`talker_cost_db`), and, for a comb, the teeth it is holding
(`comb.teeth_in_band[]`, `comb.spacing_hz`, `comb.offset_hz`). `held` is
true only while the gate is actually holding the target; `reason` is
quoted when it is not.

Below 0.5 coherence the notch tool no longer rides on the slice FIR's
own design. It used to: folded into the 255-tap soft passband, a comb
tooth was one more dip under that window's skirt, a few dB at best, and
only the 1023-tap sharp shape ever cut deep. A bank of complex IIR notch
sections, one per tooth and capped at 24, now sits ahead of the FIR, so
the depth no longer depends on the shape you picked: at least 25 dB at
the tooth centre, under 1 dB of ripple two tooth-widths away, on either
shape. `depth_db` in the squeeze status, and the curve the VISUAL tab
draws, both reflect the FIR and this bank together, the depth actually
delivered rather than the FIR's design alone.

In the CHAIN window the VISUAL tab is where you aim it: Shift+click a
signal on the curve to squeeze it, a bracket marks the held target,
teeth mark a comb; COMB and RELEASE are buttons under the curve. The
SQUEEZE row of the chain carries the same words the gate wrote, and
RELEASE on that row is the only control while a target is held.

## AUTO CLEAN: the chain decides

The gate owns every tool; what it has never owned is the decision. AUTO CLEAN
is that decision, made by measurement: it maps what the noise profile FOUND
onto the one tool that can do something about it, makes one move at a time,
scores it against the same speech-band objective DIG OUT uses, and puts the
move back if the audio got worse.

**Off by default. Off, it holds nothing.**

    GET /diversity/set?auto=on      turn it on
    GET /diversity/set?auto=off     turn it off (releases; does NOT revert)
    GET /diversity/governor         what it is holding, and why

`auto=` takes exactly `on` or `off`; anything else answers HTTP 200 with
`{"error": "bad value: auto='x' (want on|off)"}`. The same block appears under
`governor` in `/diversity`, and as the first row (`auto_clean`) of `/filter`'s
chain, at the head because it drives the rows below rather than being one.

### The rules

Stack order is the receive chain's order, so each stage acts on the residual of
the one before it. At most one move is ever in flight.

| what it saw | condition | tool | undone when |
|---|---|---|---|
| **neighbour** | ADC headroom < 3 dB | front-end guard on (it steps the LNA) | objective falls > margin |
| **impulse** | >= 1 impulse/s | blanker on at the SITE page's own threshold | objective falls; and off again after 30 s quiet |
| **floor** | noise coherence >= 0.4, mode `off`/`manual`, no focus | combiner `mode=null` | objective falls > margin |
| **carrier** | finder says "carrier", in the passband, >= 6 dB over the floor, strongest first | `squeeze=<hz>` | objective falls > margin |
| **mains** | mains comb found AND coherence >= 0.4 | `squeeze=comb` | objective falls > margin |
| **weak** | talker, objective < 6 dB, band steady to 1.5 dB | hands it to DIG OUT | never — the dig has its own A/B rule |

Coherence chooses the squeeze's TOOL, not the governor: at or above 0.5 the
target gets a spatial null, below it a spectral notch (see SQUEEZE). Hum with
no direction is left alone — the audio chain's ANF and notches already have
the tones — and a comb is never stacked on a null already 6 dB deep.
`nb=auto` means the blanker's own arm owns that knob, and AUTO stands aside.

### Not fighting you

* A knob you moved is yours for 60 s; AUTO will not touch it.
* A tool AUTO was holding that you then move is **released** to you — from any
  route, detected by the setting changing under it.
* A move that cost more than the margin (half the recent objective spread,
  0.5–2.0 dB, the DIG OUT rule) is put back, and that (kind, tool) pair is not
  tried again for 5 minutes.
* Turning AUTO off releases everything and leaves your settings exactly where
  they stand. Releasing is not reverting: what it kept, it kept on measurement.

The chain shows AUTO CLEAN as its own card, at the head of FRONT END, the
same toggle the gate's own chain[] row 0 authors. While it holds a tool, the
card that tool moved says so: a one-line note under NB, COMBINER, SQUEEZE or
the FRONT END card's own GUARD switch, reading `AUTO · <kind> · <why>` with
the score appended once one exists (`, +1.8 dB` / `, -0.9 dB`), or
`AUTO · trying · <why>` while it is still measuring one. The note is the
card's own muted "why" tone reused, not a new colour, and it disappears the
moment the gate's next answer no longer names that row.

Select the AUTO CLEAN card and its inspector adds two lines under the usual
one: the governor's own `state_label · why`, then its recent moves, newest first —
`12:41:07 · squeeze · carrier · kept +1.8 dB · <why>`,
`… · undone -0.9 dB · …`, a bare `released`, or `error: <the gate's own
words>` for one it refused. An active backoff prints as
`backing off: mains/squeeze until 12:46`. Nothing here is optimistic either:
the note and the inspector both say only what the gate's last answer holds,
never what a click just asked for — there is no click here to ask with, the
governor moves on its own.

### Scoring with nobody on the slice

With no talker there is no combined SNR, and AUTO used to stand still
and say so. It no longer does: the move is still made, scored by the
proxy the tool carries its own measurement for. Every event and
`holding[]` row gains a `scorer` (`snr`, or `proxy:depth`,
`proxy:blanking`, `proxy:floor`, `proxy:clips`), and the status gains
`objective_source` (`snr` or `none`).

| tool | proxy | kept when |
|---|---|---|
| squeeze | its own measured `depth_db` | null ≥ 6 dB (back into the floor it was picked out of), notch ≥ 10 dB |
| nb | `blanked_pct` | under 5 % (DIG OUT's own free-blanking point) |
| mode=null | the passband floor | ≥ 1 dB off it |
| guard | clips and headroom, after 10 s | either the clips fell (0 always keeps) or the headroom rose ≥ 1 dB |

The squeeze and the blanker also watch that floor, the median of
`/diversity/spatial`'s level strip over the passband, and a move that
lifts it more than 1 dB goes back whatever it scored; the guard never
does, since putting it back over a number that did not move hands the
ADC its clipping again. A proxy-kept move stays kept when a talker turns
up; it is not re-scored. DIG OUT has no proxy, it needs a talker: it is
handed a slice once per (frequency, talker) pair and never again, backs
off 30 minutes rather than 10, and banks a run's `gain_db` only when the
dig stands behind it. A tentative, cancelled or "worse" run scores 0
with the dig's own note in the `why`, and that note blocks the next
hand-off for 30 minutes from the dig's own end, not for ever. AUTO off
stops a dig AUTO started and leaves one you started alone.

### Seeing it, and turning it off

While `governor.auto` holds, three surfaces show it, in the same
emphasised ON tone every other switch in the sidebar wears (not the
warning gold: AUTO CLEAN on is not an alarm). The sidebar's own **AUTO
CLEAN** button (a checkable switch, press it to send `auto=on` or
`auto=off`) and the Diversity window's switch at the left of the NEXT line,
visible from every page, say `AUTO CLEAN ON` and nothing more — no state word, no
sentence, the operator's own words being that a switch should not carry
a status message. The switch's tooltip is one short fixed line ("The
chain is adjusting itself. Click to turn it off." / "Let the chain
adjust itself. Click to turn it on."), and its accessible description
carries a short `AUTO CLEAN ON · <state_label>` for screen readers.
`state_label` is the governor's own few plain words — `listening`,
`trying a null on the mains hum`, `trying the blanker`, `kept`, `put
back`, `holding the blanker`, `DIG OUT running`, `waiting for the
stream` — and, with the sentence (`why`), it lives in full on the AUTO
CLEAN card's inspector and on the CHAIN window's read-only header, which
has the room for `AUTO CLEAN ON · <state_label> · <why>` and no write
path of its own. A gate too old to send `state_label` falls back to the
raw `state` word there instead. Off, or on a gate with no governor block
at all, the sidebar and NEXT-line switches collapse to a bare `AUTO CLEAN`
toggle and the CHAIN header disappears.

### DIG STOP, on the line itself

While a dig is running, the NEXT line carries a **STOP** button beside the
window's own dig-stack STOP; either sends
`GET /diversity/dig?cancel=1`. The line also says who started the run:
"started by AUTO" when the governor is holding or trying `dig`, "started
by you" otherwise.

## A working session

This is the order the START page encodes, and the order to work in. Each step
makes the next one mean something: a mode set before the tuners are aligned
is solving on two signals that are not the same signal, and a filter set
before you are hearing the combined output is a filter on the wrong audio.

1. **Align the tuners.** Start the gate, connect, open the window, and wait
   for the align step to read a lag. The gate cross-correlates the two tuners and
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
   state, and the hear step says so while you are in one.

4. **Act on the noise the gate found.** The SITE page's profile runs by
   itself and lists what this address is actually making — mains hum,
   impulses, periodic modulation, tones — with the gate's verdict on each
   and one button where there is something to do about it. The noise step
   counts the findings that still offer an unused button. Work down them; the ones
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

**How much is the chain actually doing?** Hold `HEAR RAW` on the CHAIN
window. It takes the whole chain out of circuit while held, so what you
hear is the raw slice — let go and the chain is back.

## For scripts: the control port

The gate's HTTP control port (default 8731) serves:

- `GET /diversity` — the status object (mode, weight, SNRs, talker,
  memory (each entry with `voice` {centroid_hz, low_hz, high_hz, tilt_db,
  syllabic_hz, over_s, overs} or null), focus, passband, noise coherence, blanker, sources, loops,
  capture, `subband` {enabled, bins, extra_db}: the per-bin refinement of
  the tracked weight, `noise_profile` {mains_hz, hum_db, harmonics,
  impulses_per_s, impulse_db, periodic[], seconds, window_s,
  impulse_window_s, `kinds[]`}: what kind of noise this is, `post`
  {enabled, version: 1|2, and when v2: snr_in_db, snr_out_db,
  pause_fraction, hold}: the coherence post-filter (the FILTER page's
  PAIR STAGES), `mrc` {enabled, gain_over_broadband_db, bins_used}: the
  per-bin sub-band weight). Each `kinds`
  row is {kind: "mains"|"impulse"|"periodic"|"tone"|"floor", label, detail,
  db (or null), window_s, action (or null), why (or null), active}, and an
  `action` is {label, route, query} — the gate's own nomination, meant to
  be sent back as `GET <route>?<query>` unchanged.
- `GET /diversity/set?mode=&source=&phase=&ratio=&nb=&nb_db=&pan=&null_source=&focus=&subband=&post=&mrc=`
  — any subset; `source=combined|a|b|stereo` is what reaches the audio
  (stereo: A left, B right) and `pan=` what the panadapter draws;
  `focus=<id>` pins, `focus=off` releases; `subband=on|off`
  (default on: in null/track every passband bin gets its own weight
  wherever the learned noise has a direction, the talker held
  distortionless); `post=off|on|v2` sets the coherence post-filter;
  `mrc=on|off` sets the per-bin sub-band weight.
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
- `GET /diversity/set?antenna=<free text>` records what the antenna side
  is set to (which loops, which box switch positions, gain), up to 80
  characters; every beacon and noise line the site log writes from then on
  carries it as `antenna`, and a changed note is itself a new noise
  verdict. `antenna=off` clears it. The current note is in `/diversity`
  under `sitelog` with the log's `written`, `skipped` and `error`.
- `GET /diversity/compass` — the beacon compass: the array geometry fitted
  from beacons of known bearing, and under `noise` the direction the
  profiled noise is arriving from — {available, kind:
  "hum"|"lines"|"impulse"|"floor"|null, phase_deg, coherence, bearing_deg
  (null until there is a fit), mirror_deg (the reflection about the
  baseline), bins, since (epoch seconds, or null), reason} — where `reason`
  is the gate's own sentence about what the fit is still short of, e.g.
  `"hum on 147 bins at 0.42, phase only: 0 beacon(s) with a bearing and a
  ratio, 4 needed over 2 bands"`.
- `GET /diversity/dig` — status of the chain dig; `?seconds=60|180|300`
  starts one, `?verdict=better|worse|keep` labels a finished one (`worse`
  puts the chain back), `?cancel=1` stops a running one and restores. The
  status object keeps the same field names in every phase: `available`,
  `running`, `phase` ("idle"|"sampling"|"searching"|"done"), `verdict`,
  `record`, `error`, `cancelled`, `gain_db`, `steps[]`, `best`, `changed`,
  `started`, `ends`, `elapsed_s`, `seconds`, `remaining_s`,
  `objective_before`, `objective_after`, `trials_planned`, `trials_done`,
  `talker_id`, `kind`, `margin_db`, `snapshot`, plus `measured_best_db`
  and `measured_best` (the best trial's step, kept or not),
  `baseline_spread_db`, `unsteady` and `note` (the gate's own sentence on
  a baseline that swung more than 3 dB while sampling). `gain_db` is the
  sum of the kept steps' deltas, 0.0 when nothing was kept;
  `objective_before`/`objective_after` stay the raw reads. Knob values in `best`,
  `changed` and each step: `post` true|false|"v2"; `subband`, `mrc`, `nb`,
  `contour`, `anf`, `apf`, `auto_eq` booleans; `nb_db` a float; `width` a
  [low_hz, high_hz] pair; `agc` "fast"|"med"|"slow". `available: false` is
  a gate that cannot dig, and the window hides the control entirely.
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
  {analogue_hz, digital_hz, offset_hz, offset_enabled, offset_applied_hz,
  offset_max_hz}, `response` {hz[], db[]} — the measured curve —
  and `spectrum` {hz[], db[], floor_db} or null: the one-second pre-filter
  spectrum on the SAME grid as `response`, in dB below its own peak (so its
  maximum is 0.0) with `floor_db` the median of those points on that same
  scale. Null until the gate has heard a block. Numbers may be ints or
  floats.
- `GET /filter/set?low=&high=&shape=&anf=&contour=&contour_hz=&contour_db=&contour_width=&apf=&apf_hz=&apf_width=&auto=&auto_eq=&nb=&nb_db=&agc=&attack_ms=&decay_ms=&hang_ms=`
  — any subset; `shape=soft|sharp`, `agc=fast|med|slow|long|off`,
  0 ≤ low < high ≤ 20000. Replies with the status object above, or with
  `{"error": "..."}` if a value is refused.
- `GET /filter/set?roof_offset_hz=<hz>` — signed Hz from the slice centre
  for the digital roof's PEAK OFFSET, clamped to `roofing.offset_max_hz`;
  `GET /filter/set?roof_offset=on|off` — the check mark. `/filter`'s
  `chain[]` row `roof_digital` carries `checks: [{key: "roof_offset",
  label: "PEAK OFFSET", on, applied_hz, max_hz}]`.
- `GET /filter/notch?add=<hz>&width=<hz>` places a notch;
  `GET /filter/notch?clear=<hz>` removes one; `GET /filter/notch?clear=1`
  removes them all. The automatic notcher's own tones are not in this list
  and are not touched.
- `GET /diversity/memory/name?id=&name=` — label a talker (empty clears).
- `GET /diversity/memory/clear`, `GET /diversity/align`,
  `GET /diversity/capture?seconds=`.
- `GET /frontend` — the front-end linearity guard: `available`,
  `guard`, `floor_state`, `max_state`, `lna_state`,
  `dbm_calibrated`, `cal_state`, `headroom_db`, `peak_dbfs`,
  `headroom_1s_db`, `clips_1s`, `per_channel[]` {headroom_db,
  clips_1s}, `state`: idle|stepping_up|holding|stepping_down,
  `hold_until` (epoch or null), and `events[]` {t, from, to,
  reason}. `available: false` means every other key is null or
  empty — there is no guard to read. `GET
  /frontend/set?guard=on|off` switches the guard; `GET
  /frontend/set?floor=<state>` sets how far down it is allowed to
  take the LNA.
- `GET /diversity/set?squeeze=<hz>|comb|off&squeeze_width=<hz>&spacing=<hz>&offset=<hz>` — `squeeze` is a signed offset Hz (a "signal" target, slice-relative baseband, not absolute RF), the literal `comb` (with `spacing`/`offset` given: that pair outright; neither given: auto-detect off the next ~2 s), or `off`/empty to release; omit it to leave the target alone and only move `squeeze_width` on one already held. `/filter`'s `squeeze` object answers with `hz, width_hz, held, reason, tool ("null"|"notch"|null), why, phase_deg, ratio_db, coherence, depth_db, scope, target ("signal"|"comb"), comb {spacing_hz, offset_hz, teeth_in_band[], teeth_seen, coherence} | null, since, talker_cost_db, bearing_deg, mirror_deg`. `since` null means off; a number with `held` false means armed (still measuring); `held` true means the tool is live. `hz` and every `comb.teeth_in_band[]` entry are signed and slice-relative, the same baseband convention as the write side — not the absolute Hz `low_hz`/`high_hz` use elsewhere on `/filter`.
- `GET /diversity/set?auto=on|off` — the AUTO CLEAN switch (exact strings;
  anything else is `{"error": ...}`). `GET /diversity/governor` — what it
  holds and why: `auto`, `state` (`idle|measuring|applying|settling|backoff`),
  `state_label` (the few plain words a switch shows: `off`, `listening`,
  `trying <tool> on <what>`, `kept`, `put back`, `holding <tools>`,
  `DIG OUT running`, `waiting for the stream`, `failed`), `why` (the
  sentence), `settle_s`, `margin_db`, `spread_db`, `holding[]` {tool, params,
  kind, why, since, delta_db}, `pending`, `events[]` (last 50, each with
  `result: pending|kept|undone|released|error` and `delta_db`), `backoff[]`
  {kind, tool, until}, `available`, `error`. The same block is `governor`
  on `/diversity`, and the chain's first row (`auto_clean`) is its toggle.
  Off, it holds nothing; turning it off releases without reverting.

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
- The RSPduo driver flushes each tuner's FIFO lazily, on that tuner's next
  read, so the two rings used to come up 66 driver packets (33.3 ms) apart
  at every stream start and after every overflow, and the correlator
  reported it faithfully as a lag of −4158 samples at 125 kS/s. The gate
  now primes both FIFOs together at stream start and after a fault, and
  folds any leftover whole-packet skew off the stream that is ahead; the
  `[soapy] ring sync` line in the log says what came off. The lag you see
  after alignment should be small (under a packet, ±63 samples at
  125 kS/s), not thousands.
