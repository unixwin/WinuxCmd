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
    join)
      rm -f jf1 jf2 jc1 jh1 jh2 juns
      printf 'a 1\nb 2\nc 3\n' > jf1
      printf 'a X\nb Y\nd Z\n' > jf2
      printf 'a,1\nb,2\n' > jc1
      printf 'K V\na 1\n' > jh1
      printf 'K W\na Q\n' > jh2
      printf 'b 2\na 1\n' > juns ;;
    # wave3a wiring (#1488): fixed-clock fixtures so timestamp/verbose
    # output stays byte-stable across the old and new exe runs. `touch -d`
    # below is the SIDE'S OWN touch (MSYS GNU touch on the exe sides, GNU
    # coreutils on the WSL side) so both write identical atime/mtime.
    touch)
      rm -rf tref ta tb; printf seed > tref; printf seed > ta; printf seed > tb
      touch -d '2026-01-02 03:04:05' tref ta tb ;;
    ln)
      # sl1..sl4 are the -s destinations and ln1/lfresh/lv1 the hard-link
      # destinations; without them in the rm list both sides leak them
      # across runs (the WSL side and the wired exe CAN create symlinks on
      # DrvFs here), and the later rows report "File exists"/the -i prompt
      # instead of the canonical clean-state result.
      rm -rf aln bln dln sln fresh blbak sl1 sl2 sl3 sl4 ln1 lfresh lv1
      printf A1 > aln; printf xx > bln; mkdir -p dln ;;
    stat)
      rm -rf sreg sdir shard
      printf abc > sreg; mkdir -p sdir; ln -f sreg shard 2>/dev/null
      touch -d '2026-01-02 03:04:05' sreg sdir shard 2>/dev/null ;;
    seq) : ;;
    tee)
      rm -rf tpre tout1 tout2 tok
      printf 'seed\n' > tpre ;;
    paste)
      rm -rf pf1 pf2 pf3 pqin
      printf 'a\nb\nc\n' > pf1; printf '1\n2\n3\n4\n' > pf2
      printf 'only\n' > pf3; printf 'q\nr\n' > pqin ;;
    fold)
      rm -rf flong fwords
      printf 'The quick brown fox jumps over the lazy dog again and again.\n' > flong
      printf 'supercalifragilisticexpialidocious antidisestablishmentarianism word\n' > fwords ;;
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
    join)
      # #1488 wiring wave3b: join joins FILE1 FILE2 (or stdin for "-"/one
      # operand). Fixture (setup_fixture): jf1/jf2 blank-separated sorted,
      # jc1 comma-separated, jh1/jh2 with headers, juns unsorted. stdin
      # rows carry one line without trailing newline.
      cat <<'TABLE'
01|||jf1:jf2
02|||-t:,:jc1:jc1
03|||-1:2:-2:1:jf1:jf2
04|||-j:1:jf1:jf2
05|||-o:0 1.2:jf1:jf2
06|||-o:1.2:2.2:jf1:jf2
07|||-e:MISSING:-o:1.2:2.2:jf1:jf2
08|||-a:1:jf1:jf2
09|||-v:2:jf1:jf2
10|||-i:jf1:jf2
11|||--header:jh1:jh2
12|||-z:jf1:jf2
13|||jf1
14||a 1|
15|||missing-file-xyz:jf1
16|||--help
17|||--version
18|||--bogus
19|||-z:jf1:jf2:jc1
20|||--check-order:juns:jf2
21||a 1|-:jf2
TABLE
      ;;
    timeout)
      # #1488 wiring wave3b: the exit-code contract (124 timed out,
      # 125 timeout itself failed, 126 found-not-invocable, 127 not
      # found, 137 KILL) gates exit-only; the child set is
      # platform-bound (true/sleep on both sides; GNU wraps coreutils,
      # the exe wraps its own multicall children). -k/-s/--preserve-
      # status rows exercise the 124/137 matrix.
      cat <<'TABLE'
01|||1:true
02|||1s:true
03|||1s:sleep:10
04|||-k:2:1:sleep:10
05|||-k:1s:1s:sleep:10
06|||-s:KILL:1:sleep:10
07|||-s:TERM:1:sleep:10
08|||--preserve-status:1:sleep:10
09|||--preserve-status:1:true
10|||--foreground:1:true
11|||-v:1:sleep:10
12|||--help
13|||--version
14|||--bogus
15|||1
16|||abc:true
17|||1m:missing-cmd-xyz
18|||0.1s:true
19|||-s:BOGUS:1:true
20|||--:1:true
TABLE
      ;;
    # wave3a wiring matrices (#1488). Row shape: id|env|stdin|args
    # (args ':'-joined). exes run in one shared cwd per side; the GNU side
    # is the rc oracle (stdout/stderr shapes are platform-bound where noted).
    touch)
      cat <<'TABLE'
