#!/usr/bin/env bash
set -u
P="$1"
for pair in "cmp-gnu cmp-win3" "reg-gnu reg-win3" "tb2-gnu tb4-new"; do
  set -- $pair
  G="$1"; W="$2"
  echo "=== $G vs $W ==="
  n=0
  for f in "$P/$G"/*.err; do
    b=$(basename "$f")
    g=$(cat "$f" | sed "s|/usr/bin/sed||")
    w=$(cat "$P/$W/$b" 2>/dev/null | sed "s|sed:||")
    if [ "$g" != "$w" ]; then
      n=$((n+1))
      echo "  $b  GNU=[$g]  WIN=[$w]"
    fi
  done
  echo "  ERR_WORDING_DIFFS=$n"
done
