#!/usr/bin/env python3
"""Classify every package in a wpm-source index for Windows ARM64 hosts.

Implements the classification cascade of unixwin/WinuxCmd#1127 P5
("ARM64 package classification batch: native -> wpm-artifacts -> x64
emulation marking"):

  1. native         The index carries a `windows-arm64` artifact. Filled by
                    wpm-source `scripts/add-arm64.py` when the upstream
                    release ships one, or maintained by the owning project
                    (winuxcmd itself is native: this repo's CI writes both
                    artifacts via .github/scripts/update-wpm-source-index.ps1).
                    Packages with `alias_of` inherit their target's class.
  2. wpm-artifacts  No native artifact, and the native path runs through
                    unixwin/wpm-artifacts (self-hosted build/mirror, that
                    repo's charter for "no usable upstream build"): the x64
                    artifact is already served from wpm-artifacts, or it
                    does not come from a GitHub release at all, so the
                    add-arm64.py fill pass can never resolve it.
  3. x64-emulation  Every package still without a native artifact runs its
                    `windows-x64` artifact under x64 emulation on ARM64
                    hosts until tier 1 or 2 lands for it. `--apply` stamps
                    that sanction into the index as the package-level
                    marker `"arm64_emulation": true`. The marker is the
                    one fact not derivable from `artifacts` alone (an
                    x64-only entry may be sanctioned-emulated or merely
                    unclassified), so it is the only thing stamped; the
                    client's fallback contract is: ARM64 host + no
                    windows-arm64 artifact + marker present -> install
                    the windows-x64 artifact and report it as emulated.
                    No marker -> keep today's "metadata-only" behaviour.

Read-only by default: prints a summary and writes the classification
JSON (--out, default: arm64-classification.json next to the index).
--apply additionally stamps the marker into the index file, using the
same write convention as wpm-source's add-arm64.py (indent 2, LF,
trailing blank line). Stamping is idempotent.

Usage:
    python3 scripts/classify-arm64.py --index path/to/wpm-source/index.json
    python3 scripts/classify-arm64.py --index ... --out packaging/arm64-classification.json
    python3 scripts/classify-arm64.py --index ... --apply   # stamps the index
"""

import argparse
import json
import os
import re
import sys
from urllib.parse import urlparse

GH_RELEASE_RE = re.compile(r"https://github\.com/([^/]+/[^/]+)/releases/download/([^/]+)/(.+)")
SELF_HOSTED_MARK = "github.com/unixwin/wpm-artifacts/"


def asset_name(artifact):
    urls = artifact.get("urls") or [""]
    return urls[0].rstrip("/").rsplit("/", 1)[-1] if urls[0] else ""


