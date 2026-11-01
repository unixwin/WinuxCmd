#!/usr/bin/env python3
"""Only-grows gate for tests/differential/baseline.json (issue #1127 P3).

compare-baseline.py guards a live run against the recorded ledger. This
script guards the ledger itself: a change to baseline.json may only grow
the recorded evidence. It fails when the new file, compared with the old
one, drops a case, records a worse bucket for any case (by the same
ORDER ranking compare-baseline.py uses), or records fewer PASS cases in
total. New cases and improved buckets are growth and pass.

The check is a static old-vs-new comparison of the two files. It runs
no corpus case and needs no oracle, build, WSL or cache, so it cannot
false-red on the live-run noise the corpus carries (a case whose live
bucket fluctuates in the recorded shape is identical in both files and
compares equal) or on infrastructure failures in the WSL baseline job.

A deliberate shrink (for example re-recording after an oracle version
advance) is landed by a maintainer who can name, per case, why the
recorded evidence is being given up; this script's output is that list.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _bucket_order() -> dict:
    """Reuse compare-baseline.py's ORDER so both gates rank buckets alike."""
    spec = importlib.util.spec_from_file_location(
        "compare_baseline", _HERE / "compare-baseline.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.ORDER


def _load(path: Path) -> dict:
    payload = json.loads(path.read_text(encoding="utf-8"))
    cases = payload.get("cases")
    if not isinstance(cases, dict):
        raise ValueError(f"{path}: no 'cases' object")
    return {key: value["bucket"] for key, value in cases.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path, help="baseline.json at the base revision")
    parser.add_argument("new", type=Path, help="baseline.json being proposed")
    args = parser.parse_args()
    if not args.old.is_file():
        print(f"no previous baseline at {args.old}; nothing to grow from")
        return 0
    try:
        old = _load(args.old)
        new = _load(args.new)
    except (OSError, ValueError, KeyError) as error:
        print(f"baseline growth check cannot run: {error}")
        return 2
    order = _bucket_order()
    errors = []
    for key in sorted(old.keys() - new.keys()):
        errors.append(f"dropped case: {key} (was {old[key]})")
    for key in sorted(old.keys() & new.keys()):
        if order.get(new[key], 99) > order.get(old[key], 99):
            errors.append(f"worsened bucket: {key}: {old[key]} -> {new[key]}")
    old_pass = sum(1 for bucket in old.values() if bucket == "PASS")
    new_pass = sum(1 for bucket in new.values() if bucket == "PASS")
    if new_pass < old_pass:
        errors.append(f"recorded PASS count shrank: {old_pass} -> {new_pass}")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"baseline growth OK cases={len(old)}->{len(new)} pass={old_pass}->{new_pass}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
