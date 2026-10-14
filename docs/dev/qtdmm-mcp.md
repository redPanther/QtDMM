# MCP server for test sessions

`tools/qtdmm-mcp.py` is a small [MCP](https://modelcontextprotocol.io/)
server for AI assistants (Claude Code and others) that test QtDMM: it reads
a running instance through its [SCPI server](../user/scpi-server.md) and
offers the values, the status and a screenshot of the window as tools. It
is a development tool, not part of QtDMM, and is not installed with it.

It only reads: no tool starts or stops the recorder, connects the meter or
resets anything. An assistant can watch a test instance with it, but cannot
change what the instance does - also not when it is pointed at the wrong
port.

## Setup

It needs Python 3.10 or newer and the MCP SDK:

```
pip install mcp
```

Start the QtDMM instance to test with the SCPI server on (Settings → SCPI
server). The tool uses port 5099 unless told otherwise, so that it does not
reach a QtDMM in everyday use on the default port 5025 by accident. For
Claude Code, a `.mcp.json` in the working directory (not committed):

```json
{
  "mcpServers": {
    "qtdmm": {
      "command": "python3",
      "args": ["tools/qtdmm-mcp.py", "--port", "5099"]
    }
  }
}
```

Options: `--host` (default `localhost`), `--port` (default 5099),
`--timeout` in seconds (default 3).

## Tools

| Tool | SCPI | Answer |
|---|---|---|
| `identify` | `*IDN?` | `QtDMM,<meter>,0,<version>` |
| `reading` | `READ?`, `UNIT?`, `CONFigure?` | value in base units, unit, function; `overload` or `no reading` instead of the value |
| `reading2` | `READ2?`, `UNIT2?`, `CONFigure2?` | the same for the second value; a meter without one answers `no reading` and leaves `-230` in the error queue |
| `status` | `STATus:QUEStionable?`, `STATus:OPERation?`, `*STB?` | the registers and their bits: overload, hold, not connected, no reading, recording |
| `errors` | `SYSTem:ERRor?` | the error queue, emptied; it is shared by all clients of the instance |
| `screenshot` | `HCOPy:SDUMp:DATA?` | the window as PNG image |
| `watch` | `READ?` | `count` readings with time stamps, one every `interval` seconds (at most 300 and 120 s) |

When QtDMM is not running, its SCPI server is off or an answer takes longer
than the timeout, a tool returns a short error message saying so.
