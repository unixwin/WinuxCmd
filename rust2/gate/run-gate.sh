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
#   mode : strict | exit | win | posix | posix-exit   (see below)
#   setup: POSIX sh snippet run inside the per-side scratch dir (may be empty)
#   env  : comma-separated K=V assignments (may be empty)
#   stdin: literal bytes for stdin (no newlines in cases; may be empty)
#
# Modes:
#   strict     : stdout bytes AND exit status must equal GNU; C++ equality recorded
#   exit       : exit status must equal GNU; stdout is platform-form-bound (recorded)
#   win        : no GNU comparison (platform-inherent output); Rust vs C++ status
#   posix      : strict comparison, but a failure counts as PENDING, not FAIL —
#                expected until the native_path Rust port (winux-core/path.rs,
#                the shell-namespace mapper behind niubash#346) lands. Flip the
#                run with POSIX_STRICT=1 once the port is in to gate them.
#   posix-exit : exit-mode comparison under the same pending semantics.
#
# --posix-mode (shell-namespace harness mode, #1488 gate posix round):
#   Runs case tables from gate/cases-posix/ instead of gate/cases/, into a
#   separate ledger, with the shell-namespace fixture env applied to the
#   Windows sides: WINUXSH_ROOT (bare-/ and /usr/... spellings map into the
#   root), TMPDIR (/tmp and /var/tmp), HOME (/home maps to HOME's parent —
#   native_path.cppm:shell_tree_map_w). Case paths are written in POSIX
#   spelling; the GNU side resolves them natively (real /tmp under its own
#   sandbox prefix), the Windows sides must resolve them through the product
#   mapper. POSIX args handed to the Windows sides go through PowerShell, NOT
#   the local bash: the agent bash here is niubash-family and its MSYS
#   runtime rewrites POSIX spellings for native children (/tmp -> %TEMP%,
#   /usr/bin/X -> niubash-root) with MSYS_NO_PATHCONV and
#   MSYS2_ARG_CONV_EXCL both ineffective (measured 2026-11-12, see
#   gate/PROBE-METHODOLOGY.md). Python's subprocess in the qc layer is
#   verbatim by construction and needs no such detour.
#
#   posix-mode setup column convention: RELATIVE shell snippets run under
#   bash as usual (the MSYS layer never rewrites relative tokens); anything
#   with a POSIX spelling must be a `;`-separated list of simple applet
#   invocations (no quotes/redirections), which the harness runs THROUGH THE
#   SIDE'S OWN EXE — the mapper under test creates the fixture, exactly like
#   rubash driving its coreutils. The GNU side always runs setup as a plain
#   sh snippet (native oracle).
#
# Case-table names vs program names: a TSV stem may differ from the program
# name all three sides must invoke. `bracket.tsv` tests the `[` program —
# invoking the stem "bracket" instead silently turned all 26 rows into
# command-not-found on EVERY side (rc 127 == 127 == 127), i.e. vacuous-green
# (found by the #1488 QC sweep, 2026-11-10). INVOCATION_NAME maps the TSV
# stem to the program name passed to Rust dispatch / C++ dispatcher /
# GNU `env`; the ledger keeps the TSV stem as the grouping label.
#
# Usage: bash gate/run-gate.sh [--posix-mode] [command ...]
set -u

# invocation program name for a case-table command stem
invocation_name() {
    case "$1" in
        bracket) printf '[' ;;
        *) printf '%s' "$1" ;;
    esac
}

GATE_DIR="$(cd "$(dirname "$0")" && pwd)"
RUST2="$(cd "$GATE_DIR/.." && pwd)"
TREE="$(cd "$RUST2/.." && pwd)"
RUST_BIN="${RUST_BIN:-$RUST2/target/debug/winuxcmd-driver.exe}"
CPP_BIN="${CPP_BIN:-$TREE/build-vs/usr/bin/winuxcmd.exe}"
# Isolate new runs without overwriting earlier ledgers or fixture evidence.
ART="${GATE_ARTIFACT_DIR:-$RUST2/target/gate}"
GNU_REMOTE_ROOT="${GATE_GNU_REMOTE_ROOT:-/tmp/winuxcmd-gate}"
GNU_MIRROR="$ART/gnu"

