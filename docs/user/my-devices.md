# My devices

If you own several meters and use them one at a time - a UT61E on the
bench, a Brymen for the car, a Victron shunt in the van - **My devices**
keeps each of them with its connection, so switching is two clicks
instead of filling in the meter page again.

An entry holds the meter and its connection only, and of the connection
what its way needs: model and protocol, the port with its line settings, a
Bluetooth address (and for a Victron device its key and the values to
show), sigrok options, the signal of the virtual meter. Display, graph,
recorder and alarms stay with the window. An older `devices.conf` with more
in it is tidied up when QtDMM starts.

The window remembers which entry it uses. Change the port or the key of
that meter on the Multimeter page, and the entry changes with it; choose
another model there, and it is another meter - the window uses no entry
then, until you save one or switch.

## Switching

The **My devices** button at the left of the toolbar opens the list; the
device in use has a check mark. Click another one: QtDMM disconnects, takes
over its settings and connects again. A running recording stops - another
meter is another measurement - and minimum and maximum start afresh.

## Adding a device

- Set the meter up as usual on the **Multimeter** page, then choose **Save
  current device...** in the My devices menu and give it a name, or
- on the Multimeter page, **Save to my devices...**. With a device chosen
  at the top of the page, it asks whether to update that one or to keep
  the settings as a new device.

A serial port is saved under its stable name on Linux, the link in
`/dev/serial/by-id/` (`usb-WCH.CN_USB_Quad_Serial_…`): `/dev/ttyUSB0` may be
`/dev/ttyUSB1` after plugging the cable in again, the by-id name stays.

## The Multimeter page

**My devices** at the top of the page fills in the fields from an entry;
nothing changes before *OK* or *Apply*.

## Managing

**Manage my devices...** lists them: rename, duplicate, delete, drag to
reorder, *Use* to switch, *Edit...* to switch and open the Multimeter page.

## Several windows

The list is the same for all instances: it is kept in `devices.conf` next
to their settings files. A new instance (**Instances**) can start with one
of the devices right away. Two windows cannot use the same serial port at
the same time; the second one says the port is busy.

A Victron key is kept in `devices.conf` in plain text, as in the settings
of the window: whoever gets the file gets the key.
