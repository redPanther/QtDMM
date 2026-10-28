# My devices

If you own several meters and use them one at a time - a UT61E on the
bench, a Brymen for the car, a Victron shunt in the van - **My devices**
keeps each of them with its connection, so switching is one click in the
sidebar instead of filling in the meter page again.

An entry holds the meter and its connection only, and of the connection
what its way needs: model and protocol, the port with its line settings, a
Bluetooth address (and for a Victron device its key and the values to
show), sigrok options, the signal of the simulated meter. Display, graph,
recorder and alarms stay with the window. An older `devices.conf` with more
in it is tidied up when QtDMM starts.

The window remembers which entry it uses. **Info and settings...** of an
entry in the sidebar, or a double click on it, shows and changes the entry
(Shift+F8: the settings of the one in use); the window that uses it takes
the change at once.

## The sidebar

**Devices** in the toolbar shows and hides the sidebar at the left of the
window, with the node **My devices**. Each entry has a small picture of its
kind of instrument (handheld, clamp, bench meter, ...) in its maker's
colours. The device in use is bold, with a dot
that blinks green while its readings come in and turns grey when none has
come for three seconds; where a device is connected shows
when the mouse rests on it. Click another one - QtDMM waits the double-click
time first, a double click opens its info instead -: QtDMM disconnects, takes over
its settings and connects again. A running recording stops and the graph
starts empty - another meter is another measurement; readings not exported
yet are offered for export first, and **Cancel** stays with the current
device. Minimum and maximum start afresh.

With a single device the sidebar stays closed; adding the second one opens
it, and the window remembers whether you leave it open. A window without a
meter shows it whenever there are devices to choose from - pick one, or add
a new one with the big **Add device** button.

