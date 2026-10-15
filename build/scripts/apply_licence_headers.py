#!/usr/bin/env python3
"""Prepend the project licence header to source files that lack one.

Every .hpp and .cpp in a component carries the GPL header and the Emacs mode
line. The scaffolder writes them into generated files; hand-written files drift.
This script is the fix, and it is idempotent, so it can run in a pre-commit hook
or a CI check later.

Usage:
    apply_licence_headers.py [--check] [path ...]
"""

import argparse
import datetime
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
MARKER = "Copyright (C)"
YEAR = datetime.date.today().year

CPP_HEADER = f"""/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) {YEAR} Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
"""

SKIP_DIRS = {"third_party", "external", "build", ".git", ".cache"}


def candidates(paths: list[pathlib.Path]) -> list[pathlib.Path]:
    found: list[pathlib.Path] = []
    for root in paths:
        if root.is_file() and root.suffix in {".hpp", ".cpp"}:
            found.append(root)
            continue
        for path in root.rglob("*"):
            if path.suffix not in {".hpp", ".cpp"}:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            found.append(path)
    return sorted(set(found))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=pathlib.Path,
                        default=[REPO / "projects"])
    parser.add_argument("--check", action="store_true",
                        help="report files missing a header without writing")
    args = parser.parse_args()
    roots = [path.resolve() for path in (args.paths or [REPO / "projects"])]

    missing = []
    for path in candidates(roots):
        text = path.read_text(encoding="utf-8")
        if MARKER in text.split("\n\n", 1)[0]:
            continue
        missing.append(path)
        if args.check:
            continue
        path.write_text(CPP_HEADER + text, encoding="utf-8")

    for path in missing:
        print(f"{'missing' if args.check else 'patched'} {path.relative_to(REPO)}")
    print(f"{len(missing)} file(s)")
    return 1 if (missing and args.check) else 0


if __name__ == "__main__":
    sys.exit(main())
