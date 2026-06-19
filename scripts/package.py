#!/usr/bin/env python3
"""Zip the mod into an installable archive (root: Data/SKSE/Plugins/...).

Primary source is the staged layout (build/dist) produced by the CMake
POST_BUILD step. If that staging is missing (it can be skipped under some
build configurations), this falls back to locating the freshly built DLL in
the build tree and the ini in contrib/, and assembles the layout itself --
so packaging never depends on the staging step having run.

The .pdb is always excluded; players don't need debug symbols.

Usage: python scripts/package.py [--dist build/dist] [--out QuestTrackerNG.zip]
                                 [--build build] [--ini contrib/QuestTrackerNG.ini]
"""

import argparse
import sys
import zipfile
from pathlib import Path

PLUGIN = "QuestTrackerNG"
PLUGIN_SUBPATH = Path("Data") / "SKSE" / "Plugins"


def find_dll(dist: Path, build: Path):
    staged = dist / PLUGIN_SUBPATH / (PLUGIN + ".dll")
    if staged.is_file():
        return staged
    # Fallback: the linker output lives at the top of the build dir for the
    # Ninja single-config generator, but search broadly to be safe. Skip any
    # copy already under a dist/ folder to avoid picking a stale staged one.
    candidates = [
        p for p in build.rglob(PLUGIN + ".dll")
        if "dist" not in p.relative_to(build).parts
    ]
    if candidates:
        return min(candidates, key=lambda p: len(p.relative_to(build).parts))
    return None


def find_ini(dist: Path, ini_arg: Path):
    staged = dist / PLUGIN_SUBPATH / (PLUGIN + ".ini")
    if staged.is_file():
        return staged
    if ini_arg.is_file():
        return ini_arg
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", default="build/dist", help="staged dist directory")
    parser.add_argument("--out", default=PLUGIN + ".zip", help="output zip path")
    parser.add_argument("--build", default=None, help="build dir (default: parent of --dist)")
    parser.add_argument("--ini", default="contrib/" + PLUGIN + ".ini", help="fallback ini path")
    args = parser.parse_args()

    dist = Path(args.dist)
    if args.build:
        build = Path(args.build)
    else:
        build = dist.parent if str(dist.parent) else Path("build")
    ini_arg = Path(args.ini)

    dll = find_dll(dist, build)
    if dll is None:
        print(
            "error: " + PLUGIN + ".dll not found under "
            + str(dist / PLUGIN_SUBPATH) + " or in " + str(build)
            + " - build the plugin first",
            file=sys.stderr,
        )
        return 1

    ini = find_ini(dist, ini_arg)
    if ini is None:
        print("error: " + PLUGIN + ".ini not found (looked in staged dist and "
              + str(ini_arg) + ")", file=sys.stderr)
        return 1

    members = {
        str(PLUGIN_SUBPATH / (PLUGIN + ".dll")): dll,
        str(PLUGIN_SUBPATH / (PLUGIN + ".ini")): ini,
    }

    out = Path(args.out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for arcname, path in members.items():
            if path.suffix.lower() == ".pdb":
                continue
            zf.write(path, arcname)
            print("  + " + arcname + "  (from " + str(path) + ")")

    print("wrote " + str(out) + " (" + format(out.stat().st_size, ",") + " bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
