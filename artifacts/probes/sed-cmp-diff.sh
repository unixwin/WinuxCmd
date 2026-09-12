#!/usr/bin/env bash
# Compare two sed probe outdirs: <gnu-outdir> <win-outdir>
set -u
G="$1"
W="$2"
TOTAL=0
DIFF=0
for f in "$G"/*.out "$G"/*.exit; do
  n="$(basename "$f")"
  TOTAL=$((TOTAL+1))
  if [ ! -e "$W/$n" ]; then echo "MISSING $n"; DIFF=$((DIFF+1)); continue; fi
  if ! cmp -s "$f" "$W/$n"; then
    DIFF=$((DIFF+1))
    echo "DIFF $n"
    echo "   GNU: [$(cat "$f" | tr "\n" "|")]"
    echo "   WIN: [$(cat "$W/$n" | tr "\n" "|")]"
  fi
done
echo "STDOUT_EXIT_COMPARED=$TOTAL DIFFERENCES=$DIFF"
