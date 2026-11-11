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

# Fixture scratch: setup_fixture runs in the script's cwd, and some cases
# CREATE files the table calls "missing" (truncate -s on a fresh name), so
# the cwd is state, not neutral ground. Run each side in its own scratch
# dir under $OUT so sides never see each other's leftovers.
mkdir -p "$OUT/scratch"
cd "$OUT/scratch"

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
    # unlink rows 19/20 exercise wildcard operands against real matches:
    # GNU unlink.c treats argv literally (ENOENT), the 1.x C++ body
    # glob-expanded (extra operand when 2+ matches) — the new exe is
    # expected to follow GNU there, an explained r=c:no class.
    unlink)
      rm -f mxu1 mxu2 w1 w2; printf x > mxu1; printf x > w1 w2 ;;
    hostname)
      rm -f hnfile hnmissing hnbad
      printf 'myhost\n' > hnfile
      printf -- '-badname\n' > hnbad ;;
    base64|base32)
      rm -f mf mdec; printf foobar > mf; printf Zm9vYmFy > mdec ;;
    b2sum)
      # 'abc' BLAKE2b-512 from the gate tables; check rows reuse it.
      rm -f mf ck ck.bad ck.missing ck.garbage; printf abc > mf;
      printf 'ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923  mf\n' > ck
      printf '00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000  mf\n' > ck.bad
      printf 'ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923  no-such-file-xyz\n' > ck.missing
      printf 'not a checksum line at all\n' > ck.garbage ;;
    sha224sum)
      printf '23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  mf\n' > ck
      printf '0000000000000000000000000000000000000000000000000000000000  mf\n' > ck.bad
      printf '23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  no-such-file-xyz\n' > ck.missing
    sha384sum)
      printf 'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  mf\n' > ck
      printf '000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000  mf\n' > ck.bad
      printf 'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  no-such-file-xyz\n' > ck.missing
    tac)
      rm -f tf; printf 'a\nb\nc\n' > tf ;;
    truncate)
      rm -f trf; printf 0123456789ABCDEF > trf ;;

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
    base64)
      cat <<'TABLE'
01|||
02||abc||
03||abc|-d
04||Zm9vYmFy|-d
05||Zm9vYmFy|-d:-i
06||Zm9v |-d
07|||-w:2:mf
08|||-w:0:mf
09|||--wrap=2:mf
10|||-w2:mf
11|||-w:bogus:mf
12|||-w:-1:mf
13|||missing-file-xyz
14|||mf:mf
15|||--help
16|||--version
17|||--help:x
18|||--bogus
19|||-z
20|||--:mf
21|||-
22||Zm9vYmFy|-d:-
23|||mdec
24|||-i
25||YWJj|-d
26|||-w:bogus:missing-file-xyz
27|||-d:-w:bogus:mf
28|||-w:bogus:mf:extra
TABLE
      ;;
    base32)
      cat <<'TABLE'
01|||
02||abc||
03||MFRGG===|-d
04||MZXW6YTBOI======|-d
05||MZXW6YTBOI======|-d:-i
06||MZXW6YTBOI|-d
07||MZ|-d
08|||-w:4:mf
09|||-w:0:mf
10|||--wrap=4:mf
11|||-w4:mf
12|||-w:bogus:mf
13|||-w:-1:mf
14|||missing-file-xyz
15|||mf:mf
16|||--help
17|||--version
18|||--help:x
19|||--bogus
20|||-z
21|||--:mf
22|||-
23|||mdec
24||ME======|-d
25||ME======|-d:-i
26|||-w:bogus:missing-file-xyz
27|||-d:-w:bogus:mf
28|||-w:bogus:mf:extra
TABLE
      ;;
    b2sum)
      cat <<'TABLE'
