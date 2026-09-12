#!/usr/bin/env bash
set -u
cd "$(dirname "$0")" || exit 1
printf '  0 012  3   \n' > tr.txt
printf 'l1\nl2\nl3\n' > l.txt
SCRIPT_BRACE='1 { s/^0*[[:blank:]]*//; s/[[:blank:]]*$//; p; }'
SCRIPT_AND='2 { 2 { p; } }'
S1=/mnt/d/repo/unixwin-winuxcmd/build-compat-fix/sed.exe
S2=/mnt/d/repo/unixwin-winuxcmd/build-compat-fix/winuxcmd.exe
echo '### rubash posixexp trim_od form on tr.txt'
echo -n '  GNU      : '; /usr/bin/sed -n "$SCRIPT_BRACE" tr.txt | tr '\n' '|'; echo
echo -n '  standalone: '; "$S1" -n "$SCRIPT_BRACE" tr.txt 2>&1 | tr '\n' '|'; echo
echo -n '  dispatcher: '; "$S2" sed -n "$SCRIPT_BRACE" tr.txt 2>&1 | tr '\n' '|'; echo
echo '### AND-semantics of nested group addresses on l.txt'
echo -n '  GNU      : '; /usr/bin/sed -n "$SCRIPT_AND" l.txt | tr '\n' '|'; echo
echo -n '  standalone: '; "$S1" -n "$SCRIPT_AND" l.txt 2>&1 | tr '\n' '|'; echo
echo -n '  dispatcher: '; "$S2" sed -n "$SCRIPT_AND" l.txt 2>&1 | tr '\n' '|'; echo
echo '### invalid brace forms (must reject identically)'
for bad in '1 { p; } x' '1 }' '1 { p;; }extra' '{ p;' '1 { p; } }'; do
  echo -n '  script=[$bad]  GNU='
  /usr/bin/sed -n "$bad" l.txt >/dev/null 2>&1; echo -n "exit=$?"
  echo -n '  WIN='
  "$S1" -n "$bad" l.txt >/dev/null 2>&1; echo "exit=$?"
done
