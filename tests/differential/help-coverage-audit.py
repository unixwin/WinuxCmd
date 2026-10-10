#!/usr/bin/env python3
"""Regenerate the help-coverage audit (tests/differential/help-coverage-raw.json).

For every command recorded in the previous audit this script measures the
line count of `winuxcmd <cmd> --help`, compares it against the GNU oracle
line count recorded in the previous audit, and rewrites the `ours` column.
Density = ours / gnu (issue #1127 P2 tracks the minor group by density).

The GNU oracle line counts never change between runs, so they are carried
over from the previous audit unless --wsl is given, in which case they are
re-measured live through `wsl.exe bash -s` (stdin script, see AGENTS notes
in tests/differential/README.md).

Usage:
  python help-coverage-audit.py --binary build-vs/winuxcmd.exe \
      --output tests/differential/help-coverage-raw.json [--wsl]

The status/missing-options columns are preserved from the previous audit:
they classify the declared option surface (MAJOR = options missing from
--help), which is tracked separately from description density.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def count_lines(text: str) -> int:
    # Match `wc -l` semantics: count newline-terminated lines.
    return text.count("\n")


def run_help(binary: Path, command: str) -> str:
    result = subprocess.run(
        [str(binary), command, "--help"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout


def run_gnu_help(command: str) -> str:
    script = f"{command} --help\n"
    result = subprocess.run(
        ["wsl.exe", "bash", "-s"],
        input=script,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout.replace("\x00", "")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--wsl",
        action="store_true",
        help="re-measure GNU oracle line counts via WSL instead of "
        "carrying over the previous audit values",
    )
    args = parser.parse_args()

    if not args.binary.is_file():
        parser.error(f"binary not found: {args.binary}")
    previous = json.loads(args.output.read_text(encoding="utf-8"))

    updated = []
    for name, _ours, gnu, status, missing, flag in previous:
        ours = count_lines(run_help(args.binary, name))
        if args.wsl:
            gnu = count_lines(run_gnu_help(name))
        updated.append([name, ours, gnu, status, missing, flag])

    args.output.write_text(
        json.dumps(updated, ensure_ascii=False), encoding="utf-8"
    )

    total = 0
    realigned = 0
    for name, ours, gnu, _status, _missing, _flag in updated:
        if gnu:
            total += 1
            if ours / gnu >= 0.8:
                realigned += 1
    print(
        f"updated {args.output}: {len(updated)} commands, "
        f"{realigned}/{total} at density >= 0.8"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
