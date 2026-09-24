#!/usr/bin/env python3
"""Inventory user-visible output candidates without rewriting C++ source."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".cpp", ".cppm", ".h", ".hpp"}
CALL_RE = re.compile(
    r"\b(?P<call>safe(?:Error)?Print(?:Ln)?|std::(?:cout|cerr|clog)|"
    r"printf|fprintf|wprintf|puts|putchar)\s*\(", re.MULTILINE
)
ERROR_RE = re.compile(r"\b(?:std::)?unexpected\s*\(")
STRING_RE = re.compile(r'(?P<prefix>L|u8|u|U)?"(?P<body>(?:\\.|[^"\\])*)"')
DATA_HINTS = re.compile(
    r"(hash|digest|checksum|binary|content|payload|output|filename|path|file|"
    r"format|pattern|regex|expression|command_line|argv|operand)", re.I
)
FORMAT_ONLY = re.compile(
    r"^(?:\\[nrt0abfv]|\\x[0-9A-Fa-f]{2}|[\\s\\t\\r\\n.,:;+/=<>|{}()\\[\\]-]+)$"
)
TECHNICAL_PREFIX = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*=|Usage:|LS_COLORS=|[A-Za-z0-9_.-]+\\([0-9]+\\))"
)


def source_files(root: Path):
    for path in sorted((root / "src").rglob("*")):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def classify(call: str, context: str) -> str:
    if DATA_HINTS.search(context):
        return "review-data-or-format"
    if call.startswith("std::") or call in {"printf", "fprintf", "wprintf", "puts", "putchar"}:
        return "review-legacy-output"
    return "translate-candidate"


def classify_literal(call: str, literal: str | None, context: str) -> str:
    category = classify(call, context)
    if literal is None:
        return "review-data-or-format"
    if category != "translate-candidate":
        return category
    if (not literal.strip() or FORMAT_ONLY.fullmatch(literal) or
            literal.startswith("\\x1b") or TECHNICAL_PREFIX.match(literal)):
        return "review-data-or-format"
    if call.startswith("safe"):
        return "localized-legacy"
    return category


def scan_file(path: Path) -> list[dict[str, object]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    rows: list[dict[str, object]] = []
    for match in CALL_RE.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        tail = text[match.end() : min(len(text), match.end() + 420)]
        literal = STRING_RE.search(tail)
        rows.append({
            "file": str(path.relative_to(ROOT)).replace("\\", "/"),
            "line": line,
            "call": match.group("call"),
            "literal": literal.group("body") if literal else None,
            "classification": classify_literal(match.group("call"),
                                                  literal.group("body") if literal else None,
                                                  tail),
        })
    for match in ERROR_RE.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        tail = text[match.end() : min(len(text), match.end() + 280)]
        literal = STRING_RE.search(tail)
        classification = "review-error-template"
        if literal:
            value = literal.group("body")
            shared = {
                "invalid input", "error reading from file", "error reading input",
                "error reading from standard input", "missing operand",
                "missing file operand", "invalid block size", "invalid length",
                "invalid range", "invalid input range", "invalid wrap size",
                "invalid line count", "invalid regular expression",
                "target is not a directory", "cannot create directory",
                "cannot open for reading", "cannot open for writing",
                "cannot read source metadata", "cannot write destination metadata",
                "cannot preserve timestamps", "cannot preserve attributes",
                "cannot create backup for destination",
                "source and destination are the same file", "failed to hash data",
                "failed to acquire cryptographic context", "failed to create hash object",
                "failed to get hash value", "No such file or directory",
            }
            if (any(ch.isalpha() for ch in value) and
                    not value.endswith(("'", ": ")) or value in shared or
                    value.startswith(("missing operand after '",
                                                    "extra operand '",
                                                    "invalid argument '",
                                                    "error reading '",
                                                    "cannot open '",
                                                    "cannot access '",
                                                    "cannot stat '",
                                                    "cannot create '",
                                                    "error writing '",
                                                    "invalid mode: '",
                                                    "invalid group: '"))):
                classification = "localized-error"
        rows.append({
            "file": str(path.relative_to(ROOT)).replace("\\", "/"),
            "line": line,
            "call": "std::unexpected",
            "literal": literal.group("body") if literal else None,
            "classification": classification,
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    rows = [row for path in source_files(ROOT) for row in scan_file(path)]
    counts: dict[str, int] = {}
    for row in rows:
        category = str(row["classification"])
        counts[category] = counts.get(category, 0) + 1
    Path(args.output).write_text(
        json.dumps({"schema": 1, "counts": counts, "messages": rows},
                   ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"scanned {len(rows)} output candidates")
    for category, count in sorted(counts.items()):
        print(f"{category}: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
