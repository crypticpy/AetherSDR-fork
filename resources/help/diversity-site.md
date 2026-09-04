# Site

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

