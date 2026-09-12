#!/usr/bin/env bash
# ls probe. usage: ls-probe.sh <ls-binary> <outdir>
set -u
shopt -s globstar
LSBIN="$1"
OUT="$2"
FIX="$OUT/fixture"
rm -rf "$FIX"
mkdir -p "$FIX/lib/glob" "$FIX/lib/readline" "$FIX/lib/sh" "$FIX/lib/history" "$FIX/extra"
: > "$FIX/lib/glob/glob.o"
: > "$FIX/lib/readline/bind.o"
: > "$FIX/lib/readline/display.o"
: > "$FIX/lib/sh/tilde.o"
: > "$FIX/lib/history/list.o"
: > "$FIX/extra/top.o"
: > "$FIX/README"
cd "$FIX" || exit 1
run() {
  local id="$1"; shift
  "$LSBIN" "$@" > "$OUT/$id.out" 2> "$OUT/$id.err"
  printf "E=%s\n" "$?" > "$OUT/$id.exit"
}
run flat lib/**
run flat2 lib\/**
run onedir lib
run twodirs lib/glob lib/readline
run threedirs lib/glob lib/readline lib/sh
run mixed README lib/glob README
run slash lib/glob/ lib/readline/
run deep lib/glob/glob.o lib/readline lib/sh
run all .
run dot ./**
echo PROBE_DONE