# ---- --posix-mode flag + shell-namespace fixture ----
POSIX_MODE=0
COMMANDS=()
for a in "$@"; do
    if [ "$a" = "--posix-mode" ]; then
        POSIX_MODE=1
    else
        COMMANDS+=("$a")
    fi
done
POSIX_STRICT="${POSIX_STRICT:-0}"
CASES_DIR="$GATE_DIR/cases"
LEDGER="$ART/ledger.tsv"
# POSIX spellings the posix corpus may write to, and their GNU-side sandbox
# prefixes (cleaned after each GNU case; the Windows sides land inside the
# fixture dirs under target/gate/posix/, owned by this harness).
POSIX_GNU_PREFIXES="/tmp/winuxcmd-gate-posix /var/tmp/winuxcmd-gate-posix /home/winuxcmd-gate"
if [ "$POSIX_MODE" -eq 1 ]; then
    CASES_DIR="$GATE_DIR/cases-posix"
    LEDGER="$ART/ledger-posix.tsv"
    POSIX_ART="$ART/posix"
    POSIX_ROOT="$POSIX_ART/root"    # WINUXSH_ROOT: /, /usr/... land here
    POSIX_TMP="$POSIX_ART/tmp"      # TMPDIR: /tmp and /var/tmp land here
    POSIX_HOME="$POSIX_ART/home/u"  # HOME: /home/<name> lands in HOME's parent
    # fixture env handed to the Windows sides (see native_path.cppm
    # shell_tree_map_w: __RUBASH_SHELL_ROOT beats NIU_ROOT beats WINUXSH_ROOT;
    # we set the neutral __RUBASH_SHELL_ROOT so no host NIU_ROOT can shadow
    # the fixture; TMPDIR must beat TEMP/TMP which it does by priority).
    POSIX_FIXENV="__RUBASH_SHELL_ROOT=$POSIX_ROOT,TMPDIR=$POSIX_TMP,HOME=$POSIX_HOME"
    mkdir -p "$POSIX_ROOT/usr/bin" "$POSIX_TMP/var" "$POSIX_ART/home"
    # /usr/bin/<applet> exe-fold cases need the leaf to exist under the root;
    # mirror the wired exes the way the real install root does.
    for leaf in seq echo; do
        src="$CPP_BIN"
        [ -f "$src" ] || src="$RUST_BIN"
        cp -f "$src" "$POSIX_ROOT/usr/bin/$leaf.exe" 2>/dev/null
    done
    printf 'posix-mode fixture: root=%s tmp=%s home-parent=%s\n' \
        "$POSIX_ROOT" "$POSIX_TMP" "$POSIX_ART/home"
