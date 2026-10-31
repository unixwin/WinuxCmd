# WinuxCmd #1127 — P2c cluster report (misc tools)

- Branch: `fix/1127-p2c-misc-tools` · Draft PR: **unixwin/WinuxCmd#1469**
- i18n companion: **unixwin/winuxcmd-i18n#26** (draft; VERSION 0.6.24 → 0.6.25)
- Trackers: Refs #1127, Refs #1462 on every commit. Nothing merged.
- Method: oracle-source-first per the 2026-10-31 ruling. cpio claims cite
  cpio-2.15 sources, logger/column/col cite util-linux-2.39.3 sources
  (in code comments). **file / less / man / time have no local oracle
  source: every behavioral claim for them is DOC-BASED** (documented
  contracts; file's spellings/outputs additionally probed against a
  local file-5.45 binary) and flagged for the W8 audit cluster.
- Scope source: `tests/differential/help-coverage-raw.json` missing-option
  lists for the nine tools (55 options total).

## Per-tool status

### time — 6/6 implemented (DOC-BASED)
`--format` (resource specifiers %e %E %U %S %P %M %K %D %p %X %Z %F %R %W
%c %w %I %O %r %s %k %x %C %%, escapes \n \t \\; counters without a Windows
source expand to 0; %M/%R from GetProcessMemoryInfo), `--output` (opened
before the clock starts; open failure → exit 125, command not run),
`--append`, `--verbose` (GNU field list), `--quiet` (suppresses the
abnormal-termination note), `--portability` (the real GNU long name for
-p; the port's old `--posix` kept as a compat alias). 6 unit tests.
Already-present: none of the six. Deferred: none.

### locale — 2/2 implemented
`-a`'s long name corrected to `--all-locales` (`--all` kept as a compat
alias row), `--usage` (glibc-modeled short usage, exit 0). 2 unit tests.

### col — 6/6 implemented (oracle: util-linux col.c)
Long names from the oracle longopts table (`--no-backspaces --fine
--pass --tabs --spaces --lines`); `-l` default corrected 128 → **32**
(col.c:88, 651); `-h`/`-x` mutual exclusion; the default space→tab
**compression implemented** per col.c:288-303 (previously tabs were never
emitted and `-x` semantics were inverted); `bad -l argument` error text.
5 unit tests + 3 differential cases.

