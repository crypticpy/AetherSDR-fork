# Diversity: assessment and roadmap

*Written 2026-09-01, late. Local plan for the SDRplay diversity build on the
`pr/aether-gate-applet` branch of AetherSDR and `feat/rspduo-diversity` of
Aether-gate. Nothing in here is upstream policy; it is where we intend to
take the work and why.*

---

## 1. Where we are

### What exists tonight

**Gate (Aether-gate, RSPduo in dual-tuner mode, two magnetic loops)**

| Piece | What it does | State |
|---|---|---|
| Alignment | GCC-PHAT lag search between the two tuners' rings, re-run on demand | Live, lag −63 samples on this box |
| Combiner | one complex weight, y = (a + m·b) / √(1+|m|²) | Live |
| Modes | off · manual (phase/ratio) · null (smallest noise eigenvector) · track (max-SNR generalised eigenvector, held-out validated by block parity) | Live |
| Talker memory | steering vector + weight per talker, recall in one block, merge on re-hearing, stable ids, names | Live (ids/names tonight) |
| Fade guard | switches to the better loop alone when the combined output falls 3 dB behind it for 0.2 s | Live |
| Steady-carrier guard | a tone parked in the passband is reclassified as noise after 1.5 s and nulled; speech over it is a fresh over | Live |
| Blanker | two-channel impulse blanker, threshold, widened 2 samples | Live |
| Noise map | per-bin inter-loop coherence over the span, source detection, null-a-source | Live |
| Passband phase | flatness / slope / coherence of inter-loop phase across the passband | Live |
| Capture | aligned two-channel npz for offline work | Live |

**AetherSDR**

| Piece | State |
|---|---|
| Sidebar Diversity section (scope, mode, hear, noise, memory, capture) | Live; to be slimmed (see §3) |
| Diversity window (chain row, large scope, antennas, noise, stations, alignment) | Live |
| Window v2: talkers table with names, timeline, events log, legible noise panel, hover help | In progress tonight |

### What the physics allows, honestly

- Two antennas give **one** spatial degree of freedom: one null, or one
  maximum, not both. Every "smart" behaviour above is really about *when* to
  spend that one degree.
- Against **isotropic** noise (what 40 m looked like tonight, inter-loop noise
  coherence 0.10–0.16) the ceiling is maximal-ratio gain: +3 dB when the loops
  are equal, ~+1.5 dB when one is 4 dB down. We measured +0 to +1 dB on
  7.204 MHz, i.e. we are already near the ceiling on that kind of scene.
- Against **coherent** noise (a local switch-mode supply, a plasma TV, one
  strong QRM source) the null is worth 10–25 dB. That is where diversity is
  transformative, and it is a property of the operator's site, not of the code.
- A two-loop pair gives inter-antenna **phase**, not bearing. We do not draw
  compass roses. Bearing needs ≥3 elements with known geometry.

### Parity check

What other hobby tools offer: SDRuno's RSPduo diversity (manual phase/gain,
an auto-null button), a few Perseus/Elad plug-ins, and hand-rolled GNU Radio
flowgraphs. None of them has adaptive talker memory, held-out validation of
fits, automatic steady-carrier nulling, a passband flatness readout, or an
event log explaining what the combiner did. On features we are past parity.
On *experience* we are at parity: a sidebar of numbers and a window of
numbers. The next step is making the instrument teach the operator what the
air looks like, and making it serve one job: **pulling one specific station
out of the noise**.

---

## 2. Principles for the overhaul

1. **Invisible until relevant.** Nothing diversity-related is drawn unless the
   attached tuner reports it. The sidebar is a status line and a door. The
   window is the instrument. (Aether Voice is the model: you would not know it
   is there until you open it.)
2. **Honest displays.** Never draw what was not measured. Phase, not bearing.
   "—" not 0. Every automatic decision has a readout saying why.
3. **One-station focus is the primary workflow.** DX digging means: this
   talker, at this phase, keep the weight that helps *them* and treat everyone
   else as interference.
4. **N-ready.** Everything is expressed as N-channel covariances, steering
   vectors and weights. UI elements must generalise to N: the dial becomes a
   constellation, the "A/B" meters become a bank, "null a source" becomes
   "null up to N−1 sources".
5. **Each feature ships with its test, its tooltip and its doc line.**

---

## 3. Sidebar: slim it (tonight)

- Shown only when `/diversity` reports `available: true`.
- Contents: one status line (`track · #3 Bob · +1.4 dB` / `off`), the mode
  combo, and a full-width **Open Diversity window** button.
- Everything else moves to the window. An AppSettings key
  (`AetherGateDiversityPanel_ShowScope`) keeps the compact scope for anyone
  who wants a glance-view in the sidebar; default off.
- Poll cadence unchanged; the map poll runs only while the window is open.

