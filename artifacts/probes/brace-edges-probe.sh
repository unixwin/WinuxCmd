#!/usr/bin/env bash
# GNU sed brace semantics edge-case probe. usage: gnu-brace-edges.sh <sed-binary> <outdir>
set -u
SEDBIN="$1"
OUT="$2"
mkdir -p "$OUT"
printf "l1 a\nl2 a\nl3 b\n" > "$OUT/in.txt"
t() {
  local id="$1"; local script="$2"; shift 2
  "$SEDBIN" -n -e "$script" "$OUT/in.txt" > "$OUT/$id.out" 2> "$OUT/$id.err"
  printf "E=%s\n" "$?" > "$OUT/$id.exit"
}
t 101 "1 { p; }"
t 102 "1 { p; } 2 { p; }"
t 103 "1,2 { p; }"
t 104 "/a/ { p; }"
t 105 "1 { 2 { p; } }"
t 106 "1 { 3 { p; } }"
t 107 "{ 1 { p; } }"
t 108 "1 { /a/ { p; } }"
t 109 "1 { s/a/b/; } p"
t 110 "1 { p; } s/a/b/"
t 111 "1{p;}"
t 112 "1 { p }"
t 113 "1 { p;; }"
t 114 "1 { } "
t 115 "1 { }s/a/b/"
t 116 "$ { p; } 1 { p; }"
t 117 "s/a/{/"
t 118 "1 { s/a/{/g; }"
t 119 "1 { a\\nhello\\n }"
t 120 "1 { b; }"
echo DONE
