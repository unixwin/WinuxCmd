#!/usr/bin/env bash
# wave1 wiring argument matrix (#1488, see rust2/REPLACEMENT-RECIPE.md §3).
# Usage: cmd-matrix.sh CMD OUTDIR gnu                 (under WSL: coreutils oracle)
#        cmd-matrix.sh CMD OUTDIR exe <winuxcmd.exe>  (under Git Bash: product)
# Emits ledger.tsv: cmd|case|rc|out_sha|err_sha and per-case .out/.err files.
#
# The case table per command lives in this script (per_command_cases) so one
# file covers the wave; rows follow the true/false layout id|env|stdin|args
# (args ':'-joined, @EMPTY@ = empty argument), read on fd 3 with </dev/null
# for non-stdin cases.
#
# Comparison discipline (recipe gate c): the ledgers of the OLD and the NEW
# exe must be byte-equal row by row (rc + stdout + stderr hashes); the GNU
# ledger is the rc oracle (stdout/stderr shapes are platform-bound for
# path-printing commands and are recorded, not compared).
set -u
CMD="$1"; OUT="$2"; MODE="$3"
EXE="${4:-}"
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

# Idempotent per-command fixture, run before EVERY case so all sides and all
# cases see the same tree (relative names only: MSYS must not rewrite them).
setup_fixture() {
  case "$CMD" in
    readlink)
      rm -rf link regfile; printf x > regfile; ln -s regfile link ;;
    realpath)
      rm -rf rpsub rpfile; mkdir -p rpsub; printf x > rpfile ;;
    mkdir)
      rm -rf d1 d2 dm dv dp d--x existing a; mkdir -p existing ;;
    rmdir)
      rm -rf pa full d1 d2; mkdir -p d1 d2 pa/pb/pc full/inner ;;
    sync)
      rm -f synctarget; printf x > synctarget ;;
    *) : ;;
  esac
}

per_command_cases() {
  case "$CMD" in
    pwd)
      cat <<'TABLE'
01|||
02|||-L
03|||-P
04|||-L:-P
05|||-P:-L
06|||--logical
07|||--physical
08|||--help
09|||--version
10|||--help:x
11|||-h
12|||--bogus
13|||-z
14|||extra
15|||--
16|||-
17|POSIXLY_CORRECT=1||
18|POSIXLY_CORRECT=1||-P
19|PWD=relative/path||-L
20|PWD=||-L
21|PWD=/definitely/not/here||-L
22|||-L:extra
TABLE
      ;;
    printenv)
      cat <<'TABLE'
01|||WINUXCMD_MATRIX_VAR
02|WINUXCMD_MATRIX_VAR=hello||WINUXCMD_MATRIX_VAR
03|WINUXCMD_MATRIX_VAR=hello||WINUXCMD_MATRIX_VAR:UNKNOWN_MATRIX_VAR_42
04|WINUXCMD_MATRIX_VAR=hello||UNKNOWN_MATRIX_VAR_42
05|WINUXCMD_MATRIX_VAR=hello|-0|WINUXCMD_MATRIX_VAR
06|||--help
07|||--version
08|||--help:x
09|||-z
10|||--bogus
11|WINUXCMD_MATRIX_VAR=hello|-a|WINUXCMD_MATRIX_VAR
12|||--
13|||-
14|WINUXCMD_MATRIX_VAR=||WINUXCMD_MATRIX_VAR
15|WINUXCMD_MATRIX_VAR=hello||-0:WINUXCMD_MATRIX_VAR
TABLE
      ;;
    basename)
      cat <<'TABLE'
01|||
02|||abc
03|||abc:.c
04|||a/b/c
05|||a/b/c:.c
06|||a///
07|||///
08|||-s:.c:abc.c
09|||-a:abc.c:.c
10|||-a
11|||-z:abc
12|||--help
13|||--version
14|||--help:x
15|||--bogus
16|||-x
17|||a:b:c
18|||-s
19|||abc:@EMPTY@
20|||--:abc
21|||-a:-s:.c:f1.c:f2.c
22|||-s.c:abc.c
23|||a\\b
TABLE
      ;;
    dirname)
      cat <<'TABLE'
01|||
02|||a/b/c
03|||a
04|||/
05|||//
06|||a/b/
07|||a//b
08|||-z:a/b/c
09|||--help
10|||--version
11|||--help:x
12|||--bogus
13|||-x
14|||a:b
15|||--
16|||@EMPTY@
17|||a\\b
TABLE
      ;;
    readlink)
      cat <<'TABLE'
01|||
02|||link
03|||-f:link
04|||-e:link
05|||-m:link
06|||-n:link
07|||-v:link
08|||link:link
09|||--help
10|||--version
11|||--help:x
12|||--bogus
13|||missing
14|||-f:missing
15|||-e:missing
16|||-f:.
17|||-f:..
18|||-v:missing
19|||-n:link:extra
TABLE
      ;;
    realpath)
      cat <<'TABLE'
