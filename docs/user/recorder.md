# The recorder

The recorder graph — shown with the **Graph** toolbar button, hidden by
default and brought up automatically when a recording starts — records
every reading of the meter's primary value, at the time it came: a short
spike between two others is kept, and a slow meter is drawn from reading to
reading. What it records is the value in base units — a reading of 12.3 mV
is stored as 0.0123 V — so the curve stays continuous when the meter
changes range.

## Sampling

**Settings → Recording** (Ctrl+F2) sets:

- **Sample every** — the grid of the [export](#export-and-import), in
  tenths of a second, seconds, minutes, hours or days: one row per
  interval, the average of the readings in it, each weighted with how long
  it was shown. A reading counts until the next one, at most until it
  [goes stale](#gaps). The graph always shows every reading.
- **Sample time** — how long to record; the recorder stops by itself when it
  is reached. Leave it at zero to record until stopped.

**Settings → Scales** sets the visible window and the vertical scale
(automatic, or a fixed minimum and maximum).

The status bar shows how much is recorded and how much the graph keeps at
most (**Max. length**, Settings → Scales), the time left until **Sample
time** stops the recording, and whether it records: `0:42 / 10:00 - 1:59:17
left - Sampling`.

The vertical axis is in the unit the meter shows - mV while it shows mV -
and in the unit that suits the values for a loaded recording.

## Starting and stopping

Three start modes, chosen on the Recording page:

- **Manual** — *Start* (Ctrl+S) and *Stop* (Ctrl+X) in the toolbar, or the
  graph's right-click menu.
- **Predefined time** — recording begins at the given time of day.
- **Trigger** — recording begins when the reading crosses a threshold, on its
  raising or falling edge. The threshold is drawn into the graph as a line you
  can drag. With **Pre trigger** the recording reaches back by the time set
  there: QtDMM keeps the readings while it waits, so the recording shows how
  the value got to the threshold, and a green mark shows where it crossed.
  **Sample time** counts from the crossing.

*Clear* (Ctrl+Del) empties the recording. QtDMM warns before you lose unsaved data
by clearing, importing or quitting; the warning can be switched off under
**Settings → Appearance**.

[Alarms](alarms.md) can start and stop the recorder as well, on any of
their conditions.

A recording measures one thing. When you switch the meter to another
function while it runs - V DC to Ω, DC to AC, °C to °F - the recording
stops, the status bar says what changed, and an orange mark in the graph
shows where. *Start* begins a new recording in the new function. The
stopped one keeps its unit, on the axis and in the export. A change of the
range or prefix (mV to V) is no new function; the recording goes on.

## Gaps

Where the meter showed no value, the curve has a gap instead of a line:

- **OL** - the reading was out of range (overload).
- **No value** - the meter stopped sending, or the connection dropped.
  QtDMM learns how often the meter sends and takes a value as gone after
  three of its intervals (at least 1 s, at most 30 s). The digital display
  and the analog meter fade their value at the same time.

Hovering over a gap shows "OL" or "no value". In the export, an interval
that holds some values and some gaps is the average of the values there
were; the integration curve carries on across a gap.

## Looking at the data

- The buttons at the top right of the graph set the time window: **All**
  shows the whole recording so far and grows with it, **1 min**, **5 min**
  and **30 min** show the last minutes. Buttons longer than the graph's
  **Max. length** (**Settings → Scales**) are left out. Zooming ends **All**; the window set under
  **Settings → Scales** stays the start.
- **Mouse wheel** zooms the time axis; the **middle button** drags it. On the
  keyboard: Ctrl++ / Ctrl+- zoom, Ctrl+0 shows the whole recording, after a
  click into the graph also `+`, `-`, `0`, the arrow keys, Home and End (see
  [Keyboard and mouse](keyboard.md)).
- **Copy image** (right-click menu or Ctrl+Shift+C) puts a picture of the graph
  on the clipboard.
- **Export image...** (right-click menu) writes the graph to a file: **SVG** or
  **PDF** keep the curve, the axes and their labels as vectors, so they stay
  sharp at any size and can be edited in Inkscape or dropped into a document;
  **PNG** and **JPEG** are pixels. The picture is what the graph shows, so zoom
  and pan first.
- Hovering shows a crosshair with the time (to the millisecond) and the
  value of the reading at the cursor.
- **Integration** (Settings → Integration curve) draws a second curve: the
  integral over time of the readings above a threshold, in the unit times
  seconds (V·s, A·s, W·s), scaled and offset as configured — for charge or
  energy over time. Each reading counts for the time it was shown. For
  ampere-hours or watt-hours set the scale to 1/3600, `0.000277778`. A
  scale set before version 26.2, when the curve was the sum of the samples,
  is converted once, so the curve looks as before.
- **Print** (Ctrl+P) prints the graph with a title and comment.

## Colours

**Settings → Graph → Graph colours** sets the colours of the graph:

- **Neutral** follows the window's design.
- **Scope blue** (the default), **Phosphor green**, **Phosphor amber** and **Chart
  recorder** look like an oscilloscope or a paper recorder. They divide the
  graph into 10 × 8 squares of 1, 2 or 5 units each (1 V, 2 V, 5 V, 10 V,
  ...), so the visible time and the vertical scale grow to whole squares.
  The phosphor colours add a fine scale on the centre lines and dash the
  integration curve.
- **Custom** uses the colour buttons on the same page.

A curve colour you chose yourself stays in every variant. The graph's
right-click menu, **Graph colours**, can choose other colours for this graph
only; *Default* goes back to the setting.

## Export and import

*Export* (Ctrl+E) writes the recording on the grid of **Sample every** as
CSV, or - pick the file type in the dialog or just name the file `.xlsx` /
`.ods` - as an Excel or OpenDocument spreadsheet with the same four columns.
The file type **CSV, every reading** writes each reading at its own time
instead, a gap as a row of its own; *Import* reads its times back. In the spreadsheet
the timestamps are date cells (shown with milliseconds) and the values
numbers, so charts and formulas work without converting anything; the
header row is bold and stays put when you scroll. CSV is what *Import*
reads back (spreadsheets are not imported; save as CSV from there).

The CSV is semicolon-separated:

```
timestamp;time (s);value;unit
2026-09-19T10:00:00,000;0;12.3;mV
2026-09-19T10:00:01,000;1;12.4;mV
```

The value is written with an SI prefix and the matching unit, exactly as a
meter would show it. A [gap](#gaps) is written as `nan` in CSV and as an
empty cell in a spreadsheet; *Import* reads `nan` back as a gap. *Import* (Ctrl+I) reads such files back, scaling the
values into base units again, and also accepts the tab-separated format of
QtDMM versions before 0.9.5. The unit of the first row becomes the graph's
unit.
