#!/usr/bin/env bash
set -u
OUT="$1"; mkdir -p "$OUT"
printf "l1\nl2\n" > "$OUT/in.txt"
SEDBIN="${SEDBIN:-/usr/bin/sed}"
r() {
  local id="$1"; shift
  "$SEDBIN" "$@" > "$OUT/$id.out" 2> "$OUT/$id.err"
  printf "E=%s\n" "$?" > "$OUT/$id.exit"
}
r 301 --posix -n -e "1 { p; }" "$OUT/in.txt"
r 302 -n -e "1 }" "$OUT/in.txt"
r 303 -n -e "1 { p; } }" "$OUT/in.txt"
r 304 -n -e "{ p; " "$OUT/in.txt"
r 305 -n -e "1 { p; } { p; }" "$OUT/in.txt"
r 306 -n -e "1 { /x/ { p; } }" "$OUT/in.txt"
r 307 -n -e "0,/x/ { p; }" "$OUT/in.txt"
r 308 -n -e "1,/x/ { p; }" "$OUT/in.txt"
r 309 -n -e "! { p; }" "$OUT/in.txt"
r 310 -n -e "1! { p; }" "$OUT/in.txt"
r 311 -n -e "1 { ! p; }" "$OUT/in.txt"
r 312 -n -e "1 { p; q; }" "$OUT/in.txt"
r 313 -n -e "1 { =; }" "$OUT/in.txt"
r 314 -n -e "1 { x; }" "$OUT/in.txt"
r 315 -n -e "1 { N; }" "$OUT/in.txt"
echo DONE
