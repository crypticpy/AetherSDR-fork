# Filter

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

