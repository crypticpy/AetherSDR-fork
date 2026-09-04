# Band

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

