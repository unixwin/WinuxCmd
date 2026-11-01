#!/usr/bin/env python3
"""Detect drift between the source-extracted English catalog and the
published locale catalogs in unixwin/winuxcmd-i18n (issue #1127 P3).

The drift diff for a locale is the set of message keys the extracted
English catalog has that the locale catalog lacks (missing), the keys
it carries that the source no longer produces (extra), and the shared
keys whose placeholders or option tokens diverge - exactly the errors
i18n_batch.validate reports, computed here in structured form.

A fresh extraction diffed against the published catalogs is NOT empty
today: translations are reviewed artifacts that land in batches, so
master source and the zh-CN catalog carry a standing lag. Failing CI
on any drift at all would make the gate red from birth and keep it
red for reasons unrelated to the change under review. The gate is
therefore a ratchet, in the same shape as the differential baseline:
scripts/i18n-drift-baseline.json records the drift as of its last
maintainer update, and CI fails only when a change makes the drift
GROW beyond that record - the signature of a source string change
whose catalog sync was forgotten, or of a catalog edit that broke
keys. A change that deliberately adds user-visible strings updates
the record in the same PR (python3 scripts/check_i18n_drift.py
--update-baseline), which keeps the growth visible in review; a
translation sync batch shrinks it the same way.
"""

from __future__ import annotations

import argparse
import ast
import importlib.util
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = Path(__file__).resolve().with_name("i18n-drift-baseline.json")
BUCKETS = ("missing", "extra", "placeholder_mismatch", "option_token_changed", "other")


def _load_batch():
    spec = importlib.util.spec_from_file_location(
        "i18n_batch", Path(__file__).resolve().with_name("i18n_batch.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def catalog_drift(batch, base: dict, translated: dict) -> dict:
    """Structured form of i18n_batch.validate's verdict for one catalog."""
    drift = {bucket: [] for bucket in BUCKETS}
    for error in batch.validate(base, translated):
        if error.startswith("key mismatch: "):
            payload = error[len("key mismatch: "):]
            missing_part, _, extra_part = payload.partition(" extra=")
            drift["missing"] = ast.literal_eval(missing_part[len("missing="):])
            drift["extra"] = ast.literal_eval(extra_part)
        elif error.startswith("placeholder mismatch: "):
            drift["placeholder_mismatch"].append(error[len("placeholder mismatch: "):])
        elif error.startswith("option token changed: "):
            drift["option_token_changed"].append(error[len("option token changed: "):])
        else:
            drift["other"].append(error)
    return {bucket: sorted(values) for bucket, values in drift.items() if values}


def measure(batch, source: Path, i18n_repo: Path) -> dict:
    base = batch.extract(source)
    locales = {}
    for catalog_path in sorted(i18n_repo.glob("catalogs/*/catalog.json")):
        translated = json.loads(catalog_path.read_text(encoding="utf-8"))
        locales[catalog_path.parent.name] = catalog_drift(batch, base, translated)
    return locales


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--i18n-repo", type=Path, required=True,
                        help="checkout of unixwin/winuxcmd-i18n")
    parser.add_argument("--source", type=Path, default=ROOT / "src" / "commands",
                        help="source tree for i18n_batch.extract (default: src/commands)")
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--update-baseline", action="store_true",
                        help="record the currently measured drift as the baseline")
    args = parser.parse_args()
    batch = _load_batch()
    current = measure(batch, args.source, args.i18n_repo)
    if args.update_baseline:
        payload = {
            "format": 1,
            "recorded_at": datetime.now(timezone.utc).isoformat(),
            "note": ("Pre-existing drift between master source extraction and the "
                     "published locale catalogs, recorded when the drift gate "
                     "landed (issue #1127 P3). May only shrink as translation "
                     "sync batches land; growth in a PR must be deliberate and "
                     "visible here."),
            "locales": current,
        }
        args.baseline.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
                                 encoding="utf-8")
        total = sum(len(v) for loc in current.values() for v in loc.values())
        print(f"recorded drift baseline: {total} item(s) across {len(current)} locale(s)")
        return 0
    if not args.baseline.is_file():
        print(f"no drift baseline at {args.baseline}; run with --update-baseline to record one")
        return 2
    recorded = json.loads(args.baseline.read_text(encoding="utf-8")).get("locales", {})
    failures = []
    for locale in sorted(set(recorded) | set(current)):
        for bucket in BUCKETS:
            now = set(current.get(locale, {}).get(bucket, []))
            was = set(recorded.get(locale, {}).get(bucket, []))
            for item in sorted(now - was):
                failures.append(f"{locale}: new drift ({bucket}): {item}")
            for item in sorted(was - now):
                print(f"{locale}: drift resolved ({bucket}): {item}")
    if failures:
        print("\n".join(failures))
        print("i18n drift grew beyond scripts/i18n-drift-baseline.json; sync the "
              "catalogs in unixwin/winuxcmd-i18n, or - for deliberate new source "
              "strings - record the growth in the same PR with "
              "scripts/check_i18n_drift.py --update-baseline")
        return 1
    total = sum(len(v) for loc in current.values() for v in loc.values())
    print(f"i18n drift OK ({total} recorded item(s), none new)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
