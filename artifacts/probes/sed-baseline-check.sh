#!/usr/bin/env bash
set -u
S="$1"
O="$2"
mkdir -p "$O"
printf "alpha\nbravo\ncarlos\ndelta\n" > "$O/in.txt"
printf "s/a/X/# comment\ns/b/Y/\n" > "$O/f1.sed"
printf "#n\n" > "$O/f2.sed"
"$S" -e "s/a/\1/" "$O/in.txt" > "$O/06.out" 2> "$O/06.err"; printf "E=%s\n" "$?" > "$O/06.exit"
"$S" -e "s/a/\Lb\E/" "$O/in.txt" > "$O/10.out" 2> "$O/10.err"; printf "E=%s\n" "$?" > "$O/10.exit"
"$S" -e "N; s/\n/ /" "$O/in.txt" > "$O/12.out" 2> "$O/12.err"; printf "E=%s\n" "$?" > "$O/12.exit"
"$S" -e "s/a/X/# trailing comment" "$O/in.txt" > "$O/46.out" 2> "$O/46.err"; printf "E=%s\n" "$?" > "$O/46.exit"
"$S" -n -f "$O/f1.sed" "$O/in.txt" > "$O/59.out" 2> "$O/59.err"; printf "E=%s\n" "$?" > "$O/59.exit"
"$S" -f "$O/f2.sed" "$O/in.txt" > "$O/60.out" 2> "$O/60.err"; printf "E=%s\n" "$?" > "$O/60.exit"
"$S" -e "a\appended" "$O/in.txt" > "$O/43.out" 2> "$O/43.err"; printf "E=%s\n" "$?" > "$O/43.exit"
"$S" -e "i\inserted" "$O/in.txt" > "$O/44.out" 2> "$O/44.err"; printf "E=%s\n" "$?" > "$O/44.exit"
"$S" -e "c\replaced" "$O/in.txt" > "$O/45.out" 2> "$O/45.err"; printf "E=%s\n" "$?" > "$O/45.exit"
"$S" -e "#n" "$O/in.txt" > "$O/61.out" 2> "$O/61.err"; printf "E=%s\n" "$?" > "$O/61.exit"
echo DONE
