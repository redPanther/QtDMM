#!/usr/bin/env python3
"""MCP server (stdio) that reads a running QtDMM through its SCPI server.

A development tool: lets an assistant session read values, status and a
screenshot of a QtDMM test instance. Read-only - it sends no command that
starts or stops anything. Not part of QtDMM and not installed with it.

    pip install mcp
    python3 tools/qtdmm-mcp.py --port 5099

See docs/dev/qtdmm-mcp.md.
"""

import argparse
import socket
import time
from datetime import datetime

from mcp.server.fastmcp import FastMCP, Image

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("--host", default="localhost", help="host of the QtDMM instance (default: localhost)")
parser.add_argument("--port", type=int, default=5099, help="its SCPI port (default: 5099)")
parser.add_argument("--timeout", type=float, default=3.0, help="seconds to wait for an answer (default: 3)")
args = parser.parse_args()

mcp = FastMCP("qtdmm")


class Scpi:
    """One connection for a few queries; every error becomes a short RuntimeError."""

    def __enter__(self):
        try:
            self.sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
        except OSError as e:
            raise RuntimeError(f"No QtDMM SCPI server at {args.host}:{args.port} ({e.strerror or e}). "
                               "Is QtDMM running with the SCPI server on?") from None
        self.buf = b""
        return self

    def __exit__(self, *exc):
        self.sock.close()

    def _read(self, n):
        while len(self.buf) < n:
            self._more()
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def _more(self):
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            raise RuntimeError(f"No answer from QtDMM within {args.timeout} s") from None
        if not chunk:
            raise RuntimeError("QtDMM closed the connection")
        self.buf += chunk

    def query(self, cmd):
        self.sock.sendall(cmd.encode() + b"\n")
        while b"\n" not in self.buf:
            self._more()
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def block(self, cmd):
        """An IEEE 488.2 definite length block: #<n><length><data>."""
        self.sock.sendall(cmd.encode() + b"\n")
        if self._read(1) != b"#":
            raise RuntimeError(f"{cmd} did not answer with a block")
        digits = int(self._read(1))
        data = self._read(int(self._read(digits)))
        self.buf = self.buf.lstrip(b"\r\n")
        return data


def value(text):
    """+1.2E+00 as a number; overload and no reading as words."""
    v = float(text)
    if v == 9.9e37:
        return "overload"
    if v == 9.91e37:
        return "no reading"
    return v


@mcp.tool()
def identify() -> str:
    """*IDN? of the QtDMM instance: QtDMM,<meter>,0,<version>."""
    with Scpi() as s:
        return s.query("*IDN?")


@mcp.tool()
def reading() -> dict:
    """The current main reading in SI base units, with unit and function (CONFigure?)."""
    with Scpi() as s:
        return {"value": value(s.query("READ?")), "unit": s.query("UNIT?").strip('"'),
                "function": s.query("CONFigure?").strip('"')}


@mcp.tool()
def reading2() -> dict:
    """The meter's second value (Victron, Fluke 45), with unit and function."""
    with Scpi() as s:
        return {"value": value(s.query("READ2?")), "unit": s.query("UNIT2?").strip('"'),
                "function": s.query("CONFigure2?").strip('"')}


@mcp.tool()
def status() -> dict:
    """STATus:QUEStionable? (overload, hold, not connected, no reading), STATus:OPERation? and *STB?."""
    with Scpi() as s:
        q = int(s.query("STATus:QUEStionable?"))
        op = int(s.query("STATus:OPERation?"))
        stb = int(s.query("*STB?"))
    return {"questionable": q, "overload": bool(q & 1), "hold": bool(q & 2),
            "not_connected": bool(q & 4), "no_reading": bool(q & 8),
            "operation": op, "recording": bool(op & 16), "stb": stb}


@mcp.tool()
def errors() -> list[str]:
    """Empties the error queue (SYSTem:ERRor? until 0,"No error"). The queue is shared by all clients."""
    out = []
    with Scpi() as s:
        for _ in range(50):
            e = s.query("SYSTem:ERRor?")
            if e.startswith("0,"):
                break
            out.append(e)
    return out


@mcp.tool()
def screenshot() -> Image:
    """A picture of the QtDMM window (HCOPy:SDUMp:DATA?), as PNG."""
    with Scpi() as s:
        return Image(data=s.block("HCOPy:SDUMp:DATA?"), format="png")


@mcp.tool()
def watch(count: int = 10, interval: float = 1.0) -> list[dict]:
    """Collects count readings, one every interval seconds (at most 300 readings and 120 s)."""
    count = max(1, min(count, 300))
    interval = max(0.1, min(interval, 120.0 / count))
    out = []
    with Scpi() as s:
        unit = s.query("UNIT?").strip('"')
        for i in range(count):
            if i:
                time.sleep(interval)
            out.append({"time": datetime.now().isoformat(timespec="milliseconds"),
                        "value": value(s.query("READ?")), "unit": unit})
    return out


if __name__ == "__main__":
    mcp.run()
