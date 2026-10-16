#!/usr/bin/env python3
# -*- mode: python; python-indent-offset: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
"""Slice the OreStudio environment sheet into one square icon per checkout.

The seeder names an environment after its checkout label, and the toolbar wants
one picture per environment, so this script turns the provider's contact sheet
into ten files named for those labels.

The sheet is two rows of five illustrated cards with a caption printed under
each one, all on a flat page. Dividing the page into ten equal cells would be
wrong twice over. The cards sit on their own margins rather than on an even
grid, so a card does not begin where a tenth of the width falls, and a naive
cell would drag the caption of one card into the crop of the next. So the
script measures the page colour at the corners, treats every pixel that differs
from it as ink, and groups the ink into runs of rows and of columns. The row
runs that span most of the page are the two card rows, the runs of columns
within a row are the five cards, and a card is the tight box of ink where the
two cross. Each box is
squared off about its centre, so a card that came out a pixel wider than it is
tall keeps all of itself, and scaled to SIZE.

The icons are 256 pixels square. A card on the sheet is about 238 pixels, so
256 is the nearest power of two and keeps the artwork at very nearly the size
it was drawn rather than throwing detail away, and a power of two halves
cleanly for a 128 pixel toolbar tile or a doubled display.

The script is idempotent. Every run overwrites the ten files with the same
bytes, and it prints the box it chose for each card, so a run can be reviewed
rather than taken on trust.

ImageMagick does the decoding, the cropping, and the PNG encoding, and Python
only measures. That is the same split build-fluent-icons.py uses, and it is why
this script has no image library to install.

Usage:
    split-instance-icons.py [source]
"""

import argparse
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO / "assets/images/Gemini_Generated_Image_lwkv2ulwkv2ulwkv.jpeg"
OUT_DIR = REPO / "assets/instances"

# The sheet reads left to right and then top to bottom, and each card is named
# for the label the seeder already uses for that environment.
LABELS = [
    "brave_hopper",
    "bright_faraday",
    "clever_dijkstra",
    "eager_maxwell",
    "festive_dijkstra",
    "jolly_knuth",
    "merry_newton",
    "prime_origin",
    "solid_dirac",
    "swift_curie",
]

SIZE = 256

# How far a channel has to move from the page colour before the pixel counts as
# card. The page is a flat grey and the cards are framed in colour, so a small
# margin is enough and it leaves the soft edge pixels where they are.
TOLERANCE = 24

# A card row covers most of the page and is nearly all ink, while a caption is
# a line of text and is mostly page. Half the width sits well between the two.
CARD_ROW_SHARE = 0.5

# A card that is less than half ink is a crop of the page, which means the
# measurement went wrong rather than that the artwork is sparse.
INK_FLOOR = 0.5


def read_page(path):
    """Return the image as (width, height, RGB bytes), decoded by ImageMagick.

    The one decoder in the chain is ImageMagick's, because it reads the JPEG
    the provider produced and nothing in the tree reads JPEG. The pixmap comes
    back as a P6 so Python can measure it without an image library.
    """
    completed = subprocess.run(
        ["magick", str(path), "-depth", "8", "ppm:-"],
        check=True, capture_output=True)
    data = completed.stdout

    fields = []
    offset = 0
    while len(fields) < 4:
        while data[offset:offset + 1].isspace():
            offset += 1
        if data[offset:offset + 1] == b"#":
            while data[offset:offset + 1] != b"\n":
                offset += 1
            continue
        start = offset
        while not data[offset:offset + 1].isspace():
            offset += 1
        fields.append(data[start:offset])
    offset += 1

    magic, width, height, depth = fields
    if magic != b"P6":
        raise SystemExit(f"{path}: not the RGB pixmap this script reads")
    if int(depth) != 255:
        raise SystemExit(f"{path}: {depth.decode()} is not an 8 bit page")
    return int(width), int(height), data[offset:]


def page_colour(pixels, width, height):
    """Return the page colour, taken from the four corners of the image.

    The cards never reach the corners, so whatever is there is the page, and
    averaging the four is steadier than trusting one against JPEG noise.
    """
    corners = [(0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)]
    samples = []
    for x, y in corners:
        start = (y * width + x) * 3
        samples.append(pixels[start:start + 3])
    return tuple(sum(sample[i] for sample in samples) // len(samples)
                 for i in range(3))


def is_ink(pixels, offset, background):
    """True when the pixel at offset is card rather than page."""
    return max(abs(pixels[offset] - background[0]),
               abs(pixels[offset + 1] - background[1]),
               abs(pixels[offset + 2] - background[2])) > TOLERANCE


