#!/usr/bin/env bash
# sed text-command body probe. usage: sed-textbody-probe.sh <sed-binary> <outdir>
set -u
cd "$(dirname "$0")" || exit 1
S="$1"
O="$2"
mkdir -p "$O"
printf "alpha\nbravo\ncarlos\n" > "$O/in.txt"
r() {
  local id="$1"; shift
  "$S" "$@" "$O/in.txt" > "$O/$id.out" 2> "$O/$id.err"
  printf "E=%s\n" "$?" > "$O/$id.exit"
}
for f in tb/*.sed; do
  n=$(basename "$f" .sed)
  r "$n" -n -f "$f"
done
echo DONE
