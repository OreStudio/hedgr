#!/usr/bin/env python3
"""Copy the non-system shared libraries an executable needs into a directory.

The publish tree has to be self-contained. On the build host the loader finds
OreStudio's libraries through an absolute RUNPATH that points into the OreStudio
build output, and that path exists nowhere else. So the libraries are copied in
and their RUNPATH is rewritten relative to the file itself.

Copying the files is not enough on its own. A bundled library keeps the RUNPATH
it was built with, and when that library needs a sibling, the loader resolves
the sibling through the needing library's RUNPATH and never consults the
executable's. An OreStudio library that carries an absolute build-host path
therefore fails to find its own dependency even though the file is sitting
beside it. Every copied library gets its RUNPATH set to $ORIGIN for that reason.

Anything found in a search directory is bundled. Anything not found there is the
operating system's job and is left alone, which is why the system library
directories are never passed as search directories.

The walk reads each library's NEEDED entries with readelf rather than asking the
loader. ldd has to resolve every dependency to report anything, and it cannot do
that until the copies already exist, so it cannot be what decides what to copy.

Usage:
    bundle-dependencies.py --binary publish/bin/hedgr.imgui \\
        --destination publish/lib --search /path/to/ores/publish/lib
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

NEEDED = re.compile(r"\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]")
SONAME = re.compile(r"\(SONAME\)\s+Library soname:\s+\[([^\]]+)\]")
RUNPATH = re.compile(r"\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]")

PATCHELF = shutil.which("patchelf")


def elf_entries(path: pathlib.Path) -> tuple[list[str], str | None, str | None]:
    """The needed names, the soname and the runpath of one ELF file.

    Read without loading it. An unreadable file reports nothing rather than
    failing, which is the normal case for a linker script or a stray text file.
    """
    try:
        out = subprocess.run(
            ["readelf", "-d", str(path)],
            capture_output=True, text=True, check=False, timeout=30,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return [], None, None
    found = RUNPATH.search(out)
    return (NEEDED.findall(out),
            SONAME.search(out).group(1) if SONAME.search(out) else None,
            found.group(1) if found else None)


def resolve(name: str, search: list[pathlib.Path]) -> pathlib.Path | None:
    """The first file with this name among the search directories, or nothing."""
    for directory in search:
        candidate = directory / name
        if candidate.exists():
            return candidate
    return None


def place(source: pathlib.Path, destination: pathlib.Path,
          soname: str | None, needed_as: str) -> str:
    """Copies a library, links the names it is asked for, and frees its runpath.

    The chain is preserved rather than flattened. A library that ships as
    libfoo.so.0.0.28 with a libfoo.so.0 symlink is copied under the real name
    with the symlink recreated, because the NEEDED entry asks for the soname and
    not for the file the build happened to produce.
    """
    real = source.resolve()
    target = destination / real.name
    if not target.exists():
        shutil.copy2(real, target)
        target.chmod(target.stat().st_mode | 0o200)

    for link in {soname, needed_as}:
        if not link or link == real.name:
            continue
        link_path = destination / link
        if link_path.is_symlink() or link_path.exists():
            link_path.unlink()
        link_path.symlink_to(real.name)

    # The library's own absolute runpath would otherwise take precedence over
    # the executable's when this library looks for its own dependencies.
    _, _, runpath = elf_entries(target)
    if runpath is None or "$ORIGIN" not in runpath:
        subprocess.run([PATCHELF, "--set-rpath", "$ORIGIN", str(target)],
                       check=True, capture_output=True)
    return target.name


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--destination", required=True, type=pathlib.Path)
    parser.add_argument("--search", required=True, action="append", type=pathlib.Path,
                        help="directory to resolve dependencies from, repeatable")
    parser.add_argument("--list-system", action="store_true",
                        help="also print the libraries left to the operating system")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    if not args.binary.exists():
        print(f"error: no such binary: {args.binary}", file=sys.stderr)
        return 1

    # Without this the copy looks complete and the deployed tree still fails to
    # start, which is worse than refusing to bundle at all.
    if PATCHELF is None:
        print("error: patchelf is required so the bundled libraries can find "
              "each other. Without it the publish tree is not portable.",
              file=sys.stderr)
        return 1

    search = [d.resolve() for d in args.search if d.is_dir()]
    if not search:
        print("error: none of the search directories exist", file=sys.stderr)
        return 1
    args.destination.mkdir(parents=True, exist_ok=True)

    copied: set[str] = set()
    system: set[str] = set()
    seen: set[str] = set()
    queue: list[pathlib.Path] = [args.binary]

    while queue:
        current = queue.pop()
        needs, _, _ = elf_entries(current)
        for name in needs:
            if name in seen:
                continue
            seen.add(name)
            found = resolve(name, search)
            if found is None:
                system.add(name)
                continue
            _, found_soname, _ = elf_entries(found)
            copied.add(place(found, args.destination, found_soname, name))
            queue.append(found)

    # Prove the result rather than trust the copy. A library that still points
    # at the build host is the exact failure this script exists to prevent.
    polluted = []
    for name in sorted(copied):
        _, _, runpath = elf_entries(args.destination / name)
        if runpath is None or "$ORIGIN" not in runpath:
            polluted.append(f"{name}: {runpath}")

    if not args.quiet:
        for name in sorted(copied):
            print(f"bundled {name}")
        print(f"{len(copied)} libraries bundled into {args.destination}")
        print(f"{len(system)} libraries left to the operating system")
        if args.list_system:
            for name in sorted(system):
                print(f"  system {name}")

    if polluted:
        print("error: these libraries still point at the build host:",
              file=sys.stderr)
        for line in polluted:
            print(f"  {line}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
