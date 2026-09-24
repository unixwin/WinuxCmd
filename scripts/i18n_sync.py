#!/usr/bin/env python3
"""Prepare and publish the external WinuxCmd I18N catalog repository."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(*args: str, cwd: Path | None = None) -> None:
    subprocess.run(args, cwd=cwd, check=True)


def extract(output: Path) -> None:
    run("python", str(ROOT / "scripts" / "i18n_batch.py"), "extract",
        "--output", str(output))


def validate_catalogs(repo: Path, english: Path) -> None:
    for catalog in sorted(repo.glob("*/catalog.json")):
        run("python", str(ROOT / "scripts" / "i18n_batch.py"), "validate",
            "--base", str(english), "--translated", str(catalog))


def prepare(args: argparse.Namespace) -> None:
    args.output.mkdir(parents=True, exist_ok=True)
    english = args.output / "en-US" / "catalog.json"
    english.parent.mkdir(parents=True, exist_ok=True)
    extract(english)
    if args.repo:
        validate_catalogs(args.repo, english)
    print(f"prepared English catalog at {english}")


def publish(args: argparse.Namespace) -> None:
    english = args.source / "en-US" / "catalog.json"
    if not english.is_file():
        raise SystemExit(f"missing prepared catalog: {english}")
    run("git", "checkout", "-B", args.branch, cwd=args.repo)
    destination = args.repo / "en-US"
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(english, destination / "catalog.json")
    validate_catalogs(args.repo, english)
    run("git", "add", "en-US/catalog.json", cwd=args.repo)
    result = subprocess.run(
        ("git", "diff", "--cached", "--quiet", "--", "en-US/catalog.json"),
        cwd=args.repo,
    )
    if result.returncode == 0:
        print("I18N catalog is already current")
        return
    run("git", "commit", "-m", f"chore: sync WinuxCmd {args.version} catalog",
        cwd=args.repo)
    run("git", "push", "--force-with-lease", "origin", args.branch,
        cwd=args.repo)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prepare_parser = sub.add_parser("prepare")
    prepare_parser.add_argument("--output", type=Path, required=True)
    prepare_parser.add_argument("--repo", type=Path)
    prepare_parser.set_defaults(func=prepare)
    publish_parser = sub.add_parser("publish")
    publish_parser.add_argument("--source", type=Path, required=True)
    publish_parser.add_argument("--repo", type=Path, required=True)
    publish_parser.add_argument("--version", required=True)
    publish_parser.add_argument("--branch", required=True)
    publish_parser.set_defaults(func=publish)
    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
