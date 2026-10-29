#!/usr/bin/env python3
"""Reject new or worsened differential buckets against a baseline."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ORDER = {
    "PASS": 0,
    # KNOWN_DIFF is the bucket runner.sh assigns when a case differs in a
    # way whitelist.txt explains. It is an accepted terminal state, not a
    # worsening, so it ranks with PASS: a recorded PASS that starts
    # producing its whitelisted diff has not regressed, while a move from
    # PASS or KNOWN_DIFF to any *unexplained* bucket below still fails.
    # Without an entry the bucket scored 99, which cut both ways: an
    # explained diff on a PASS case counted as a regression, and a
    # recorded KNOWN_DIFF degrading to OUT_DIFF scored 2 < 99 and passed.
    "KNOWN_DIFF": 0,
    "CRLF_DIFF": 1,
    "OUT_DIFF": 2,
    "EXIT_DIFF": 3,
    "SKIP": 4,
}


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
            # A skip the baseline itself records is explained: it is the
            # case's declared verdict in the recorded shape, produced by
            # a precondition (oracle:/subject:) that shape cannot
            # satisfy. The skip stays visible in every run's census.
            # Any other skip means the environment drifted from the
            # recorded one, which is what this rule exists to catch.
            if old != "SKIP":
                errors.append(f"unexplained skip: {key}")
        elif old == "SKIP":
            # The recorded shape cannot run this case, so a verdict in
            # that shape means the launch environment changed under the
            # baseline. Demand a deliberate refresh instead of silently
            # adopting a bucket that was never dispositioned.
            errors.append(f"recorded skip produced a verdict: {key}: SKIP -> {bucket}")
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
