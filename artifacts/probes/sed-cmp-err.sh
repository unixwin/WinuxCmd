#!/usr/bin/env bash
set -u
G="$1"
W="$2"
for f in "$G"/*.err; do
  n="$(basename "$f")"
  g="$(cat "$f")"
  w="$(cat "$W/$n")"
  if [ "$g" != "$w" ]; then
    echo "ERR-DIFF $n"
    echo "   GNU: [$g]"
    echo "   WIN: [$w]"
  fi
done
echo "ERR_COMPARE_DONE"
