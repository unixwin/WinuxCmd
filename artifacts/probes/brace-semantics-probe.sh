#!/usr/bin/env bash
# probe: } must end line, and --posix brace handling
set -u
OUT="$1"; mkdir -p "$OUT"
printf "l1\nl2\nl3\n" > "$OUT/in.txt"
SEDBIN="${SEDBIN:-/usr/bin/sed}"
g() {
  local id="$1" script="$2"; shift 2
  "$SEDBIN" "$@" -e "$script" "$OUT/in.txt" > "$OUT/$id.out" 2> "$OUT/$id.err"
  printf "E=%s\n" "$?" > "$OUT/$id.exit"
}
g 201 '1 { p; } p'
g 202 $'1 { p; }\n p'
g 203 '1 { p; } 2 { q; }'
g 204 --posix "1 { p; }"
g 205 --posix "1{p;}"
g 206 --posix "{ p; }"
g 207 '1 { 2 { p; } }'
g 208 '2 { 2 { p; } }'
g 209 $'1 { p; }\n2 { p; }'
g 210 $'1 {\n p;\n }'
g 211 '1 { p; } extra'
g 212 '1 { a\\nhello\\n }'
g 213 $'1 {\n a\\\nhello\n p;\n }'
echo DONE
