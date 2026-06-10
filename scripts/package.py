#!/usr/bin/env python3
"""Zip the staged mod layout (build/dist) into an installable archive.

The archive root contains Data/SKSE/Plugins/..., so it installs directly
with MO2/Vortex or by extracting into the Skyrim folder.

Usage: python scripts/package.py [--dist build/dist] [--out QuestTrackerNG.zip]
"""

import argparse
import sys
import zipfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", default="build/dist", help="staged dist directory")
    parser.add_argument("--out", default="QuestTrackerNG.zip", help="output zip path")
    args = parser.parse_args()

    dist = Path(args.dist)
    dll = dist / "Data" / "SKSE" / "Plugins" / "QuestTrackerNG.dll"
    if not dll.is_file():
        print(f"error: {dll} not found - build the plugin first", file=sys.stderr)
        return 1

    out = Path(args.out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(dist.rglob("*")):
            if path.is_file() and path.suffix.lower() != ".pdb":
                zf.write(path, path.relative_to(dist))
                print(f"  + {path.relative_to(dist)}")

    print(f"wrote {out} ({out.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