01|||
02||abc||
03|||mf
04|||-b:mf
05|||-t:mf
06|||-z:mf
07|||--tag:mf
08|||-l:128:mf
09|||-l:512:mf
10|||-l:bogus:mf
11|||-l:520:mf
12|||--check:ck
13|||--check:ck.bad
14|||--check:ck.missing
15|||--check:ck.garbage
16|||-w:--check:ck.garbage
17|||--quiet:--check:ck.bad
18|||--status:--check:ck.bad
19|||--strict:--check:ck.garbage
20|||--ignore-missing:--check:ck.missing
21||ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923  mf|-c
22|||--tag:mf:-c
23|||-b:-c:ck
24|||--zero:--check:ck
25|||--quiet
26|||--help
27|||--version
28|||--bogus
29|||-q
30|||mf:mf
31|||--:mf
32|||-
33|||--tag:--check:-l:bogus
34|||-l:bogus:missing-file-xyz
TABLE
      ;;
    sha224sum)
      cat <<'TABLE'
01|||
02||abc||
03|||mf
04|||-b:mf
05|||-t:mf
06|||-z:mf
07|||--tag:mf
08|||--check:ck
09|||--check:ck.bad
10|||--check:ck.missing
11|||--check:ck.garbage
12|||-w:--check:ck.garbage
13|||--quiet:--check:ck.bad
14|||--status:--check:ck.bad
15|||--strict:--check:ck.garbage
16|||--ignore-missing:--check:ck.missing
17||23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  mf|-c
18|||-l:224:mf
19|||-b:mf:-c
20|||--quiet
21|||--help
22|||--version
23|||--bogus
24|||-q
25|||mf:mf
26|||--:mf
27|||-
TABLE
      ;;
    sha384sum)
      cat <<'TABLE'
01|||
02||abc||
03|||mf
04|||-b:mf
05|||-t:mf
06|||-z:mf
07|||--tag:mf
08|||--check:ck
09|||--check:ck.bad
10|||--check:ck.missing
11|||--check:ck.garbage
12|||-w:--check:ck.garbage
13|||--quiet:--check:ck.bad
14|||--status:--check:ck.bad
15|||--strict:--check:ck.garbage
16|||--ignore-missing:--check:ck.missing
17||cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  mf|-c
18|||-l:384:mf
19|||-b:mf:-c
20|||--quiet
21|||--help
22|||--version
23|||--bogus
24|||-q
25|||mf:mf
26|||--:mf
27|||-
TABLE
      ;;
    tac)
      cat <<'TABLE'
01||abc||
02||abc|-b
03||abc|-r
04||abc|-s:x
05||a1b2|-s:1
06|||tf
07|||-b:tf
08|||-r:tf
09|||-s:c:tf
10|||-r:-s:[a-c]:tf
11|||-r:-s:x*:tf
12|||-s:@EMPTY@:tf
13|||missing-file-xyz
14|||tf:tf
15|||-r:-s:[:tf
16|||-r:-s:@EMPTY@:tf
17|||--help
18|||--version
19|||--bogus
20|||--:tf
21|||-
22||a1b2c|-r:-s:[0-9]
23||a1b2c|-r:-b:-s:[0-9]
24||x|-s:@EMPTY@
TABLE
      ;;
    truncate)
      cat <<'TABLE'
01|||-s:10:trf
02|||--size=5:trf
03|||-s:+2:trf
04|||-s:-2:trf
05|||-s:%5:trf
06|||-s:/5:trf
07|||-s:>10:trf
08|||-s:<20:trf
09|||-s:1K:trf
10|||-s:1KiB:trf
11|||-s:1kB:trf
12|||-s:@EMPTY@:trf
13|||
14|||-s:5
15|||-s:bogus:trf
16|||-r:trf:trf
17|||-r:missing-file-xyz:-s:5:trf
18|||-c:-s:5:missing-file-xyz
19|||-s:5:missing-file-xyz
20|||-o:-s:1K:trf
21|||--help
22|||--version
23|||--bogus
24|||-x
25|||--:trf
26|||-c:-s:10:trf
TABLE
      ;;
    whoami)
      cat <<'TABLE'
