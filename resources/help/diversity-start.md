# Start

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

**The START page — five cards, in the order that gets the best signal.**
START is the window's first tab. It draws one fixed-height card per step —
RECEIVER, SITE NOISE, BAND, STATION, LISTEN — always in that order, always
all five: nothing on a card appears or disappears as you progress, only its
glyph, its state sentence and whether it carries a button change. A tick
(`✓`) is a step behind you, a filled dot (`●`) is the one to do next, a
hollow circle (`○`) is one still ahead. Only the next step's card shows a
button, and only when AUTO CLEAN is on; with it off every card still shows
its own state, but nothing offers to act for you. Each card also carries two
fixed lines of what that step buys you and one line of when it resets — see
*A working session* below for the five in full.

**The NEXT strip — at the foot of the window, not the top.** Every page
carries the same one-line footer above the gate status strip: the single
step still ahead, quoted with the gate's own state sentence for it, e.g.
`NEXT · SITE NOISE · 2 findings`, and its button when AUTO CLEAN offers one.
Once RECEIVER, SITE NOISE, BAND and STATION are all done the line collapses
to `● listening · <who is talking>` — the LISTEN step's own state — because
there is nothing left to nudge about; clicking the collapsed line reopens it.
AUTO CLEAN's own switch lives at the left end of this same row, and while a
DIG run is live its STOP button lives at the right end: both apply on every
page for the same reason the PAIR row does, and neither is a step.

**DIG — an offer, never a step.** Two buttons on the START page's OFFERS row
below the five cards: QUICK START sends the three writes RECEIVER is asking
for (`mode=track`, `source=combined`, `auto=on`) in that order and nothing
else, and DIG OUT's `1 MIN`, `3 MIN` and `5 MIN` durations start the gate
moving one knob of the chain at a time against a live objective, keeping
whatever measurably helped and putting back whatever did not. The OFFERS
row's own line reports `no run yet` until one has, then the run's progress
or its verdict; the durations become the STOP button on the NEXT strip while
a run is live. When it lands you answer with `BETTER`, `KEEP` or `WORSE` on
the FILTER page's own DIG controls. A gate that cannot dig shows none of this.

## A working session

This is the order the START page's cards encode, and the order to work in.
Each step makes the next one mean something: a mode set before the tuners
are aligned is solving on two signals that are not the same signal, and a
filter set before you are hearing the combined output is a filter on the
wrong audio.

1. **RECEIVER.** Two loops lined up sample-for-sample, one weight solved on
   whoever is talking, and that combined output in your ears — nothing below
   this reads true until it is. Once when you sit down; again after any gate
   restart, since the gate comes back with the combiner off. If it does not
   settle, or you have just restarted a stream, press REALIGN on the CHAIN
   window's PAIR row and read what it answers.

2. **SITE NOISE.** The shape of your own noise floor named — mains hum and
   harmonics, impulses per second, periodic modulation, tones — each with the
   one control on the SITE page that acts on it. It runs by itself; read it
   when you sit down and whenever the receiver sounds worse.

3. **BAND.** Eighteen transmitters of known power and known bearing on a
   three-minute rota, on the SITE page: which way the band is open, how far
   down the power steps you hear, and A against B. Checked when you sit down
   and again whenever the band changes.

4. **STATION.** Who is on frequency, remembered by voice, and the filter
   that fits their signal, so DIG OUT and AUTO CLEAN have someone to measure
   against instead of silence. Locked in once a voice is remembered; resets
   when the talker changes or you retune off them.

5. **LISTEN.** Not a chore — the destination the four steps above were for.
   BAND says where to be (the direction-coloured waterfall, FINDER's ranked
   conversations); the SLICE page says who you have (talker memory, the live
   weight, two minutes of A/B/OUT).

Then, working a specific station: wait for them to be remembered (one over
of a few seconds) on the SLICE page, double-click the name cell and label
them, select the row and **Lock**. Callers are nulled while the lock holds.
Use *Hear A only* on the PAIR row at any point to hear the difference the
combiner is making.

Beacons (SITE) are a separate errand from the five steps and only pay on the
higher bands — see below.

