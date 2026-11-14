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
    fmt)
      rm -rf fpara fpre
      printf 'This is the first paragraph of the sample text used by the fmt matrix. It has enough words to wrap around several times when the width is small.\n\nThis is the second paragraph.\n' > fpara
      printf '  indented line one\n  indented line two\n' > fpre ;;
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
      printf '23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  no-such-file-xyz\n' > ck.missing ;;
    sha384sum)
      printf 'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  mf\n' > ck
      printf '000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000  mf\n' > ck.bad
      printf 'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7  no-such-file-xyz\n' > ck.missing ;;
    tac)
      rm -f tf; printf 'a\nb\nc\n' > tf ;;
    truncate)
      rm -f trf; printf 0123456789ABCDEF > trf ;;
    # wave A1 (#1488): fixtures for the Phase-A1 command set. Relative names
    # only (niubash spawn-layer rewriting never sees POSIX spellings).
    chcon|chgrp|chmod|chown)
      rm -f cf1; printf x > cf1; mkdir -p cdir ;;
    date)
      rm -f df1; printf '2020-01-02 03:04:05 UTC\nnot a date\n' > df1 ;;
    du)
      rm -rf dudir; mkdir -p dudir/sub
      printf x > dudir/f1; printf yy > dudir/sub/f2 ;;
    mkfifo|mknod|mktemp)
      rm -f mf1 mn1 mt1 mtd1; rm -rf mtdir ;;
    pathchk)
      rm -rf pcdir; mkdir -p pcdir/a/b ;;

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
    fmt)
      cat <<'TABLE'
01|||fpara
02|||-w:30:fpara
03|||--width=40:fpara
04|||-s:fpara
05|||-u:fpara
06|||-c:fpre
07|||-t:fpre
08|||-g:20:fpara
09|||-p:  :fpre
10||This is the first paragraph of the sample text used by the fmt matrix. It has enough words to wrap around several times when the width is small.|
11||This is the first paragraph of the sample text used by the fmt matrix. It has enough words to wrap around several times when the width is small.|-w:30
12|||fmissing
13|||fpara:fpre
14|||-w:5:fpara
15|||-w:1000000:fpara
16|||--help
17|||--version
18|||--bogus
19|||
20|||-w
21|||-w:30:fpara:fpre:fpara
22|||-d
TABLE
      ;;
    arch)
      # #1488 wiring wave A1: arch prints the native machine name; operand
      # and option grammar rows are the GNU getopt_long surface.
      cat <<'TABLE'
01|||
02|||--help
03|||--version
04|||--bogus
05|||-x
06|||extra
07|||a:b
08|||--
09|||--:--help
10|||--ver
11|||-h
12|||-V
13|||--help:extra
14|||--version:extra
TABLE
      ;;
    chcon)
      # #1488 wiring wave A1: SELinux is unsupported on Windows; every row
      # is an rc/error-shape probe (context parse, modifiers, operands).
      cat <<'TABLE'
01|||
02|||system_u:object_r:httpd_sys_content_t:s0:cf1
03|||user_u:role_r:type_t:s0:cf1
04|||-u:user_u:cf1
05|||-r:role_r:cf1
06|||-t:type_t:cf1
07|||-l:s0:cf1
08|||-c:user_u:role_r:type_t:s0:cf1
09|||--reference:cf1:cf1
10|||--bogus
11|||--help
12|||--version
13|||--help:x
14|||extra1:extra2:extra3
15|||-R:--bogus
16|||--:cf1
TABLE
      ;;
    chgrp)
      # #1488 wiring wave A1: group change is ACL-backed on Windows; the
      # matrix pins operand/option grammar and error shapes.
      cat <<'TABLE'
01|||
02|||0:cf1
03|||+0:cf1
04|||999999:cf1
05|||nosuchgroupxyz:cf1
06|||cf1
07|||cf1:cdir
08|||-R:0:cdir
09|||-f:0:cf1
10|||-v:0:cf1
11|||-c:0:cf1
12|||--reference:cf1:cf1
13|||--from=0:0:cf1
14|||--bogus
15|||--help
16|||--version
17|||--:0:cf1
18|||0
19|||0:cf1:cdir:extra
TABLE
      ;;
    chmod)
      # #1488 wiring wave A1: mode grammar (octal, symbolic, reference)
      # plus traversal flags; Windows ACL mapping is the value model.
      cat <<'TABLE'
01|||
02|||644:cf1
03|||0644:cf1
04|||u+x:cf1
05|||a=rwx:cf1
06|||--reference:cf1:cf1
07|||-R:755:cdir
08|||-v:644:cf1
09|||-c:644:cf1
10|||-f:644:cf1
11|||-H:644:cf1
12|||-L:644:cf1
13|||-P:644:cf1
14|||--preserve-root:-R:755:cdir
15|||--bogus
16|||--help
17|||--version
18|||badmode:cf1
19|||644
20|||--:644:cf1
TABLE
      ;;
    chown)
      # #1488 wiring wave A1: owner[:group] grammar, numeric IDs, --from,
      # reference and traversal flags.
      cat <<'TABLE'
