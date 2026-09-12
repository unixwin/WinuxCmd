#!/usr/bin/env bash
# Run every sed brace probe against one sed binary into one outdir.
# usage: sed-run-all.sh <sed-binary> <outdir>
set -u
BASE="$(cd "$(dirname "$0")" && pwd)"
cd "$BASE" || exit 1
SEDBIN="$1"
OUT="$2"
rm -rf "$OUT"
mkdir -p "$OUT"
bash "$BASE/sed-brace-probe.sh" "$SEDBIN" "$OUT" >/dev/null 2>&1
bash "$BASE/brace-edges-probe.sh" "$SEDBIN" "$OUT" >/dev/null 2>&1
SEDBIN="$SEDBIN" bash "$BASE/brace-semantics-probe.sh" "$OUT" >/dev/null 2>&1
SEDBIN="$SEDBIN" bash "$BASE/brace-sem2-probe.sh" "$OUT" >/dev/null 2>&1
echo "RUN_ALL_DONE $(basename "$OUT")"