---

## 4. Window: from readout to instrument

### 4.1 In flight tonight (v2)
Talkers table (ids, editable names, live row), 120 s timeline of A/B/OUT with
a talker band, derived events log, noise map with a frequency axis and the
passband marked, antenna balance block, hover help on everything, bigger text.

### 4.2 Station Focus ("Lock on") — the DX feature
- **What:** pick a talker (row button or "lock the one talking now"). The gate
  fits weights for *that* steering vector only: memory recall is restricted to
  it, other talkers' overs are treated as interference, the fade guard and
  refits are evaluated on the locked talker's SNR, and long time constants are
  allowed because the target is known.
- **Why it is new:** every diversity tool optimises "whatever is loudest now".
  A DX chaser wants the opposite: the weak one, even while the loud one is
  keying up on top.
- **Gate:** `/diversity/set?focus=<id>` and `focus=` (clear); status
  `focus: {id, locked_s, snr_db, overs}`. Interferer handling: when a
  non-focus talker is recognised, switch to the weight that nulls *them*
  (memory has their steering vector, so the null is known in one block) and
  return to the focus weight when they stop.
- **Window:** a FOCUS banner across the scope (`FOCUS #3 "VK2…" · 4 overs ·
  best +7.2 dB`), a Lock/Unlock button per row, and the timeline shading the
  focus talker's overs.

### 4.3 Dig mode (weak-signal profile)
A one-click profile for the focused talker: lower talk-detection threshold
gated by steering-vector match (so noise cannot trigger it), longer signal
time constants, fade guard hold longer, refresh less often. Readout: `dig`
lamp. Reverts on unlock.

### 4.4 Over ledger
Every over becomes a row: start time, duration, talker, SNR A/B/OUT, gain,
weight source (memory / refit / fade / null). Exportable CSV. This is both the
research-station view and the dataset for tuning the algorithms.

### 4.5 Spatial scope (the dial, grown up)
- Talkers as markers sized by their best SNR, the live one pulsing, the focus
  one ringed; trail of the live weight; the null direction drawn as a line
  when a source is nulled.
- Phase-across-passband strip (tonight): 16 bins of inter-loop phase and
  coherence, so you can *see* whether one weight fits the whole passband.
- N-antenna generalisation: N−1 relative phases; show any two as axes, the
  rest as a small multiples row.

### 4.6 Noise scene
- Coherence **waterfall** (map history, ~5 min): intermittent sources become
  visible as stripes.
- "Auto-null strongest coherent source while idle" toggle: when nobody is
  talking and a source's coherence is above 0.5, spend the degree of freedom
  on it; give it back at the first over.
- Per-source history (when it was there, how strong), click-to-null stays.

### 4.7 Honest A/B/OUT blind test
Press **Blind test**: for 10 s the audio source is A, B or OUT at random;
you vote which sounded best; the window tallies. Novel, cheap, and the only
way to know whether the combiner *sounds* better rather than *measures*
better.

### 4.8 Session stats
Talk time, mean gain over the best loop, share of time each loop was best,
fade-guard switches, steady-carrier events, talkers heard. Derived in the
window from the timeline; also written by the overnight recorder.

### 4.9 Small things that make it feel finished
Keyboard: `1/2/3` hear A/B/OUT, `Space` hold-to-compare, `L` lock the live
talker, `N` null selected source. A compact mode (window ≤ 700 px tall drops
the events panel). Theme-checked in every built-in theme.

---

## 5. Gate / DSP roadmap

| # | Feature | Why | Cost |
|---|---|---|---|
| G1 | **Focus / lock** (§4.2) with interferer nulling from memory | the DX workflow | 1 evening |
| G2 | **Passband phase export** (16-bin phase/coherence) | the honest readout for sub-band weights | tonight |
| G3 | **Sub-band weights** (2–4 bands) gated on flatness < 0.7 | only if the recorder shows real slope | 1 evening |
| G4 | **Persisted talker names** (`~/.aether-gate/diversity-names.json`) | names survive a restart | tonight |
| G5 | **Background null while idle** (§4.6) | uses the idle degree of freedom | small |
| G6 | **Spatial blanker**: blank an impulse only when it is coherent across both loops (local) and leave sky-borne static to the audio NB | fewer holes in the audio | small |
| G7 | **Loop balance check**: "B is 6 dB down for 10 min: check the loop preamp" | most diversity disappointment is a sick antenna | small |
| G8 | **Adaptive time constants** driven by SNR and talk modulation | faster on strong, patient on weak | medium |
| G9 | **Overnight recorder + morning report** (`tools/diversity_recorder.py`, `tools/diversity_report.py`) | the data that decides G3/G8 and the demo numbers for the fork | tonight |
| G10 | **Supervised NR** trained on strong local stations as pseudo-clean plus captured band noise | the neural-net idea, done honestly (Noise2Noise did not pay: +0.2/+0.4 dB) | weeks |
| G11 | **Tune route on the control port** (`/tune?hz=&mode=`) mirrored to the SmartSDR client | scripted band surveys | small, needs the client sync done right |

