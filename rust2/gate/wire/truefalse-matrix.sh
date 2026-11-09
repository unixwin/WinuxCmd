#!/usr/bin/env bash
# true/false wiring-wave argument matrix (#1488, see rust2/REPLACEMENT-RECIPE.md).
# Usage: truefalse-matrix.sh OUTDIR gnu                (under WSL: coreutils oracle)
#        truefalse-matrix.sh OUTDIR exe <winuxcmd.exe> (under Git Bash: product)
# Emits ledger.tsv: cmd|case|rc|out_sha|err_sha  and per-case .out/.err files.
set -u
OUT="$1"; MODE="$2"
EXE="${3:-}"
mkdir -p "$OUT"
LEDGER="$OUT/ledger.tsv"
: > "$LEDGER"

run_case() {
  local cname="$1" id="$2" envspec="$3" stdin_data="$4"; shift 4
  local o="$OUT/$cname-$id.out" e="$OUT/$cname-$id.err" rc
  if [ "$MODE" = gnu ]; then
    if [ -n "$stdin_data" ]; then
      printf '%s' "$stdin_data" | env $envspec /usr/bin/"$cname" "$@" >"$o" 2>"$e"
    else
      env $envspec /usr/bin/"$cname" "$@" >"$o" 2>"$e" </dev/null
    fi
  else
    if [ -n "$stdin_data" ]; then
      printf '%s' "$stdin_data" | env $envspec "$EXE" "$cname" "$@" >"$o" 2>"$e"
    else
      env $envspec "$EXE" "$cname" "$@" >"$o" 2>"$e" </dev/null
    fi
  fi
  rc=$?
  printf '%s|%s|%s|%s|%s\n' "$cname" "$id" "$rc" \
    "$(sha256sum "$o" | cut -d' ' -f1)" "$(sha256sum "$e" | cut -d' ' -f1)" >>"$LEDGER"
}

# id|env|stdin|args...
CASES=$(cat <<'TABLE'
01|||
02|||-h
03|||--help
04|||--version
05|||--help:x
06|||--version:x
07|||x:--help
08|||--bogus
09|||-z
10|||--
11|||-
12|||---
13|||--Help
14|||--help=x
15|||-x:-y
16|||extra:args:here
17||hello-data|
18|||/dev/null
19|POSIXLY_CORRECT=1||--help
20|POSIXLY_CORRECT=||--help
21|POSIXLY_CORRECT=1||--version
22|||-x:--help
TABLE
)

# Field layout: id|env|stdin|args (args ':'-joined). The table is read on
# fd 3 so a subject that reads its stdin cannot swallow the remaining rows.
# Every non-stdin case gets </dev/null so neither side sees a stray tty/pipe.
while IFS='|' read -r id envspec stdin_data argspec <&3; do
  IFS=':' read -r -a args <<<"${argspec:-}"
  # normalize empty arg list (no args -> zero operands)
  if [ "${#args[@]}" -eq 1 ] && [ -z "${args[0]}" ]; then args=(); fi
  run_case true "$id" "$envspec" "$stdin_data" "${args[@]+"${args[@]}"}"
  run_case false "$id" "$envspec" "$stdin_data" "${args[@]+"${args[@]}"}"
done 3<<ROWS
$CASES
ROWS

echo "ledger at $LEDGER"
