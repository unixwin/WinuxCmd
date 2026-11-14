#!/bin/bash
# gate/preflight-gnu.sh — prove the GNU oracle side is real before scoring.
#
# The 2026 #1632 precedent: a gate run whose GNU leg never came up quietly
# compared the Rust side against `MISSING`, which is red for strict rows but
# vacuous for the win-mode rows and impossible to audit after the fact. The
# CI matrix job therefore runs this script inside WSL and THROWS on a non-zero
# exit instead of letting run-gate.sh score whatever the distro happens to
# have.
#
# Checks, all read-only:
#   1. every requested program resolves on the distro's PATH (run-gate.sh
#      invokes each through `env <name>`, so a missing name is a MISSING leg);
#   2. the resolved environment is GNU, shown by the distro's own
#      coreutils/findutils version banners (hostname/getconf are not
#      coreutils, so no per-name --version test is applied);
#   3. the GNU scratch root the runner uses is writable.
#
# Usage: bash gate/preflight-gnu.sh <scratch-root> <command> [...]
set -u

scratch="${1:-}"
if [ -n "$scratch" ]; then
    shift
else
    scratch="${GATE_GNU_REMOTE_ROOT:-/tmp/winuxcmd-gate}"
fi

if [ "$#" -eq 0 ]; then
    echo 'preflight: no command names given' >&2
    exit 2
fi

missing=0
for c in "$@"; do
    # TSV stems may differ from the program name (bracket.tsv drives `[`);
    # run-gate.sh's invocation_name() maps the same way.
    probe="$c"
    [ "$c" = bracket ] && probe='['
    if ! command -v "$probe" >/dev/null 2>&1; then
        echo "preflight: MISSING GNU program: $c (probed as $probe)" >&2
        missing=1
    fi
done
[ "$missing" -eq 0 ] || exit 3

# `env` and `timeout` are the two wrappers run-gate.sh needs on every row.
for c in env timeout; do
    command -v "$c" >/dev/null 2>&1 || {
        echo "preflight: MISSING GNU wrapper: $c" >&2
        exit 3
    }
done

ls_version=$(ls --version 2>/dev/null | head -1)
xargs_version=$(xargs --version 2>/dev/null | head -1)
echo "preflight: $ls_version"
echo "preflight: $xargs_version"
case "$ls_version" in
    *GNU\ coreutils*) ;;
    *) echo 'preflight: ls is not GNU coreutils; the oracle side is not usable' >&2; exit 4 ;;
esac
case "$xargs_version" in
    *GNU\ findutils*) ;;
    *) echo 'preflight: xargs is not GNU findutils; the oracle side is not usable' >&2; exit 4 ;;
esac

mkdir -p "$scratch" 2>/dev/null && rm -rf "$scratch/.preflight" &&
    mkdir -p "$scratch/.preflight" || {
    echo "preflight: scratch root not writable: $scratch" >&2
    exit 5
}
rm -rf "$scratch/.preflight"
printf 'PREFLIGHT OK: progs=%s oracle="%s" scratch=%s\n' "$#" "$ls_version" "$scratch"
