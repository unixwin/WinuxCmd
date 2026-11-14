#!/usr/bin/env bash
# tail-follow.sh — follow-mode scenario harness for rust2 tail (#1488).
#
# The differential gate (run-gate.sh) runs one non-interactive command per
# row with literal argv, so it can only pin follow rows that TERMINATE on
# their own (a dead --pid, "no files remaining", option errors). The live
# behaviors — appended data, truncation, rename/recreate under -F, delete
# under --retry, multi-file header switching, --pid exit — need a writer
# racing the follower. This harness runs each scenario as a small script
# on both sides:
#
#   GNU  : WSL coreutils 9.4 `tail`, scratch on ext4 (/tmp, inotify path)
#   RUST : rust2/target/debug/winuxcmd-driver.exe tail, scratch on NTFS
#
# Every scenario backgrounds the follower with `--pid=$P` where P is a
# short-lived `sleep` (the "writer"), drives the file changes with fixed
# sleeps between steps (STEP seconds, far above both sides' wake latency),
# then waits for the follower to exit on its own when P dies. The run is
# therefore deterministic and terminating; a `timeout` guard catches a
# follower that never exits (rc 124 = FAIL).
#
# Usage:
#   bash gate/wire/tail-follow.sh [scenario ...]     run + compare, write
#                                                    gate/wire/tail-follow.out
#   bash tail-follow.sh --scenario NAME              (internal) run one
#                                                    scenario in the cwd with
#                                                    the follower in $TAIL
#
# Normalization: none on stdout/stderr (byte-compared). The only
# normalized field is the exit status of the broken-pipe scenario: GNU
# dies by SIGPIPE (141 from the shell); the Rust command returns
# Err(CmdError::Io) which the driver maps to 125 and the product host maps
# to 141 — the ledger records both raw values and compares "died on the
# broken pipe" (rc in {125,141}).
set -u

STEP="${STEP:-0.5}"

# ---------------------------------------------------------------- scenarios
# Each sc_* runs in an empty cwd, writes out/err/rc. $TAIL is word-split on
# purpose (it is "tail" or "<driver.exe> tail").

# life SECONDS: start the writer stand-in, P = its pid
life() {
    sleep "$1" &
    P=$!
}

fin() {
    wait "$TP"
    echo $? > rc
    wait "$P" 2>/dev/null
}

st() { sleep "$STEP"; }

sc_append() {
    printf 'a\nb\n' > f
    life 2.6
    $TAIL -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; echo c >> f
    st; printf 'd' >> f
    st; printf 'e\n' >> f
    fin
}

sc_append_default_interval() {
    # default -s 1.0: data must still arrive event-driven / within a poll
    printf 'a\n' > f
    life 2.6
    $TAIL -f --pid=$P f > out 2> err & TP=$!
    st; echo b >> f
    st; st; echo c >> f
    fin
}

sc_multi_headers() {
    printf 'f0\n' > f
    printf 'g0\n' > g
    life 3.1
    $TAIL -f -s 0.1 --pid=$P f g > out 2> err & TP=$!
    st; echo g1 >> g
    st; echo f1 >> f
    st; echo f2 >> f
    st; echo g2 >> g
    fin
}

sc_multi_quiet() {
    printf 'f0\n' > f
    printf 'g0\n' > g
    life 2.6
    $TAIL -q -f -s 0.1 --pid=$P f g > out 2> err & TP=$!
    st; echo f1 >> f
    st; echo g1 >> g
    st; echo f2 >> f
    fin
}

sc_verbose_single() {
    printf 'a\n' > f
    life 2.1
    $TAIL -v -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; echo b >> f
    st; echo c >> f
    fin
}

sc_truncate() {
    printf '1\n2\n3\n' > f
    life 2.6
    $TAIL -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; : > f
    st; echo new >> f
    st; echo more >> f
    fin
}

sc_truncate_rewrite() {
    printf 'a long first line\n' > f
    life 2.1
    $TAIL -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; printf 's\n' > f
    st; echo t >> f
    fin
}

sc_desc_rename() {
    # descriptor mode keeps reading the renamed file, ignores the new name
    printf 'a\n' > f
    life 2.6
    $TAIL -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; mv f f.1
    st; echo more >> f.1
    st; echo other > f
    fin
}

sc_desc_delete() {
    printf 'a\n' > f
    life 1.6
    $TAIL -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; rm f
    fin
}

sc_name_rotate() {
    # -F: rename away, recreate later (log rotation, two-step)
    printf 'a\n' > f
    life 3.1
    $TAIL -F -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; mv f f.1
    st; echo new > f
    st; echo old-more >> f.1
    st; echo new-more >> f
    fin
}

sc_name_replace() {
    # -F: atomic replacement by rename over the name
    printf 'a\n' > f
    printf 'g\n' > g
    life 2.6
    $TAIL -F -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; mv g f
    st; echo h >> f
    fin
}

sc_retry_delete() {
    printf 'a\n' > f
    life 2.6
    $TAIL -F -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; rm f
    st; echo back > f
    st; echo more >> f
    fin
}