01|||ta
02|||-c:tmissing
03|||-c:ta
04|||-a:ta
05|||-m:ta
06|||-c:-a:tmissing
07|||-d:2026-05-06 07:08:09:td1
08|||-d:@1767225600:td2
09|||-d:2026-05-06:td3
10|||-t:202605060708.09:tt1
11|||-t:05060708:tt2
12|||-t:2605060708.09:tt3
13|||-r:tref:tr1
14|||-r:tmissing:tr2
15|||-d:2026-05-06 07:08:09:-t:202605060708.09:tmix
16|||-a:-m:ta
17|||--no-create:tmissing2
18|||--date=2026-05-06 07:08:09:td4
19|||-f:ta
20|||ta:tb
21|||--:ta
22|||--help
23|||--version
24|||--help:x
25|||--bogus
26|||
TABLE
      ;;
    ln)
      cat <<'TABLE'
01|||aln:ln1
02|||aln:bln
03|||-f:aln:bln
04||n|-i:aln:bln
05||y|-i:aln:bln
06|||-i:aln:lfresh
07|||-s:aln:sl1
08|||-s:lmissing:sl2
09|||-s:-f:aln:sl1
10|||-v:aln:lv1
11|||-t:dln:aln
12|||-T:aln:bln
13|||-b:aln:bln
14|||-b:-S:~b:aln:bln
15|||--backup:aln:bln
16|||aln:bln:dln
17|||-s:dln:sldd
18|||-v:-s:aln:sl3
19|||--help
20|||--version
21|||--bogus
22|||
23|||aln
24|||--:bln
25|||-s:-i:aln:sl4
TABLE
      ;;
    stat)
      # stdout shapes are platform-bound (device/inode numbers, NTFS vs
      # ext4): the GNU ledger is the rc oracle; old-vs-new must be byte-equal
      # on the shared NTFS fixture (fixed atime/mtime via the side's touch).
      cat <<'TABLE'
01|||sreg
02|||-t:sreg
03|||-c:%n:sreg
04|||-c:%s:sreg
05|||-c:%A:sreg
06|||-c:%a:sreg
07|||-c:%F:sreg
08|||-c:%h:sreg
09|||-c:%y:sreg
10|||-c:%Y:sreg
11|||-c:%x:sreg
12|||-c:%X:sreg
13|||-c:%n %s %A:sreg
14|||--printf=%s\\n:sreg
15|||-c:%F:sdir
16|||-c:%n:smissing
17|||-c:%n:sreg:sdir
18|||-c:%b:sreg
19|||-c:%o:sreg
20|||-c:%D:sreg
21|||-c:%C:sreg
22|||-c:%!:sreg
23|||--help
24|||--version
25|||--bogus
26|||
27|||-c:%n
28|||-f:-c:%n:sreg
TABLE
      ;;
    seq)
      cat <<'TABLE'
01|||5
02|||1:5
03|||1:2:9
04|||-w:1:10
05|||-w:08:10
06|||-s:,:1:3
07|||-s: :1:3
08|||-f:%.2f:1:3
09|||-f:%05.1f:1.5:2:7
10|||1.5:2
11|||-0.5:0.25:0.5
12|||5:1
13|||-w:-1:1
14|||1e2:1e2
15|||--:-1:1
16|||-s:@EMPTY@:1:3
17|||abc
18|||1:2:3:4
19|||-f:%!d:1:3
20|||-w:1.0:2.0
21|||--help
22|||--version
23|||--bogus
24|||
25|||-.5:.5
TABLE
      ;;
    tee)
      cat <<'TABLE'
01||abc|
02||hello|tout1
03||xy|tout1:tok
04||zz|-a:tpre
05||zz|-a:tpre:tok
06||z|tout1:-
07||q|--:tout1
08||q|-i:tok
09||q|-p:tok
10||q|--output-error=warn:tok
11||q|tdir-missing/out:tok
12||q|--bogus
13||q|--help
14||q|--version
15||cc|-ap:tpre
16|||tok
17||q|tout1:tok:tpre
18||q|-
19||q|-a:tdir-missing/out
TABLE
      ;;
    paste)
      cat <<'TABLE'
01|||pf1:pf2
02|||pf1
03|||-s:pf1
04|||-s:pf1:pf2
05|||-d:,:pf1:pf2
06|||-d:\n:pf1:pf2
07||x\ny|-:-:pqin
08||x\ny|pf1:-
09|||-z:pf1
10|||pf1:pf2:pf3
11|||
12||-s
13|||pmissing
14|||--help
15|||--version
16|||--bogus
17|||--:pf1
18|||-d:@:pf1:pf2
19|||-d:|:pf1:pf2
20|||-s:-d:,:pf1:pf2
21|||-d,:pf1:pf2
TABLE
      ;;
    fold)
      cat <<'TABLE'
01|||flong
02|||-w:10:flong
03|||-w10:flong
04|||--width=10:flong
05|||-s:-w:14:flong
06|||-b:-w:5:flong
07|||-w:3:fwords
08||The quick brown fox jumps over the lazy dog.|
09||abcdefghij|-w:4:-
10|||-w:5:flong:fwords
11|||fmissing
12|||-w:0:flong
13|||-w:-3:flong
14|||-10:flong
15|||--help
16|||--version
17|||--bogus
18|||-w
19|||--:flong
20|||-c:-w:10:flong
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
