#!/usr/bin/env python3
"""Mine conservative, literal GNU shell fixture invocations into case files."""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path

COMMANDS = {"cat", "head", "tail", "sort", "uniq", "split", "grep", "cp", "mv", "ls"}
SKIP_RE = re.compile(r"\$|`|\$\(|\b(for|while|case|compare|returns_|framework_failure_)\b")
PIPE_RE = re.compile(r"^(?:printf|echo)\s+(['\"])(.*?)\1\s*\|\s*(.+)$")
REDIRECT_RE = re.compile(r"\s+(?:\d?>|\d?>>|2>&1).*$")


def shell_words(text: str) -> list[str] | None:
    if SKIP_RE.search(text):
        return None
    text = REDIRECT_RE.sub("", text).strip()
    if not text:
        return None
    # The miner only accepts simple shell words. Complex quoting is reviewed
    # manually instead of being silently misparsed.
    if any(ch in text for ch in "|;&(){}"):
        return None
    words = re.findall(r"'(?:[^']*)'|\"(?:[^\"]*)\"|[^\s]+", text)
    return [word[1:-1] if len(word) >= 2 and word[0] == word[-1] else word for word in words]


def case_name(source: Path, line_no: int, command: str) -> str:
    digest = hashlib.sha1(f"{source}:{line_no}:{command}".encode()).hexdigest()[:10]
    return f"{source.stem}-{line_no}-{digest}.case"


def mine_file(source: Path, output: Path) -> tuple[int, int]:
    generated = skipped = 0
    lines = source.read_text(encoding="utf-8", errors="replace").splitlines()
    for line_no, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#") or SKIP_RE.search(line):
            skipped += 1
            continue
        pipe = PIPE_RE.match(line)
        stdin = None
        if pipe:
            stdin, line = pipe.group(2), pipe.group(3)
        words = shell_words(line)
        if not words:
            skipped += 1
            continue
        index = next((i for i, word in enumerate(words) if word in COMMANDS), None)
        if index is None:
            skipped += 1
            continue
        command = words[index]
        args = words[index + 1 :]
        target = output / command / case_name(source, line_no, command)
        target.parent.mkdir(parents=True, exist_ok=True)
        body = [f"cmd: {command}", f"args: {' '.join(args)}", "timeout: 10"]
        if stdin is not None:
            body += ["stdin: |", f"  {stdin}"]
        body += [f"source: {source}:{line_no}", "tags: mined", ""]
        target.write_text("\n".join(body), encoding="utf-8")
        generated += 1
    return generated, skipped


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    files = [*args.source.rglob("*.sh"), *args.source.rglob("*.pl")]
    generated = skipped = 0
    for source in sorted(files):
        made, ignored = mine_file(source, args.output)
        generated += made
        skipped += ignored
    print(f"files={len(files)} generated={generated} skipped={skipped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
