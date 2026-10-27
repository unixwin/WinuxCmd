# Changelog

All notable changes to WinuxCmd are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow the
repository release tags.

## [1.1.21] - Unreleased

Release-prep status: this section is being prepared ahead of the release.
The release baseline is `main` **after** PRs #1414, #1415 and #1423 merge;
before tagging, this section must be re-reconciled against the final
`main` and the three `[PENDING]` entries below either promoted to regular
entries or dropped.

### Added

- **FFI acceleration library (experimental, opt-in, default off)**: the
  package now carries the FFI payload described in
  `packaging/ffi/README.md` — `usr/bin/ffi/v1.1.21/winuxcore.dll` with its
  `tiers.tsv` tier table, a `usr/bin/ffi/current.txt` version pointer, and
  a package SHA-256 manifest that includes the DLL. Nothing changes
  unless the host shell is started with `NIU_WINUXCMD_FFI=1`; the PATH /
  spawn path remains the default, the fallback, and the reference
  implementation. The DLL in this release is **unsigned**: expect possible
  SmartScreen / Microsoft Defender attention when it is loaded, and see
  `usr/bin/ffi/FFI-EXPERIMENTAL-NOTICE.md` in the installed tree before
  enabling it. A signed DLL (same certificate line as the rest of the
  product) is a hard gate for any non-experimental FFI release.
- **README**: winget install row (package ID `caomengxuan666.WinuxCmd`)
  and a note on SignPath Foundation code signing.

### Fixed