01|||
02|||0:cf1
03|||0:0:cf1
04|||:0:cf1
05|||user:cf1
06|||nosuchuserxyz:cf1
07|||--reference:cf1:cf1
08|||-R:0:cdir
09|||-h:0:cf1
10|||-H:0:cf1
11|||-L:0:cf1
12|||-P:0:cf1
13|||-v:0:cf1
14|||--from=0:0:0:cf1
15|||--dereference:0:cf1
16|||--bogus
17|||--help
18|||--version
19|||0
20|||--:0:cf1
TABLE
      ;;
    date)
      # #1488 wiring wave A1: fixed-clock probes only (noargs prints the
      # wall clock and can never byte-match across sides). The -f - /
      # --file=- rows exercise the ABI-v2 stdin election.
      cat <<'TABLE'
01|TZ=UTC||-d:@1234567890:+%F_%T
02|TZ=UTC||-d:@1234567890:+%s
03|TZ=UTC||-u:-d:@1234567890
04|TZ=UTC||-R:-d:@1234567890
05|TZ=UTC||--rfc-3339=seconds:-d:@1234567890
06|TZ=UTC||-I:-d:@1234567890
07|TZ=UTC||-Iseconds:-d:@1234567890
08|TZ=UTC||-f:df1
09||2020-01-02 03:04:05 UTC|-f:-u:+%F_%T
10||2020-01-02 03:04:05 UTC|--file=-:-u:+%F_%T
11|TZ=UTC||--file=df1:+%F
12|TZ=UTC||-r:df1:+%F_%T
13|TZ=UTC||+%F:-d:@1234567890
14|TZ=UTC||+%Q:-d:2020-01-02
15|||--bogus
16|||--help
17|||--version
18|||-d
19|||+%F:-d:@1234567890
20|||--resolution
TABLE
      ;;
    du)
      # #1488 wiring wave A1: the --files0-from=- row exercises the ABI-v2
      # stdin election (stdin carries one NUL-less name; GNU accepts the
      # EOF-terminated final name).
      cat <<'TABLE'
01|||dudir
02|||-s:dudir
03|||-a:dudir
04|||-h:dudir
05|||-c:dudir
06|||-d:1:dudir
07|||-b:dudir
08|||-k:dudir
09|||-m:dudir
10|||-l:dudir
11|||-L:dudir
12|||-P:dudir
13|||-S:dudir
14|||-t:1:dudir
15|||-x:dudir
16|||-0:dudir
17|||--files0-from=dulist
18||dudir/f1|--files0-from=-
19|||--files0-from=dulist:dudir
20|||nosuch-du-path-xyz
21|||--bogus
22|||--help
23|||--version
24|||--:dudir
25|||dulist:dudir
TABLE
      ;;
    groups)
      # #1488 wiring wave A1: membership content is the platform value
      # model (NetUserGetGroups on both Windows sides); GNU-shaped errors
      # gate strictly.
      cat <<'TABLE'
01|||
02|||--help
03|||--version
04|||--bogus
05|||-z
06|||nosuch-groups-user-xyz
07|||a:b
08|||--
09|||--help:x
10|||--version:x
11|||.
12|||-
13|||-h
14|||a:b:c
15|||@EMPTY@
16|||--:.
TABLE
      ;;
    mkfifo)
      # #1488 wiring wave A1: real filesystem object creation relative to
      # the probe cwd; -m/-Z option grammar and error shapes.
      cat <<'TABLE'
01|||mf1
02|||mf1:mf1b
03|||-m:600:mf1
04|||-m:000:mf1
05|||-m:u+x:mf1
06|||-m600:mf1
07|||-Z:mf1
08|||-Z:ctx:mf1
09|||-Zctx2:mf1
10|||--mode=600:mf1
11|||--context:mf1
12|||--bogus
13|||--help
14|||--version
15|||--:mf1
16|||mf1
TABLE
      ;;
    mknod)
      # #1488 wiring wave A1: device-node grammar (p/c/b types on Windows
      # are the platform value model); option and operand errors gate.
      cat <<'TABLE'
01|||mn1:p
02|||mn1:c:1:3
03|||mn1:b:1:3
04|||-m:600:mn1:p
05|||-m:0644:mn1:c:1:3
06|||-Z:mn1:p
07|||-Zctx:mn1:p
08|||--mode=600:mn1:p
09|||--context:mn1:p
10|||--bogus
11|||--help
12|||--version
13|||--:mn1:p
14|||mn1
15|||mn1:x
16|||mn1:p:1
17|||mn1:p:1:2:3
TABLE
      ;;
    mktemp)
      # #1488 wiring wave A1: template parsing, -d/-u/-q/-p/-t/--suffix
      # shapes; created names differ per run so rows compare rc/stderr and
      # the stdout template shape only where deterministic (-u, errors).
      cat <<'TABLE'