01|||
02|||extra
03|||a:b
04|||-h
05|||--help
06|||--version
07|||--help:x
08|||--bogus
09|||-z
10|||--
11|||@EMPTY@
12|||-
13|||--:extra
14|||--Help
15|||--no-such-option
16|||WINUXSH_USER=
TABLE
      ;;
    nproc)
      cat <<'TABLE'
01|||
02|||--all
03|||-a
04|||--ignore=2
05|||--ignore=0
06|||--ignore=+1
07|||--ignore:2
08|||--ignore
09|||--ignore=bad
10|||--ignore=-1
11|||extra
12|||-h
13|||--help
14|||--version
15|||--help:x
16|||--bogus
17|||-z
18|||--
19|||@EMPTY@
TABLE
      ;;
    unlink)
      cat <<'TABLE'
01|||mxu1
02|||missing-file-xyz
03|||-x:mxu1
04|||--help
05|||--version
06|||--help:x
07|||--bogus
08|||-h
09|||
10|||mxu1:mxu2
11|||--
12|||--:mxu1
13|||@EMPTY@
14|||-
15|||.
16|||..
17|||mxu1//
18|||w1
19|||w*
20|||w?1
21|||reg//
TABLE
      ;;
    sleep)
      cat <<'TABLE'
01|||0
02|||0.001
03|||1
04|||0:0
05|||0.001s:0.001s
06|||1x
07|||--help
08|||--version
09|||--help:x
10|||-h
11|||--bogus
12|||-1
13|||
14|||--
15|||abc
16|||0.001:xyz
17|||0.001m:0.001h
TABLE
      ;;
    hostname)
      cat <<'TABLE'
01|||
02|||newname
03|||a:b
04|||--help
05|||--version
06|||-h
07|||-V
08|||--bogus
09|||--short
10|||-s
11|||-i
12|||-f
13|||-d
14|||-I
15|||-y
16|||-n
17|||-a
18|||-A
19|||-b
20|||-F:hnfile
21|||--file:hnfile
22|||--file=hnfile
23|||-F:hnmissing
24|||-F:hnbad
25|||-F:hnbad:x
26|||--version:x
27|||--
28|||-F
29|||-sF:hnfile
TABLE
      ;;
    uname)
      cat <<'TABLE'
01|||
02|||-a
03|||-s
04|||-n
05|||-r
06|||-v
07|||-m
08|||-p
09|||-i
10|||-o
11|||-s:-n:-r
12|||--all
13|||--kernel-name
14|||--sysname
15|||--release
16|||--help
17|||--version
18|||--help:x
19|||-h
20|||--bogus
21|||extra-operand
22|||-a:extra
23|||--
24|||-snrvm
TABLE
      ;;
    id)
      cat <<'TABLE'
01|||
02|||-u
03|||-g
04|||-G
05|||-n
06|||-un
07|||-gn
08|||-Gn
09|||-ur
10|||-a
11|||-Z
12|||-z
13|||--zero
14|||--help
15|||--version
16|||--help:x
17|||-h
18|||--bogus
19|||--:extra
20|||cmx
21|||nosuchuser
22|||-u:nosuchuser
23|||-ug
24|||-nr
25|||-az
TABLE
      ;;
    getconf)
      cat <<'TABLE'
01|||PATH_MAX
02|||ARG_MAX
03|||NAME_MAX
04|||_POSIX_PATH_MAX
05|||pagesize
06|||PAGESIZE
07|||NPROCESSORS_ONLN
08|||_NPROCESSORS_CONF
09|||PHYS_PAGES
10|||avphys_pages
11|||NOSUCHVAR
12|||-a
13|||--all
14|||-v:POSIX-V6-ilp32-off32:PATH_MAX
15|||--help
16|||--version
17|||--help:x
18|||-h
19|||--bogus
20|||
21|||PATH_MAX:.
22|||NAME_MAX:nosuchdir
23|||ARG_MAX:.
24|||PATH_MAX:.:extra
25|||path_max:.
26|||--
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
