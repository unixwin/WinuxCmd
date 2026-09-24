# WinuxCmd audit round 2c — remaining commands (chmod…mkpasswd)

Date: 2026-09-21. Final coverage round: with this file, all 178 commands have
been audited. Group A is source-based vs coreutils 9.7; groups B/C are
doc-based. Companion to audit-coreutils-9.7.md, audit-non-coreutils.md,
audit-procps-misc.md, audit-util-linux.md, audit-wpm.md.

## Group A — coreutils (source-based)

### chmod (chmod.cpp)
- [BUG] `-R` swallows child failures (results discarded at chmod.cpp:764,810;
  top level inspects only the root call 929-933) — GNU exits nonzero if any
  file in the tree fails.
- [BUG] symbolic mode grammar misses copy forms `u=go`/`o=u` and empty `=`
  (clear bits), rejected as "invalid mode" (chmod.cpp:355-367).
- [COSMETIC] -v/-c transition display is synthetic and can overstate;
  duplicate error print under -f (872-879).
- [PLATFORM-OK] w-bit↔readonly mapping, -H/-L/-P/-h no-ops,
  --preserve-root default-off matches chmod.c:435,467.

### chown (chown.cpp)
- [COSMETIC] verbose "retained as" lines on stderr while "changed ownership
  of" is stdout (GNU: all on stdout); --dereference beats -h regardless of
  order (GNU last-wins) (chown.cpp:697-699,829-860).
- [PLATFORM-OK] numeric uid/gid via RID→SID substitution.
- Otherwise matches chown.c/chown-core.c closely (spec parsing, --from,
  --reference, -R, preserve-root, error strings).

### chgrp (chgrp.cpp)
- [BUG] `--reference` is a silent no-op — change_group returns early and
  never calls SetNamedSecurityInfoW (chgrp.cpp:373-383).
- [BUG] bare numeric GIDs rejected unless an account with that name exists
  (chgrp.cpp:123-142,355); GNU accepts all-digit groups.
- [BUG] --from invalid group reports "invalid user" (chgrp.cpp:361);
  recursive dir failures ignored for exit status (488-491).
- [COSMETIC] -f declared twice; -h/-H/-L/-P parsed and discarded; verbose
  wording differs.

### chcon / chroot / runcon
[PLATFORM-OK] full GNU option surface with explicit unsupported errors
(exit 1/125 as appropriate). [COSMETIC] minor wording ("no such directory"
vs "No such file or directory"; runcon no-args lacks usage dump).

### pr (pr.cpp)
- [BUG] `-D/--date-format` advertised but unimplemented (pr.cpp:93-94,719-721).
- [BUG] `-N` unvalidated before std::stoi — `pr -N abc` throws (crash risk)
  (pr.cpp:1148,1386); pr.c validates via xstrtoui.
- [COSMETIC] --pages ranges unsupported; `-F` long alias bound to "-f"
  colliding with --form-feed (pr.cpp:96); stale help disclaimer.
- Verified good: page structure, -d, -s/-w join semantics, -S, -m conflicts,
  "+" operand, column layouts.

### logname / hostid
logname matches (GetUserNameW = closest Windows login equivalent).
hostid: [COSMETIC] extra operands silently ignored (GNU errors).

## Group B — ncurses family (doc-based)

- clear: matches; [COSMETIC] always clears scrollback (no -x).
- reset: [BUG-minor] -I inverted — prints init strings when told not to
  (reset.cpp:97-100).
- tput: [COSMETIC] 5 caps, operand params ignored (`tput cup 5 5` → \033[H).
- tic: [BUG-minor] exit 0 when source given on the unsupported path (tic.cpp:58).
- toe: fabricated entries, [COSMETIC].
- infocmp: [BUG-minor] ignores TERMNAME argument, always "succeeds".

## Group C — misc (doc-based)

- hexdump: [BUG] no duplicate-row squeeze at all (`-v` no-op in the wrong
  direction, hexdump.cpp:144-282); -e advertised but rejected; -f/-L dead.
- look: matches. stty: [BUG] hardcoded `speed 38400 baud; rows 50; columns
  120` and constant -g output (stty.cpp:123,198-201) — not replayable.
- uptime / which / ldd / pldd / hmac256 / mpicalc / chattr / cygpath /
  regtool / mkgroup / mkpasswd: matches (thin adaptations, documented).
- tzset: [BUG-minor] prints `TZ=<value>` so the documented
  `export TZ=$(tzset)` sets the wrong value (tzset.cpp:37 vs 127,132).
- lsof: [BUG-minor] `-t` redefined as --timeout-ms (upstream: terse PID-only
  output) (lsof.cpp:70-71); -a redefined; -d advertised no-op; always exit 0.
- getfacl: [BUG] six advertised options dead: -c -R -a/-d -s -t parsed and
  never read (getfacl.cpp:154-207).
- lsattr: [BUG-minor] `', '` multi-char literal appended as one byte → space
  separator instead of comma (lsattr.cpp:81).

## Top 10 fixes

1. chgrp --reference no-op → implement via SetNamedSecurityInfoW.
2. getfacl: honor -c/-R/-a/-d/-s/-t (six dead advertised options).
3. chmod/chgrp: propagate recursive child failures to exit code.
4. pr: validate -N before stoi (crash), implement or unlist -D.
5. stty: query real console size/speed; derive -g from actual mode.
6. hexdump: implement squeeze + -v, remove dead -e/-f/-L.
7. chgrp: accept numeric GIDs; fix "invalid user" → "invalid group".
8. lsof: restore -t as terse; move timeout to long-only.
9. tzset: emit bare zone id so `export TZ=$(tzset)` works.
10. ncurses minors: reset -I inversion, tic/infocmp exit codes.
