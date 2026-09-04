# Session

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

**The FLOW line — at the foot of the window, not the top.** Five steps in
the order that gets the best signal, plus a sixth that is not a step at
all, on one line above the gate status strip:

```
FLOW  ✓ align lag −63 · ✓ mode track · ● hear · A only → hear OUT · ○ noise · ○ filter
```

A tick is a step behind you, a filled dot is the one to do next, a hollow
circle is one still ahead. **Only the next step is clickable**, and clicking
it goes to the page that step lives on and, where there is a single obvious
action, also takes it — align realigns, mode sets track when the combiner is
off, hear switches to the combined output. The noise and filter steps only
change the page, because there is a choice to make on both and the line
should not make it for you. Every state on the line is a number or a word
the gate said; nothing there is computed by the window.

It was a strip of five buttons under the page tabs until an operator met it,
and it read as a second row of tabs: five lit boxes under four lit boxes is
navigation whatever the words on them say. At the bottom of the window,
written as a checklist rather than as controls, nothing in the layout can be
mistaken for the tab bar.

**The line follows the tab you are on.** The steps that belong to the page in
front of you are drawn in full and the rest go dim — align, mode and hear on
SLICE; noise on SITE; filter on FILTER. The step still ahead on the page you
are standing on also quotes its state, which is the reason you went there:
on FILTER the last step reads `○ filter 210–2840 soft · AUTO` rather than
just its name. BAND owns no step, so there everything is dim except the one
to do next. The next step is never hidden by any of this: when it lives on
another page its link says which one (`● noise · 2 findings → SITE`), so the
single thing to do next is readable, and reachable, from all four pages.
See *A working session* below for what each step is for.

**DIG — the sixth thing on the line, and the only one that is an offer.**
The five steps are an order of operations; this is not in it. Press `1 MIN`,
`3 MIN` or `5 MIN` and the gate spends that long moving one knob of the
chain at a time against a live objective, keeps whatever measurably helped,
and puts back whatever did not. While it runs the line reads
`digging 1:12 of 3:00 · +2.1 dB so far · trying width` and the three
durations become one `STOP`, which ends the run and puts your chain back
exactly as you had it. When it lands you get one sentence naming only what
it *moved* — `+4.1 dB: post v2, width 100-2400, nb 11 dB`, or
`nothing beat your settings` — and three words to answer it with: `BETTER`
keeps the changes and tells the gate they worked, `KEEP` keeps them without
judging, and `WORSE` puts the chain back on your own settings and tells the
gate the measurement was wrong. Your ears win that argument and the gate
learns from it. The word you gave stays on the line until the next run. A
gate that cannot dig has no sixth step and no buttons — nothing greyed out
to wonder about. DIG is never the *next* step and never a tick: it is a
button that is either worth pressing tonight or not.

## A working session

This is the order the FLOW line encodes, and the order to work in. Each step
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

