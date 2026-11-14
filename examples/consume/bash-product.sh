#!/usr/bin/env bash
# Example: a bash product uses WinuxCmd as its coreutils provider.
#
# Two modes:
#   1. applets already on PATH (installer/wpm created the hardlinks)
#   2. direct dispatch through one winuxcmd.exe (no link farm)
#
# Usage:
#   WINUXCMD_BIN="/c/Program Files/WinuxCmd/winuxcmd.exe" ./bash-product.sh
#   ./bash-product.sh --shims       # build a private hardlink dir first
set -u

WINUXCMD_BIN="${WINUXCMD_BIN:-winuxcmd.exe}"

if [ "${1:-}" = "--shims" ]; then
  # A shell product that cannot rely on the installer can build its own
  # hardlink farm. cmd.exe needs backslash paths; this uses cygpath-free
  # absolute paths because the samples live under the current drive.
  SHIM_DIR="${SHIM_DIR:-$PWD/.winux-shims}"
  mkdir -p "$SHIM_DIR"
  for applet in ls cat cp mv rm mkdir grep sed find sort wc head tail cut tr; do
    if [ ! -e "$SHIM_DIR/$applet.exe" ]; then
      cmd //c mklink //H "$(pwd -W 2>/dev/null || pwd)/$SHIM_DIR/$applet.exe" \
        "$(command -v "$WINUXCMD_BIN")" >/dev/null 2>&1 || true
    fi
  done
  export PATH="$SHIM_DIR:$PATH"
fi

# --- mode 1: the shell resolves applets from PATH -------------------------
if command -v ls >/dev/null 2>&1; then
  printf 'applet: %s\n' "$(command -v ls)"
  ls -1 "$PWD" | head -3
fi

# --- mode 2: one entry point, no link farm ---------------------------------
winux() { "$WINUXCMD_BIN" "$@"; }

printf '\nversion: '
winux --version | head -1

printf '\nls via dispatch:\n'
winux ls -1 "$PWD" | head -3

printf '\nsort via dispatch (stdin):\n'
printf 'b\na\nc\n' | winux sort

printf '\ngrep via dispatch:\n'
printf 'alpha\nbeta\n' | winux grep 'a$'

# A product that wants an explicit, auditable coreutils surface pins the
# binary path and the ABI/version instead of trusting PATH order:
printf '\nexplicit: %s\n' "$WINUXCMD_BIN"