Below My devices the node **Open devices** lists the meters this QtDMM
reads with their current reading: this window's own, then those opened
beside it (**Open beside**, see below). The active device is in bold, one
not connected is grey. A double click or *Show front panel* in the context
menu brings its front panel back; *Connect*/*Disconnect* there connects or
disconnects it, *Close* closes one opened beside.

### Several meters: front panels and the active device

Each open meter has its own front panel: its LCD with its keys, as a
window next to the analog meter. The X of a front panel only hides it, the
meter stays open and keeps recording. The meter of the front panel clicked
last is the **active device**: *Connect*, *Reset* (min/max) and the device
settings (Shift+F8) act on it. The status bar shows its name while more
than one meter is open. All front panels share the LCD colours of the
window's display. Each device has its own [alarms](alarms.md) and
[PASS/FAIL test](limit-test.md); its front panel takes their colours, and
*PASS/FAIL test*, *Limits...* and *Alarms...* are in the context menu of
its front panel and of its row. The window layout remembers the meters open beside and
opens them again at the next start, after the window's own; a workspace
file does the same when it is loaded. **Services** shows what this window offers to other
programs: the [SCPI server and HiSLIP](scpi-server.md) with their port, or
*off*.

Above My devices, folded at the start, the node **Workspaces** lists the
window layouts loaded or saved last; a double click loads one (see
[Window layout](connecting.md#window-layout)). Without any it is hidden.

### The meter's values

For a meter with more than one value (a second display, the CO2,
temperature and humidity of the TFA AirCO2ntrol, the voltage, current and
state of charge of a Victron) each open device has a row for each
value below it in **Open devices**: its number, its name and its reading.
A device with one value has the same menu in its own row, under **Value**. The name is
the one the meter gives the value (a Victron's *Battery voltage*), else
unit and coupling (*V DC*); the tooltip has the full one. A
coloured dot shows that the graph draws it, in the colour of its curve, so
the rows are the graph's legend as well. This is where you say which window
shows which value - right-click a value:

- **LCD** ▸ **Main value**, **Secondary value left**, **Secondary value
  right** - the place on the device's front panel: big (the min/max memory
  follows it) or on the small line below it. The value that had the place goes where
  this one was, or off the display when this one was not on it.
- **Analog meter** - the value the needle shows (one).
- **Graph** ▸ **Off**, **Left axis**, **Right axis** - the value's curve
  off or on that axis. A value in the unit the axis has already joins it
  (two voltages on the left, a current on the right); one in another unit
  takes the axis over, and the curves of the old unit there go off - the
  status line says which. The main value is always on the left. A value of
  a device opened beside joins an axis in its unit; one with no room there
  is not drawn, and the status line says so.
- **Poincaré plot** - the value the plot takes (one).
- **Readings table** - whether the [readings table](readings-table.md)
  lists it (all values of this window's device at first; another device
  comes in with the value chosen).

A choice opens the window it is for, also when the toolbar is hidden. The
choices are kept with the window layout.

With more than one device open, the context menus of the graph, the
readings table, the analog meter and the Poincaré plot have **Sources...**:
all open devices with their values, to tick those the view shows - several
for the graph and the table, one for the analog meter and the Poincaré
plot. A newly opened device changes nothing by itself.

The context menu of a device:

- **Info and settings...** (also a double click) - two or three tabs:
  - **Info**: the device at a glance as on the assistant's last page
    (picture, name, model, type, protocol, connection); whether it is in use
    in this window, open beside it or not at all; in this window
    whether it is connected, how long ago the last reading came, a low
    battery, and its values with their readings, updated every second. Below
    a link to the maker's website (in the browser) and to the list of
    supported devices in this help.
  - **Data** (only for some meters - UNI-T UT803, UT804, UT61B+/D+/E+,
    UT161B/D/E, ZOTEK ZT-5B / ANENG V05B, HoldPeak HP-570C-APP, CMT 3503): the specifications from the
    meter's manual, per function the ranges with resolution and accuracy
    (per frequency band for AC), the input impedance, the overload
    protection, the manual's notes and its page. They were transcribed from
    the manuals by the [dmm-tools](https://github.com/antoinecellerier/dmm-tools)
    project (the HP-570C-APP's from its manual filed with the FCC, the CMT
    3503's from its data sheet, one accuracy per function) - check the
    manual before relying on a number.
  - **Settings**: the meter's settings as in the assistant; with **OK** the
    device in use takes them at once.
- **Rename** - in place.
- **Open beside** - opens the device in this QtDMM beside the window's
  device, with a front panel of its own: it shows its reading there and in
  **Open devices**, records whenever the window records, is a variable in
  [calculated values](calculated-values.md) and answers over SCPI
  (`INSTrument`). A device whose port the window uses is not opened twice.
- **Remove from My devices** - after a question; a window using it keeps its
  meter.

Drag a device to change the order.

## Adding a device

**Add device...** (the device symbol with a + at the left of the toolbar,
or **+ Add device...** at the end of My devices in the sidebar) asks step
by step: how the meter is connected - *Cable*, *Bluetooth*, *Network*,
*sigrok* or *Simulated / calculated* -, which meter it is, with a name, and
where it goes: **In this window** as the current device, or **Beside it**,
opened beside the current device as with *Open beside*. Either way it is
kept in My devices. The page of the meter offers only the models of that
connection; *Next* waits until what the meter needs is filled in (the port,
the key of a Victron device, a formula that works).

A serial port is saved under its stable name on Linux, the link in
`/dev/serial/by-id/` (`usb-WCH.CN_USB_Quad_Serial_…`): `/dev/ttyUSB0` may be
`/dev/ttyUSB1` after plugging the cable in again, the by-id name stays.

## Where the list is kept

The list is kept in `devices.conf` next to the settings file, the same for
every `--config-id`. An entry keeps the device's rules too: its alarms and
its PASS/FAIL test. Alarms set up for the window before 26.4 move into the
entry of the window's device at the first start. A port is read by one meter at a time: a device whose
port is in use in this window or beside it is not opened again, and
another program (or another QtDMM) using it says the port is busy.

A Victron key is kept in `devices.conf` in plain text, as in the settings
of the window: whoever gets the file gets the key.