01|||
02|||.
03|||..
04|||rpsub
05|||-e:.
06|||-e:rpsub
07|||-s:.
08|||-m:./a/../b
09|||-L:.
10|||-P:.
11|||-q:missing
12|||--help
13|||--version
14|||--help:x
15|||--bogus
16|||-x
17|||.:..
18|||--
19|||-z:.
20|||--logical:.
21|||--physical:.
22|||--canonicalize-missing:./x
23|||--canonicalize-existing:.
TABLE
      ;;
    mkdir)
      cat <<'TABLE'
01|||d1
02|||-p:d2/d3
03|||existing
04|||-m:750:dm
05|||-v:dv
06|||-p:-v:dp/dq
07|||--help
08|||--version
09|||--help:x
10|||--bogus
11|||-z
12|||
13|||-p:existing
14|||--:d--x
15|||-p:a:b
16|||-m:750
TABLE
      ;;
    rmdir)
      cat <<'TABLE'
01|||d1
02|||missing
03|||-p:pa/pb/pc
04|||--ignore-fail-on-non-empty:full
05|||-p:full
06|||--help
07|||--version
08|||--help:x
09|||--bogus
10|||-z
11|||
12|||d1:d2
13|||-v:d1
14|||--:d1
15|||-p:full/inner
16|||--ignore-fail-on-non-empty
TABLE
      ;;
    test)
      cat <<'TABLE'
01|||
02|||-n
03|||-z:@EMPTY@
04|||-z:abc
05|||abc:=:abc
06|||abc:!=:def
07|||1:-eq:1
08|||1:-lt:2
09|||-e:.
10|||-d:.
11|||-f:.
12|||-x:--help
13|||--help
14|||--version
15|||--help:x
16|||--bogus
17|||!
18|||a:-a:b
19|||a:-o:b
20|||1:-gt:2
21|||-s:@EMPTY@
22|||-t:1
23|||abc:==:abc
24|||!:abc
TABLE
      ;;
    '[')
      cat <<'TABLE'
01|||
02|||]
03|||-n:abc:]
04|||-z:@EMPTY@:]
05|||abc:=:abc:]
06|||abc:!=:def:]
07|||1:-eq:1:]
08|||-d:.:]
09|||--help
10|||--version
11|||--help:x:]
12|||--bogus
13|||missing-]
14|||-f:.]
15|||a:-a:b:]
16|||-x:--help:]
TABLE
      ;;
    sync)
      cat <<'TABLE'
01|||
02|||--data
03|||-d
04|||-f
05|||--file-system
06|||-d:-f
07|||--help
08|||--version
09|||--help:x
10|||--bogus
11|||-z
12|||
13|||synctarget
14|||missing-file-xyz
15|||-f:missing-file-xyz
16|||--
17|||-d:synctarget
18|||a:b:c
19|||-d
TABLE
      ;;
    hostid)
      # #1488 wiring wave3b: hostid takes no operands (GNU hostid.c reports
      # "extra operand" rc 1). The printed id is the platform value model
      # (win): old exe hashed MachineGuid, new exe follows the rust2 model —
      # the id VALUE differs by design and the ledger records it.
      cat <<'TABLE'
01|||
02|||--help
03|||--version
04|||-
05|||--bogus
06|||-h
07|||x
08|||x:y
09|||--
10|||-x
11|||--help:x
12|||--version:x
13|||/dev/null
14|||.:.
15|||a:b:c
TABLE
      ;;
    users)
      # #1488 wiring wave3b: users takes at most one operand (the utmp FILE,
      # a missing path is the GNU error rc 1, a second operand is "extra
      # operand" rc 1). The printed list is the platform value model (win);
      # the 1.x WTS session-state filter is not inherited.
      cat <<'TABLE'
01|||
02|||--help
03|||--version
04|||-h
05|||--bogus
06|||-z
07|||missing-file-xyz
08|||a:b
09|||a:b:c
10|||--
11|||--help:x
12|||--version:x
13|||-h:x
14|||.
15|||-:-
TABLE
      ;;
    tsort)
      # #1488 wiring wave3b: tsort reads whitespace-separated token pairs
      # from stdin or FILE; stdin rows carry one-line inputs (no newlines
      # in the matrix table). Known 1.x divergence recorded, not inherited:
      # -w (C++ accepts, GNU 9.4 rejects with usage rc 1; Rust follows GNU).
      cat <<'TABLE'
01|||
02|||a b c d
03|||a b b a
04|||a b a b
05|||a b c
06|||a a
07|||m a m z
08|||a b b c
09||1 2|-
10|||--help
11|||--version
12|||-w
13|||--bogus
14|||missing-file-xyz
15|||a:b
16|||-
17|||--
TABLE
      ;;
    *)
      echo "cmd-matrix.sh: no case table for $CMD" >&2
      exit 2
      ;;
  esac
}

while IFS='|' read -r id envspec stdin_data argspec <&3; do
  IFS=':' read -r -a args <<<"${argspec:-}"
  if [ "${#args[@]}" -eq 1 ] && [ -z "${args[0]}" ]; then args=(); fi
  local_i=0
  for local_i in "${!args[@]}"; do
    [ "${args[$local_i]}" = "@EMPTY@" ] && args[$local_i]=""
  done
  setup_fixture
  run_case "$CMD" "$id" "$envspec" "$stdin_data" "${args[@]+"${args[@]}"}"
done 3< <(per_command_cases)

echo "ledger at $LEDGER"
