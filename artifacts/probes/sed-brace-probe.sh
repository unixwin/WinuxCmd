#!/usr/bin/env bash
# sed brace-group probe. usage: probe.sh <sed-binary> <outdir>
set -u
SEDBIN="$1"
OUT="$2"
mkdir -p "$OUT"
printf "  0 012  3   \n" > "$OUT/in.txt"
printf "0a 0b  0c\n" > "$OUT/in2.txt"
printf "one\n" >> "$OUT/in2.txt"
run() {
  local id="$1"; shift
  "$SEDBIN" "$@" > "$OUT/$id.out" 2> "$OUT/$id.err"
  printf "EXIT=%s\n" "$?" > "$OUT/$id.exit"
}
run 1 -n "1 { s/^0*[[:blank:]]*//; s/[[:blank:]]*$//; p; }" "$OUT/in.txt"
run 2 -n "$ { s/a/b/; p; }" "$OUT/in2.txt"
run 3 -n "{ s/a/b/; p; }" "$OUT/in2.txt"
run 4 -n "2 { d; }" "$OUT/in2.txt"
run 5 -n "1{ s/a/b/;p }" "$OUT/in2.txt"
run 6 -n "s/a/b/; 1 { p; }" "$OUT/in2.txt"
run 7 -n "1 { s/^0*[[:blank:]]*//; s/[[:blank:]]*$//; p; } 3 { s/x/y/; p; }" "$OUT/in2.txt"
printf "1 {\n  s/^0*[[:blank:]]*//\n  s/[[:blank:]]*$//\n  p\n}\n" > "$OUT/script8.sed"
"$SEDBIN" -f "$OUT/script8.sed" "$OUT/in.txt" > "$OUT/8.out" 2> "$OUT/8.err"
printf "EXIT=%s\n" "$?" > "$OUT/8.exit"
run 9 -n "s/^0*[[:blank:]]*//; s/[[:blank:]]*$//; p" "$OUT/in.txt"
echo PROBE_DONE
