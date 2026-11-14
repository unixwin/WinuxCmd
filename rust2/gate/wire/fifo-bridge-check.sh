#!/usr/bin/env bash
# gate/wire/fifo-bridge-check.sh — #1038 fifo-bridge cross-side probe.
#
# The Rust mkfifo/mknod create the same on-disk `!<fifo>` marker as the 1.x
# C++ body. The bridge that turns a marker into a blocking named-pipe
# rendezvous lives in TWO places: the C++ file_io layer (every applet that
# still runs the 1.x body) and rust2's fs_win.rs (the wired commands that
# open operand files: digestsum, paste, tee, stat, find). A wire is only
# honest if both sides rendezvous, so this probe exercises four pairs:
#   A  Rust-created marker -> C++ reader  + C++ writer
#   B  C++-created marker  -> Rust reader (md5sum) + Rust writer (tee)
#   C  Rust-created marker -> Rust reader + Rust writer
#   D  C++-created marker  -> C++ reader + Rust writer
# Each case is bounded by a watchdog so a broken rendezvous fails loudly
# instead of hanging the run.
#
# Usage (Git Bash / niubash, from anywhere):
#   RUST_BIN=<wired exe> CPP_BIN=<pre-wire exe> bash gate/wire/fifo-bridge-check.sh
# Both default to the local build-vs tree (the same executable when CPP_BIN
# is not given — then A/B/D measure the Rust-vs-Rust path only and the log
# says so).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
TREE="$(cd "$HERE/../../.." && pwd)"
NEW="${RUST_BIN:-$TREE/build-vs/winuxcmd.exe}"
OLD="${CPP_BIN:-$TREE/build-vs/usr/bin/winuxcmd.exe}"
# Absolute native spellings BEFORE the cd into the scratch dir (niubash
# resolves a native child through the drive form, not the shell one).
NEW="$(cygpath -m "$(cd "$(dirname "$NEW")" && pwd)")/$(basename "$NEW")"
OLD="$(cygpath -m "$(cd "$(dirname "$OLD")" && pwd)")/$(basename "$OLD")"
WORK="${FIFO_BRIDGE_WORK:-$TREE/rust2/target/fifo-bridge}"
if [ "$NEW" = "$OLD" ]; then
    echo "note: RUST_BIN == CPP_BIN; the C++ legs are the same binary" >&2
fi
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"

fail=0
check() { # name file want
    local name="$1" file="$2" want="$3"
    if [ -f "$file" ] && [ "$(cat "$file")" = "$want" ]; then
        printf '%-28s PASS  %s\n' "$name" "$(cat "$file")"
    else
        printf '%-28s FAIL  got=%s want=%s\n' "$name" "$(cat "$file" 2>/dev/null)" "$want"
        fail=$((fail+1))
    fi
}

check_md5() { # name md5sum-output-file expected-hash
    local name="$1" file="$2" want="$3" got
    got="$(cut -d' ' -f1 "$file" 2>/dev/null)"
    if [ -f "$file" ] && [ "$got" = "$want" ]; then
        printf '%-28s PASS  md5=%s\n' "$name" "$got"
    else
        printf '%-28s FAIL  got=%s want=%s\n' "$name" "$got" "$want"
        fail=$((fail+1))
    fi
}

wait_read() { # pid: bounded wait for a background reader
    local i=0
    while kill -0 "$1" 2>/dev/null; do
        i=$((i+1))
        if [ "$i" -gt 200 ]; then
            kill "$1" 2>/dev/null
            return 1
        fi
        sleep 0.05
    done
    wait "$1"
}

# ---- A: Rust mkfifo marker, C++ reader + C++ writer ---------------------
"$NEW" mkfifo pipeA || fail=$((fail+1))
# Read the marker bytes with Python (raw file IO): every WinuxCmd reader
# bridges the marker and would block on the rendezvous, which is the point
# of the next two lines.
python -c "import sys; d=open('pipeA','rb').read(); sys.exit(0 if d==b'!<fifo>' else 1)" || {
    echo "A marker bytes wrong"
    fail=$((fail+1))
}
"$OLD" cat pipeA > outA.txt & rp=$!
sleep 0.4
printf 'hello-from-cpp' | "$OLD" dd of=pipeA status=none
wait_read "$rp"; check "A rust-mkfifo cpp<-cpp" outA.txt hello-from-cpp

# ---- B: C++ mkfifo marker, Rust reader + Rust writer -------------------
"$OLD" mkfifo pipeB || fail=$((fail+1))
"$NEW" md5sum pipeB > outB.txt & rp=$!
sleep 0.4
printf 'hello-from-rust' | "$NEW" tee pipeB > /dev/null
wait_read "$rp"
printf 'hello-from-rust' > wantB.txt
"$NEW" md5sum wantB.txt | cut -d' ' -f1 > wantB.md5
check_md5 "B cpp-mkfifo rust<-rust" outB.txt "$(cat wantB.md5)"

# ---- C: Rust marker, Rust reader + Rust writer -------------------------
"$NEW" mkfifo pipeC || fail=$((fail+1))
"$NEW" md5sum pipeC > outC.txt & rp=$!
sleep 0.4
printf 'hello-rust-both' | "$NEW" tee pipeC > /dev/null
wait_read "$rp"
printf 'hello-rust-both' > wantC.txt
"$NEW" md5sum wantC.txt | cut -d' ' -f1 > wantC.md5
check_md5 "C rust-mkfifo rust<-rust" outC.txt "$(cat wantC.md5)"

# ---- D: C++ marker, C++ reader + Rust writer ---------------------------
"$OLD" mkfifo pipeD || fail=$((fail+1))
"$OLD" cat pipeD > outD.txt & rp=$!
sleep 0.4
printf 'hello-rust-writes-cpp' | "$NEW" tee pipeD > /dev/null
wait_read "$rp"; check "D cpp-mkfifo cpp<-rust" outD.txt hello-rust-writes-cpp

printf 'fifo-bridge: %s\n' "$([ "$fail" -eq 0 ] && echo PASS || echo "FAIL($fail)")"
exit "$fail"
