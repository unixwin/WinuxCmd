#!/usr/bin/env python3
"""Reject new or worsened differential buckets against a baseline."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ORDER = {"PASS": 0, "CRLF_DIFF": 1, "OUT_DIFF": 2, "EXIT_DIFF": 3, "SKIP": 4}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    expected = {key: value["bucket"] for key, value in baseline.get("cases", {}).items()}
    actual = {}
    for line in args.results.read_text(encoding="utf-8").splitlines():
        if line.strip():
            key, _command, bucket = line.split("\t", 2)
            actual[key] = bucket
    errors = []
    for key, bucket in actual.items():
        old = expected.get(key)
        if old is None:
            errors.append(f"new case: {key}={bucket}")
        elif bucket == "SKIP":
            errors.append(f"unexplained skip: {key}")
        elif ORDER.get(bucket, 99) > ORDER.get(old, 99):
            errors.append(f"regression: {key}: {old} -> {bucket}")
    for key in expected.keys() - actual.keys():
        errors.append(f"missing case: {key}")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"baseline OK cases={len(actual)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
