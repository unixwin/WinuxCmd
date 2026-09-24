#!/usr/bin/env python3
"""Convert runner TSV output into a reproducible case baseline."""

from __future__ import annotations

import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--oracle", required=True)
    args = parser.parse_args()
    cases = {}
    for line in args.results.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        case, command, bucket = line.split("\t", 2)
        cases[case] = {"command": command, "bucket": bucket}
    payload = {
        "format": 1,
        "oracle": args.oracle,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "cases": dict(sorted(cases.items())),
    }
    args.baseline.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"baseline cases={len(cases)} oracle={args.oracle}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
