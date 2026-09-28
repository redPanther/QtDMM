#!/usr/bin/env python3
"""Builds QtDMM's icon themes in assets/icons/theme/ from KDE's Breeze icons.

QtDMM ships the Breeze symbols it uses, light and dark, as two icon themes
compiled into the resources (qtdmm-breeze, qtdmm-breeze-dark). This script
copies them from an installed Breeze theme (/usr/share/icons/breeze and
breeze-dark, package breeze-icon-theme or breeze-icons) and adds QtDMM's own
symbols from assets/icons/own/, whose dark variant only swaps the text
colour. Run it after adding a name to BREEZE or a file to own/, and commit
the result; the build does not need Breeze installed.

Breeze is LGPL-3.0-or-later (assets/icons/theme/LICENSE.breeze).
"""
import pathlib
import shutil
import sys

# the Breeze names QtDMM uses (QIcon::fromTheme / <iconset theme=...>)
BREEZE = [
    "application-exit", "application-menu", "configure", "document-open",
    "document-print", "document-save", "edit-delete", "edit-reset", "go-home",
    "go-next", "go-previous", "media-playback-pause", "media-playback-start",
    "help-about", "help-contents", "help-whatsthis", "list-add", "list-remove",
    "measure", "media-playback-stop", "media-record", "network-connect",
    "network-disconnect", "network-server", "network-wired", "notifications",
    "office-chart-line", "preferences-desktop-theme-global", "system-run", "table",
]
LIGHT_TEXT, DARK_TEXT = "#232629", "#eff0f1"
THEMES = {"qtdmm-breeze": ("breeze", None), "qtdmm-breeze-dark": ("breeze-dark", DARK_TEXT)}

INDEX = """[Icon Theme]
Name={name}
Comment=Breeze symbols used by QtDMM (LGPL-3.0-or-later, KDE)
Directories=22

[22]
Size=22
Type=Scalable
MinSize=8
MaxSize=256
"""


def find(src_theme: pathlib.Path, name: str) -> pathlib.Path:
    for context in ("actions", "devices", "places", "preferences", "status", "apps"):
        p = src_theme / context / "22" / (name + ".svg")
        if p.exists():
            return p.resolve()
    sys.exit(f"{name}.svg not found in {src_theme}")


def main() -> None:
    root = pathlib.Path(__file__).resolve().parent.parent
    icons = root / "assets" / "icons"
    system = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/share/icons")
    for theme, (breeze, text) in THEMES.items():
        out = icons / "theme" / theme
        if out.exists():
            shutil.rmtree(out)
        (out / "22").mkdir(parents=True)
        (out / "index.theme").write_text(INDEX.format(name=theme))
        for name in BREEZE:
            shutil.copyfile(find(system / breeze, name), out / "22" / (name + ".svg"))
        for own in sorted((icons / "own").glob("*.svg")):
            svg = own.read_text()
            if text:
                svg = svg.replace(LIGHT_TEXT, text)
            (out / "22" / own.name).write_text(svg)
    print(f"{len(BREEZE)} Breeze + {len(list((icons / 'own').glob('*.svg')))} own icons per theme")


if __name__ == "__main__":
    main()
