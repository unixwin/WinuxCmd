#!/usr/bin/env bash
# cp success-quietness probe with relative paths (Windows binaries cannot open /mnt/d).
# usage: cp-quiet2-probe.sh <cp-binary> <outdir>
set -u
cd "$(dirname "$0")" || exit 1
CPBIN="$1"
OUT="$2"
mkdir -p "$OUT/work"
printf "hello\n" > "$OUT/work/src.txt"
"$CPBIN" "$OUT/work/src.txt" "$OUT/work/dst.txt" > "$OUT/succ.out" 2> "$OUT/succ.err"
printf "EXIT=%s\n" "$?" > "$OUT/succ.exit"
"$CPBIN" "$OUT/work/does-not-exist.txt" "$OUT/work/dst2.txt" > "$OUT/err.out" 2> "$OUT/err.err"
printf "EXIT=%s\n" "$?" > "$OUT/err.exit"
"$CPBIN" "$OUT/work/src.txt" "$OUT/work/dst.txt" "$OUT/work/dst3.txt" > "$OUT/multi.out" 2> "$OUT/multi.err"
printf "EXIT=%s\n" "$?" > "$OUT/multi.exit"
"$CPBIN" -v "$OUT/work/src.txt" "$OUT/work/dst4.txt" > "$OUT/verb.out" 2> "$OUT/verb.err"
printf "EXIT=%s\n" "$?" > "$OUT/verb.exit"
echo PROBE_DONE