sc_name_noretry_delete() {
    # --follow=name without --retry: the name vanishing ends the run
    printf 'a\n' > f
    life 2.1
    $TAIL --follow=name -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; rm f
    st
    fin
}

sc_name_noretry_multi_reappear() {
    # --follow=name, two names: a deleted one is re-resolved while the
    # other keeps the run alive, and reported when it reappears
    printf 'f0\n' > f
    printf 'g0\n' > g
    life 2.6
    $TAIL --follow=name -s 0.1 --pid=$P f g > out 2> err & TP=$!
    st; rm g
    st; echo g1 > g
    st; echo f1 >> f
    fin
}

sc_name_noretry_rename_last() {
    # --follow=name, the LAST name renamed away: GNU's inotify watch on
    # the still-existing inode keeps the run alive until the name returns
    printf 'a\n' > f
    life 2.1
    $TAIL --follow=name -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; mv f f.1
    st; echo new > f
    fin
}

sc_retry_missing() {
    life 2.1
    $TAIL -F -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; echo hi > f
    st; echo more >> f
    fin
}

sc_retry_missing_multi() {
    printf 'f0\n' > f
    life 2.6
    $TAIL -F -s 0.1 --pid=$P f g > out 2> err & TP=$!
    st; echo g0 > g
    st; echo f1 >> f
    st; echo g1 >> g
    fin
}

sc_desc_retry_missing() {
    printf 'g0\n' > g
    life 2.1
    $TAIL -f --retry -s 0.1 --pid=$P f g > out 2> err & TP=$!
    st; echo f0 > f
    st; echo g1 >> g
    fin
}

sc_pid_exit() {
    # no writes: the follower must exit by itself when P dies (rc 0)
    printf 'a\n' > f
    life 1
    $TAIL -f --pid=$P f > out 2> err & TP=$!
    fin
}

sc_stdin_file() {
    printf 'a\nb\n' > f
    life 2.1
    $TAIL -f -s 0.1 --pid=$P < f > out 2> err & TP=$!
    st; echo c >> f
    st; echo d >> f
    fin
}

sc_stdin_pipe() {
    # -f on a pipe stdin: read to EOF, print, exit (POSIX: -f ignored)
    life 0.1
    { printf 'p\n'; sleep "$STEP"; printf 'q\n'; } | $TAIL -f > out 2> err
    echo $? > rc
}

sc_stdin_pipe_plus_file() {
    printf 'a\n' > f
    life 2.1
    printf 'p\n' | $TAIL -f -s 0.1 --pid=$P - f > out 2> err & TP=$!
    st; echo b >> f
    st; echo c >> f
    fin
}

sc_bytes_from() {
    printf 'abcdef\n' > f
    life 2.1
    $TAIL -f -c +3 -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; printf 'gh' >> f
    st; printf 'ij\n' >> f
    fin
}

sc_lines_zero() {
    printf 'a\nb\n' > f
    life 2.1
    $TAIL -f -n 0 -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; echo c >> f
    st; echo d >> f
    fin
}

sc_zero_term() {
    printf 'a\0b\0' > f
    life 2.1
    $TAIL -z -n 1 -f -s 0.1 --pid=$P f > out 2> err & TP=$!
    st; printf 'c\0' >> f
    st; printf 'd\0' >> f
    fin
}

sc_sleep_fraction() {
    # -s 0.1 polling cadence with several quick appends
    printf 'a\n' > f
    life 2.1
    $TAIL -s 0.1 -f --pid=$P f > out 2> err & TP=$!
    st; echo b >> f; sleep 0.15; echo c >> f; sleep 0.15; echo d >> f
    st
    fin
}

sc_broken_pipe() {
    # the reader goes away; the next follow write must end the follower
    printf 'a\n' > f
    life 3
    ( $TAIL -f -s 0.1 --pid=$P f 2> err | head -n 1 > out
      echo "${PIPESTATUS[0]}" > rc.raw ) &
    TP=$!
    st; echo b >> f
    st; echo c >> f
    st; echo d >> f
    wait "$TP"
    # rc = "died on the write" (GNU 141 by SIGPIPE; Rust Err -> driver
    # 125, or 141 when the shell's pipeline teardown got there first)
    case "$(cat rc.raw)" in 141|125) echo "pipe-death" > rc ;; *) cat rc.raw > rc ;; esac
    wait "$P" 2>/dev/null
}

SCENARIOS="append append_default_interval multi_headers multi_quiet
verbose_single truncate truncate_rewrite desc_rename desc_delete
name_rotate name_replace retry_delete name_noretry_delete name_noretry_multi_reappear
name_noretry_rename_last retry_missing
retry_missing_multi desc_retry_missing pid_exit stdin_file stdin_pipe
stdin_pipe_plus_file bytes_from lines_zero zero_term sleep_fraction
broken_pipe"

if [ "${1:-}" = "--scenario" ]; then
    "sc_$2"
    exit 0
fi

