#!/bin/bash
# gate/stream-check.sh — 64 MiB streaming-stdin ABI ledger (#1488, #1550).
#
# Proves the v2 streaming entry (winuxcmd_run_stream, host pull callback)
# against the v1 whole-buffer entry (winuxcmd_run) on real commands:
#   * byte equality: three 64 MiB input kinds (random ASCII text, multi-byte
#     UTF-8 with CJK/emoji, no-trailing-newline) x {tac, base64} x
#     {v1 whole-buffer, v2 default chunking, v2 feed-chunk 65536, v2
#     feed-chunk 997 (uneven)} — stdout, stderr and rc must all match;
#   * memory: PeakWorkingSetSize / PeakPagefileUsage (GetProcessMemoryInfo,
#     sampled once post-run via the driver's --mem-report) must show the ABI
#     adds no whole-input copy: v2 peak < v1 peak - MARGIN for tac/base64
#     (the commands themselves still accumulate internally — the delta is
#     the ABI-layer copy the streaming path removes).
#
# Usage: bash gate/stream-check.sh [SIZE_BYTES]
# Requires: target/debug/winuxcmd-driver.exe (cargo build), Git Bash
# coreutils (awk/head/cmp). Artifacts under rust2/target/stream-check/.
set -u

GATE_DIR="$(cd "$(dirname "$0")" && pwd)"
RUST2="$(cd "$GATE_DIR/.." && pwd)"
DRIVER="$RUST2/target/debug/winuxcmd-driver.exe"
WORK="$RUST2/target/stream-check"
SIZE="${1:-67108864}"          # 64 MiB
# required v1 - v2 peak gap: half the input size (the ABI copy plus the
# driver slurp that v2 removes are ~1-2x input; 1/2 is the safe bound).
MARGIN=$((SIZE / 2))

if [ ! -x "$DRIVER" ]; then
    echo "building driver..."
    (cd "$RUST2" && cargo build -p winuxcmd-driver) || exit 1
fi
if [ ! -x "$DRIVER" ]; then
    echo "FAIL: $DRIVER not found" >&2
    exit 1
fi

