#!/bin/bash
# gate/stderr-check.sh — byte-compare stderr of the Rust side against GNU
# for the rows of one case table, after a `run-gate.sh <cmd>` run
# (#1488 sed lane). run-gate.sh gates stdout + rc only; commands whose
# diagnostics are part of the contract (sed's "-e expression #N, char M"
# family) are checked here from the artifacts the gate already keeps:
#   Rust: target/gate/scratch/<id>.rust/.err
#   GNU : target/gate/gnu/<id>/.err
# win-mode rows are skipped (platform-inherent text). Rows listed in
# STDERR_ALLOW (comma-separated ids) are reported but do not fail.
#
# Usage: bash gate/stderr-check.sh <cmd>
set -u
GATE_DIR="$(cd "$(dirname "$0")" && pwd)"
ART="$GATE_DIR/../target/gate"
cmd="${1:?usage: stderr-check.sh <cmd>}"
table="$GATE_DIR/cases/$cmd.tsv"
allow=",${STDERR_ALLOW:-},"
same=0; diff=0; allowed=0; missing=0
while IFS=$'\t' read -r id mode _rest; do
    case "$id" in ''|'#'*) continue ;; esac
    [ "$mode" = win ] && continue
    r="$ART/scratch/$id.rust/.err"; g="$ART/gnu/$id/.err"
    if [ ! -f "$r" ] || [ ! -f "$g" ]; then
        missing=$((missing+1)); echo "MISSING $id"; continue
    fi
    if cmp -s "$r" "$g"; then
        same=$((same+1))
    elif [ "${allow#*,$id,}" != "$allow" ]; then
        allowed=$((allowed+1)); echo "ALLOWED $id"
    else
        diff=$((diff+1))
        echo "DIFF $id"
        echo "  rust: $(head -c 200 "$r" | od -An -c | tr -s ' ' | head -3)"
        echo "  gnu : $(head -c 200 "$g" | od -An -c | tr -s ' ' | head -3)"
    fi
done < "$table"
echo "stderr-check $cmd: same=$same diff=$diff allowed=$allowed missing=$missing"
[ "$diff" -eq 0 ] && [ "$missing" -eq 0 ]