def classify_all(packages):
    """Return {name: entry} plus a summary dict. Pure: no I/O."""
    by_name = {p["name"]: p for p in packages}

    def resolve(name, seen):
        """Effective artifacts for a package, following alias_of chains."""
        pkg = by_name[name]
        arts = pkg.get("artifacts") or {}
        if arts or name in seen:
            return pkg, arts
        target = pkg.get("alias_of")
        if target and target in by_name:
            return resolve(target, seen | {name})
        return pkg, arts

    entries = {}
    for pkg in packages:
        name = pkg["name"]
        owner, arts = resolve(name, set())
        via = None
        if owner["name"] != name:
            via = f"alias_of: {owner['name']}"

        if "windows-arm64" in arts:
            entry = {"class": "native", "arm64_asset": asset_name(arts["windows-arm64"])}
            if via:
                entry["via"] = via
            entries[name] = entry
            continue

        if "windows-x64" not in arts:
            entries[name] = {"class": "index-only"}
            continue

        x64 = arts["windows-x64"]
        url = (x64.get("urls") or [""])[0]
        entry = {"class": "x64-emulation", "x64_asset": asset_name(x64)}
        if SELF_HOSTED_MARK in url:
            entry["route"] = "wpm-artifacts"
            entry["reason"] = (
                "x64 artifact already served from unixwin/wpm-artifacts; "
                "the native path is an arm64 build in that repo's workflow set"
            )
        elif GH_RELEASE_RE.match(url):
            m = GH_RELEASE_RE.match(url)
            entry["route"] = "upstream"
            entry["reason"] = (
                f"upstream {m.group(1)}@{m.group(2)} ships no Windows arm64 "
                "asset (add-arm64.py no-candidate); re-running the fill pass "
                "picks one up if upstream ever publishes it"
            )
        else:
            host = urlparse(url).netloc or url
            entry["route"] = "wpm-artifacts"
            entry["reason"] = (
                f"x64 artifact is not a GitHub-release asset ({host}); the "
                "fill pass cannot resolve it, so the native path is a "
                "wpm-artifacts mirror/build"
            )
        entries[name] = entry

    summary = {"total": len(packages), "native": 0, "x64_emulation": 0,
               "route_wpm_artifacts": 0, "route_upstream": 0, "index_only": 0}
    for e in entries.values():
        if e["class"] == "native":
            summary["native"] += 1
        elif e["class"] == "x64-emulation":
            summary["x64_emulation"] += 1
            if e.get("route") == "wpm-artifacts":
                summary["route_wpm_artifacts"] += 1
            else:
                summary["route_upstream"] += 1
        else:
            summary["index_only"] += 1
    return entries, summary


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--index", required=True, help="path to a wpm-source index.json")
    ap.add_argument("--out", default="",
                    help="classification JSON output (default: arm64-classification.json next to the index)")
    ap.add_argument("--apply", action="store_true",
                    help="also stamp \"arm64_emulation\": true into the index for every x64-emulation package")
    args = ap.parse_args()

    with open(args.index, newline="") as f:
        index = json.load(f)
    packages = index["packages"]
    entries, summary = classify_all(packages)

    out_path = args.out or os.path.join(
        os.path.dirname(os.path.abspath(args.index)), "arm64-classification.json")
    classification = {
        "schema": 1,
        "kind": "wpm-arm64-classification",
        "rule": "unixwin/WinuxCmd#1127 P5 cascade: native -> wpm-artifacts -> x64-emulation",
        "marker": "index package field \"arm64_emulation\": true (stamped by --apply)",
        "source_index": {
            "repo": "unixwin/wpm-source",
            "version": index.get("version"),
            "updated": index.get("updated"),
            "packages": len(packages),
        },
        "summary": summary,
        "packages": {name: entries[name] for name in sorted(entries)},
    }
    with open(out_path, "w", newline="\n") as f:
        json.dump(classification, f, indent=2, ensure_ascii=False)
        f.write("\n")

    print(f"classified {summary['total']} packages from index "
          f"{index.get('version')} ({index.get('updated')})")
    print(f"  native:         {summary['native']}")
    print(f"  x64-emulation:  {summary['x64_emulation']} "
          f"(route wpm-artifacts: {summary['route_wpm_artifacts']}, "
          f"route upstream: {summary['route_upstream']})")
    print(f"  index-only:     {summary['index_only']}")
    print(f"classification written to {out_path}")

    if args.apply:
        by_name = {p["name"]: p for p in packages}
        stamped = unstamped = 0
        for name, e in entries.items():
            pkg = by_name[name]
            if e["class"] == "x64-emulation":
                if pkg.get("arm64_emulation") is not True:
                    pkg["arm64_emulation"] = True
                    stamped += 1
            elif "arm64_emulation" in pkg:
                # A package that gained a native artifact (fill pass or a
                # project bump) must not keep a stale emulation sanction.
                del pkg["arm64_emulation"]
                unstamped += 1
        with open(args.index, "w", newline="\n") as f:
            json.dump(index, f, indent=2, ensure_ascii=False)
            f.write("\n\n")
        print(f"stamped arm64_emulation on {stamped} packages, removed "
              f"{unstamped} stale markers ({summary['x64_emulation']} total carry it)")


if __name__ == "__main__":
    sys.exit(main())