### logger — 7/7 implemented + dead faces made real (oracle: logger.c)
New: `--socket` (`-u`, AF_UNIX datagram), `--socket-errors` (required
argument on|off|auto, logger.c:1141/891; invalid → warn + auto),
`--msgid`, `--sd-id`/`--sd-param` (ordered via string_occurrences;
reserved `[timeQuality tzKnown="1"]` element first, logger.c:783ff),
`--octet-count` (RFC 6587 framing; newline framing otherwise),
`--journald[=FILE]` (FIELD=value entry, logger.c:338ff; MESSAGE/
PRIORITY/SYSLOG_IDENTIFIER mapped onto the port's sinks).
Real wire syslog: `-n` now actually sends (UDP default, TCP with `-T`,
last of `-T`/`-d` wins, `-P` port; RFC 5424 default for remote, RFC 3164
with `--rfc3164`).
Declared-but-dead faces fixed: `-f` logs the file line-by-line; stdin is
logged **per line** (was: concatenated into one message); `-e` drops
empty lines; `--prio-prefix` parses per-line `<NNN>` prefixes (0..191,
facility 0 inherits the default); `-S` truncates; `-i`/`--id` is an
optional-argument option (`--id=PID`); default tag is the login name
(logger.c:945, was hardcoded "logger"); `--no-act` suppresses writes;
event-log write failures exit nonzero. 10 unit tests.
Deferred: journal fields other than MESSAGE/PRIORITY/SYSLOG_IDENTIFIER
have no Windows sink and are dropped (documented in code).

### man — 8/8 implemented (DOC-BASED, man-db contracts)
`--all` (every operand now displays in turn — previously only the first;
missing page errors per name, exit 1 if any missing; `-w` loops too),
`--apropos` (registry name+description search, "name - description"
lines, `<word>: nothing appropriate.` + exit 1 when nothing matches),
`--config-file` (nonexistent → error; existing → accepted, settings do
not apply to the built-in registry), `--debug` (lookup traces on stderr),
`--ascii` + `--default` (accepted; pages are plain ASCII),
`--catman` + `--ditroff` (decline-with-diagnostic, exit 1, like -t).
The REGISTER [DIFFERS] list no longer claims the implemented options.
7 unit tests.

### file — 8/8 implemented (DOC-BASED; probed vs local file-5.45)
`--mime-encoding` (charset part of the existing classification),
`--exclude`/`--exclude-quiet` (`-e`; stage switches — soft/ascmagic/
encoding/text gate the port's pipeline stages, apptype/compress/elf/tar
accepted as no-ops; unknown test errors for --exclude, ignored for
--exclude-quiet), `--list` (built-in table dump, no operands needed),
`--debug` (`-d`, stderr traces). Output modes resolve last-one-wins.
Simplified [DIFFERS]: `--extension` and `--apple` derive from the
built-in signature table, mirroring file-5.45's answers for known
formats (pdf/png/`jpeg/jpg/jpe/jfif`/gif/zip/gz/bmp/exe; Apple pairs
`8BIMJPEG`/`8BIMGIFf`/`????BMPp`, `UNKNUNKN` elsewhere — file-5.45
prints UNKNUNKN for nearly everything without full magic entries).
`--compile` (`-C`) declines with a diagnostic (magic is built in).
5 unit tests + 2 differential cases (--mime-encoding charsets, the only
file outputs that are exact without the full magic database).

### cpio — 7/7 implemented + copy-pass (oracle: cpio-2.15)
`--append` (`-A`; copy-out only, requires `-F` — oracle usage text
otherwise, main.c:616; trailer splice per util.c:636), `--block-size`
(×512, final-archive zero padding; `invalid block size`, main.c:124/324),
`--directory` (`-D`; change_dir per main.c:115/util.c:1504 — input is
read first, matching GNU's open order; `-d` creates a missing DIR),
`--dot` (`-V`, copyout.c:831/copyin.c), `--device-independent` +
`--reproducible` alias (main.c:469-472/202-204: inodes renumbered from
1, directory nlink 2; device fields were already 0),
`--absolute-filenames` (member names resolve literally — `/` = current
drive root — instead of folding through the payload-root boundary),
`--dereference` (`-L`; accepted — the port already archives target
content). **Already-present: `--file` (landed earlier, #1422).**
Also implemented: **-p copy-pass mode** (was a stub error), directory
members materialize on extract, zero-length files are archived and
extracted (create used to skip them as "unreadable"). 9 unit tests.
Deferred: symlink members (create stores target content; extract-side
symlink creation is privilege-gated on Windows) — same stance as the
standing code.

### column — 3/3 implemented + the missing default mode (oracle: column.c)
`--fillrows` (`-x` rebound from --output-fields, which stays long-only),
`--table-header-repeat` (`-e` rebound from --table-empty, long-only now;
formfeed-only input line = page break, header row re-emitted),
`--table-noextreme` (`-E` rebound from --table-noescape, long-only now;
required argument per column.c:813 — when the table must shrink to fit,
the named columns' widths give way, libsmartcols semantics).
**Default (non-table) columnation implemented** — it previously echoed
the input verbatim: pitch `(maxlength + 8) & ~7`, columns = width/pitch,
tab/space padding to pitch slots, column-major fill (column.c:669-731);
`-c` width → console → 80 fallback; empty lines dropped unless `-L`.
Also fixed: `--output-width` was read from `--columns` by mistake.
REGISTER text corrected (rows-first is the `-x` behavior, not the
default). 6 unit tests + 2 differential cases.

### less — 8/8 implemented (DOC-BASED)
`--line-num-width` (dynamic -N field width, default 6; negative →
usage error), `--rscroll` (chopped -S lines show the marker char,
default `>`, empty disables), `--file-size` (prompt gains
`bytes <shown>/<total>`), `--no-histdups` (search history drops all
older duplicates), `--no-keypad` (accepted; no keypad init strings
exist on the Windows console), `--mouse` (console switched to mouse
input, wheel = 3 lines/notch, original mode restored by
ConsoleInputMode), `--incsearch` (prompt-line preview: `[line N]` /
`[no match]` / `[invalid pattern]` while typing), `--follow-name` +
**F follow mode** (250 ms polling; growth/truncation reloads and jumps
to the end; identity change stops the follow unless --follow-name
reloads by name; `-K` quit key exits from follow). 2 unit tests
(headless-safe faces only — see below).
Deferred part: --incsearch's full screen-jump preview (the prompt
reports the target line instead); follow is limited to seekable file
documents (not stdin).

## Tests added
- Unit: time 6, locale 2, col 5, logger 10, man 7, file 5, cpio 9,
  column 6, less 2 — all in the existing per-tool suites, auto-globbed.
- Differential: col ×3, column ×2, file ×2 (GNU answers verified against
  the oracle sources or the local file-5.45 binary). Skipped for
  time/logger/man/less/cpio (nondeterministic output, platform sinks,
  registry content, binary archives). A plain `column -t` case was
  deliberately not added — see findings.

## CI status
First full run: `oracle` **passed**; the four Windows jobs failed on a
single compile error in my less follow code (unqualified
`load_seekable_document`, C3861) — fixed in 53528e5 and re-pushed;
awaiting the re-run at delivery time. Known standing noise (not mine):
`regressions/70-env` red on main itself; GitHub cache-400s are infra.

## Unverifiable without Windows/WSL (explicit)
- **All compilation and the unit suite** — MSVC-only project; CI on
  PR #1469 is the only gate. No local build was possible.
- **less interactive faces** — follow mode, mouse wheel, incsearch
  preview, --file-size prompt bytes, rscroll marker and
  --line-num-width rendering need a real console; the test harness
  only exercises the non-console passthrough path. Code-complete,
  headless-unverified.
- **logger wire delivery** — UDP/TCP sends to a real syslog server and
  AF_UNIX datagram delivery (`--socket`, needs Windows 10 1803+ for
  afunix.h support) are implemented against Winsock but no server
  exists in this sandbox; formats are unit-inspected only. The `-i`
  optional-argument parse shape (first short-option use of
  OPTIONAL_STRING_TYPE) is covered only by the new unit tests in CI.
- **man pager faces** — page_text under a real console (unit tests
  cover the fallback print path).
- **cpio --absolute-filenames** — drive-rooted member writes are not
  exercised by tests (they would write outside the sandbox TempDir).

## Findings for the collector (outside recorded scope, not fixed)
1. **column -t separator divergence**: libsmartcols' default column
   separator is one space (smartcolsP.h: `colsep(tb) ?: " "`) on top of
   cell padding; the port's table renderer pads cells and adds a
   two-space separator, so GNU and port table output differ by one
   space per column. Pre-existing; no differential -t case added.
2. **i18n extractor gap**: cpio and time option descriptions never
   extract into catalog keys (only description/synopsis do — true on
   master too), so their option strings have no zh-CN keys to
   translate. The companion PR covers everything that extracts
   (42 added / 10 updated / 5 stale col keys removed).
3. **cpio create-mode -v prints member names to stdout** (safePrintLn),
   which interleaves with the archive when no -F is given; GNU prints
   them to stderr. Pre-existing; left untouched to keep the cluster
   scoped.