def runs(counts, floor):
    """Return the (first, last) index of each run of counts above floor."""
    spans = []
    start = None
    for index, count in enumerate(counts):
        if count > floor and start is None:
            start = index
        elif count <= floor and start is not None:
            spans.append((start, index - 1))
            start = None
    if start is not None:
        spans.append((start, len(counts) - 1))
    return spans


def ink_per_row(pixels, width, height, background):
    """Return how many ink pixels each row of the page holds."""
    counts = []
    for y in range(height):
        base = y * width * 3
        counts.append(sum(1 for x in range(width)
                          if is_ink(pixels, base + x * 3, background)))
    return counts


def ink_per_column(pixels, width, background, top, bottom):
    """Return how many ink pixels each column holds between two rows."""
    counts = []
    for x in range(width):
        counts.append(sum(1 for y in range(top, bottom + 1)
                          if is_ink(pixels, (y * width + x) * 3, background)))
    return counts


def tight_box(pixels, width, background, left, right, top, bottom):
    """Return the tight (left, top, right, bottom) box of ink in a region."""
    box = [right, top, left, bottom]
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            if is_ink(pixels, (y * width + x) * 3, background):
                box[0] = min(box[0], x)
                box[1] = min(box[1], y)
                box[2] = max(box[2], x)
                box[3] = max(box[3], y)
    return tuple(box)


def find_cards(page):
    """Return the box of every card on the sheet, in reading order."""
    width, height, pixels = page
    background = page_colour(pixels, width, height)

    rows = ink_per_row(pixels, width, height, background)
    floor = width * CARD_ROW_SHARE
    bands = [span for span in runs(rows, 0)
             if max(rows[span[0]:span[1] + 1]) > floor]
    if len(bands) != 2:
        raise SystemExit(
            f"expected two rows of cards, found {len(bands)}; the page colour "
            f"or the tolerance no longer separates the cards from the sheet")

    cards = []
    for top, bottom in bands:
        columns = ink_per_column(pixels, width, background, top, bottom)
        for left, right in runs(columns, 0):
            cards.append(tight_box(pixels, width, background, left, right,
                                   top, bottom))
    if len(cards) != len(LABELS):
        raise SystemExit(
            f"expected {len(LABELS)} cards, found {len(cards)}; the sheet is "
            f"not the two by five grid this script slices")
    return cards


def square_crop(box):
    """Return an (x, y, side) square that holds the whole of a card box."""
    left, top, right, bottom = box
    side = max(right - left + 1, bottom - top + 1)
    x = round((left + right + 1) / 2 - side / 2)
    y = round((top + bottom + 1) / 2 - side / 2)
    return x, y, side


def write_icon(source, box, path):
    """Crop the card out of the sheet, scale it, and write the PNG."""
    x, y, side = square_crop(box)
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["magick", str(source),
         "-crop", f"{side}x{side}+{x}+{y}",
         "+repage",
         "-filter", "Lanczos",
         "-resize", f"{SIZE}x{SIZE}",
         # Stripping keeps the bytes identical between runs. Without it the
         # PNG carries the time it was written and every run differs.
         "-strip",
         str(path)],
        check=True)
    return x, y, side


def coverage(path):
    """Return the written icon's size and the fraction of it that is card."""
    width, height, pixels = read_page(path)
    background = page_colour(pixels, width, height)
    ink = sum(1 for offset in range(0, len(pixels), 3)
              if is_ink(pixels, offset, background))
    return width, height, ink / (width * height)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source", nargs="?", type=pathlib.Path, default=SOURCE,
        help=f"the contact sheet, {SOURCE.relative_to(REPO)} by default")
    args = parser.parse_args()

    if not args.source.is_file():
        raise SystemExit(f"no such sheet: {args.source}")

    page = read_page(args.source)
    cards = find_cards(page)

    for label, box in zip(LABELS, cards):
        path = OUT_DIR / f"{label}.png"
        x, y, side = write_icon(args.source, box, path)
        width, height, ink = coverage(path)
        if (width, height) != (SIZE, SIZE):
            raise SystemExit(f"{path}: {width}x{height}, expected {SIZE}x{SIZE}")
        if ink < INK_FLOOR:
            raise SystemExit(
                f"{path}: {ink:.1%} of the icon is card, which is a crop of "
                f"the page rather than of a card")
        print(f"{label}: sheet box {box} crop {side}x{side}+{x}+{y} "
              f"icon {width}x{height} card {ink:.1%}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
