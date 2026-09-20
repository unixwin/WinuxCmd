#!/usr/bin/env bash
# Training workloads for the MSVC PGO build.
#
# The command shapes are ported from uutils/coreutils util/build-pgo.sh (our
# commands are GNU-compatible, so the same lines train the same paths); the
# corpus is generated procedurally exactly like upstream so profiles stay
# machine-independent.  WinuxCmd additions at the bottom: a pass over the
# differential corpus fixtures and a Windows-flavoured file walk.
#
# Usage: pgo-train.sh <path-to-winuxcmd.exe or bin dir>
# Every invocation may fail individually; a partial profile is still usable.
set -u

BIN_DIR="${1:?usage: pgo-train.sh <bin-dir>}"
EXE="$BIN_DIR/winuxcmd.exe"
[ -x "$EXE" ] || EXE="$BIN_DIR/winuxcmd"

W="$PWD/pgo-work"
rm -rf "$W"
mkdir -p "$W"

# ---- procedural corpus (uutils build-pgo.sh, ported verbatim) ----
WORDS="$W/words.txt"
NUMBERS="$W/numbers.txt"
REPEATED="$W/repeated.txt"
PAIRS="$W/pairs.txt"
COLUMNS="$W/columns.txt"

for ((i = 0; i < 200000; i++)); do printf 'w%06d\n' $(((i * 7919) % 200000)); done > "$WORDS"
for ((i = 0; i < 100000; i++)); do printf 'line%04d\n' $((i % 1000)); done > "$REPEATED"
for ((i = 0; i < 100000; i++)); do printf '%08d value%d\n' "$i" "$i"; done > "$PAIRS"
for ((i = 0; i < 2000; i++)); do printf 'user%d:x:%d:%d:User %d:/home/user%d:/bin/sh\n' "$i" $((1000 + i)) $((1000 + i)) "$i" "$i"; done > "$COLUMNS"
# Use the instrumented binary itself so seq/dd are sampled too (upstream trick).
"$EXE" seq 500000 > "$NUMBERS" 2>/dev/null || seq 500000 > "$NUMBERS"
BLOB="$W/blob.bin"
"$EXE" dd "if=$EXE" "of=$BLOB" bs=64K count=64 2>/dev/null || dd "if=$EXE" "of=$BLOB" bs=64K count=64 2>/dev/null

run() { "$EXE" "$@" >/dev/null 2>&1 || true; }
raw() { "$EXE" "$@" 2>/dev/null || true; }

# ---- sort: the single hottest util ----
run sort "$WORDS" -o "$W/sorted.txt"
run sort -n "$NUMBERS" -o "$W/sorted-n.txt"
run sort -u "$REPEATED" -o "$W/sorted-u.txt"
run sort -r "$WORDS" -o "$W/sorted-r.txt"
run sort -k2,2 -t: "$COLUMNS" -o "$W/sorted-k.txt"

# ---- wc / cat / head / tail ----
run wc -l "$WORDS"
run wc -w "$WORDS"
run wc -c "$BLOB"
run wc "$WORDS" "$NUMBERS"
raw cat "$WORDS" > "$W/cat.txt"
raw cat -n "$WORDS" > "$W/cat-n.txt"
raw head -n 50000 "$WORDS" > "$W/head.txt"
raw tail -n 50000 "$WORDS" > "$W/tail.txt"
run head -c 1048576 "$BLOB"

raw cat "$WORDS" | "$EXE" sort | "$EXE" uniq -c > "$W/pipe1.txt"
raw seq 200000 | "$EXE" wc -l > /dev/null
raw cat "$BLOB" | "$EXE" sha256sum > /dev/null
raw sort -n "$NUMBERS" | "$EXE" tail -n 1000 | "$EXE" cut -c1-4 > "$W/pipe2.txt"

# ---- text utils ----
run uniq "$REPEATED"
run uniq -c "$REPEATED"
run uniq -u "$REPEATED"
run cut -d: -f1,3 "$COLUMNS"
run cut -c1-10 "$WORDS"
run nl "$WORDS"
run fold -w 40 "$WORDS"
run expand "$WORDS"
run unexpand "$WORDS"
run paste "$WORDS" "$WORDS"
run join "$PAIRS" "$PAIRS"
run split -l 20000 "$WORDS" "$W/split-"
raw tr a-z A-Z < "$WORDS" > "$W/tr.txt"
raw tr -d aeiou < "$WORDS" > "$W/tr-d.txt"
raw tee "$W/tee.txt" < "$NUMBERS" > /dev/null

# ---- numbers / encoding / hashing ----
run seq 1000000
run seq 0 0.1 10000
raw base64 "$BLOB" > "$W/encoded.b64"
run base64 -d "$W/encoded.b64"
run cksum "$BLOB"
run md5sum "$BLOB"
run sha1sum "$BLOB"
run sha256sum "$BLOB"
run b2sum "$BLOB"

# ---- file management ----
run cp "$WORDS" "$W/copy.txt"
SRC_TREE="$(cd "$(dirname "$0")/.." && pwd)/src/commands"
run cp -r "$SRC_TREE" "$W/tree"
run mv "$W/copy.txt" "$W/moved.txt"
run mv "$W/tree" "$W/tree2"
run ls -la "$SRC_TREE"
run ls -lR "$(cd "$SRC_TREE/.." && pwd)"
run ls --color=always -la "$(cd "$SRC_TREE/.." && pwd)"
run du -sh "$SRC_TREE"
run df -h
run rm -rf "$W/tree2"

# ---- WinuxCmd addition: differential corpus fixtures as training data ----
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CORPUS="$SCRIPT_DIR/../tests/differential/corpus"
if [ -d "$CORPUS" ]; then
  while IFS= read -r case_file; do
    cmd=""; args=""
    while IFS= read -r line; do
      case "$line" in
        cmd:\ *) cmd=${line#cmd: } ;;
        args:*) args=${line#args:}; args=${args# } ;;
      esac
    done < "$case_file"
    [ -n "$cmd" ] || continue
    (cd "$W" && printf '%s' "$args" | xargs "$EXE" "$cmd" >/dev/null 2>&1) || true
  done < <(find "$CORPUS" -name '*.case' | sort)
fi

rm -rf "$W"
echo "training workloads complete"
