#!/usr/bin/env bash
# sed-compare.sh LEFT_DIR RIGHT_DIR [LABEL]
# Joins two sed-matrix ledgers (id|rc|out|err|tree) row by row and prints
# per-case rc/stdout/stderr/tree equality plus totals. Exit 0 iff every
# field is equal. (compare.sh is the 4-column true/false ledger shape and
# would misalign the tree hash column, so this lane carries its own.)
set -u
L="$1"; R="$2"; LABEL="${3:-compare}"
if [ "$(wc -l <"$L/ledger.tsv")" != "$(wc -l <"$R/ledger.tsv")" ]; then
  echo "$LABEL: row count mismatch"; exit 2
fi
paste -d'|' "$L/ledger.tsv" "$R/ledger.tsv" | awk -F'|' -v label="$LABEL" '
$1 == $6 {
  rc = ($2 == $7); out = ($3 == $8); err = ($4 == $9); tree = ($5 == $10)
  printf "%-5s rc:%s out:%s err:%s tree:%s\n", $1,
         (rc ? "=" : "DIFF"), (out ? "=" : "DIFF"),
         (err ? "=" : "DIFF"), (tree ? "=" : "DIFF")
  n++; nrc += rc; nout += out; nerr += err; ntree += tree
}
END {
  printf "%s: rows=%d rc=%d/%d stdout=%d/%d stderr=%d/%d tree=%d/%d\n",
         label, n, nrc, n, nout, n, nerr, n, ntree, n
  exit !(nrc == n && nout == n && nerr == n && ntree == n)
}'