---

## 6. Groundwork for N antennas

**Hardware reality.** Coherent multichannel HF is the hard part, not the
maths. RSPduo gives two coherent tuners and cannot take an external
reference, so two RSPduos are *not* coherent with each other. Candidates for
four channels: a KrakenSDR is five coherent channels but starts at 24 MHz
(no HF without converters); Afedri and RX-888 class devices are single
channel; USRP/LimeSDR are two channels and need upconverters. The honest
path is: two channels now, and software that is N-ready so the day a
coherent 4-channel HF box is on the bench nothing has to be rewritten.
FlexRadio 6600/8600 offer two SCUs (true diversity); AetherSDR already has a
SmartLink client, so a Flex "adapter" for the gate's covariance path is a
plausible sponsor demo.

**Software N-readiness checklist**

- Covariances N×N, weights and steering vectors length N (today's code is
  written 2×2 in places: `_snr_of`, `weight_to_polar`, memory match).
- MVDR/LCMV solver with K ≤ N−1 null constraints (today: eigen-null only).
- Memory match on |⟨s₁,s₂⟩|² generalises unchanged.
- Geometry file (element positions, loop orientations) → **real bearing**
  and elevation (DOA) become possible and the UI may then draw a compass.
- Mechanical degrees: loop rotation/tilt as slow outer-loop optimisation
  (scan, measure SNR of the focus talker, step). The focus feature (G1) is
  the inner loop this needs.
- Status contract already carries `channels`; the window's meters/dial must
  be built from that count.

---

## 7. The public build

1. Branch `sdrplay-diversity` on `crypticpy/AetherSDR-fork` and
   `crypticpy/Aether-gate-fork` (public forks of GPL-3 projects; permitted).
   Never on the forks' `main`.
2. README banner: unofficial, unsupported, for RSPduo owners, what it adds,
   bugs to us. Point everyone else upstream.
3. Releases: zipped `AetherSDR.app` + gate wheel; note the right-click-open
   step for an unnotarized app. A ten-second before/after audio clip from a
   capture, and one screenshot of the window mid-QSO.
4. The morning report (G9) numbers go in the README: "over N hours on 40 m,
   the combiner beat the better loop by X dB on Y% of overs".
5. Courtesy note to the upstream maintainers once it is presentable.
6. Sponsor pitch: the N-antenna roadmap above, with a Flex 6600/8600 as the
   named ask.

---

## 8. Tonight's execution order

1. Land window v2 (talkers, timeline, events, noise, help). Gate, commit,
   restart AetherSDR.
2. Slim the sidebar (§3). Commit.
3. Gate: G2 passband phase export, G4 persisted names. Commit, restart gate.
4. Window: phase-across-passband strip, session stats. Commit, restart.
5. G9 recorder running for the rest of the night; report script written and
   tested on the first hour's data.
6. G1 focus/lock on the gate with tests, and the Lock UI in the window, if
   time allows before the morning; otherwise its spec is above.
7. `docs/DIVERSITY.md` user guide; morning pack on the Desktop (screenshots,
   report, night changelog).

Standing rules: no pushes; signed commits per unit; every gate green before
a commit; restart the gate or AetherSDR only when a unit needs it; the
certificate question stays parked; never probe the SDRplay service by
trial-and-error device opens.

## 9. Beyond parity: fifteen material steps

Ranked by what they would do for a listener, not by ease. Each carries the
physics it depends on and the way it can disappoint.

1. **Per-bin weights.** One complex weight per passband bin instead of one
   for the whole channel. Tonight's report measured a phase slope with a
   p90 of 16°/kHz, so a wideband weight is 20° wrong at the edges of an
   SSB passband; per-bin weights fix that and, because each bin has its own
   degree of freedom, a beam on the voice can coexist with a null on a
   heterodyne at 1.2 kHz. Cost: overlap-add STFT on the audio path.
2. **Pile-up separation.** The memory already holds a steering vector per
   talker. With two talkers on frequency, the 2×2 inverse of those vectors
   unmixes them into two audio streams: "the DX" and "the caller". Routed
   to two AetherSDR slices or left/right. Quality depends on how far apart
   the two are spatially; same phase and level means no separation.
3. **Spatial stereo monitor.** Place each talker in the stereo field by
   inter-loop phase and let the listener's own cocktail-party effect do the
   rest. Nearly free once (2) exists; a big perceived win for pile-ups.
