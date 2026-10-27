#!/usr/bin/env python3
"""Generate tiers.tsv for winuxcore.dll at build time.

Reads the audited manifest (tiers-manifest.tsv) and the live command set
(src/commands/*.cpp) and emits the full tier table that ships next to the
DLL (DESIGN 4.1: the table is data, versioned and reviewed with the DLL).

Every registered command appears exactly once:
  - commands in the manifest keep their audited tier;
  - commands absent from the manifest are emitted as tier=spawn
    (unclassified: R1-R7 audit pending), which hosts must treat as
    "never call in-process".

Usage: generate_tiers.py <manifest> <commands_dir> <version> <output>
Output is LF-only, tab-separated, with a '#' header.
"""
import pathlib
import sys

HEADER = "command\ttier\tfallback_shapes\tnote"


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    manifest_path, commands_dir, version, output = sys.argv[1:5]

    audited = {}
    for line in pathlib.Path(manifest_path).read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or line.startswith("command\t"):
            continue
        parts = line.split("\t")
        if len(parts) != 4:
            print(f"generate_tiers: malformed manifest line: {line!r}", file=sys.stderr)
            return 1
        audited[parts[0]] = parts[1:]

    commands = sorted(
        p.stem for p in pathlib.Path(commands_dir).glob("*.cpp") if p.stem != "link"
    )
    missing = sorted(set(audited) - set(commands))
    if missing:
        print(f"generate_tiers: manifest names unknown commands: {missing}", file=sys.stderr)
        return 1

    lines = [
        "# winuxcore FFI tier table (generated at build time; do not edit)",
        f"# winuxcmd-version: {version}",
        "# source-manifest: src/ffi/tiers-manifest.tsv",
        HEADER,
    ]
    for name in commands:
        if name in audited:
            tier, shapes, note = audited[name]
        else:
            tier, shapes, note = "spawn", "-", "unclassified: R1-R7 audit pending"
        lines.append(f"{name}\t{tier}\t{shapes}\t{note}")

    pathlib.Path(output).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"generate_tiers: wrote {output} ({len(commands)} commands)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
