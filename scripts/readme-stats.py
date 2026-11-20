#!/usr/bin/env python3
"""Refresh the auto-generated blocks in README.md (readme-stats workflow).

Rewrites ONLY the two marked blocks in README.md:

  <!-- AUTO:CODE-SIZE:START --> ... <!-- AUTO:CODE-SIZE:END -->
      Code size by language, measured with tokei over the whole tree
      (tests included). Top 10 languages by code lines; the rest are
      rolled into "Other".

  <!-- AUTO:CONTRIBUTORS:START --> ... <!-- AUTO:CONTRIBUTORS:END -->
      Everyone who has landed commits (GitHub contributors API, bots
      excluded), by commit count.

The output is a pure function of the tree and the API response -- no
dates, no HEAD sha -- so a run that changes nothing produces no diff
and the workflow commits only on real change. The maintainers list is
curated by hand and deliberately NOT one of these blocks.

Usage:
  readme-stats.py [--readme PATH] [--repo OWNER/NAME]
                  [--tokei-json PATH] [--contributors-json PATH]

The --*-json options feed recorded tool/API output instead of running
tokei / calling the API (local testing). In CI the workflow installs
tokei and exports GH_TOKEN (or GITHUB_TOKEN).
"""

import argparse
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

CODE_START = "<!-- AUTO:CODE-SIZE:START -->"
CODE_END = "<!-- AUTO:CODE-SIZE:END -->"
CONTRIB_START = "<!-- AUTO:CONTRIBUTORS:START -->"
CONTRIB_END = "<!-- AUTO:CONTRIBUTORS:END -->"
TOP_LANGS = 10


def fmt(n):
    return f"{n:,}"


def code_size_block(tokei_json):
    rows = []
    for lang, v in tokei_json.items():
        if lang == "Total":
            continue
        code = int(v.get("code", 0))
        if code <= 0:
            continue
        files = len(v.get("reports", []))
        rows.append((lang, files, code))
    rows.sort(key=lambda r: (-r[2], r[0]))
    total_files = sum(r[1] for r in rows)
    total_code = sum(r[2] for r in rows)
    shown = rows[:TOP_LANGS]
    rest = rows[TOP_LANGS:]
    out = ["| Language | Files | Code lines |", "|---|---:|---:|"]
    for lang, files, code in shown:
        out.append(f"| {lang} | {files} | {fmt(code)} |")
    if rest:
        out.append(
            f"| Other | {sum(r[1] for r in rest)} | {fmt(sum(r[2] for r in rest))} |"
        )
    out.append(f"| **Total** | **{total_files}** | **{fmt(total_code)}** |")
    return "\n".join(out)


def contributors_block(contributors):
    users = [c for c in contributors if c.get("type") == "User"]
    users.sort(key=lambda c: (-int(c.get("contributions", 0)), c["login"]))
    out = ["| Contributor | Commits |", "|---|---:|"]
    for c in users:
        login = c["login"]
        out.append(
            f"| [@{login}](https://github.com/{login}) | {fmt(int(c['contributions']))} |"
        )
    return "\n".join(out)


def splice(text, start_marker, end_marker, body):
    if start_marker not in text or end_marker not in text:
        raise SystemExit(f"markers not found: {start_marker} / {end_marker}")
    head, rest = text.split(start_marker, 1)
    _old, tail = rest.split(end_marker, 1)
    return head + start_marker + "\n" + body + "\n" + end_marker + tail


def fetch_contributors(repo, token):
    out = []
    page = 1
    while True:
        url = (
            f"https://api.github.com/repos/{repo}/contributors"
            f"?per_page=100&page={page}"
        )
        req = urllib.request.Request(
            url,
            headers={
                "Accept": "application/vnd.github+json",
                "User-Agent": "readme-stats",
            },
        )
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(req) as resp:
            batch = json.load(resp)
        out.extend(batch)
        if len(batch) < 100:
            return out
        page += 1


def detect_repo(root):
    if os.environ.get("GITHUB_REPOSITORY"):
        return os.environ["GITHUB_REPOSITORY"]
    url = subprocess.run(
        ["git", "-C", str(root), "remote", "get-url", "origin"],
        capture_output=True,
        text=True,
    ).stdout.strip()
    slug = url.rstrip("/").removesuffix(".git")
    for sep in ("github.com/", "github.com:"):
        if sep in slug:
            return slug.split(sep, 1)[1]
    raise SystemExit(f"cannot detect owner/repo from remote: {url!r}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Refresh README auto blocks")
    ap.add_argument("--readme", default=None)
    ap.add_argument("--repo", default=None)
    ap.add_argument("--tokei-json", default=None)
    ap.add_argument("--contributors-json", default=None)
    args = ap.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    readme = Path(args.readme) if args.readme else root / "README.md"

    if args.tokei_json:
        tokei_data = json.loads(Path(args.tokei_json).read_text(encoding="utf-8"))
    else:
        raw = subprocess.run(
            ["tokei", "--output", "json", "."],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if raw.returncode != 0:
            raise SystemExit(f"tokei failed: {raw.stderr.strip()}")
        tokei_data = json.loads(raw.stdout)

    if args.contributors_json:
        contrib_data = json.loads(
            Path(args.contributors_json).read_text(encoding="utf-8")
        )
    else:
        repo = args.repo or detect_repo(root)
        token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
        contrib_data = fetch_contributors(repo, token)

    text = readme.read_text(encoding="utf-8")
    new = splice(text, CODE_START, CODE_END, code_size_block(tokei_data))
    new = splice(new, CONTRIB_START, CONTRIB_END, contributors_block(contrib_data))
    if new != text:
        readme.write_text(new, encoding="utf-8")
        print(f"{readme}: auto blocks updated")
    else:
        print(f"{readme}: auto blocks unchanged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
