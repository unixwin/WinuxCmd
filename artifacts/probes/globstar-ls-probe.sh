#!/usr/bin/env bash
# globstar fixture: identical 17-arg expansion into GNU ls and WinuxCmd ls.
# usage: globstar-ls-probe.sh <ls-binary> <outdir>
set -u
shopt -s globstar
cd "$(dirname "$0")" || exit 1
LSBIN="$1"
OUT="$2"
F="$OUT/gst"
rm -rf "$F"
mkdir -p "$F"
cd "$F" || exit 1
mkdir lib builtins
mkdir lib/glob lib/readline lib/sh
: > lib/glob/glob.o
: > lib/glob/smatch.o
: > lib/glob/strmatch.o
: > lib/readline/bind.o
: > lib/readline/callback.o
: > lib/readline/compat.o
: > lib/readline/complete.o
: > lib/readline/display.o
: > lib/sh/casemod.o
: > lib/sh/clktck.o
: > lib/sh/clock.o
: > lib/sh/eaccess.o
: > lib/sh/itos.sh
echo '### expansion of lib/**' > "$OUT/all.txt"
echo "$(echo lib/**)" >> "$OUT/all.txt"
echo "words=$(echo lib/** | wc -w)" >> "$OUT/all.txt"
echo >> "$OUT/all.txt"
echo '### ls --version' >> "$OUT/all.txt"
"$LSBIN" --version 2>&1 | head -1 >> "$OUT/all.txt"
echo >> "$OUT/all.txt"
echo '### ls lib/**' >> "$OUT/all.txt"
"$LSBIN" lib/** >> "$OUT/all.txt" 2>&1
echo "exit=$?" >> "$OUT/all.txt"
echo PROBE_DONE