mkdir -p "$WORK"
rm -f "$WORK"/*.bin "$WORK"/*.out "$WORK"/*.err "$WORK"/*.rc "$WORK"/mem-*.txt
pass=0; fail=0; notes=()

note() { notes+=("$1"); echo "note: $1"; }
ok()   { pass=$((pass+1)); echo "PASS: $1"; }
bad()  { fail=$((fail+1)); echo "FAIL: $1"; }

# ---------- input generation (deterministic, seeded) ----------

gen_text() {   # random ASCII lines
    awk 'BEGIN{srand(42); cs="abcdefghijklmnopqrstuvwxyz0123456789 .,-:_";
        for(;;){n=int(rand()*60)+10; line="";
            for(j=0;j<n;j++) line=line substr(cs,int(rand()*length(cs))+1,1);
            print line}}' | head -c "$SIZE"
}
gen_utf8() {   # multi-byte UTF-8 lines (CJK, emoji, latin accents)
    awk 'BEGIN{srand(7); split("中文内容块 🎉🚀emoji éüñ-accents 日本語のテキスト Ω≈ç√inter", t, " ");
        for(;;){n=int(rand()*4)+2; line="";
            for(j=0;j<n;j++) line=line t[int(rand()*6)+1] " ";
            print line}}' | head -c "$SIZE"
}

echo "generating inputs (${SIZE} bytes each) under $WORK ..."
gen_text  > "$WORK/text.bin"
gen_utf8  > "$WORK/utf8.bin"
# no-trailing-newline: cut one byte off the ASCII stream, append a plain
# letter so the last byte is never \n (streams have non-empty lines).
{ gen_text | head -c -1; printf 'T'; } > "$WORK/notrail.bin"

for f in text utf8 notrail; do
    sz=$(wc -c < "$WORK/$f.bin")
    echo "  $f.bin: $sz bytes"
done

# ---------- runner ----------

# run_mode MODE INPUT OUT ERR RC [cmd args...]
run_mode() {
    local mode="$1" input="$2" out="$3" err="$4" rc="$5"; shift 5
    local flags=()
    case "$mode" in
        v1)    flags=(--abi) ;;
        v2)    flags=(--abi-stream) ;;
        v2f64) flags=(--abi-stream --feed-chunk 65536) ;;
        v2f997) flags=(--abi-stream --feed-chunk 997) ;;
        *) echo "unknown mode $mode" >&2; return 9 ;;
    esac
    timeout 300 "$DRIVER" "${flags[@]}" "$@" < "$input" > "$out" 2> "$err"
    echo $? > "$rc"
}

# check_pair LABEL INPUT CMD ARGS...
check_command() {
    local label="$1" input="$2"; shift 2
    local cmd="$1"; shift
    run_mode v1     "$input" "$WORK/$label-v1.out"     "$WORK/$label-v1.err"     "$WORK/$label-v1.rc"     "$cmd" "$@"
    run_mode v2     "$input" "$WORK/$label-v2.out"     "$WORK/$label-v2.err"     "$WORK/$label-v2.rc"     "$cmd" "$@"
    run_mode v2f64  "$input" "$WORK/$label-v2f64.out"  "$WORK/$label-v2f64.err"  "$WORK/$label-v2f64.rc"  "$cmd" "$@"
    run_mode v2f997 "$input" "$WORK/$label-v2f997.out" "$WORK/$label-v2f997.err" "$WORK/$label-v2f997.rc" "$cmd" "$@"
    local mode
    for mode in v2 v2f64 v2f997; do
        if cmp -s "$WORK/$label-v1.out" "$WORK/$label-$mode.out" \
           && cmp -s "$WORK/$label-v1.err" "$WORK/$label-$mode.err" \
           && [ "$(cat "$WORK/$label-v1.rc")" = "$(cat "$WORK/$label-$mode.rc")" ]; then
            ok "$label $mode: rc+stdout+stderr byte-identical to v1"
        else
            bad "$label $mode: differs from v1 (rc $(cat "$WORK/$label-v1.rc") vs $(cat "$WORK/$label-$mode.rc"))"
        fi
    done
}

echo
echo "== byte equality: streaming vs whole buffer ($((SIZE / 1048576)) MiB inputs) =="
for f in text utf8 notrail; do
    check_command "tac-$f"    "$WORK/$f.bin" tac
    check_command "base64-$f" "$WORK/$f.bin" base64
done

# roundtrip: decode streamed bytes (base64 -d) on the text input
run_mode v1 "$WORK/text.bin" "$WORK/b64enc-v1.out" "$WORK/b64enc-v1.err" "$WORK/b64enc-v1.rc" base64
check_command "base64-d-text" "$WORK/b64enc-v1.out" base64 -d

# ---------- memory peaks ----------

echo
echo "== memory peaks (GetProcessMemoryInfo, coarse) =="
mb() { awk -v b="$1" 'BEGIN{printf "%.1f MiB", b/1048576}'; }
peak_of() { awk '/^peak_pagefile_bytes:/ {print $2}' "$1"; }
ws_of()   { awk '/^peak_working_set_bytes:/ {print $2}' "$1"; }

mem_fail=0
for cmd in tac base64; do
    timeout 300 "$DRIVER" --abi        --mem-report "$WORK/mem-v1-$cmd.txt" "$cmd" < "$WORK/text.bin" > /dev/null 2>&1
    timeout 300 "$DRIVER" --abi-stream --mem-report "$WORK/mem-v2-$cmd.txt" "$cmd" < "$WORK/text.bin" > /dev/null 2>&1
    v1=$(peak_of "$WORK/mem-v1-$cmd.txt")
    v2=$(peak_of "$WORK/mem-v2-$cmd.txt")
    w1=$(ws_of "$WORK/mem-v1-$cmd.txt")
    w2=$(ws_of "$WORK/mem-v2-$cmd.txt")
    if [ -z "$v1" ] || [ -z "$v2" ]; then
        bad "mem $cmd: missing peak report"
        mem_fail=1
        continue
    fi
    delta=$((v1 - v2))
    echo "  $cmd: v1 pagefile-peak $(mb "$v1") (ws $(mb "$w1")), v2 $(mb "$v2") (ws $(mb "$w2")), ABI-copy removed >= $(mb "$delta")"
    if [ "$delta" -ge "$MARGIN" ]; then
        ok "mem $cmd: streaming peak is >= $(mb "$MARGIN") (SIZE/2) below whole-buffer (no ABI-layer whole-input copy)"
    else
        bad "mem $cmd: v1-v2 gap only $(mb "$delta") (< $(mb "$MARGIN"))"
        mem_fail=1
    fi
done

# ---------- direct-path smoke (the gate path must be untouched) ----------

smoke=$(printf 'a\nb\nc\n' | "$DRIVER" tac)
if [ "$smoke" = "$(printf 'c\nb\na')" ]; then
    ok "direct dispatch path unchanged (tac smoke)"
else
    bad "direct dispatch path changed (tac smoke: $smoke)"
fi

echo
echo "== ledger =="
for cmd in tac base64; do
    for f in text utf8 notrail; do
        printf '  %-14s rc(v1)=%s rc(v2)=%s\n' "$cmd-$f" \
            "$(cat "$WORK/$cmd-$f-v1.rc")" "$(cat "$WORK/$cmd-$f-v2.rc")"
    done
done

echo
echo "pass=$pass fail=$fail"
if [ "$fail" -ne 0 ] || [ "$mem_fail" -ne 0 ]; then
    exit 1
fi
exit 0
