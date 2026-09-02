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

**SLICE / BAND / SITE.** The three buttons at the left of the chain row
switch pages. SLICE, described below, is about the frequency you are tuned
to. BAND is about the whole span the gate can see, and is where you go to
decide where to be tuned. SITE is about neither: it is about your station —
what kind of noise this address makes, and what the world's beacon network
measures your antennas to be worth.

**Chain row.** MODE (off / manual / null / track), HEAR (OUT — the combined
output, A, B, STEREO — what goes to the audio; the combiner keeps learning
whichever you pick), *Hear A only* (press-and-hold A/B comparison that restores the
previous mode on release), REALIGN, and CAPTURE with a duration: writes an
aligned two-channel recording on the gate.

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
signature), phase, level, hits, last heard, first heard, and **TX** — the
upper edge of the station's transmitted audio in kHz, which is their rig's
filter and stays put when the antennas move and the spatial signature does
not. Hover a row for the whole print: both audio edges, where the voice is
centred and how it tilts, syllables per second, how long their overs run.
The gate learns it from the combined audio over their overs (a print needs
an over of 1.5 s or more), so a talker heard once briefly shows `—`. The
row that is lit is whoever is talking. Buttons:

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

## The BAND page

Everything on the SLICE page is about one channel, so none of it can answer
"where should I be listening?". The BAND page asks the gate two questions
about its whole 125 kHz instead. Both are click-to-tune: a click moves
AetherSDR's active slice and switches the combiner to **track**, so the
first over you hear is already being solved for.

**SPATIAL WATERFALL.** One row per poll (4 Hz) across the gate's span, with
colour meaning *direction* rather than strength: hue is the inter-loop
arrival phase, saturation is coherence, brightness is level over a 50 dB
window below the row's loudest bin. Two stations arriving from different
places cannot share a colour. One local noise source paints a single flat
colour across everything it touches, which is how you recognise it. Sky
noise has no direction, so it goes grey. The receiver's passband is
bracketed. Click a column to tune there; hover for the numbers.

**FINDER.** The activity strip is the share of the last ten minutes each
column carried voice; the table below it is the conversations the gate
found there, best first (at most twelve, ranked by the gate): frequency,
score, SNR, syllabic rate, how long it has been active, how long ago it was
last heard, arrival phase, coherence, and `gain` — the diversity gain the
pair can actually earn on that signal, which on plain sky noise is often
near zero and says so. **Tune** (or a double-click on the row) goes there.

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

The whole page is polled only while it is on screen, and a gate too old to
serve the beacon route says `beacon watch: not available from this gate`.
Missing numbers render as `—` rather than as zeros.

## A working session

1. Start the gate, connect, open the window. Wait for `aligned` in the
   chain line (the gate cross-correlates the tuners; lag is a few tens of
   samples and stays put).
2. Set MODE to **track** and listen. Watch `gain`: near zero on a quiet
   band is expected.
3. If BALANCE shows coherence above 0.5 the noise has a direction. Try
   **null** mode, or in track mode just let the idle-time null handle it
   between overs.
4. Working a specific station: wait for them to be remembered (one over of
   a few seconds), double-click the name cell and label them, select the
   row and **Lock**. Callers are now nulled while the lock holds.
5. Use *Hear A only* to hear the difference the combiner makes; use CAPTURE
   to keep a recording of the aligned pair for later analysis.

## For scripts: the control port

The gate's HTTP control port (default 8731) serves:

- `GET /diversity` — the status object (mode, weight, SNRs, talker,
  memory (each entry with `voice` {centroid_hz, low_hz, high_hz, tilt_db,
  syllabic_hz, over_s, overs} or null), focus, passband, noise coherence, blanker, sources, loops,
  capture, `subband` {enabled, bins, extra_db}: the per-bin refinement of
  the tracked weight, `noise_profile` {mains_hz, hum_db, harmonics,
  impulses_per_s, impulse_db, periodic[], seconds}: what kind of noise
  this is).
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
- `python -m aether_gate.replay CAPTURE.npz` (in the gate) — the replay
  lab: a capture through the live combiner path as A / B / wideband /
  per-bin WAVs at one gain, plus `summary.json`.
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
