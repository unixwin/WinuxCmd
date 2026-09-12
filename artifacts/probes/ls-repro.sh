#!/usr/bin/env bash
# Minimal ls multi-arg repro. usage: ls-repro.sh <ls-binary> <outdir>
set -u
cd "$(dirname "$0")" || exit 1
LSBIN="$1"
OUT="$2"
F="$OUT/fix"
rm -rf "$F"
mkdir -p "$F/sub"
: > "$F/sub/x"
: > "$F/a"
: > "$F/b"
cd "$F" || exit 1
echo "### ls --version" > "$OUT/all.txt"
"$LSBIN" --version 2>&1 | head -1 >> "$OUT/all.txt"
echo >> "$OUT/all.txt"
echo "### ls sub a b" >> "$OUT/all.txt"
"$LSBIN" sub a b >> "$OUT/all.txt" 2>&1
echo "exit=$?" >> "$OUT/all.txt"
echo >> "$OUT/all.txt"
echo "### ls a b sub" >> "$OUT/all.txt"
"$LSBIN" a b sub >> "$OUT/all.txt" 2>&1
echo "exit=$?" >> "$OUT/all.txt"
echo >> "$OUT/all.txt"
echo "### ls sub sub/x" >> "$OUT/all.txt"
"$LSBIN" sub sub/x >> "$OUT/all.txt" 2>&1
echo "exit=$?" >> "$OUT/all.txt"
echo >> "$OUT/all.txt"
echo "### ls sub" >> "$OUT/all.txt"
"$LSBIN" sub >> "$OUT/all.txt" 2>&1
echo "exit=$?" >> "$OUT/all.txt"
echo REPRO_DONE