- **shared operand boundary (payload-root audit, #1364; residuals
  #1385)**: `locate -d`, `stat`, `cygpath`, `updatedb`, `mktemp` and
  `patch` file reads now fold shell-namespace spellings through the
  shared operand boundary instead of resolving a bare `/` (or a dialect
  spelling) against the caller's drive; the remaining operand-boundary
  residuals across the command set were routed through the same fold.
- **find**: `-printf` time directives completed with verbatim unknowns,
  GNU flag semantics and exact `%X@` (#1379); GNU time-predicate windows,
  fractions and relative `-newermt` (#1311); symbolic copy clauses in
  `-perm` (#1314); `-size` applies to every file type (#1310); the full
  GNU `-type` letter set and comma lists (#1377); file-system loops under
  `-L` detected instead of expanded (#1378); traversal stops at `-quit`
  even with no match (#1316); `-exec` spawn failures diagnosed and `{}`
  substituted in command position (#1315); default `-print` suppressed
  for `-ls`/`-fls`/`-quit` with full ls fields (#1309); `-mindepth` above
  `-maxdepth` is a never-matching range (#1312); `--opt=value` forms and
  a leading `--` no longer misroute as traversal roots (#1317); find owns
  its diagnostics instead of the generic parser (#1380); directory times
  are sampled before the enumeration open (#1402).
- **option parser and dispatch (GNU conformance)**: bool long options
  reject `--flag=value` (#1301); an explicit empty `--int=` value no
  longer swallows the next argv (#1302); arity-aware `--help`/`--version`
  interception and abbreviation expansion (#1303); `POSIXLY_CORRECT`
  honored — option permutation stops at the first operand (#1304);
  GNU-shaped option-error tails (#1305); short clusters with
  optional-value options keep parsing, including `-NUM` forms for
  uniq/diff/split (#1306).
- **invented options removed (#1355, #1411)**: options GNU does not have
  are no longer accepted — `seq -t/--terminator`, `tee --diagnose`,
  `join --output`, `cp -c` as a `--context` alias, `mv -I`,
  `sort --check-silent`, and a second batch of pseudo-options across the
  set. `nice` stops option parsing at the wrapped command (#1408) and
  warns when the requested priority cannot be applied (#1359).
- **cp / mv / ln / install / shred / patch**: `cp -i` reads answers
  line-wise so later prompts get their own answer (#1324); `mv -u`
  precedence follows GNU (explicit value last-wins, `-u` weak) and the
  overwrite mode is derived from parser occurrences, not raw argv
  (#1325, #1326); `ln` treats a dangling symlink destination as existing
  (#1367) and `--backup` follows the GNU argmatch/numbering rules
  (#1308); a failed `cp --reflink=always` clone no longer leaves a
  half-written destination (#1369); `cp --parents` grafts absolute
  sources below their root (#1366); cross-volume `mv` recreates symlinks
  verbatim instead of copying contents (#1365); `install` uses GNU
  backup naming/control and fails loudly on strip errors (#1357);
  `shred` renames to a fresh random name and verifies the result, with
  the exact GNU `-s` suffix grammar (#1348); `patch` implements context
  and normal (ed-style) diff formats, `-p` reads its INT value, and the
  GNU option surface (`-V`/`--prefix`/`--quoting-style`/`--get`/
  `--read-only`, real `-l` matching) is covered (#1345, #1346, #1381).
- **chmod / mkdir / rm**: chmod's symbolic grammar handles multi-op
  pairs, copy clauses and empty perms (#1331); `mkdir -m` evaluates
  symbolic modes with the full GNU grammar (#1330); `rm` uses typed `-i`
  prompts with GNU prompt selection and the final directory prompt
  (#1327), and verbose wording plus operand-relative display paths
  (#1328).
- **text tools**: the `hexdump -e/-f` format engine (block model,
  squeezed repeated blocks, trailing odd bytes, one continuous stream
  across operands) (#1373); `sort -h` compares (suffix rank, mantissa)
  lexicographically per GNU and `-V` ordering was rewritten to filevercmp
  (#1371, #1340); `comm`/`join` use byte-exact line keys (no CR
  normalization, no BOM stripping) (#1354); `cksum -b/-t` are
  order-sensitive, last spelling wins (#1362); `tac` uses the Basic regex
  dialect for separators (#1361); `seq` walks bundled short options
  per-character and drops the 1e9 iteration ceiling, stopping when the
  output pipe closes (#1356, #1376); `grep` rejects negative context
  lengths with the GNU diagnostic (#1338); `sed` keeps the backslash in
  address escapes and suppresses empty matches that abut a non-empty one
  (#1339, #1284); `uniq` rejects an explicitly negative `-w` (#1341);
  `tail -z` headers and separators stay newlines (#1342); `echo` prints
  `\E` literally (#1343); `stat --printf` supports `\xHH` escapes and an
  explicit format beats `-t` in any order (#1334);
  `ls --quoting-style=locale`, exact-then-unique-prefix option matching,
  and bare `--color` meaning `always` (#1335, #1307); `dirname` folds
  all-separator results to a single slash and the bare `//` operand to
  `/` (#1337).
- **xargs**: quote parsing and `-I` line mode rewritten; `-p` answers are
  read from `CONIN$` first and fail loudly on EOF; child exit codes
  1–254 map to 123 with the 255 abort diagnostic; `-s` accounting uses
  raw argv bytes and a single oversized item errors; `-t` output is
  quoted; `-0`/`-d` are last-wins with the `-E` warning and the six-line
  `--show-limits`; empty input yields zero items in `-0`/`-d` modes.
- **process wrappers**: `timeout` carries the full Linux signal table
  (numeric specs valid only 0..31 and 34..64) and reports 137 on SIGKILL
  timeout (#1384, #1350); `env` stops option parsing at the first
  non-option argument, preserves environment order, and `-i` starts from
  an empty block (#1352, #1353); `stdbuf`/`time`/`runcon`/`chroot` stop
  option parsing at the wrapped command and wrapper option errors exit
  125 (#1375); `file` classifies by content only (extension maps deleted)
  (#1383); `touch -d` accepts the empty string (today 00:00), `@epoch`
  fractions to 100ns, fortnight and weekday items (#1382); `sum` selects
  the algorithm from parsed options, last of `-r`/`-s` wins (#1370);
  `cmp` aligns suffix grammar, count parsing, open-error and EOF reports
  with GNU (#1347); `truncate --size` skips leading whitespace (#1360);
  `mktemp` template precedence and GNU suffix/X rules (#1358); `du`
  counts hard links once and `--inodes` reports pure inode counts
  (#1351, #1372).
- **paths, long paths and console output**: operands are probed and
  opened through extended spellings past `MAX_PATH`, and `winuxcmd.exe`
  is marked `longPathAware` in its manifest (#1288); literal trailing
  `.`/` ` components survive path folding (#1283); a device-named
  on-disk directory no longer vanishes from probes or collapses onto the
  device (#1297); file operands in uniq/hexdump/xxd/dd/tree/patch expand
  through the shared glob path under `WINUX_SHELL_GLOB=native` (#1374);
  stdout write failures surface with the GNU exit mapping instead of
  being swallowed (#1368); optioned `cat`/`head` stream correctly on
  short reads from pipes and stdin, and `grep` matches `.` by UTF-8
  codepoint with GNU `\b`/`\B` word boundaries.
- **wpm**: a successful install now reports the package name, version
  and destination (#1363).

### Build

- GCC module builds: the global-fragment class of failures is addressed
  for the covered modules (`::size_t` provision in module global
  fragments, redundant inner exports dropped) (#1401, #1406, #1407,
  #1412).
- Stray `part{a,b,c}` split artifacts removed from the repository root
  (#1413).

### Pending merge — placeholders, NOT in the baseline this section was drafted against

The following entries describe PRs that are still open. They enter this
release only if the PRs merge before tagging; the pre-release checklist
(`packaging/ffi-interim-release-checklist.md`) requires re-reconciling
this section against the final `main` and deleting any entry whose PR
did not merge.

- **[PENDING PR #1414]** `diff`: pair same-name entries in recursive
  directory comparison (#1344).
- **[PENDING PR #1415]** path layer: trailing dot/space operands and the
  on-disk `NUL.txt` case (#1332, #1333).
- **[PENDING PR #1423]** build: GCC wall class completion and the `cpio`
  operand boundary (#1412, #1422).

## [1.1.20] - 2026-10-22

### Fixed

- **bare drive-root operands** (`ls /e`, `cp /e ...`, every applet that
  touches the filesystem outside the shared `native_path` boundary): a bare
  MSYS drive root (`/e`, `/e/`) bypassed the drive-dialect recognizer and
  decayed to a cwd-drive-relative path, so resolution depended on the
  caller's drive. Glob expansion, digest open-failure probes, `mktemp` and
  the classification probes of head/tail/sort/nl/fmt/fold/comm/join/paste/
  cut and the *sum family now run on the resolved spelling; `df` keeps the
  operand spelling for diagnostics. `sort -T` normalizes the dialect hint at
  temp-dir construction and at chunk cleanup, so spill chunks no longer leak
  onto the hinted drive root (#1230).
- **env --file/-f**: the operand was opened with a raw `ifstream`, so MSYS
  drive-dialect spellings (`/e/...`) decayed to a cwd-relative path and
  missed the named file. `read_env_file` resolves the operand once through
  `normalize_api_operand`; the directory-classification probe runs on the
  native spelling while diagnostics keep the caller's text (#1275).
- **csplit -f/--prefix**: same family — the output-operand prefix reached
  `fopen` unfolded, so `/e/prefix-` created `prefix-00` on the cwd drive.
  The prefix folds through the shared boundary before any creation
  (#1276).
- **build**: the unity build matched command sources with
  `ABS_SOURCE_FILE MATCHES "${COMMANDS_DIR}.*"`, building the regex from a
  literal path. On checkouts under paths containing regex metacharacters
  (e.g. `D:\code\C++Files\...`) the pattern cannot compile and configure
  aborts with "Regular expression ... cannot compile". The metacharacters
  are escaped before the match (#1281).

### Added

- **`--help` footer**: every applet's `--help` output now ends with the
  unixwin.dev portal link, so the documentation surface is reachable from
  the binary itself (#1273). The README and the guides link the portal too
  (#1271).

## [1.1.19] - 2026-10-21

### Fixed

- **chgrp / chown**: #1249 CR follow-ups (#1262). `//e/..`-class operands
  (the POSIX two-slash rule keeps them rooted at `//e`, never `/`) now
  report `cannot access '//e/..': No such file or directory` from both
  commands - chown printed the Win32 invalid-name text where chgrp and
  the MSYS oracle print ENOENT. `chgrp -R` propagates a failing
  descendant through `process_directory` like chown's
  `process_recursive` does, instead of still exiting 0. The operand
  spelling convention is now documented in DOCS/gnu-coreutils: messages
  quote the operand verbatim, so `///` prints as `///` where MSYS shows
  `/` after its own argv-layer collapse (same refusal, display-only
  divergence).
- **opt/ payload argv adapter (gawk CRLF)**: `usr\bin\<cmd>` shims and the
  `winuxcmd <cmd>`/`<cmd>.exe` forwarding path now splice a fixed argv
  prefix ahead of the forwarded arguments for commands that need one. The
  only current entry is `awk`/`gawk` -> `-v BINMODE=3`: the bundled
  ezwinports gawk is a text-mode mingw build whose CRT rewrote every `\n`
  as `\r\n` on write and folded `\r\n` into `\n` on read, while every
  winuxcmd applet does binary stdio (unixwin/niubash#467). That build
  carries gawk's BINMODE feature but ignores the `BINMODE` environment
  variable, so the command-line switch is the only lever; splicing it
  first keeps a user's own `-v BINMODE=...` authoritative (later
  assignments win). The `bin\<cmd>` PATH-entry link for adapted commands
  now hardlinks a forwarder (`winuxcmd-shim.exe`, or `winuxcmd.exe` on an
  installation that predates it) instead of the payload itself, and both
  forwarders probe the installation root at the `bin\` depth as well as
  the canonical `usr\bin\` one.
- **`winuxcmd <verb>` forwarding**: `winuxcmd <name> args` forwarded the
  verb itself as the payload's first argument, so `winuxcmd gawk '{print}'`
  failed with "cannot open file '{print $1}'" and every `layout: shim`
  payload reached through the verb form saw a corrupted argv. The verb
  token is now skipped when the forwarded command line is rebuilt.

## [1.1.18] - 2026-10-20

### Fixed

- **winuxcmd-shim**: `winuxcmd-shim.exe` previously gated child creation
  flags on `GetConsoleMode(GetStdHandle(...))` and passed
  `DETACHED_PROCESS` whenever stdio was redirected (`python - <<'EOF'`,
  pipes, agent runs). That stripped the inherited console
  (`GetConsoleCP() == 0` in the payload), causing every console-subsystem
  grandchild (`python` -> `git.exe`, `node` -> child tools) to allocate a
  fresh `conhost.exe` and flash a visible console window on the desktop
  (unixwin/niubash#466). The shim now releases only a sole-owned *visible*
  auto-allocated console (`IsWindowVisible` + `GetConsoleProcessList == 1`)
  when stdio is non-console, keeps any inherited or hidden console
  (`GetConsoleCP() != 0` -> `flags = 0`), and uses `CREATE_NO_WINDOW`
  (never `DETACHED_PROCESS`) when `GetConsoleCP() == 0`.
- **mkfifo / mknod**: FIFO/device-node path operands resolved against the
  cwd drive (#1235, #1230 sweep). The raw `/e/p` spelling reached the
  existence probe and `create_winux_fifo_w`, and Win32 roots a leading `/e`
  on the CURRENT drive, so from a `C:` cwd `mkfifo /e/p` probed
  `<cwd>:\e\p` (ENOENT for a real `E:\p`) and would have landed the marker
  beside the cwd; `mknod /e p` from a `D:` cwd really created a `D:\e`
  marker file. Operands now fold once through the shared
  `make_api_path_operand` boundary before any filesystem call;
  `/cygdrive/e`, `/mnt/e` and bare `/e` (which now reports "File exists"
  like MSYS) share the fold, and messages keep the operand spelling as
  typed.
- **chgrp / chown / chcon**: `--reference` and target file operands
  resolved against the cwd drive (#1241, same sweep). An unfolded
  `/e/...` spelling made `GetFileAttributesW` /
  `GetNamedSecurityInfoW` / the `WRITE_OWNER` open answer for
  `<cwd>:\e\...`, so existing `E:` files were reported as missing from
  any other cwd, `chgrp -R --preserve-root /e` missed the failsafe and
  descended into `<cwd>:\e`, and chown/chcon globs enumerated the wrong
  drive. Security lookups, attribute probes, the recursive walks and the
  glob pattern all fold through the shared boundary first (children
  re-fold from their display spelling, chmod `-R` precedent); error text
  keeps the user's spelling. The preserve-root failsafe gained the same
  fold, plus direct root matches for the spellings the operand layer
  collapses onto `/`: all-separator forms (`//`, `///` - a verbatim
  `GetFullPathNameW("//")` answers for an UNC-ish name, which let
  `chown -R --preserve-root X //` bypass the check and really walk the
  shell root) and lexically collapsing forms (`/e/..`, `/tmp/..`), the
  latter with the `(same as '/')` suffix MSYS prints; `//e/..` keeps the
  POSIX two-slash rule and resolves against `//e` like MSYS.
- **date**: `date -r FILE` / `--reference=FILE` probed the operand the
  same way - the raw spelling went straight to `GetFileAttributesExW`,
  so `/e/Data/ref` and its `/cygdrive/e`, `/mnt/e` spellings reported
  "failed to get modification time" from a foreign cwd for a file that
  exists on the operand's drive (#1256, same sweep; the `-f DATEFILE`
  open was fixed in #1239, this is that family's second read point in
  date). The operand folds once through the shared
  `normalize_api_operand` boundary before the attribute probe; the
  failure message keeps quoting the operand exactly as typed, like MSYS.

## [1.1.17] - 2026-10-19

### Added

- **wpm**: a package entry can be an alias for another one (`alias_of`), so one
  tool has one name - `wpm install git` installs `niugit` and says so, with a
  depth bound, a cycle detector and a clear error when the target is missing.
  `wpm info` shows the forwarding; `wpm list` labels such an entry `alias`.
- **wpm**: a `layout: shim` command's payload is resolved from the artifact's
  `files` mapping when it is not at the `opt/<pkg>/<command>.exe` convention, so
  a payload that has to keep its own directory layout (an executable beside its
  resource directories) gets a shim that can find it.

### Fixed

- **shred**: `shred /e/Data/secret` from a `C:` cwd handed the raw "/e/..."
  text to CreateFileW, and Win32 resolved a leading `/e` against the
  CURRENT drive (#1240). For the only data-destroying command in the
  toolset that misparse was doubly dangerous: when a shadow `C:\e\...`
  happened to exist, shred silently overwrote THAT file and exited 0 -
  the requested E:\ target stayed intact, i.e. a false success over the
  wrong victim; when no shadow existed, a perfectly good E:\ file was
  reported "cannot open". Every operand now folds through the shared
  `make_api_path_operand` boundary before the overwrite, the `--force`
  retry, the native-glob pass (a `/e/*.log` pattern expands against the
  operand drive) and the `-u`/`--remove` unlink, and the Win32 calls use
  the extended form like the other post-#1233 commands. Messages keep
  printing the operand spelling as typed; `/cygdrive/e`, `/mnt/e` and
  Windows-form spellings share the fold, and a missing operand still
  fails with rc 1 like MSYS.
- **mkdir**: `mkdir -p /e/Data` from a `C:` cwd really created a shadow
  `C:\e\Data` tree - the one audited misparse that writes to the wrong
  place (#1233, found by the #1230 sweep). Win32 resolves a leading `/e`
  against the CURRENT drive, so the raw operand reached
  CreateDirectoryW / `create_directories` rooted on the wrong volume, an
  existing `E:\Data` reported ENOENT instead of "File exists", and `-m` /
  `-v` answered for the shadow path. Every operand now folds through the
  shared `normalize_api_operand` boundary once on entry to
  `create_directory` (and in the `-m`/existence helpers) before any
  filesystem call; GNU-style messages keep printing what the user typed,
  like MSYS does. `/cygdrive/e`, `/mnt/e` and bare `/e` spellings all
  share the fold, and `mkdir -p /e` (bare drive root) is a silent no-op
  instead of creating `<cwd>:\e`.
- **ln**: `/e/...` link operands resolved against the cwd drive (#1234,
  same sweep). `std::filesystem::absolute`/`GetFileAttributesW`/
  `FindFirstFileW`/`CreateHardLinkW`/`CreateSymbolicLinkW` all rooted a
  raw `/e/Data` spelling on the current drive, so the hard-link source
  probe failed with "failed to access", `-t DIR` rejected a real
  directory, and a privileged `ln -s` wrote the link beside the cwd
  instead of on the operand's drive. Sources and targets fold through
  `normalize_api_operand` once per job (and in the directory-detection
  probes) before any Win32 call; create/removal error text quotes the
  operand spelling as typed. Messages and link text stay verbatim; only
  the API-facing path is folded.
- **ldd**: `/e/x.exe`, `/cygdrive/e/x.exe` and `/mnt/e/x.exe` operands were
  handed as raw text to a bare `std::ifstream`, and the CRT resolves a
  leading `/e` against the CURRENT drive - from a foreign cwd every POSIX
  spelling reported `cannot open` for a file that exists on the operand's
  drive, while MSYS ldd opens the same spelling from anywhere (#1238,
  found by the #1230 sweep). The operand now folds once through the shared
  `normalize_api_operand` boundary at `load_file`, the single point every
  caller opens through (covering the default, `--name` and `--unused`
  paths); error messages keep quoting the operand exactly as typed.
- **date**: `date -f DATEFILE` opened the file the same way, so
  `/e/Data/dates.txt` and its `/cygdrive/e`, `/mnt/e` spellings reported
  `No such file or directory` from a foreign cwd, and the directory probe
  that produces the "Is a directory" report ran against the shadow path
  too (#1239, same sweep). The `-f` operand folds through the shared
  `normalize_api_operand` boundary before the `is_directory` probe and
  the open; the open-failure messages keep printing the operand as the
  user typed it. Bare `/e` (drive root) now correctly reports
  "read error: Is a directory" instead of ENOENT.

### Added

- **native path**: `/mnt/X/...` operands resolve like `/X/...` and `X:\...` -
  the WSL interop spelling (#1172). WSL mounts the Windows drives under
  `/mnt`, and that spelling reached argv verbatim whenever a tool was invoked
  directly (PowerShell, cmd, a scheduler, an agent) rather than through
  rubash, whose argv funnel already pre-translates the form for every child
  (rubash#444, rubash#592). `normalize_api_operand_w` now folds `/mnt/X` to
  the MSYS `/X` spelling before the drive branch, so a single drive-letter
  arm keeps owning every POSIX drive form. The accepted shape matches
  rubash's `windows_mnt_drive_argument` byte-for-byte: a lowercase `mnt`
  literal, a drive letter of either case, exactly `/mnt/X` or `/mnt/X/...`.
  Bare `/mnt` (the WSL root) and non-drive children (`/mnt/data`,
  `/mnt/cfoo`, `/mnt/123`) stay verbatim, `/mnt/cygdrive/d` cannot fold
  twice, and the existing verbatim classes (`/cygdrive/*`, `/dev/*`, bare
  `/`, `/X...` switches) are untouched. Every tool on the shared operand
  boundary (ls, du, sed, cp, ...) picks the form up at once; under a
  configured shell root the resolution is unchanged, since the niubash#346
  shell-ns arm already mapped this exact shape.
- **realpath/readlink/stat**: the shared resolver behind `realpath`,
  `readlink -f/-e/-m` and `stat -L` folded only the single-letter drive
  form, so `/cygdrive/e/...` and `/mnt/e/...` stranded on the current drive
  (or, with a shell root configured, mapped under the shell tree) while the
  same spelling folded correctly for stat/test -f at the argv boundary -
  "test -f said yes, realpath said no" for one and the same operand
  (#1246, the #850 family, confirmed by the #1245 review). The resolver now
  runs the cygdrive (#1145) and /mnt (#1172) prefix folds before the
  single-letter rule, in the exact order the argv boundary does, so every
  command answers one spelling identically; a folded prefix operand is
  native-dialect input and is never re-expressed as a shell-namespace path.
  stat itself routes its operand through the shared argv boundary since
  #1245, which also made shell-namespace spellings (`stat /usr/bin` under a
  configured shell root) resolve like ls and readlink -f instead of
  reporting ENOENT.
- **look**: a dictionary ending in a final newline carried a phantom empty
  entry at the END of the sorted line array, and the binary search's
  midpoint could land on it - an empty string sorts before every key - so
  lower_bound jumped past the last real word and `look -f delt` printed
  nothing for a dictionary ending "…charlie\ndelta\n" (#1251). The final
  newline at EOF no longer synthesizes an entry (matching util-linux look,
  which also treats a lone "\n" file as exactly one empty entry); empty
  lines in the middle of the dictionary keep working as real entries.
- **tree**: the `-o` output operand did not fold through the shared
  `normalize_api_operand` boundary, so `tree -o /e/probe/out.txt` from a
  foreign cwd opened a shadow `C:\e\probe\out.txt` on the cwd's drive - or
  failed to open at all - instead of writing on the named drive (#1254,
  the write-side follow-up to #1236). The output operand folds once like
  the root operand; the open-failure message keeps quoting the spelling as
  typed.

## [1.1.16] - 2026-10-19

### Fixed

- **mkdir / ln**: POSIX drive-dialect operands (`/e/...`, `/cygdrive/e/...`)
  reached creation and removal folded onto the cwd drive, so `mkdir /e/d`
  from a `C:` cwd created `C:\e\d`. Both commands fold operands through the
  shared boundary before any Win32 call; `ln` also quotes the operand
  spelling in `-f` removal failures, and `canonicalize_path_w` folds the
  dialect so `realpath`/`readlink -f` agree (#1231, #1233, #1234).

## [1.1.15] - 2026-10-19

### Added

- **native_path**: `/mnt/X` WSL-interop drive operands resolve like `/e`
  and `/cygdrive/e` (with a required separator, so `/mnt` itself stays a
  directory) (#1172). The release archive ships the package shim and maps
  it; indexing refuses files the archive lacks.

### Fixed

- **echo, text tools, xxd, *sum**: octal `\0nnn` escapes, CRLF CR
  retention, and `xxd -p` wrapping gained the text-marker treatment
  (#1203, #1204, #1205).
- **wpm**: the shim forwarder is delivered and migrated across the wpm
  lifecycle (#1207), and an update keeps **two** rollback copies
  (#1206, #1208).

## [1.1.14] - 2026-10-17

### Added

- **wpm**: `wpm links verify` reports, by path, every command link that does not
  run the installed `winuxcmd.exe` - the diagnosis that was missing while a
  stale link could keep one command on an older build for hours without a word.
- **wpm**: `winuxcmd-shim.exe`, a small forwarder behind `layout: shim`
  packages. `usr\bin\python.exe` and its siblings used to be a hardlink to an
  older `winuxcmd.exe` whose only job was to spawn `opt\<pkg>\<command>.exe`,
  so every `python`/`node`/`curl`/`ssh` call paid a whole WinuxCmd startup; the
  dispatch hop drops from a measured 24.5 ms to 12.1 ms. An installation without
  the forwarder keeps the previous behaviour, and `wpm links rebuild --force`
  (which activation already runs) switches existing installs over.

### Fixed

- **wpm**: `wpm update winuxcmd` no longer leaves a command link on the previous
  image silently. A link that cannot be deleted is parked and replaced, the
  failing link is named, the links are verified after the swap and relinked
  once, and the detached update helper writes its output to
  `.wpm\logs\self-update-<stamp>-<pid>.log` (newest three kept) instead of
  discarding it.
- **wpm**: the self-update keeps the newest two rollback copies of
  `winuxcmd.exe` and prunes older ones - one machine had accumulated 61.8 MB -
  and `wpm clean backup` (and `clean all`) reclaims them.

## [1.1.13] - 2026-10-17

### Fixed

- **wpm**: a package upgrade now replaces files that a running program holds
  open, instead of failing halfway. 1.1.12 refused up front when it *thought* a
  destination could not be replaced, but the probe it used — opening the name
  with `DELETE` access — succeeds against a file another process has mapped as
  its image, while the delete and the overwrite that follow both fail with
  Access denied. On a real installation this meant `wpm install niugit --force`
  still failed while git's `fsmonitor--daemon` held
  `libexec/git-core/git.exe`, and it failed *after* the reconcile and the entry
  links had run, leaving the package half-updated.

  Destinations are now **renamed aside** (`<name>.wpm-old-<pid>`) before
  anything is written: a rename is exactly what a mapped image permits, the
  running process keeps its old bytes, and the operation is reversible. If a
  destination cannot even be renamed, the ones already parked are put back and
  the install refuses with the culprit and the processes running from it — so
  an upgrade is either complete or untouched. Once the install commits, the
  parked names are deleted; while the old process still holds them the name
  stays, and `wpm clean` removes it later.
- **wpm**: a superseded path the new version no longer maps is parked the same
  way instead of aborting the upgrade when a live process holds it.
- **wpm**: `wpm clean staging` and `wpm clean all` also remove parked
  `.wpm-old-<pid>` names left by earlier installs.

Verified on a live installation with 28 `fsmonitor--daemon` processes holding
the payload: the forced upgrade succeeds, `usr/bin/git.exe` and `bin/git.exe`
both share the payload inode, and `.wpm/staging` is pruned.

## [1.1.12] - 2026-10-16

### Fixed

- **wpm**: an install is refused **up front** when a destination is held open by
  a running program, instead of failing halfway through. A file held by another
  process cannot be replaced on Windows at all — delete and overwrite both
  return *Access denied* — so the old behaviour could leave a package
  half-updated; an upgrade of niugit was observed with `opt/niugit` on the new
  version while the PATH entry still ran the old one. WPM now probes every
  destination before writing anything, lists the files and the programs running
  from them, and leaves the installation exactly as it was. Close the listed
  programs and retry.
- **wpm**: flat-layout file mappings are copied from the package's **payload**
  rather than the extraction directory, so a mapping that duplicates a path
  already covered by a directory mapping no longer produces a second copy of
  the package and a PATH entry pointing into `.wpm/staging` (the two
  destinations now share one inode). The staging tree is pruned after a
  successful install.

### Added

- **wpm**: every package command gets a PATH entry link at
  `<root>/bin/<cmd>.exe` — a hardlink to the real binary (the payload under
  `opt/<pkg>/` for shim packages, the mapped binary in `usr/bin/` for flat
  ones), never a launcher, so nothing extra runs per invocation. The links are
  recorded in the package receipt and kept in sync by install, update,
  `wpm links rebuild` and uninstall.

## [1.1.11] - 2026-10-16

### Fixed

- **wpm**: `wpm update winuxcmd` swaps the image by unlinking the old name
  and moving the staged binary into place instead of overwriting a
  running/mapped image in place. The in-place write fails with "being used by
  another process" whenever any process holds the file (observed on a live
  install, where the update silently kept the old version), and writing under
  a mapped image is unsafe for the processes already running from it. The
  in-place path remains the fallback when the unlink itself fails.
- **wpm**: installing a package reconciles its own previous install first -
  destinations the new artifact no longer maps are removed and, on a version
  change, files inside still-mapped directories that the new payload dropped
  are pruned. Before, a layout change left inert leftovers behind (the retired
  shim launcher `opt\<pkg>\<cmd>.exe` after a move to `layout: flat`, or a
  stale DLL in a PATH directory from older flat packaging). Only paths
  recorded in the package's own receipt are touched, they must resolve inside
  the root, and the winuxcmd image and the running executable are never
  deleted.
- **wpm**: `outdated` orders versions instead of comparing strings, so a stale
  mirror advertising an older version no longer reports a
  `update available: 2.55.0.5 -> 2.55.0.4` downgrade. The order is the
  dotted-numeric core plus, when present, a Debian-style packaging revision
  (`5.4.1-unixwin2` > `5.4.1-unixwin1` > `5.4.1`) - what a repackaged artifact
  needs, since the upstream version stays and our rebuild gets a revision.
  Vendor spellings (`9.5.0.0p1`, `r42`, ...) keep the conservative "any
  difference is an update" rule.

## [1.1.10] - 2026-10-16

### Added

- **native_path**: normalized API operands now resolve the shell tree — the bare
  `/` and every path below the configured install root, `/tmp`, `/var/tmp`,
  `/home` and the `/mnt/X` WSL drive form — and a shell-namespaced operand's
  canonical result is re-expressed in the POSIX face, so `readlink -f` and
  `realpath` agree with `pwd`. The executable-leaf open order (an existing
  as-spelled path wins, else the `X.exe` sibling) keeps `test -f /usr/bin/seq`
  and `head -1 /usr/bin/seq` consistent while `ls -d` keeps the operand
  spelling. The layer is inert unless a shell root is configured, so standalone
  WinuxCmd keeps its native dialect (unixwin/niubash#346).

## [1.1.9] - 2026-10-16

### Fixed

- **wpm**: installing a payload file over an existing `usr\bin` command name
  now breaks the old WinuxCmd multicall hardlink with POSIX-semantics unlink
  instead of a plain `DeleteFileW`. Every `usr\bin` entry is a link to the
  same on-disk image as the running `wpm.exe`/`winuxcmd.exe`, so the plain
  delete self-locked and failed with `Access denied` ("failed to break
  WinuxCmd hardlink"). Only `layout: flat` packages, whose payload maps the
  real binary onto `usr\bin\<cmd>.exe`, ever reach this path — first hit by
  `wpm install niugit --force` (unixwin/niu-git#8). The same POSIX unlink is
  now used by the stale-link cleanup and alias-replacement paths, which had
  the identical hazard.

## [1.1.8] - 2026-10-15

### Changed

- **wpm**: shim payload resolution checks shallow per-package locations
  (`<pkg>/<cmd>.exe`, plus `bin/`, `libexec/`, `lib/`, `sbin/`, `usr/bin/` at
  depth 1-3, shallowest wins) before falling back to the bounded recursive
  scan, so command-differs-from-package shims (`git` -> `niugit`, `cargo` ->
  `rust`, `clang` -> `llvm`, `nu` -> `nushell`, `nvim` -> `neovim`) no longer
  pay a whole-`opt/` tree walk on every invocation (~15k files). `time git`
  through the shim 212 ms -> 77 ms; a prompt-style git hook batch
  847 ms -> 291 ms; direct-hit shims (`python`, `curl`, `node`) are unchanged
  (unixwin/niu-git#8).

### Fixed

- **ls**: `/` (also spelled `//`, `///`) resolves to the shell root named by
  `__RUBASH_SHELL_ROOT`/`NIU_ROOT`/`WINUXSH_ROOT`/`RUBASH_ROOT` instead of
  enumerating the drive root, independently of the current directory;
  recursive listings keep the logical `/...` headers while lookups and entry
  metadata go through the shell root (unixwin/niubash#346).

## [1.1.7] - 

### Added

- **diff3**: full GNU diffutils 3.10 alignment on the shared diff engine —
  three-way block merge (`make_3way_diff`), default report with GNU's
  odd-one-out section rules, ed scripts (`-A`/`-e`/`-E`/`-3`/`-x`/`-X` with
  `-i` final write), merged output (`-m`, `-L` labels, `-T` initial tab,
  `--strip-trailing-cr`, `--diff-program`), `-` stdin operands and GNU exit
  statuses (0 ok / 1 conflicts / 2 trouble) with byte-verified usage
  diagnostics (issue #1127, P1).
- **sdiff**: rebuilt on `diff --side-by-side` output — GNU's gutter formula,
  `|`/`<`/`>`/`(`/`)` row markers and `/`/`\` separators for rows missing a
  trailing newline; `-l`/`-s`, `-w`/`--tabsize`/`-t`, the ignore-option
  family (`-i`/`-E`/`-Z`/`-b`/`-W`/`-B`/`-I`/`-a`/`--strip-trailing-cr`,
  `-d`/`-H`/`--diff-program`) forwarded into matching only, and `-o` merged
  output that auto-merges non-interactively (overlaps keep the left column)
  per issue #1127, P1.

### Changed

- **winuxcmd**: the bare `winuxcmd` listing groups commands into GNU
  coreutils-style sections (File operations, Text processing, Shell
  utilities, ...) with alphabetical entries and a discovery guide
  (`winuxcmd <command> --help`, `winuxcmd help <command>`,
  `winuxcmd --list-all`, `wpm search`); `winuxcmd --list-all` preserves the
  flat alphabetical listing. When the system locale is non-English and its
  i18n catalog is not installed, one hint line points at the matching WPM
  package (`wpm install winuxcmd-i18n-zh-cn` + `WINUX_LANG`) (#1155).
- **seq**: output is assembled into 64 KiB buffer chunks instead of one
  `WriteFile` per line, and integral first/increment sequences (default
  fixed format, precision 0, no `--format`/`-w`, |operands| <= 2^52) emit
  through a `std::to_chars` fast path like GNU seq.c's integer bypass.
  100k lines, release build: `seq 1 100000` 332 ms -> ~45 ms and
  `seq 1 100000 | wc -l` 903 ms -> ~45 ms (GNU/WSL oracle 7-10 ms; the
  residual is Windows process spawn). The byte stream is unchanged — a
  regression test pins all 588,895 bytes across several flush rounds
  (#1173).
- **diff**: side-by-side output now shares the GNU diffutils `side.c`
  printer with sdiff (identical rows, `(`/`)` remainders, `/`/`\`
  separators, common rows for equal inputs); `-E` expands tabs across the
  whole line for matching, `-B`/`-I` drop ignored hunks and realign their
  lines positionally instead of filtering buffers, and a final line without
  a newline only compares equal to the other side's incomplete last line
  (issue #1127, P1).
- **docs**: the GNU coreutils compatibility records the one deliberate
  Windows deviation around literal-glob operands — NTFS/Win32 forbid `*` in
  file names, so `touch 'x*x'` reports the NTFS error where GNU creates the
  literal file, `touch -c 'x*x'` silently skips like GNU (fixed in #1152),
  and the derived `ls 'x*x'` miss is a consequence of GNU's own creation
  succeeding; the touch test exemption is registered in
  `DOCS/gnu-coreutils/touch.md` and `COMPATIBILITY_MATRIX.md` (#1143, #1167).

### Fixed

- **spawn**: every child-process launch (the xargs/`find -exec`/pipeline
  spawn family) resolves through a shared GNU `execvp`-style resolver —
  bare names search PATH only (no cwd or implicit System32 shadowing, so
  `sort`/`find`/`tree`... pick the GNU-compatible binary the caller put on
  PATH first) and POSIX PATH spellings (`/c/...`, `/cygdrive/c/...`,
  `:`-separated entries) are split and converted before resolution
  (owner-reported, #1169).
- **find**: POSIX drive-form start points (`/c/...`, `/d/...`,
  `/cygdrive/c/...`) now resolve through the same operand boundary as every
  other tool's path operands, matching msys find semantics where the tool's
  own POSIX layer handles the start point. Command-line roots, `-files0-from`
  entries, and the `-files0-from` source operand all go through it (#1170).
- **tests**: the grep junction recursion cases degrade to a SKIP on hosts
  without symlink-creation privilege (error 1314) instead of failing, mirroring
  the `*_or_skip` helper every other unit test file uses (#1168). New tput
  GNU-differential tests pin that every tput path — success, unknown
  capability, missing operand — leaves piped stdin untouched, the contract
  GNU tput is known for (#1166).
- **i18n**: the WPM message catalog resolves at the WinuxCmd install root, so
  localized messages work when `winuxcmd.exe` is invoked through a shim or
  PATH entry outside the install directory (#1151).
- **touch**: GNU-parity handling of unmatched literal-glob operands — names
  that glob to nothing are treated as missing under `-c` (skipped) instead of
  being created verbatim, matching GNU coreutils (#1152).
- **wpm**: interaction-audit fixes (#1156). `wpm outdated` now anchors
  installed versions on install receipts, so it keeps reporting a pending
  update after `wpm index update` instead of flipping to "all up to date"
  while old files are still on disk. `wpm install --json` prints one
  machine-readable payload (statuses `installed`, `already_installed`,
  `would_install`, `not_found`, `not_installable`, `preflight_failed`,
  `download_failed`, `error`) with all human progress routed to stderr, and
  `wpm index update --json` prints a matching payload. `wpm --help` now
  documents the `install` `--force` reinstall/overwrite semantics. Verified
  by measurement: piped `y`/`n` confirmation on `wpm uninstall` stdin (and
  `-y` in any argument position) already behaved correctly on this branch,
  and locked destination files already fail loudly with exit 1 on both
  install and uninstall; regression tests pin all of these behaviors.
- **du**: an operand's trailing separator run collapses to a single slash —
  `du src/` prints `src/` and `src/bin` (never `src//bin`), `du src//` prints
  byte-identically to `du src/`, and `du src` keeps the plain form, matching
  GNU coreutils (#1159).
- **du, sed**: POSIX drive-form operands (`/d/...`, `/cygdrive/d/...`)
  resolve through the shared operand boundary — du used to reject them
  with ENOENT while accepting `D:/...`, and sed could not read them as
  input files, `-i` targets, `-f` script files or the `r`/`w`/`W`
  script-embedded paths. Both now route operands through the same
  boundary grep/wc/head/find already use (the L2 self-parse contract,
  uniformly applied); error messages and the `=`/`F` filename display
  keep the operand text as typed (#1174).
- **wpm**: candidate index sources are the built-in registry plus
  `user_sources` only — a `sources` array declared inside the cached index
  file is no longer merged, so one poisoned fetch can no longer persistently
  inject new download locations; `wpm index update` now records the winning
  source (`last_success_source`) and the SHA-256 of the persisted index
  (`last_index_sha256`) and warns when the index content changes while being
  served from a non-preferred source; the `wpm index update --json` payload
  carries `index_sha256` (#1176).
- **wpm**: the loopback proxy fallback (probing `127.0.0.1:7897/7890/7891/
  7892/7893/10808/10809` after direct failures) is off by default; any
  unprivileged local process could otherwise listen there and serve both
  index and artifacts past the index's own sha256 checks. Opt in via
  `allow_loopback_fallback: true` in `.wpm/config.json`, or configure a
  trusted proxy with `--proxy`/environment variables (#1177).

## [1.1.5] - 2026-10-03

### Fixed — upstream lane wt50 (2026-10-03)

- **yes**: a broken-pipe write death now reports exit status 141 (the
  SIGPIPE emulation other streaming tools use) instead of exit 1, so
  `yes | head -n 3` shows 141 on the producer side exactly like GNU
  coreutils (#1142). Non-pipe write errors keep the fatal exit-1 path.

### Fixed — upstream lane wt48 (2026-10-03)

- **cp**: every stat/open route now goes through the shared API-path
  boundary (`native_path::make_api_path_operand`, the mv/install
  invariant), so POSIX-drive-form operands (`/d/x`, `/cygdrive/d/x`) work
  in any combination with Windows-form operands instead of failing
  `cannot stat ... No such file or directory` at the source-existence,
  destination-directory, `-t`, same-file, symlink, and recursive-child
  gates (#1145, unixwin/niubash#124 Option B). `cp -s` stores the
  dialect-resolved source spelling as link text (a typed `/d/x` link text
  can never resolve on native Windows).
- **ls**: file operands and `-d` directory operands resolve POSIX drive
  forms — `std::filesystem::absolute` used to fold `/d/x` against the
  current drive root (`D:\d\x`) before the boundary could convert it
  (#1145). A bare root operand (`ls /`, `ls -d /`, `ls //`) lists the
  current drive root (the platform's `/` mapping, consistent with
  `cygpath -w /`): the `\\?\` spelling of a drive root is rejected by
  attribute probes, and the root/`\*` enumeration pattern is now built
  with a path-aware join (`GetFullPathNameW` mis-parses the concatenated
  `/\*` form).
- **native_path (shared boundary)**: `normalize_api_operand_w` folds the
  Cygwin drive prefix (`/cygdrive/d/x`, both separators) into the MSYS
  spelling before drive-letter conversion, giving every command that
  already routes through the boundary (cat, cp, install, mv, ...) cygdrive
  support uniformly; `to_extended_path` keeps bare drive roots un-prefixed
  and folds separators before `GetFullPathNameW` (#1145).

### Fixed — upstream lane wt41 (2026-10-02)

- **mv**: operands in MSYS drive form (`/d/x`) are routed through the
  shared API-path boundary like cp/install, so mixing Windows-form and
  POSIX-form operands in one invocation works in any combination (#1140,
  unixwin/niubash#124); GNU coreutils under MSYS accepts either dialect.
  The copy fallback also no longer mislabels regular files as directories,
  actually moves directories across volumes (they used to report success
  while doing nothing), refuses to move a directory into itself
  (`cannot copy a directory, X, into itself, Y`, GNU copy.c:2091), and
  honors a trailing separator on DEST (`failed to access 'DEST': Not a
  directory` instead of silently creating/overwriting the stripped name).
- **mktemp**: the printed name keeps the `-p`/`--tmpdir` operand's dialect
  (`-p /d/...` echoes `/d/.../x.XXXXXX`, `-p /cygdrive/d/...` likewise),
  so shell round-trips that capture the output stay self-consistent with
  POSIX-form variables (#1141); Windows-form operands and the TMPDIR/
  default-temp paths keep the native display form.
- **xargs**: option parsing stops at the utility name; every argument
  after it is passed to the utility verbatim (POSIX xargs operand
  semantics, GNU findutils) — `xargs cat -n` no longer errors on `-n`,
  `xargs wc -l` no longer silently consumes `-l` as `--max-lines`, and
  `xargs echo -- help` keeps the `--` (#1139).

## [1.1.4] - 2026-09-30

### Fixed — issue triage round (2026-09-29)

- **grep -r**: recursive walk rewritten (#1135). pnpm-style crossed NTFS
  junctions are not symlinks to std::filesystem, so
  `recursive_directory_iterator` descended them until the stack died with
  0xC0000409 and zero output — indistinguishable from "no matches". The
  hand-rolled walk skips reparse directories under -r, dedups canonical
  real paths under -R, and warns
  `grep: warning: <dir>: cyclic directory junction; skipped` on loops.
- **hexdump -b/-c**: rows are the GNU-fixed 71 columns — 16 slots of 4
  with only the final trailing space trimmed (#1133); short lines carried
  one extra space.
- **getconf ARG_MAX**: prints the Windows per-process limit (32767,
  CreateProcess command-line cap) instead of falling through to the
  unknown-variable error (#1134).
- **build**: /bigobj for the commands unity TU (176-file merge exceeded
  the default COFF section limit).

## [1.1.3] - 2026-09-24

### Fixed — diff rewritten to GNU diffutils 3.10 output parity

- **diff**: every output format byte-compared against GNU diffutils 3.10
  (25/25 golden cases): normal, unified, context, side-by-side (GNU tab
  gutter layout with the exact half-width/gutter formula), ed script (-e),
  forward ed (-f), RCS (-n), --ifdef, --line-format and the
  --GTYPE-group-format family, --from-file/--to-file.
- **diff**: options that were declared in help but rejected at runtime are
  now implemented: -e/-f/-n, -D/--ifdef, -p/--show-c-function,
  -F/--show-function-line, -r/--recursive directory comparison with
  -x/-X/--exclude-dir/-S filters, -l/--paginate (pr-style 66-line pages),
  --color[=WHEN], --left-column, --horizon-lines, --speed-large-files,
  -d/--minimal accepted (the LCS is already minimal).
- **diff**: -Z rebinding per GNU 3.4+: -Z is now --ignore-trailing-space;
  --strip-trailing-cr keeps its long form; -E/--ignore-tab-expansion added.
- **diff**: hunk grouping distance fixed — the old heuristic never split
  hunks; now measures the common-line gap change-to-change (context 0-3
  outputs match GNU for the first time).
- **help**: the i18n catalog's 112 placeholder strings ("新增选项：x")
  replaced with real translations (synced in winuxcmd-i18n 0.6.13);
  diff help text now matches GNU wording.
- **parser**: glued values now accepted on int options (diff -U1, head
  -n5 style), matching GNU; usage errors (missing/extra operands) exit 2
  as GNU does.

### Added

- **PGO in release CI** (x64): the tag build now runs the instrumented
  build, collects profiles via `scripts/pgo-train.sh`, and relinks with
  `/USEPROFILE` — matching the local `scripts/build-pgo.ps1` pipeline
  (~12% smaller, faster hot paths). ARM64 stays non-PGO: profiles must be
  collected on the target architecture.

### Removed

- **link** command (again): its hardlink-style name shadows the MSVC
  toolchain linker `link.exe` on PATH. Added a CMake reserved-command
  blacklist guard (`WINUXCMD_BANNED_COMMANDS`) that fails configuration
  with the command name and rejection reason if the source ever reappears;
  the rule is also recorded in `AGENTS.md`.

## [1.1.0] - 2026-09-21

### Fixed — GNU/upstream parity (audit rounds 1-2, 178 commands audited)

- **grep**: conflicting matchers are fatal (exit 2); `-q` keeps error
  diagnostics while suppressing output; `-L` exits 0 iff any line was
  selected anywhere; `-Z` NULs only the filename separator; an empty `-f`
  pattern file selects nothing (exit 1); input opens report
  `grep: FILE: <strerror>`.
- **date**: military zone `J` under `-u` resolves "local" to UTC
  (`date -u -d 1024j` = 10:24, matching GNU).
- **find**: `-exec CMD \;` no longer poisons the exit status; `-exec {} +`
  stays true on child failure; `-execdir` passes `./basename` and batches
  per-directory; findutils-default regex dialect; `-printf` width/precision;
  `-daystart`; `-xdev`.
- **sed**: I/O errors exit 2 (usage stays 1); `\L \U \u \l \E` case
  conversion in replacements; `y///` escapes; `r`/`R` diagnostics;
  `sed -i` with stdin is a hard error (exit 4); `m`/`M` flags;
  `--debug`/`--follow-symlinks` accepted.
- **patch**: engine rework — multi-file patches, blank-line context,
  `\ No newline at end of file`, GNU per-hunk reporting and exit codes
  (0/1/2), default `FILE.rej`, backup-if-mismatch, upstream fuzz/offset
  search.
- **findutils family**: xargs `-r`/`-x` semantics; locate GNU option
  surface (`-l LIMIT`, glob/regex modes, exit 1 on no-match); updatedb
  `--localpaths`/`--prunepaths` and atomic database replace.
- **ls**: built-in default color table (gnulib) when `LS_COLORS` is unset.

### Added

- MSVC **PGO build pipeline** (`scripts/build-pgo.ps1`, CMake
  `WINUXCMD_PGO_INSTRUMENT`/`WINUXCMD_PGO_USE`): training workloads ported
  from uutils `build-pgo.sh` plus the differential corpus. Measured:
  `sort` −18%, `nl` −17%, `fold` −12%, binary size −12% (3.95 MB for 180
  commands).
- Differential audit reports for all 178 commands (coreutils 9.7,
  findutils 4.10.0, grep 3.12, sed 4.9, patch 2.7.6, util-linux 2.41,
  procps-ng 4.0.4 sources; wpm audited against its own specification).

### Removed

- `skills/` agent-skill package (superseded by niubash).
- `scaffold` generator and its DSL docs.
- `BUILD_STANDALONE` broken per-command executable path.
- UPX packing integration (AV false-positive risk; see
  `docs/performance-roadmap.md`).
- Dead FFI library and examples.
