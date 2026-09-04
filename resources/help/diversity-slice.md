# Slice

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