4. **Noise-sense mode.** Loop B as a noise antenna and a sample-by-sample
   NLMS canceller that tracks drifting mains and switching-supply noise,
   the digital and automatic version of an MFJ-1026. 20 to 40 dB on a
   coherent local source is realistic; nothing on sky noise.
5. **Impulse subtraction, not blanking.** A lightning crash or a local
   impulse has a spatial signature; project it out instead of punching a
   hole in the audio. Harder than it sounds when the impulse saturates.
6. **Talkers name themselves.** AetherSDR already has speech recognition.
   A phonetic-alphabet parser on the transcript catches the callsign, the
   applet writes it to the gate's memory, and a lookup adds location.
   Accuracy is the risk; confirm before writing rather than guess.
7. **Spatial waterfall.** A panadapter row where hue is inter-loop phase
   and brightness is coherence. Stations from different directions show in
   different colours across the whole band; a local noise source is one
   flat colour. The gate computes the per-bin numbers already.
8. **Bearing mode.** A geometry wizard (loop spacing, orientation, a
   calibration on a station of known bearing) turns phase plus level ratio
   into a compass bearing with the 180° ambiguity drawn. Orthogonal loops
   on one mast give Watson-Watt direction finding. The scope becomes a
   rose with talkers plotted on it.
9. **Propagation gauge per talker.** Envelope correlation between loops,
   fade rate and phase spread tell multipath from single-mode and predict
   the diversity gain before the combiner earns it. Honest science: this
   is exactly when two antennas help and when they cannot.
10. **Follow the DX across a split.** A locked station keeps its spatial
    signature after a QSY; scan a few kHz for it and offer "heard at
    +2.1 kHz, click to tune". The gate cannot tune, so the offer lives in
    the window.
11. **AUTO mode.** A scene classifier (coherence, talk activity, steady
    carrier, focus) picks null, track, idle-null or manual and explains
    its choice in EVENTS. The default for someone who never opens the
    window.
12. **Station book.** Talkers across sessions in SQLite: signature per
    band, callsign, bearing, best hours, SNR history. Answers "when do I
    hear this one best".
13. **Transmitter and voice fingerprint.** Carrier offset, audio
    bandwidth, pitch and cadence statistics alongside the spatial
    signature, so two stations at the same phase are told apart and a
    station that moved antennas is still the same talker.
14. **Capture to replay lab.** Run captured pairs through any combiner
    configuration offline, export A, B and OUT audio per over, and keep a
    regression bench. Also the before/after clips for the fork's README
    and any sponsor conversation.
15. **Four coherent channels.** Two RSPduos on a shared reference clock
    (the RSPduo has reference in and out) give four tuners; GCC-PHAT
    aligns the start. Three degrees of freedom: a beam and two nulls, or a
    two-dimensional bearing. The quad path with about 300 dollars of
    hardware, and the reason the code was written N-ready.

## 10. Second night's execution order (2026-09-02, 02:00 →)

The user has the keychain prompt clicked, the bridge is up, and tuning
anywhere is authorised ("you have the radio"). Order, judged by the first
night's pace; nothing is pushed.

1. **Done** (gate `4f64aaa`, `ebe3481`). Gate: `core/finder.py` — live per-bin spatial rows (`/diversity/spatial`)
   and the conversation finder (`/diversity/finder`): rings of both loops'
   spectra, syllabic-modulation voice detector, candidates ranked with the
   pair's phase, coherence and predicted gain. Tests. Restart the gate.
2. **Done** (`9d5555cd`). AetherSDR (agent): BAND page in the Diversity window — spatial waterfall
   (hue = phase, saturation = coherence, brightness = level), FINDER table
   with click-to-tune, new test binary `diversity_band_test`.
3. **Done** as far as it goes (gate `6addb61` noise profile; `444ca05` a
   weight per passband bin instead of an NLMS canceller — the per-bin MVDR
   nulls several sources at once and needs no noise antenna; `mode=sense`
   is dropped). Gate: noise profile (mains-locked periodicity, comb
   spacing, impulse rate) in the status.
4. **Gate done** (`2a26404`); results page in the window: the SITE page.
   Beacon sweep: NCDXF/IARU table, the applet parks the slice on a beacon
   frequency for one 3-minute cycle, the gate reports per-slot SNR on A and
   B, phase, coherence and the lowest power step heard; results page.
5. **Done** as `python -m aether_gate.replay` (gate `ff74ce3`): run a capture through any combiner
   configuration, write A/B/OUT WAVs per over.
6. Stretch: **stereo monitor done** (gate `bed8002`: `source=stereo`, A
   left, B right; the same commit fixed HEAR, which had been changing the
   panadapter rather than the audio). **Voice and rig print done** (gate
   `0b0dd4c`: `core/voiceprint.py`, `voice` on each memory entry; app
   `5c53d708`: the TALKERS **TX** column is the rig's upper audio edge and
   the row's hover is the whole print).
