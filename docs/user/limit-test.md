# PASS/FAIL limit test

The limit test judges every reading of the meter's main value against
limits: **PASS** inside, **FAIL** outside, with **HI** or **LO** for the
side. For checking parts on the bench - resistors, batteries, a supply
voltage - without setting up alarms.

## Setting it up

Right-click the digital display:

- **PASS/FAIL test** switches the test on and off. The limits stay when it
  is off. Switched on without limits, it asks for them.
- **Limits...** opens the dialog:
  - **Form**: *Nominal ± percent* (5 V ± 2 %), *Nominal ± amount*
    (2 V ± 0.05 V) or *Lower and upper limit* (4.9 V to 5.1 V).
  - **= reading** takes the value shown now as the nominal - measure a
    good part, take its value, set the tolerance.
  - The line **PASS** shows the limits as the test uses them.
  - **Display in green or red** (on by default): the display and the
    analog meter take the verdict's colour. A raised
    [alarm](alarms.md)'s colour comes first.
  - **Sound on HI**, **Sound on LO** and **Sound on PASS**: an alarm
    sound when the verdict turns FAIL HI, FAIL LO or PASS; with
    **repeat** again every second while the verdict stays - a steady
    tick while the trimmer is right, say. A new verdict ends it.

The limits are kept with the meter's settings. The limits themselves
pass: 4.9 V passes 5 V ± 2 %.

## On the display

While the test is on, the display grows wider by a column right of the
value: **PASS** on green or **FAIL** on red, **HI** or **LO** below it,
and the deviation from the nominal in percent - from the middle of the
range for *Lower and upper limit*. Without a reading (not connected, no
number shown) the column is faint, like the unlit annunciators.

## In the graph, on the analog meter and in the status line

The [graph](recorder.md) draws the limits as two dashed green lines, faintly
green between them, while the tested value is on its left axis. The
reference line snaps to them. The [analog meter](analog-meter.md) shows
the passing range as a green band on its arc while its needle shows the
tested value; a limit beyond the scale ends the band at the scale's end.

The status line counts the verdicts, one per reading: `PASS 812 · FAIL
23 (2.8 %)`, the tooltip splits FAIL into too high and too low. The
count starts anew when the limits change and with **Reset min/max**.

The verdict is recorded with each reading: the [readings
table](readings-table.md) gets a column *Limit*, and the
[recorder](recorder.md)'s CSV export a column `limit`.

An overload counts as FAIL HI, a negative one (-OL) as FAIL LO. There is
no minimum duration and no hysteresis: each reading has its verdict. For
"too long too high", use an [alarm](alarms.md).
