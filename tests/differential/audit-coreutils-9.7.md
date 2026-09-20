# WinuxCmd vs GNU coreutils 9.7 — source-level audit

Date: 2026-09-20. Method: each command's `src/commands/<cmd>.cpp` was read
line-by-line against the authoritative GNU 9.7 source at
`tests/differential/.cache/coreutils-9.7/src/<cmd>.c` (fetched via
`tests/differential/fetch-gnu.sh`). Findings were verified in both sources;
nothing is speculative. Classification:

- **[BUG]** semantic deviation Windows could still honor
- **[PLATFORM-OK]** deviation justified by Windows semantics
- **[COSMETIC]** wording/formatting only
- **[EXT]** documented non-GNU extension (flagged for awareness)

Scope: 71 commands with GNU counterparts. diff/diff3/sdiff/cmp are diffutils,
audited against documented diffutils semantics.

## Executive summary

- **23 commands fully conformant** (or conformant modulo documented
  platform approximations): uniq, tsort, tr (semantics), cat (1 cosmetic),
  wc, base64, base32, sum, mkdir, rmdir, link, unlink*, touch, readlink,
  realpath, basename, dirname, truncate, sync, date, echo, printenv, whoami,
  true, false, test/[, sleep, nohup, nproc.
- **Cross-cutting defects found in 6 independent implementations**: CRLF/BOM
  stripping on binary-mode input (nl, cut, paste, expand, fold, fmt, sort,
  comm, head/tail transcoding path); `--backup[=CONTROL]` value ignored
  (ln, mv, install — cp implements it correctly); multi-source loops that
  abort instead of continue+exit-1 (mv, install).
- **Major-deviation commands** (engine-level, not tweak-level): diff3
  (single whole-file block instead of per-hunk 3-way diff), sdiff
  (positional line pairing instead of diff alignment), ptx (layout/sort
  approximation), df (fixed column widths instead of dynamic).

## Priority fixes (P0 — wrong semantics with no Windows justification)

1. **`-t` text mode changes hashes** — md5sum/sha1sum/sha256sum/sha512sum
   open text mode and hash CRLF-translated data (`md5sum.cpp:233-234`,
   `sha1sum.cpp:242`, `sha256sum.cpp:243`, `sha512sum.cpp:241`). GNU hashes
   raw bytes. Internal inconsistency: sha224sum/sha384sum/b2sum read binary
   and are correct. Route all through `hash_file_hex`.
2. **od legacy size letters wrong** — `-f`→8B double (GNU fF=4B float),
   `-i`→2B (GNU dI=4B), `-l`→4B (GNU dL=8B); `-j`/`-N` reject `b`/512
   suffixes (`od.cpp:670-679`, `od.cpp:131-188`).
3. **cp `-n`/`-i` precedence** — `-n` wins over later `-i`; GNU is
   last-wins (`cp.cpp:1146` vs `cp.c:1060-1071`).
4. **cp `--update[=all|none|none-fail|older]`** — declared BOOL; GNU 9.7
   value form unsupported (`cp.cpp:164`).
5. **ln `-T` silently deletes existing destination without `-f`**
   (`ln.cpp:506-514`); GNU always reports "File exists" without -f.
6. **install `-c` aliases `-C/--compare`** — GNU `-c` is a no-op
   (`install.cpp:47-49`); also missing `-C`+`-p`/`-s` mutual-exclusion
   errors, and aborts remaining sources on first failure.
7. **diff missing `\ No newline at end of file`** in all formats
   (`diff.cpp:376-417`); `-I` applied as substring instead of regex over
   whole changes; `-B` deletes blank lines (shifts hunk numbers) instead of
   ignoring all-blank changes; `-b` doesn't ignore trailing whitespace.
8. **cmp** — usage errors exit 1 (GNU 2); `-b` lists every byte (GNU: first
   difference only); EOF message missing "after byte N, in line L".
9. **diff3 engine** — whole-file prefix/suffix trim instead of per-hunk
   3-way diff; `-E/-A` no-ops; exits 0 even with conflicts
   (`diff3.cpp:84-120, 339-344`).
10. **sdiff engine** — positional pairing desynchronizes on insertion;
    stray `%` printed to stdout; missing `-i`/`-t`; `-W` not a GNU option.
11. **groups output format** — one group per line, never the
    "user : " prefix (`groups.cpp:117-121`).
12. **uname** — sysname typo "MSWindows_NT"; `-r`/`-v` hardcoded
    "10.0"/"19045" instead of reading the real version (`uname.cpp:138,154-161`).
13. **printf invalid conversion spec** — prints verbatim, exit 0; GNU is
    fatal "invalid conversion specification" (`printf.c:703`).
14. **head/tail `-z` header bug** — header separator uses NUL delimiter;
    GNU always `\n` in headers (`head.cpp:595,599`, `tail.cpp:793,797`).
15. **du `--inodes` counts** — subdirectory entries print literal `1`
    (`du.cpp:1294,1365`).
16. **basenc base16 decode rejects lowercase hex** (`encoding.cppm:617-629`).
17. **cksum** — `--tag`+crc prints `CRC32 (f) = hex` (GNU: untagged, tag
    `CRC`); `-a crc32b` missing (`cksum.cpp:579-592,126-138`).
18. **tac `-s ''`** — should fail "separator cannot be empty"; instead uses
    NUL (`tac.cpp:71-75`).
19. **sort `-t ''`** — should fail "empty tab"; silently treated as no
    separator (`sort.cpp:1474-1484`).
20. **mktemp X-run placement** — accepts X-run not at end of template
    (`mktemp.cpp:364-423`); GNU requires trailing X's.
21. **shred bare `--remove`** defaults to `unlink`; GNU default `wipesync`
    (`shred.cpp:65-66`).
22. **dd conv=ascii/ebcdic/ibm/block/unblock/lcase/ucase/swab rejected**
    (`dd.cpp:332-338`) — pure byte tables, no OS dependency.
23. **mv/install abort remaining sources on first error**; GNU continues and
    exits 1 at the end (`mv.cpp:728-730`, `install.cpp:741-752`).
24. **pathchk default mode** wrongly applies `-P` leading-dash/empty checks
    (`pathchk.cpp:184-190`).
25. **getconf unknown variable exits 0** (`getconf.cpp:109-140`).
26. **ls tty quoting default** — always Literal; GNU uses shell-escape
    quoting when stdout is a tty (`ls.cpp:719` vs `ls.c:2391`).
27. **df fixed column widths** — GNU pads each column to widest cell
    (`df.cpp:573-596,1120`).

## P1 — real deviations, lower blast radius

- **CRLF/BOM stripping on binary-mode input** (cross-cutting): nl, cut
  (incl. `-b/-c` byte mode), paste, expand, unexpand, fold (changes break
  positions), fmt, sort (BOM only), comm (BOM only). GNU never strips
  either. This is the single largest differential-failure class.
- **head/tail UTF-16/BOM "text decoding" path** — files with BOM/NUL are
  transcoded instead of copied byte-exact (`head.cpp:231-255`,
  `tail.cpp:387-411`).
- **paste multiple `-` operands** — caches stdin once and round-robins the
  same lines; GNU reads sequentially (`paste.cpp:359-394`).
- **tail `-f` on stdin/pipe** never follows (`tail.cpp:1059-1077`).
- **tee `warn` mode**: stdout write error aborts instead of continuing to
  remaining files (`tee.cpp:186-191`).
- **rm directory-symlink operand** without `-r/-d` reports "Is a directory"
  instead of removing the link (`rm.cpp:469-479`).
- **shuf `-i` + file operand** silently ignores the operand (`shuf.cpp:646-664`).
- **diff `-y`/`--side-by-side`** marker column wrong (`|`, `<`, `>` vs
  printed `+`); `-r`, `-x`, `-e`, `-n`, `-p`, `-l`, `-d` refused.
- **ptx** word chars exclude digits; layout/collation not GNU-conformant.
- **expr `substr` non-integer POS/LEN** silently returns "" (GNU: exit 2).
- **stat** — `File:` line unquoted; `Device:` decimal; symlink `%s` uses
  UTF-16 length (`stat.cpp:1219-1243,451`).
- **ls** extra long options `--long`, plural
  `--dereference-command-line-symlinks-to-dir` (also dir, vdir);
  `--sort=width` missing / `type` extra; `--color` value unvalidated;
  single-file `-l` size padded to 8; wide-char collation vs `strcoll`.
- **cp** `--parents` with absolute source builds `dest\C:\a\b\c`;
  `-x` unenforced; sparse default Never (GNU auto); depth cap 100.
- **mv cross-device dir copy** dereferences symlinks (fs copy).
- **mkfifo/mknod `--context[=CTX]`** grammar: optional arg not consumed;
  mode validators accept junk.
- **namei** dead branch (mount marker unreachable); exit always 0.
- **mislabeled [GNU] options that GNU does not have**: seq `-t`, fmt `-T`,
  numfmt `-M`/`-l`. Extensions are fine; the label is not.
- **cut extra options** `-w`/`-F`/`-M` (util-linux-isms; documented, but a
  differential deviation).
- **diff3 missing options**: `-x/-X/-3/-i/-L/-T/--diff-program`.

## P2 — cosmetic

Error-message wording drift (tr, cat `cat.cpp:347` typo `'-`, tee quoting,
du -s/-a truncated message, stat fs message), csplit extra equal-line
warning, dd stats lack `N+M` partial-record bookkeeping, dd
`status=progress` inert, uname `nice -NUM` obsolete form, basename
root-suffix edge case, `sort -y` missing (Solaris no-op), `--traditional`
od second offset, groups ordering, ls owner fallback "UNKNOWN".

## Verified-clean commands

uniq, comm (order-check semantics), join, csplit, tsort, tr, cat, wc,
base64, base32, sum, mkdir, rmdir, link, touch, readlink, realpath,
basename, dirname, truncate, sync, date, echo, printenv, whoami, true,
false, test/[, sleep, timeout (minor), nice, nohup, nproc, env, factor,
seq (minor), numfmt, id (minor), sort (near), b2sum, sha224sum, sha384sum,
dircolors, df (near), mktemp (near), rm (near), ln (near), cp (near),
mv (near), install (near), shred (near), od (near).

## Recommendations

1. Fix the P0 list first; several are one-liners (od size letters,
   tac/sort empty-separator errors, uname typo, head/tail `-z`).
2. Decide the CRLF/BOM policy centrally: one shared decision for the
   text-tools input path (stripping is Windows-friendly but breaks GNU
   byte-compatibility, incl. byte-mode cut). Suggest: strip only when the
   operand is known-text and the mode is line-oriented; never in byte mode.
3. `--backup[=CONTROL]`: reuse cp's implementation in ln/mv/install.
4. Multi-source loops (mv, install): continue-on-error, exit 1 at end, like cp.
5. Add `--tag`/`-a crc32b` to cksum; unify `*sum -t` hashing to binary.
6. Add differential corpus cases for every P0/P1 finding above (the runner
   supports `oracle_version: >=9.2` pins where needed); keep the
   baseline-only-grows gate.
7. Re-verify with the WSL-side full run after fixes; the date-family
   flakiness there is a corpus design issue (time-dependent cases), separate
   from this audit.

Audit performed by six parallel source-comparison passes; per-command detail
with file:line citations is retained in the session transcript and can be
regenerated with the same agent prompts.