01|||mt1.XXXXXX
02|||-d:mt1.XXXXXX
03|||-u:mt1.XXXXXX
04|||-q:-u:mt1.XXXXXX
05|||--suffix=.txt:mt1.XXXXXX
06|||-p:mtdir:mt1.XXXXXX
07|||-t:mt1.XXXXXX
08|||--tmpdir=mtdir:mt1.XXXXXX
09|||mt1
10|||mt1.XXX
11|||mt1.XXXXXX:extra
12|||-d:-u:mt1.XXXXXX
13|||-u:-d:mt1.XXXXXX
14|||TMPDIR=mtdir:-u:mt1.XXXXXX
15|||--bogus
16|||--help
17|||--version
18|||--:mt1.XXXXXX
19|||-u
20|||nosuchdir/mt1.XXXXXX
TABLE
      ;;
    nice)
      # #1488 wiring wave A1: leading-'+' optstring; child rows are
      # platform-bound (GNU resolves Linux children, Windows sides run
      # cmd) and compare old-vs-new only — the 1.x applet answered 126 for
      # every child, the wired body propagates the real exit code.
      cat <<'TABLE'
01|||
02|||--help
03|||--version
04|||--bogus
05|||-n:5
06|||-n
07|||nosuch-nice-child-xyz
08|||cmd:/c:exit:3
09|||-n:5:cmd:/c:exit:0
10|||-n:-5:cmd:/c:exit:0
11|||-n:999:cmd:/c:exit:0
12|||--:cmd:/c:exit:0
13|||--adjustment=5
14|||cmd:/c:echo:hi
15|POSIXLY_CORRECT=1||cmd:/c:exit:0
TABLE
      ;;
    pathchk)
      # #1488 wiring wave A1: NAME validation; -p/-P/--portability modes,
      # empty/leading-dash/non-portable names, length limits.
      cat <<'TABLE'
01|||pcdir/a/b
02|||-p:pcdir/a/b
03|||-P:pcdir/a/b
04|||-p:-P:pcdir/a/b
05|||--portability:pcdir/a/b
06|||-p:-P
07|||pcdir/a:pcdir/b
08|||-x:pcdir/a
09|||-
10|||--
11|||--help
12|||--version
13|||--bogus
14|||-p:pcdir/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z
15|||-p:a b
16|||-p:a:b
17|||-P:-foo
18|||-p:CON
19|||@EMPTY@
20|||--:pcdir/a/b
TABLE
      ;;
    pinky)
      # #1488 wiring wave A1: finger-style user lookup; content is the
      # platform value model, option grammar and errors gate.
      cat <<'TABLE'
01|||
02|||--help
03|||--version
04|||--bogus
05|||-z
06|||nosuch-pinky-user-xyz
07|||lfbwhips
08|||-l
09|||-b
10|||-f
11|||-w
12|||-i
13|||-q
14|||-h
15|||-p
16|||-s
17|||-l:-s
18|||-s:-l
19|||--help:x
20|||--version:x
21|||a:b
22|||--:.
TABLE
      ;;
    runcon)
      # #1488 wiring wave A1: SELinux contexts are unsupported on Windows;
      # rows pin the 125 usage-error contract and option grammar.
      cat <<'TABLE'
01|||
02|||system_u:system_r:httpd_t:s0
03|||system_u:system_r:httpd_t:s0:true
04|||-c:true
05|||-u:user_u:true
06|||-r:role_r:true
07|||-t:type_t:true
08|||-l:s0:true
09|||-c:-u:user_u:-r:role_r:-t:type_t:true
10|||--bogus
11|||--help
12|||--version
13|||--help:x
14|||ctx
15|||ctx:cmd:/c:exit:0
16|||--:ctx:true
TABLE
      ;;
    tty)
      # #1488 wiring wave A1: stdin is </dev/null on every row, so all
      # sides deterministically print "not a tty" rc 1 — the real-handle
      # contract. -s suppresses output, keeping rc only.
      cat <<'TABLE'
01|||
02|||-s
03|||-s:-s
04|||extra
05|||--bogus
06|||--help
07|||--version
08|||--help:x
09|||-z
10|||--:extra
11|||--quiet
12|||--silent
TABLE
      ;;
    who)
      # #1488 wiring wave A1: session enumeration is the platform value
      # model; option grammar and operand errors gate. The TIME-width
      # divergence (1.x width 12, GNU 16) is the documented r=c:no class.
      cat <<'TABLE'
01|||
02|||-H
03|||-q
04|||-a
05|||-b
06|||-d
07|||-l
08|||-m
09|||-p
10|||-r
11|||-s
12|||-t
13|||-T
14|||-u
15|||-w
16|||--lookup
17|||--message
18|||--writable
19|||--heading
20|||--count
21|||--all
22|||--bogus
23|||--help
24|||--version
25|||whmiss
26|||a:b:c
27|||--
28|||am:i
29|||mom:likes
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
