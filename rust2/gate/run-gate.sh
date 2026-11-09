#!/bin/bash
# gate/run-gate.sh — three-way differential gate for WinuxCmd 2.0 wave1 (#1488).
#
# Sides:
#   RUST : rust2/target/debug/winuxcmd-driver.exe  (library semantics via dispatch)
#   CPP  : build-vs/usr/bin/winuxcmd.exe           (1.x C++ dispatcher, Debug build)
#   GNU  : WSL /usr/bin/<cmd>                      (coreutils 9.4 oracle)
#
# Case table: gate/cases/<cmd>.tsv, TAB-separated columns:
#   id | mode | setup | env | stdin | arg1 | arg2 | ...
#   mode : strict | exit | win   (see below)
#   setup: POSIX sh snippet run inside the per-side scratch dir (may be empty)
#   env  : comma-separated K=V assignments (may be empty)
#   stdin: literal bytes for stdin (no newlines in cases; may be empty)
#
# Modes:
#   strict : stdout bytes AND exit status must equal GNU; C++ equality recorded
#   exit   : exit status must equal GNU; stdout is platform-form-bound (recorded)
#   win    : no GNU comparison (platform-inherent output); Rust vs C++ status
#
# Usage: bash gate/run-gate.sh [command ...]
set -u

GATE_DIR="$(cd "$(dirname "$0")" && pwd)"
RUST2="$(cd "$GATE_DIR/.." && pwd)"
TREE="$(cd "$RUST2/.." && pwd)"
RUST_BIN="${RUST_BIN:-$RUST2/target/debug/winuxcmd-driver.exe}"
CPP_BIN="${CPP_BIN:-$TREE/build-vs/usr/bin/winuxcmd.exe}"
ART="$RUST2/target/gate"
GNU_REMOTE_ROOT="/tmp/winuxcmd-gate"
GNU_MIRROR="$ART/gnu"
# WSL view of this checkout. Derived from $TREE (/d/repo/x -> /mnt/d/repo/x)
# instead of a hard-coded worktree path, so the gate runs from any worktree;
# override with TREE_MNT=... when the drive mapping differs.
if [ -z "${TREE_MNT:-}" ]; then
    case "$TREE" in
        /[a-zA-Z]/*) TREE_MNT="/mnt/$(printf %s "${TREE:1:1}" | tr 'A-Z' 'a-z')${TREE:2}" ;;
        [a-zA-Z]:/*) TREE_MNT="/mnt/$(printf %s "${TREE:0:1}" | tr 'A-Z' 'a-z')${TREE:2}" ;;
        *) TREE_MNT="$TREE" ;;
    esac
fi

mkdir -p "$ART/scratch" "$GNU_MIRROR"

if command -v timeout >/dev/null 2>&1; then WIN_TIMEOUT="timeout 10"; else WIN_TIMEOUT=""; fi

pass=0; fail=0; winpass=0; winfail=0
FAILED_CASES=()

# quote a word for single-quoted shell embedding on both sides
qsh() {
    local s="$1"
    s="${s//\'/\'\\\'\'}"
    printf "'%s'" "$s"
}

# env prefix for the simple command (shell assignments export to children).
# IMPORTANT: assignments must stay UNQUOTED as a whole — bash treats a
# quoted 'K=V' as a command word, not an assignment. Quote only the value
# when it contains characters that need it.
env_prefix() {
    local envstr="$1" out="" kv k v
    [ -z "$envstr" ] && return 0
    local items
    IFS=',' read -ra items <<< "$envstr"
    for kv in "${items[@]}"; do
        k="${kv%%=*}"
        v="${kv#*=}"
        if [ "$k" = "$kv" ]; then
            continue # malformed, no '='
        fi
        case "$v" in
            *[!A-Za-z0-9_@%+=:,./-]*)
                out="$out $k=$(qsh "$v")"
                ;;
            *)
                out="$out $k=$v"
                ;;
        esac
    done
    printf '%s' "$out"
}

run_case() {
    local cmd="$1" id="$2" mode="$3" setup="$4" envstr="$5" stdin="$6"
    shift 6
    local args=("$@")

    local scase="$ART/scratch/$id"
    rm -rf "$scase" "$scase.rust" "$scase.cpp" "$scase.gnu.sh" "$GNU_MIRROR/$id"
    mkdir -p "$scase" "$GNU_MIRROR/$id"

    local envpre
    envpre=$(env_prefix "$envstr")

    # ---------- windows sides (Rust driver, C++ dispatcher) ----------
    local side exe sdir exe_q
    for side in rust cpp; do
        sdir="$scase.$side"
        mkdir -p "$sdir"
        if [ "$side" = rust ]; then exe="$RUST_BIN"; else exe="$CPP_BIN"; fi
        exe_q=$(qsh "$exe")
        {
            [ -n "$setup" ] && printf '%s\n' "$setup"
            printf 'printf %%s %s > .stdin\n' "$(qsh "$stdin")"
            printf '%s %s %s' "$envpre" "$WIN_TIMEOUT" "$exe_q"
            printf ' %s' "$(qsh "$cmd")"
            local a
            for a in "${args[@]}"; do
                printf ' %s' "$(qsh "$a")"
            done
            printf ' < .stdin > .out 2> .err\n'
            printf 'echo $? > .rc\n'
        } > "$sdir/run.sh"
        ( cd "$sdir" && bash run.sh >/dev/null 2>&1 )
    done

    # ---------- GNU side (WSL, native ext4 scratch) ----------
    local gdir="$GNU_REMOTE_ROOT/$id"
    {
        echo '#!/bin/bash'
        printf 'rm -rf %s; mkdir -p %s || exit 97\n' "$(qsh "$gdir")" "$(qsh "$gdir")"
        printf 'cd %s || exit 97\n' "$(qsh "$gdir")"
        [ -n "$setup" ] && printf '%s\n' "$setup"
        printf 'printf %%s %s > .stdin\n' "$(qsh "$stdin")"
        # `env <name>` invokes the real coreutils binary with argv[0] equal to
        # the bare command name, so GNU's program-name prefix in verbose and
        # usage messages matches what the Rust driver prints ("rmdir:", not
        # "/usr/bin/rmdir:"). timeout wraps env so a stuck command is killed.
        printf 'timeout 10 env'
        if [ -n "$envstr" ]; then
            local kv
            local items
            IFS=',' read -ra items <<< "$envstr"
            for kv in "${items[@]}"; do
                printf ' %s' "$(qsh "$kv")"
            done
        fi
        printf ' %s' "$cmd"
        local a
        for a in "${args[@]}"; do
            printf ' %s' "$(qsh "$a")"
        done
        printf ' < .stdin > .out 2> .err\n'
        printf 'echo $? > .rc\n'
        printf 'cp .out .err .rc %s/gnu/%s/\n' "$TREE_MNT/rust2/target/gate" "$id"
    } > "$scase.gnu.sh"
    MSYS_NO_PATHCONV=1 wsl bash "$TREE_MNT/rust2/target/gate/scratch/$id.gnu.sh" >/dev/null 2>&1
    # wsl.exe can return before the artifact copy settles
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        [ -f "$GNU_MIRROR/$id/.rc" ] && break
        sleep 0.2
    done

    # ---------- read artifacts ----------
    local r_out r_rc c_out c_rc g_out g_rc g_err
    r_out=$(cat "$scase.rust/.out" 2>/dev/null; printf x); r_out="${r_out%x}"
    r_rc=$(cat "$scase.rust/.rc" 2>/dev/null || echo MISSING)
    c_out=$(cat "$scase.cpp/.out" 2>/dev/null; printf x); c_out="${c_out%x}"
    c_rc=$(cat "$scase.cpp/.rc" 2>/dev/null || echo MISSING)
    g_out=$(cat "$GNU_MIRROR/$id/.out" 2>/dev/null; printf x); g_out="${g_out%x}"
    g_rc=$(cat "$GNU_MIRROR/$id/.rc" 2>/dev/null || echo MISSING)
    g_err=$(cat "$GNU_MIRROR/$id/.err" 2>/dev/null; printf x); g_err="${g_err%x}"

    local ok=0 note=""
    case "$mode" in
        strict) [ "$r_out" = "$g_out" ] && [ "$r_rc" = "$g_rc" ] && ok=1 ;;
        exit)   [ "$r_rc" = "$g_rc" ] && ok=1 ;;
        win)    [ "$r_rc" = "$c_rc" ] && ok=1 ;;
        note)   ok=1; note="record-only" ;;  # documented deviation, never gates
        *)      ok=0; note="bad-mode($mode)" ;;
    esac

    local cpp_match=no gnu_out_match=no gnu_rc_match=no
    { [ "$r_out" = "$c_out" ] && [ "$r_rc" = "$c_rc" ]; } && cpp_match=yes
    [ "$r_out" = "$g_out" ] && gnu_out_match=yes
    [ "$r_rc" = "$g_rc" ] && gnu_rc_match=yes

    printf '%s|%s|%s|rc(r/g/c)=%s/%s/%s|out(r=g:%s r=c:%s)|%s\n' \
        "$cmd" "$id" "$mode" "$r_rc" "$g_rc" "$c_rc" \
        "$gnu_out_match" "$cpp_match" "$note" >> "$ART/ledger.tsv"

    if [ -n "$g_err" ] && [ "$mode" != win ]; then
        printf '=== %s (%s) GNU stderr ===\n%s\n' "$id" "$cmd" "$g_err" \
            >> "$ART/gnu-stderr-samples.txt"
    fi

    if [ "$mode" = win ]; then
        if [ $ok -eq 1 ]; then
            winpass=$((winpass+1))
        else
            winfail=$((winfail+1))
            FAILED_CASES+=("$cmd/$id($mode) r_rc=$r_rc c_rc=$c_rc")
        fi
    else
        if [ $ok -eq 1 ]; then
            pass=$((pass+1))
        else
            fail=$((fail+1))
            FAILED_CASES+=("$cmd/$id($mode) r_rc=$r_rc g_rc=$g_rc r_out=[${r_out:0:40}] g_out=[${g_out:0:40}]")
        fi
    fi
}

run_command() {
    local cmd="$1"
    local file="$GATE_DIR/cases/$cmd.tsv"
    if [ ! -f "$file" ]; then
        echo "no case table for $cmd ($file)" >&2
        return
    fi
    while IFS= read -r line <&3; do
        [ -z "$line" ] && continue
        case "$line" in '#'*) continue ;; esac
        # split on tabs PRESERVING empty fields (read collapses IFS-whitespace
        # runs, which silently deletes empty setup/env/stdin columns)
        fields=()
        tmp="$line"
        while [[ "$tmp" == *$'\t'* ]]; do
            fields+=("${tmp%%$'\t'*}")
            tmp="${tmp#*$'\t'}"
        done
        fields+=("$tmp")
        id="${fields[0]}"; mode="${fields[1]}"
        setup="${fields[2]:-}"; envstr="${fields[3]:-}"; stdin="${fields[4]:-}"
        args=("${fields[@]:5}")
        # @EMPTY@ encodes a literal empty-string argument
        local i
        for i in "${!args[@]}"; do
            [ "${args[$i]}" = "@EMPTY@" ] && args[$i]=""
        done
        run_case "$cmd" "$id" "$mode" "$setup" "$envstr" "$stdin" "${args[@]}"
    done 3< "$file"
}

: > "$ART/ledger.tsv"
: > "$ART/gnu-stderr-samples.txt"

if [ $# -gt 0 ]; then
    for c in "$@"; do run_command "$c"; done
else
    for f in "$GATE_DIR"/cases/*.tsv; do
        run_command "$(basename "$f" .tsv)"
    done
fi

echo
echo "==================== GATE SUMMARY ===================="
echo "GNU-gated cases:  pass=$pass fail=$fail"
echo "win-only cases:   pass=$winpass fail=$winfail"
if [ ${#FAILED_CASES[@]} -gt 0 ]; then
    echo "FAILED:"
    for f in "${FAILED_CASES[@]}"; do echo "  $f"; done
fi
echo "ledger:             $ART/ledger.tsv"
echo "gnu stderr samples: $ART/gnu-stderr-samples.txt"
[ "$fail" -eq 0 ] && [ "$winfail" -eq 0 ]
