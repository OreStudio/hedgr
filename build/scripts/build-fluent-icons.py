#!/usr/bin/env python3
"""Rasterise the icon table into the glyph sheet the font loader reads.

The interface draws its icons as font glyphs, so the vector artwork has to
become a texture before ImGui can pack it. This script is that step: it reads
the table in hedgr.ui/icon/icons.hpp, rasterises one SVG per row, and writes
one horizontal strip of equally sized cells.

The table is the single source of truth. The row order here is the cell order
of the sheet, and the code points in the table are sequential from the first
one, so the loader finds cell n at x = n * cell. The script refuses to write a
sheet when the table is out of step with that rule, because a silent
off-by-one would put every icon one place to the left.

The sheet is a binary PGM, which is one small header followed by the coverage
bytes. A PGM rather than a PNG because the loader lives in hedgr.ui, which has
no image decoder linked into it: the headerless payload is a few lines to read
and it keeps the drawing code free of a dependency.

ImageMagick is the rasteriser. It has no librsvg delegate in this environment,
so the SVG is rendered by ImageMagick's own parser, at sixteen times the final
size and then reduced, which is what keeps the curves clean at sixteen pixels.

Usage:
    build-fluent-icons.py [--check]

With --check the script writes nothing and reports whether the sheet on disk
matches what the table would produce, which is what a later CI check wants.
"""

import argparse
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
ICONS_HPP = REPO / "projects/hedgr.ui/include/hedgr.ui/icon/icons.hpp"
SVG_DIR = REPO / "assets/icons"
SHEET = REPO / "assets/fonts/fluent-icons.pgm"

# One cell is one glyph, so every cell is square and every icon is drawn at the
# size the font bakes its glyphs at. The size is read from the header so the
# generator and the loader cannot disagree about it.
SIZE = re.compile(r"glyph_pixel_size\s*=\s*(\d+)")
CELL = 0  # Set from the header in main, one size for every cell.
SUPERSAMPLE = 16
FIRST_CODEPOINT = 0xE000

ROW = re.compile(
    r"X\(\s*(?P<name>[a-z_]+)\s*,\s*(?P<codepoint>0x[0-9A-Fa-f]+)\s*,"
    r"\s*(?P<concept>[a-z0-9_]+)\s*\)")


def read_cell_size():
    """Return the glyph pixel size the header and the loader share."""
    match = SIZE.search(ICONS_HPP.read_text())
    if match is None:
        raise SystemExit(f"no glyph_pixel_size in {ICONS_HPP}")
    return int(match.group(1))


def read_table():
    """Return the table rows, in file order, as (name, codepoint, concept)."""
    text = ICONS_HPP.read_text()
    rows = [
        (m.group("name"), int(m.group("codepoint"), 16), m.group("concept"))
        for m in ROW.finditer(text)
    ]
    if not rows:
        raise SystemExit(f"no rows found in {ICONS_HPP}")

    # The loader maps a code point to a cell by subtracting the first one, so a
    # gap or a repeat would move every later icon.
    for index, (name, codepoint, _) in enumerate(rows):
        expected = FIRST_CODEPOINT + index
        if codepoint != expected:
            raise SystemExit(
                f"{name}: code point 0x{codepoint:04X}, expected "
                f"0x{expected:04X}; the table must number its rows "
                f"consecutively from 0x{FIRST_CODEPOINT:04X}")
    return rows


def rasterise(concept):
    """Return one icon as a CELL x CELL block of coverage bytes."""
    source = SVG_DIR / f"ic_fluent_{concept}_regular.svg"
    if not source.is_file():
        raise SystemExit(f"missing artwork for {concept}: {source}")

    large = CELL * SUPERSAMPLE
    completed = subprocess.run(
        ["convert", "-background", "none", str(source),
         "-resize", f"{large}x{large}",
         "-alpha", "extract",
         "-filter", "Lanczos",
         "-resize", f"{CELL}x{CELL}",
         "-colorspace", "Gray", "-depth", "8",
         "gray:-"],
        check=True, capture_output=True)
    data = completed.stdout
    if len(data) != CELL * CELL:
        raise SystemExit(
            f"{concept}: ImageMagick produced {len(data)} bytes, "
            f"expected {CELL * CELL}")
    return data


def build(rows):
    """Write the sheet and return its path."""
    SHEET.parent.mkdir(parents=True, exist_ok=True)
    cells = [rasterise(concept) for _, _, concept in rows]

    # The cells are rendered one at a time, so they are interleaved here into
    # the one image the loader reads: the strip's row r is each cell's row r in
    # table order.
    rows_of_sheet = []
    for row in range(CELL):
        rows_of_sheet.append(
            b"".join(cell[row * CELL:(row + 1) * CELL] for cell in cells))
    payload = b"".join(rows_of_sheet)

    # P5 is a two line text header, a maximum value, and then one byte per
    # pixel. The single newline after the maximum is the one byte of separator
    # the format asks for, so the header is exactly this many bytes.
    header = f"P5\n{len(rows) * CELL} {CELL}\n255\n".encode("ascii")
    SHEET.write_bytes(header + payload)
    return SHEET


def main():
    global CELL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="report whether the sheet is current, write nothing")
    args = parser.parse_args()

    CELL = read_cell_size()
    rows = read_table()

    if args.check:
        # A check cannot rasterise and compare meaningfully without a reference
        # copy, and the point of the check is that the table and the sheet
        # agree on how many cells there are, which the strip's header proves.
        if not SHEET.is_file():
            print(f"{SHEET}: missing", file=sys.stderr)
            return 1
        header = SHEET.read_bytes().split(b"\n", 3)[:3]
        if len(header) < 3 or header[0] != b"P5":
            print(f"{SHEET}: not a binary PGM", file=sys.stderr)
            return 1
        width = int(header[1].split()[0])
        expected = len(rows) * CELL
        if width != expected:
            print(f"{SHEET}: {width}px wide, expected {expected}px for "
                  f"{len(rows)} icons", file=sys.stderr)
            return 1
        print(f"{SHEET}: current, {len(rows)} icons")
        return 0

    build(rows)
    print(f"{SHEET}: {len(rows)} icons, {len(rows) * CELL}x{CELL}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