# ------------------------------------------------------------------- runner
HERE="$(cd "$(dirname "$0")" && pwd)"
RUST2="$(cd "$HERE/../.." && pwd)"
TREE="$(cd "$RUST2/.." && pwd)"
RUST_BIN="${RUST_BIN:-$RUST2/target/debug/winuxcmd-driver.exe}"
ART="$RUST2/target/tail-follow"
OUTFILE="${OUTFILE:-$HERE/tail-follow.out}"
if [ -z "${TREE_MNT:-}" ]; then
    case "$TREE" in
        /[a-zA-Z]/*) TREE_MNT="/mnt/$(printf %s "${TREE:1:1}" | tr 'A-Z' 'a-z')${TREE:2}" ;;
        [a-zA-Z]:/*) TREE_MNT="/mnt/$(printf %s "${TREE:0:1}" | tr 'A-Z' 'a-z')${TREE:2}" ;;
        *) TREE_MNT="$TREE" ;;
    esac
fi
SELF_MNT="$TREE_MNT/rust2/gate/wire/tail-follow.sh"
ART_MNT="$TREE_MNT/rust2/target/tail-follow"

[ "$#" -gt 0 ] && SCENARIOS="$*"
rm -rf "$ART"
mkdir -p "$ART"

# GNU side: one WSL script running every scenario on ext4, then copying the
# artifacts back to the tree mirror.
{
    echo '#!/bin/bash'
    echo "export STEP=$STEP TAIL=tail"
    for s in $SCENARIOS; do
        echo "rm -rf /tmp/wc-tailf/$s; mkdir -p /tmp/wc-tailf/$s && cd /tmp/wc-tailf/$s"
        echo "timeout 20 bash $SELF_MNT --scenario $s"
        echo "mkdir -p $ART_MNT/gnu/$s; cp out err rc $ART_MNT/gnu/$s/ 2>/dev/null"
    done
    echo "rm -rf /tmp/wc-tailf"
} > "$ART/gnu.sh"
MSYS_NO_PATHCONV=1 wsl bash "$ART_MNT/gnu.sh" >/dev/null 2>&1 &
GNU_JOB=$!

# Rust side (in parallel: separate machines' worth of scratch).
for s in $SCENARIOS; do
    d="$ART/rust/$s"
    mkdir -p "$d"
    ( cd "$d" && TAIL="$RUST_BIN tail" STEP="$STEP" timeout 20 bash "$HERE/tail-follow.sh" --scenario "$s" ) >/dev/null 2>&1
    [ -f "$d/rc" ] || echo 124 > "$d/rc"
done
wait "$GNU_JOB"

# documented deviations (rust2/crates/winux-cmds/src/tail/follow.rs doc)
KNOWN=" "
pass=0; fail=0; known=0
{
    echo "# tail-follow.sh results (GNU coreutils 9.4 under WSL vs rust2 driver)"
    echo "# STEP=$STEP; columns: scenario | rc gnu/rust | stdout | stderr | verdict"
    for s in $SCENARIOS; do
        g="$ART/gnu/$s"; r="$ART/rust/$s"
        grc=$(cat "$g/rc" 2>/dev/null || echo MISSING)
        rrc=$(cat "$r/rc" 2>/dev/null || echo MISSING)
        if cmp -s "$g/out" "$r/out"; then o="="; else o="DIFF"; fi
        if cmp -s "$g/err" "$r/err"; then e="="; else e="DIFF"; fi
        if [ "$grc" = "$rrc" ] && [ "$o" = "=" ] && [ "$e" = "=" ]; then
            v=PASS; pass=$((pass+1))
        elif [ "${KNOWN#* $s }" != "$KNOWN" ]; then
            v=KNOWN-DEVIATION; known=$((known+1))
        else
            v=FAIL; fail=$((fail+1))
        fi
        printf '%-26s rc %s/%s  out:%s  err:%s  %s\n' "$s" "$grc" "$rrc" "$o" "$e" "$v"
    done
    echo "# totals: pass=$pass fail=$fail known-deviation=$known"
    echo
    echo "# ---- GNU transcripts (stdout via cat -A, then stderr) ----"
    for s in $SCENARIOS; do
        g="$ART/gnu/$s"; r="$ART/rust/$s"
        echo "== $s (rc $(cat "$g/rc" 2>/dev/null))"
        cat -A "$g/out" 2>/dev/null
        echo "-- stderr"
        cat "$g/err" 2>/dev/null
        if ! cmp -s "$g/out" "$r/out" || ! cmp -s "$g/err" "$r/err" || \
           [ "$(cat "$g/rc" 2>/dev/null)" != "$(cat "$r/rc" 2>/dev/null)" ]; then
            echo "-- RUST (rc $(cat "$r/rc" 2>/dev/null)) stdout"
            cat -A "$r/out" 2>/dev/null
            echo "-- RUST stderr"
            cat "$r/err" 2>/dev/null
        fi
    done
} > "$OUTFILE"
head -n $((3 + $(echo $SCENARIOS | wc -w) + 1)) "$OUTFILE"
[ "$fail" -eq 0 ]