fi
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
# The artifact directory can be outside the default target/gate tree.
# Use its WSL view for BOTH script execution and result copies.
if [ -z "${ART_MNT:-}" ]; then
    case "$ART" in
        "$TREE"/*) ART_MNT="$TREE_MNT/${ART#"$TREE"/}" ;;
        /[a-zA-Z]/*) ART_MNT="/mnt/$(printf %s "${ART:1:1}" | tr 'A-Z' 'a-z')${ART:2}" ;;
        [a-zA-Z]:/*) ART_MNT="/mnt/$(printf %s "${ART:0:1}" | tr 'A-Z' 'a-z')${ART:2}" ;;
        *) ART_MNT="$ART" ;;
    esac
fi

mkdir -p "$ART/scratch" "$GNU_MIRROR"

# The GNU leg invokes native wsl.exe with leading-slash POSIX argv
# ("wsl.exe --exec /bin/bash /mnt/<drive>/..."). Git Bash (MSYS2) rewrites
# such arguments into Windows-native paths before the spawn — measured on
# the CI runner: /bin/bash arrived as D:/Git/usr/bin/bash and the distro
# failed execvpe for every case, so the whole GNU side scored MISSING
# while wsl.exe itself "succeeded" (exit swallowed). MSYS_NO_PATHCONV=1
# disables that rewriting under Git Bash and is ignored by the niubash
# family, where this call shape never converted in the first place.
export MSYS_NO_PATHCONV=1

# Preflight: prove one GNU-side roundtrip through the exact call shape
# below before spending minutes on the corpus, so a broken invocation
# fails here in seconds instead of MISSING-ing every row.
wsl.exe --exec /bin/bash -c "echo ok > '$ART_MNT/gnu-preflight'"
if [ ! -f "$ART/gnu-preflight" ]; then
    echo "FATAL: GNU-side WSL roundtrip failed (see the MSYS_NO_PATHCONV note above)" >&2
    exit 3
fi
rm -f "$ART/gnu-preflight"

if command -v timeout >/dev/null 2>&1; then WIN_TIMEOUT="timeout 10"; else WIN_TIMEOUT=""; fi

pass=0; fail=0; winpass=0; winfail=0
posixpass=0; posixpending=0
FAILED_CASES=()

# quote a word for single-quoted shell embedding on both sides
qsh() {
    local s="$1"
    s="${s//\'/\'\\\'\'}"
    printf "'%s'" "$s"
}

# quote a word for a PowerShell single-quoted string literal ('' escapes ')
psq() {
    local s="$1"
    s="${s//\'/\'\'}"
    printf "'%s'" "$s"
}

# MSYS-form path -> Windows form for the PowerShell side (no-op fallback
# when cygpath is absent, e.g. running the gate from a native shell)
winpath() {
    local w
    if w=$(cygpath -w "$1" 2>/dev/null); then
        printf '%s' "$w"
    else
        printf '%s' "$1"
    fi
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

# Quote a word for a raw Windows command line (Start-Process -ArgumentList
# takes a single raw string in Windows PowerShell 5.1 and joins arrays
# WITHOUT quoting, so the harness quotes each token itself). Corpus
# restriction: no embedded double quotes in posix-mode args (documented in
# PROBE-METHODOLOGY.md).
cmdq() {
    local s="$1"
    case "$s" in
        *" "*) printf '"%s"' "$s" ;;
        *'"'*) printf '"%s"' "$s" ;;
        *) printf '%s' "$s" ;;
    esac
}

# join tokens into a raw Windows command-line argument string
join_cmdq() {
    local out t
    out="$(cmdq "$1")"
    shift
    for t in "$@"; do
        out="$out $(cmdq "$t")"
    done
    printf '%s' "$out"
}

# POSIX-mode setup shape check: a `;`-separated list of simple applet
# invocations (no quotes, redirections, pipes, or shell builtins) — setup
# runs THROUGH THE SIDE'S OWN EXE so the mapper under test materializes the
# fixture, exactly like rubash driving its coreutils. Anything else is a
# harness bug and fails the case loudly.
posix_setup_is_applet_list() {
    local s="$1"
    case "$s" in
        *'"'*) return 1 ;;
        *"'"*) return 1 ;;
        *'`'*) return 1 ;;
        *'|'*|*'&'*) return 1 ;;
        *'<'*|*'>'*) return 1 ;;
        *'('*|*')'*) return 1 ;;
        *'\'*|*'$'*) return 1 ;;
    esac
    local seg
    local IFS=';'
    for seg in $s; do
        seg="${seg# }"
        case "$seg" in
            [A-Za-z0-9_]*) ;;
            *) return 1 ;;
        esac
    done
    return 0
}

# Run the windows sides (rust/cpp) for a posix-mode case. POSIX-spelled args
# go through PowerShell verbatim (the local bash is niubash-family and its
# MSYS runtime rewrites POSIX spellings for native children — see
# PROBE-METHODOLOGY.md channel 1; measured 2026-11-12): Start-Process with
# raw argument string and FILE redirects keeps both argv bytes and
# stdout/stderr bytes untouched (PS text pipelines would re-encode).
run_posix_win_side() {
    local side="$1" scase="$2" prog="$3" setup="$4" envstr="$5" stdin="$6"
    shift 6
    local -a args
    args=("$@")
    local sdir="$scase.$side"
    mkdir -p "$sdir"
    local exe
    if [ "$side" = rust ]; then exe="$RUST_BIN"; else exe="$CPP_BIN"; fi
    local ps1="$sdir/run.ps1"

    if [ -n "$setup" ] && ! posix_setup_is_applet_list "$setup"; then
        printf 'posix-mode setup must be an applet list, got: %s (case %s)\n' \
            "$setup" "$scase" >&2
        return 9
    fi

    local exe_win sdir_win
    exe_win=$(winpath "$exe")
    sdir_win=$(winpath "$sdir")
    {
        printf '$ErrorActionPreference = "Continue"\n'
        # shell-namespace fixture (native_path.cppm shell_tree_map_w):
        # __RUBASH_SHELL_ROOT beats NIU_ROOT/WINUXSH_ROOT so no host value
        # can shadow the fixture; TMPDIR beats TEMP/TMP by priority. Values
        # are Windows-form: the product mapper joins native bases.
        printf '$env:__RUBASH_SHELL_ROOT = %s\n' "$(psq "$(winpath "$POSIX_ROOT")")"
        printf '$env:TMPDIR = %s\n' "$(psq "$(winpath "$POSIX_TMP")")"
        printf '$env:HOME = %s\n' "$(psq "$(winpath "$POSIX_HOME")")"
        local kv k v
        if [ -n "$envstr" ]; then
            local items
            IFS=',' read -ra items <<< "$envstr"
            for kv in "${items[@]}"; do
                k="${kv%%=*}"; v="${kv#*=}"
                [ "$k" = "$kv" ] && continue
                printf '$env:%s = %s\n' "$k" "$(psq "$v")"
            done
        fi
        printf 'Set-Location -LiteralPath %s\n' "$(psq "$sdir_win")"

        # ---- setup through the side exe (ignored rc, like the bash path) ----
        if [ -n "$setup" ]; then
            local seg
            local IFS=';'
            for seg in $setup; do
                seg="${seg# }"
                local -a toks=()
                local w
                IFS=$' \t'
                for w in $seg; do toks+=("$w"); done
                IFS=';'
                local segargs
                segargs=$(join_cmdq "${toks[0]}" "${toks[@]:1}")
                printf 'Start-Process -FilePath %s -ArgumentList %s -Wait -NoNewWindow | Out-Null\n' \
                    "$(psq "$exe_win")" "$(psq "$segargs")"
            done
        fi

        # ---- gated invocation ----
        local argstr
        argstr="$(cmdq "$prog")"
        local a
        for a in "${args[@]}"; do
            argstr="$argstr $(cmdq "$a")"
        done
        if [ -n "$stdin" ]; then
            printf 'Set-Content -LiteralPath %s -Value %s -NoNewline -Encoding Ascii\n' \
                "$(psq "$sdir_win/.stdin")" "$(psq "$stdin")"
            printf '$p = Start-Process -FilePath %s -ArgumentList %s -Wait -PassThru -NoNewWindow -RedirectStandardInput %s -RedirectStandardOutput %s -RedirectStandardError %s\n' \
                "$(psq "$exe_win")" "$(psq "$argstr")" \
                "$(psq "$sdir_win/.stdin")" "$(psq "$sdir_win/.out")" "$(psq "$sdir_win/.err")"
        else
            printf '$p = Start-Process -FilePath %s -ArgumentList %s -Wait -PassThru -NoNewWindow -RedirectStandardOutput %s -RedirectStandardError %s\n' \
                "$(psq "$exe_win")" "$(psq "$argstr")" \
                "$(psq "$sdir_win/.out")" "$(psq "$sdir_win/.err")"
        fi
        printf 'Set-Content -LiteralPath %s -Value $p.ExitCode -Encoding Ascii\n' \
            "$(psq "$sdir_win/.rc")"
    } > "$ps1"
    powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$ps1" 2>/dev/null || printf '%s' "$ps1")" >/dev/null 2>&1
}

run_case() {
    local cmd="$1" id="$2" mode="$3" setup="$4" envstr="$5" stdin="$6"
    shift 6
    # Keep declaration separate: compound local arrays lose backslashes
    # on the current Niubash host (for example, '\x41' becomes 'x41').
    local -a args
    args=("$@")

    local scase="$ART/scratch/$id"
    rm -rf "$scase" "$scase.rust" "$scase.cpp" "$scase.gnu.sh" "$GNU_MIRROR/$id"
    mkdir -p "$scase" "$GNU_MIRROR/$id"

    local envpre
    envpre=$(env_prefix "$envstr")
    local prog
    prog=$(invocation_name "$cmd")

    # ---------- windows sides (Rust driver, C++ dispatcher) ----------
    if [ "$POSIX_MODE" -eq 1 ]; then
        run_posix_win_side rust "$scase" "$prog" "$setup" "$envstr" "$stdin" "${args[@]}"
        run_posix_win_side cpp "$scase" "$prog" "$setup" "$envstr" "$stdin" "${args[@]}"
    else
        local side exe sdir exe_q
        for side in rust cpp; do
            sdir="$scase.$side"
            mkdir -p "$sdir"
            if [ "$side" = rust ]; then exe="$RUST_BIN"; else exe="$CPP_BIN"; fi
            exe_q=$(qsh "$exe")
            {
                # MSYS2 path conversion rewrites POSIX-looking arguments (/a/b/,
                # /dev/null, ...) for native children, which would corrupt pattern
                # and path operands before the gate compares them. The library
                # contract handles POSIX drive spellings itself (fs_win::api_path
                # folds /d/... forms), so the gate measures raw argv both sides.
                # The C++ package bin dir joins PATH for both Windows sides: it is
                # the WinuxCmd /usr/bin analog, and child-spawning commands (nohup,
                # timeout, env, xargs) exec their siblings through it exactly the
                # way a POSIX /usr/bin package resolves them.
                printf 'export MSYS_NO_PATHCONV=1\n'
                printf 'export PATH="$PATH:%s"\n' "$(dirname "$CPP_BIN")"
                [ -n "$setup" ] && printf '%s\n' "$setup"
                printf 'printf %%s %s > .stdin\n' "$(qsh "$stdin")"
                printf '%s %s %s' "$envpre" "$WIN_TIMEOUT" "$exe_q"
                printf ' %s' "$(qsh "$prog")"
                local a
                for a in "${args[@]}"; do
                    printf ' %s' "$(qsh "$a")"
                done
                printf ' < .stdin > .out 2> .err\n'
                printf 'echo $? > .rc\n'
            } > "$sdir/run.sh"
            ( cd "$sdir" && bash run.sh >/dev/null 2>&1 )
        done
    fi

    # ---------- landing-leak check (posix mode) ----------
    # A pending case must never be scored by an ACCIDENTAL literal landfall:
    # mkdir -p on an unmapped /tmp spelling happily creates D:/tmp/... and
    # matches GNU's rc, so the exact niubash#346 landfall bug would pass as
    # posix-ok. After the Windows sides run, walk each POSIX-spelled
    # operand's tail against the cwd-drive roots; anything existing beyond
    # the first component is a leak -> pending (never ok), and is removed so
    # reruns start clean.
    local landing="ok"
    if [ "$POSIX_MODE" -eq 1 ]; then
        local drive="${TREE:0:2}"
        local a tail zone cand leak_zone=""
        local -a operands=("${args[@]}")
        if [ -n "$setup" ]; then
            # tokenize the setup so `touch /tmp/x` contributes the spelling
            local w
            for w in $setup; do operands+=("$w"); done
        fi
        for a in "${operands[@]}"; do
            case "$a" in
                /tmp/*) tail="${a#/tmp/}"; zone="$drive/tmp" ;;
                /var/tmp/*) tail="${a#/var/tmp/}"; zone="$drive/var/tmp" ;;
                /home/*) tail="${a#/home/}"; zone="$drive/home" ;;
                /usr/*) tail="${a#/usr/}"; zone="$drive/usr" ;;
                *) continue ;;
            esac
            # walk the tail from full path up to (not including) the zone
            # root: an unmapped spelling lands LITERALLY on the cwd drive,
            # and any existing component beyond the pre-existing zone root
            # can only be this case's landfall (fixture lives elsewhere).
            cand="$zone/$tail"
            leak_zone=""
            while [ -n "$tail" ] && [ "$cand" != "$zone" ]; do
                if [ -e "$cand" ]; then
                    # keep walking: the TOPMOST existing component is the
                    # landfall root; removing it clears deeper leftovers too
                    leak_zone="$cand"
                    landing="leak"
                fi
                cand="${cand%/*}"
            done
            if [ "$landing" = "leak" ] && [ -n "$leak_zone" ]; then
                rm -rf "$leak_zone"
                break
            fi
        done
        if [ "$landing" = "leak" ]; then
            rm -rf "$leak_zone"
        fi
    fi

    # ---------- GNU side (WSL, native ext4 scratch) ----------
    local gdir="$GNU_REMOTE_ROOT/$id"
    {
        echo '#!/bin/bash'
        printf 'rm -rf %s; mkdir -p %s || exit 97\n' "$(qsh "$gdir")" "$(qsh "$gdir")"
        printf 'cd %s || exit 97\n' "$(qsh "$gdir")"
        if [ "$POSIX_MODE" -eq 1 ]; then
            # posix corpus writes under fixed sandbox prefixes; pre-create the
            # prefix dirs so read/probe cases see the same tree shape the
            # fixture pre-creates on the Windows side.
            printf 'mkdir -p'
            local pfx
            for pfx in $POSIX_GNU_PREFIXES; do
                printf ' %s' "$(qsh "$pfx")"
            done
            printf ' || exit 97\n'
        fi
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
        printf ' %s' "$prog"
        local a
        for a in "${args[@]}"; do
            printf ' %s' "$(qsh "$a")"
        done
        printf ' < .stdin > .out 2> .err\n'
        printf 'echo $? > .rc\n'
        if [ "$POSIX_MODE" -eq 1 ]; then
            # sandbox the prefixes the corpus writes to (see header); each
            # prefix its own argument — one quoted blob would rm a single
            # space-named directory and leave the real prefixes behind
            printf 'rm -rf'
            local pfx
            for pfx in $POSIX_GNU_PREFIXES; do
                printf ' %s' "$(qsh "$pfx")"
            done
            printf '\n'
        fi
        printf 'cp .out .err .rc %s\n' "$(qsh "$ART_MNT/gnu/$id/")"
    } > "$scase.gnu.sh"
    wsl.exe --exec /bin/bash "$ART_MNT/scratch/$id.gnu.sh" >/dev/null 2>&1
    # wsl.exe can return before the artifact copy settles
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        [ -f "$GNU_MIRROR/$id/.rc" ] && break
        sleep 0.2
    done

    # ---------- read artifacts ----------
    local r_rc c_rc g_rc
    r_rc=$(cat "$scase.rust/.rc" 2>/dev/null || echo MISSING)
    c_rc=$(cat "$scase.cpp/.rc" 2>/dev/null || echo MISSING)
    g_rc=$(cat "$GNU_MIRROR/$id/.rc" 2>/dev/null || echo MISSING)

    # Shell variables cannot represent NUL. Compare captured files directly.
    local cpp_match=no cpp_out_match=no gnu_out_match=no gnu_rc_match=no
    cmp -s "$scase.rust/.out" "$scase.cpp/.out" && cpp_out_match=yes
    cmp -s "$scase.rust/.out" "$GNU_MIRROR/$id/.out" && gnu_out_match=yes
    [ "$r_rc" = "$g_rc" ] && gnu_rc_match=yes
    { [ "$cpp_out_match" = yes ] && [ "$r_rc" = "$c_rc" ]; } && cpp_match=yes

    local ok=0 note=""
    case "$mode" in
        strict)     [ "$gnu_out_match" = yes ] && [ "$r_rc" = "$g_rc" ] && ok=1 ;;
        exit)       [ "$r_rc" = "$g_rc" ] && ok=1 ;;
        win)        [ "$r_rc" = "$c_rc" ] && ok=1 ;;
        posix)      [ "$gnu_out_match" = yes ] && [ "$r_rc" = "$g_rc" ] && ok=1 ;;
        posix-exit) [ "$r_rc" = "$g_rc" ] && ok=1 ;;
        note)       ok=1; note="record-only" ;;  # documented deviation, never gates
        *)          ok=0; note="bad-mode($mode)" ;;
    esac
    if [ "$mode" != note ]; then
        case "$r_rc" in
            ''|*[!0-9]*) ok=0; note="missing-or-invalid-rust-status" ;;
        esac
        if [ "$mode" = win ]; then
            case "$c_rc" in
                ''|*[!0-9]*) ok=0; note="missing-or-invalid-cpp-status" ;;
            esac
        else
            case "$g_rc" in
                ''|*[!0-9]*) ok=0; note="missing-or-invalid-gnu-status" ;;
            esac
        fi
    fi

    if [ -s "$GNU_MIRROR/$id/.err" ] && [ "$mode" != win ]; then
        {
            printf '=== %s (%s) GNU stderr ===\n' "$id" "$cmd"
            cat "$GNU_MIRROR/$id/.err"
            printf '\n'
        } >> "$ART/gnu-stderr-samples.txt"
    fi

    if [ "$mode" = win ]; then
        if [ $ok -eq 1 ]; then
            winpass=$((winpass+1))
        else
            winfail=$((winfail+1))
            FAILED_CASES+=("$cmd/$id($mode) r_rc=$r_rc c_rc=$c_rc")
        fi
    elif [ "$mode" = posix ] || [ "$mode" = posix-exit ]; then
        # pending semantics: failures are expected until the native_path Rust
        # port lands; POSIX_STRICT=1 turns them into hard failures. A landing
        # leak is a pending case too, even when the compared bytes matched.
        if [ "$landing" = "leak" ]; then
            ok=0
        fi
        if [ $ok -eq 1 ]; then
            posixpass=$((posixpass+1))
            note="posix-ok"
        elif [ "$POSIX_STRICT" = "1" ]; then
            fail=$((fail+1))
            note="posix-FAIL"
            FAILED_CASES+=("$cmd/$id($mode) r_rc=$r_rc g_rc=$g_rc stdout files: $scase.rust/.out $GNU_MIRROR/$id/.out")
        else
            posixpending=$((posixpending+1))
            note="posix-pending"
            [ "$landing" = "leak" ] && note="posix-pending(landing=leak)"
        fi
    else
        if [ $ok -eq 1 ]; then
            pass=$((pass+1))
        else
            fail=$((fail+1))
            FAILED_CASES+=("$cmd/$id($mode) r_rc=$r_rc g_rc=$g_rc stdout files: $scase.rust/.out $GNU_MIRROR/$id/.out")
        fi
    fi

    printf '%s|%s|%s|rc(r/g/c)=%s/%s/%s|out(r=g:%s r=c:%s)|%s\n' \
        "$cmd" "$id" "$mode" "$r_rc" "$g_rc" "$c_rc" \
        "$gnu_out_match" "$cpp_match" "$note" >> "$LEDGER"
}

run_command() {
    local cmd="$1"
    local file="$CASES_DIR/$cmd.tsv"
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

: > "$LEDGER"
: > "$ART/gnu-stderr-samples.txt"

if [ "${#COMMANDS[@]}" -gt 0 ]; then
    for c in "${COMMANDS[@]}"; do run_command "$c"; done
else
    for f in "$CASES_DIR"/*.tsv; do
        run_command "$(basename "$f" .tsv)"
    done
fi

echo
echo "==================== GATE SUMMARY ===================="
echo "GNU-gated cases:  pass=$pass fail=$fail"
echo "win-only cases:   pass=$winpass fail=$winfail"
if [ "$POSIX_MODE" -eq 1 ]; then
    echo "posix cases:      pass=$posixpass pending=$posixpending$([ "$POSIX_STRICT" = 1 ] && printf ' (POSIX_STRICT: pending counts as FAIL)')"
fi
if [ ${#FAILED_CASES[@]} -gt 0 ]; then
    echo "FAILED:"
    for f in "${FAILED_CASES[@]}"; do echo "  $f"; done
fi
echo "ledger:             $LEDGER"
echo "gnu stderr samples: $ART/gnu-stderr-samples.txt"
[ "$fail" -eq 0 ] && [ "$winfail" -eq 0 ]
