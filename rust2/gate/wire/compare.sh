#!/usr/bin/env bash
# compare.sh LEFT_DIR RIGHT_DIR [LABEL]
# Joins two probe-matrix ledgers row by row and prints per-case rc/out/err
# equality plus a totals line. Exit 0 iff every rc, stdout and stderr is equal.
set -u
L="$1"; R="$2"; LABEL="${3:-compare}"
if [ "$(wc -l <"$L/ledger.tsv")" != "$(wc -l <"$R/ledger.tsv")" ]; then
  echo "$LABEL: row count mismatch"; exit 2
fi
paste -d'|' "$L/ledger.tsv" <(cut -d'|' -f3- "$R/ledger.tsv") | awk -F'|' -v label="$LABEL" '
{
  rc = ($3 == $6); out = ($4 == $7); err = ($5 == $8)
  printf "%-6s %-3s rc:%s/%s %s out:%s err:%s\n", $1, $2, $3, $6, (rc ? "=" : "DIFF"), (out ? "=" : "DIFF"), (err ? "=" : "DIFF")
  n++; nrc += rc; nout += out; nerr += err
}
END {
  printf "%s: rows=%d rc=%d/%d stdout=%d/%d stderr=%d/%d\n", label, n, nrc, n, nout, n, nerr, n
  exit !(nrc == n && nout == n && nerr == n)
}'
