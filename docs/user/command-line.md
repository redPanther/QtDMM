# Command line

```
qtdmm [options]
```

| Option | Meaning |
|---|---|
| `--config-id <id>` | Use the named configuration instead of `default`. Each id has its own settings file, so one meter can be set up per id. |
| `--config-dir <dir>` | Directory for the configuration files (default: the platform's user config location). |
| `--debug` | Print every frame received from the meter as hex to the console. Useful when a meter is not decoded correctly — include this output in a bug report. |
| `-h`, `--help` | Show the options. |
| `-v`, `--version` | Show the version. |

## Several meters at once

Every QtDMM window is one *instance*, identified by its `--config-id`. A
device of [My devices](my-devices.md) opens in an instance of its own with
*Open in a new window* in its context menu in the sidebar, and the
assistant *Add device* offers *In a new window* as well. The new instance
starts with a copy of the current one's settings (graph, display, alarms,
…) and the device; its window does not take over the position, and an SCPI
server stays off until you enable it with a free port.

The node **Instances** of the sidebar (F9) lists the configured and the
running instances with what each one currently reads and, below it, its
device; this window's own is bold. A click brings a running instance to
the front; a double click (or **Start** in its context menu) starts a
stopped one. A stopped instance can be renamed (F2 or
its context menu) and deleted. Renaming also changes the formulas that use
the old name, in the other instances and in My devices (see
[Calculated values](calculated-values.md)); an instance that is running
uses the new formula after a restart. Running
instances know about each other through shared memory — the same mechanism
that stops two windows from using the `default` id at once. An instance can
also compute its value from the others' readings, see
[Calculated values](calculated-values.md).

## Debug output

`--debug` writes each received frame as a line of hex bytes, e.g.

```
30 30 30 30 30 31 3B 30 30 30 3A 30 0D 0A
```

These lines are exactly what the decoder test fixtures are made of, so a short
capture together with what the meter displayed at the time is the most useful
thing to attach when reporting a decoding problem.
