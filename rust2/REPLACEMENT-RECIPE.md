# Mechanical replacement recipe (WinuxCmd 2.0, #1488)

How to move one applet from its C++ implementation to the rust2 library
inside `winuxcmd.exe`.

Distilled from the first real replacement (`true` + `false`) and hardened by
the waves that followed, including the grep "split-engine" wire (#1660). The
rules it enforces: **mechanical replacement, the old implementation is
deleted in the same change (no long-lived dual track), and every replaced
command passes three gates** — GNU oracle, existing tests, byte equality with
the previous exe.

> Status at the 2.0 freeze (2026-11-14): 50 applets wired on main, 32 more
> with a green library gate waiting for a wiring wave, and grep/sed/find in
> flight (#1660/#1635/#1634). `DOCS/2.0-inventory.tsv` is the authoritative
> per-command state.

## 1. Decisions fixed by the pilot wave (one-time, already done)

| decision | choice | why |
|---|---|---|
| link shape | **staticlib** (`winuxcmd_ffi.lib`, crate `winuxcmd-ffi` with `crate-type = ["cdylib", "staticlib"]`) | #1487 / niubash#727 traced the FFI breakage to the DLL boundary (load-time CRT stdio binding in a separately linked module). A staticlib is folded into winuxcmd.exe by the same link: one image, one CRT, no load-time binding. The cdylib stays for host experiments only. |
| CRT | Rust built with `-C target-feature=+crt-static` | rustc's MSVC default is `/defaultlib:msvcrt` (/MD); winuxcmd.exe is `/MT` (`USE_STATIC_CRT=ON`). With the flag the archive asks for `libcmt`. Verified: no LNK4098 in the link. With `USE_STATIC_CRT=OFF` CMake drops the flag. |
| ABI | `winuxcmd_run` (v1) plus the additive streaming entry `winuxcmd_run_stream` (v2); argv/envp/cwd as UTF-8 C strings, stdout/stderr returned as Rust-owned buffers, `catch_unwind` -> 125 | #1488 "ABI 面只传显式句柄/缓冲". C mirror: `src/shared/rust2_abi.h` (change together with `lib.rs`, bump `WINUXCMD_RUST2_ABI_VERSION`). ABI version is 2 since the streaming adoption (#1556). |
| C++/Rust boundary | C++ keeps the `REGISTER_COMMAND` registration (help/man/completion metadata) and the dispatcher's shared pre-handler machinery (sole `--help`/`--version` interception, rewrite hooks, option policy). The **handler body** becomes one call into Rust. | The product's help/version text is WinuxCmd-branded for all 176 applets; moving it per command would break gate 3 and fork the help system. What gets deleted is the C++ code that implements the command. |
| bridge | `src/bridge/rust2_bridge.cppm`, module `rust2_bridge`, `winux::rust2::run_command(name, ctx.raw_args)` | The only C++ that touches the ABI. Snapshots env (`GetEnvironmentStringsW`, skipping `=C:` pseudo-vars) and cwd (`GetCurrentDirectoryW`), no exceptions (winuxcmd builds without /EHsc), drains stdout/stderr through `safePrint`/`safeErrorPrint` so capture handles and console handling stay on the one output exit. |
| split engine | when a command keeps one C++ face, Rust exports a **capability predicate** and the C++ handler branches; the retained face is deleted only where unreachable | #1660: `grep -P` stays on PCRE2, so Rust exports `winuxcmd_grep_uses_perl` (additive, ABI stays 2) and the handler routes non-Perl to rust2, `-P` to the retained C++ face. `egrep`/`fgrep` route unconditionally (upstream wrappers are `exec grep -E/-F`; `egrep -P` is a matcher conflict rc 2, never Perl — the 1.x body running Perl there was a standing oracle deviation). |
| CMake | `WINUXCMD_ENABLE_RUST2` (default ON iff `cargo` is found), list `WINUXCMD_RUST2_COMMANDS`, custom command `cargo rustc --release --locked -p winuxcmd-ffi --lib --crate-type staticlib` with `CARGO_TARGET_DIR=<build>/rust2-target`, imported static lib + pinned native libs | A machine without Rust configures to OFF and still builds; it then ships **without** the listed commands (their sources and the bridge are filtered out). That is the price of "no dual track"; there is no C++ fallback body. |

Native libs pinned in CMake (`advapi32 kernel32 ntdll userenv ws2_32
dbghelp`) come from
`cargo rustc --release -p winuxcmd-ffi --lib --crate-type staticlib -- -C target-feature=+crt-static --print native-static-libs`.
Re-run it and update the list if a rust2 change makes the link fail with
LNK2019. Symbol check: `dumpbin /LINKERMEMBER:1 winuxcmd_ffi.lib` lists
`winuxcmd_run`, `winuxcmd_run_stream`, `winuxcmd_free_buf`,
`winuxcmd_abi_version`, `winuxcmd_has_command` unmangled (plus any
per-command capability predicate, e.g. `winuxcmd_grep_uses_perl`).

Cost observed: winuxcmd.exe (Debug) 20,887,552 -> 21,923,328 bytes (+1.0 MB,
Rust std incl. backtrace support; one-time, shared by later commands). First
cargo build of the workspace is seconds (std only, no external crates).

## 2. Per-command steps (repeat for each applet)

Preconditions — stop and fix first if any fails:

- The command is in `winux-cmds::registry()` and `bash rust2/gate/run-gate.sh <cmd>` is green.
- The inventory row is `deferred(#1488)` with `engine=rust2-ready` (or the
  branch you are wiring is the one that lands the library).
- Read the dispatcher's behavior for it (`src/core/dispatcher.cppm` `behavior_for()`,
  `src/core/command_context.cppm` `option_policy_for_command()`): rewrite hooks,
  abbreviation expansion, sole-argument help/version, and the macro's
  `make_context` parse all run **before** the handler. The Rust function
  receives `ctx.raw_args` after those steps. If C++ can reject an argument
  first (an option parse error), that diagnostic stays C++ — decide whether
  the C++ option policy must become permissive for this command.

### 2.1 Baseline (before touching anything)

Build (`powershell -ExecutionPolicy Bypass -File scripts/build-with-vs.ps1`),
copy `build-vs/winuxcmd.exe` to a scratch dir **keeping the name
`winuxcmd.exe`** (the multicall routes by argv[0]; a copy named
`winuxcmd-old.exe` answers `command not found: winuxcmd-old`), run the full
`ctest` in `build-vs`, save the failing set. Keep that baseline exe: gate c
compares against it byte-for-byte.

### 2.2 Library check

Re-run the rust2 gate for the command and re-read its `note`/`win` rows: a
`note` row can hide a pilot divergence. Fix the Rust side first, in its own
commit.

### 2.3 Wire shape A — one engine (the default)

1. Edit `src/commands/<cmd>.cpp`: keep the `REGISTER_COMMAND` metadata
   byte-identical (help/man text is generated from it), add
   `import rust2_bridge;`, replace the handler body with
   `return winux::rust2::run_command("<cmd>", ctx.raw_args);`, delete every
   helper, include and static the old body used.
2. Append `<cmd>` to `WINUXCMD_RUST2_COMMANDS` in `CMakeLists.txt`
   (union — never replace the existing set).
3. If the command reads stdin, see §5 before choosing the entry.
4. Build, then run the gates (§3).
5. Record: update `rust2/GATE-REPORT.md`, flip the inventory row to
   `rust2-wired` with the PR/commit anchor, commit (Rust fix / wiring / docs
   as separate commits).

### 2.4 Wire shape B — split engine (#1660 grep)

Use it when exactly one face of the command genuinely belongs to C++ (today:
`grep -P` and its PCRE2 dependency; the decision is recorded in
`DOCS/2.0-regex-engine-decision.md` §6).

1. Export a capability predicate from `winuxcmd-ffi`
   (`winuxcmd_<cmd>_uses_<face>`), add it to `src/shared/rust2_abi.h`, and
   expose it from the bridge. Additive only — do not bump the ABI for a new
   symbol.
2. In the handler, parse only enough to decide the face (for grep: the
   effective matcher after option clustering; `egrep`/`fgrep` force `-E`/`-F`),
   then call the streaming rust2 entry for the non-C++ face and the retained
   C++ function for the other. The retained code is moved to a named
   function; everything unreachable from it is deleted.
3. Add the command to the **bridge drain policy** when it has a
   command-specific write-error status (grep family keeps GNU
   `EXIT_TROUBLE 2`; the generic broken-pipe policy must not rewrite it).
4. Keep the C++ surface honest: `egrep`/`fgrep` must not accept a Perl
   matcher conflict — upstream wrappers are `exec grep -E/-F`.
5. Gates add one row class: **split smoke** — every retained-face invocation
   byte-equal old vs new, plus at least one non-face invocation proving the
   Rust path. Evidence: `rust2/target/wire-i18n/` (#1660: 10/10 -P split
   smoke byte-equal).

### 2.5 Record the retirement

If the 1.x body carried WinuxCmd-only options, list each one in
`DOCS/2.0-MIGRATION.md` §3 with its GNU source citation, and re-pin the
affected ctest to the GNU contract (the e7ddc432 precedent: echo
`-u`/`-r`, fold `-c`, fmt `-m/-P/-X/-x/-T`).

## 3. The three gates

**Gate a + c with the existing harness, zero new code** — point the gate's
two Windows sides at the new and the old product exe:

```sh
cd rust2
RUST_BIN=D:/repo/<tree>/build-vs/winuxcmd.exe \
CPP_BIN=<baseline-dir>/winuxcmd.exe \
bash gate/run-gate.sh <cmd>
```

The "rust" side is now the wired product: `strict`/`exit` rows gate it
byte-for-byte against WSL coreutils 9.4 (gate a); the ledger column `r=c`
compares new vs old exe rc + stdout (gate c). Every `r=c:no` row must be
explained, or the replacement is not mechanical.

**stderr and extra argument shapes**: run a matrix probe that records
rc/stdout/stderr for GNU, old exe and new exe, then compare
(`rust2/gate/wire/cmd-matrix.sh` + `compare.sh`; one matrix block per
command, new rows appended in the same change). GNU side runs under WSL
(`MSYS_NO_PATHCONV=1 wsl bash <script>`), exe sides under Git Bash — see the
env pitfall below.

**Gate b**: full `ctest` after the change; the failing set must equal the
baseline set exactly, and the command's own unit tests + differential corpus
(`WINUXCMD_BIN=<dir> bash tests/differential/runner.sh --only <cmd>` under WSL)
must pass on both old and new exe.

## 4. Pitfalls met in the pilot (and the fix)

- **The pilot was wrong, the gate didn't say so.** rust2 `true`/`false`
  honored `--help`/`--version` in any position and `false --help` returned 0;
  GNU true.c honors them only when `argc == 2` and keeps EXIT_STATUS (1 for
  false). The rows were classified `note` ("C++ deviates"), which never gate.
  Re-derive classification when wiring; promote rows once the premise is gone.
- **Measured beats remembered.** coreutils 9.4 `true --help` prints even with
  `POSIXLY_CORRECT=1`. Probe WSL before writing a rule.
- **argv[0] multicall**: keep the baseline copy named `winuxcmd.exe`.
- **Probe loop stdin**: a `while read ... done <<TABLE` loop loses rows when
  the Windows exe inherits and drains the loop's stdin. Read the table on
  fd 3 and give non-stdin cases `</dev/null`.
- **WSL interop does not forward env vars** to Windows children (only
  `WSLENV`): env-dependent cases (`POSIXLY_CORRECT`, ...) must drive the exe
  from the Windows side.
- **Hard-coded gate paths**: `run-gate.sh` had `TREE_MNT=/mnt/d/repo/wc-20rust`;
  the GNU side silently produced `MISSING` in any other worktree. Now derived
  from the checkout (override with `TREE_MNT=`).
- **clang-format reorders the #1412 pre-include** (`<cstddef>` must precede
  `pch.h`'s windows.h chain). Keep `<cstddef>` and `<stdio.h>` in separate
  comment-delimited blocks in module global fragments.
- **No /EHsc in winuxcmd**: bridge code must not throw (no
  `std::filesystem::current_path()`); use Win32 calls with error returns.
- **No-Rust configure**: `find_program` re-searches when a cached value is
  `-NOTFOUND`, so simulating "no cargo" means hiding it from PATH and from
  `CARGO_HOME`/`USERPROFILE\.cargo\bin`, not just passing a cache value.
- **Ctx literals**: tests and new code must use `Ctx::new(cwd, &env)` /
  `Ctx::with_host(...)`; `Ctx { .. }` is banned (E0063 after the host seam,
  #1659).
- **A green `run-gate.sh` is not a green product**: the driver's process-IO
  tier differs from the wired product's dispatcher (help interception, option
  policy, stdout drain). Gate a with `RUST_BIN` pointing at the wired exe.

## 5. Streaming stdin ABI (v2, additive; #1488, #1550 follow-up)

### The problem

ABI v1 (`winuxcmd_run`) slurps stdin: the host hands one whole buffer and
the boundary copies it into a `Vec` (`stdin_buf` -> `to_vec()`), so a 64 MiB
input costs a full extra copy at the ABI layer on top of whatever the
command itself accumulates, and "read a chunk, write a chunk" (`cat`/`tr`
class) cannot be expressed at all. Commands that read stdin could not be
wired without turning streaming into slurp-then-run.

### Choice: pull callback (function pointer + user_data), not a shared ring buffer

1. **Pull callback (chosen).** The host registers one "fill this buffer"
   function; Rust calls it synchronously whenever the command asks for more
   input.
   * The rust2 shape contract is synchronous and single-threaded. A ring
     buffer needs a producer on one side — a thread plus locks or lock-free
     indices — and splits buffer ownership across two parties with a wakeup
     protocol. The pull callback keeps one thread and one owner per call.
   * The callback maps 1:1 onto `std::io::Read::read` (partial reads are
     legal, `0` is EOF, exactly like `read(2)`). Every command already
     consumes `Io.stdin: &mut dyn Read`, so the whole command layer is
     streaming-capable with **zero command-file changes** — streaming is
     purely an ABI/core/driver property.
   * No ring allocation, no retained pointers: the host owns the buffer for
     the duration of one call, Rust borrows it and forgets it.
2. **Shared ring buffer (not chosen).** It only pays off when a host wants
   overlapped producer/consumer pipelining; nothing in the 2.0 wiring needs
   that. If a host does later, it can layer it *behind* the pull callback —
   no ABI change required.

### Signature (C form, `src/shared/rust2_abi.h`)

```c
/* Fill buf (cap bytes, cap >= 1) with the next stdin bytes. Returns:
 *   > 0   bytes filled (may be < cap; partial read is a valid result)
 *   0     end of stream
 *  -1     host stopped feeding: timeout / interrupted (see 124 below)
 *  -2     host read failure (any other negative is reserved, treated as -2)
 * The pointer is borrowed for the duration of the call and never stored. */
typedef long long winuxcmd_stream_read_fn(void* user_data,
                                          unsigned char* buf, size_t cap);

/* Like winuxcmd_run, but stdin arrives through the host callback.
 * stdin_read == NULL means empty stdin (as stdin_buf == NULL in v1).
 * stdout/stderr keep the v1 shape: Rust-owned buffers freed with
 * winuxcmd_free_buf. */
int winuxcmd_run_stream(const char* name, int argc,
                        const char* const* argv, const char* const* envp,
                        const char* cwd,
                        winuxcmd_stream_read_fn* stdin_read,
                        void* stdin_user_data,
                        winuxcmd_cbuf* stdout_buf,
                        winuxcmd_cbuf* stderr_buf);
```

Rust mirror: `winux_core::stream::StreamReadFn` (the fn-pointer type),
`winux_core::stream::StreamReader` (the `impl Read` wrapper), and the
`winuxcmd_run_stream` export in `winuxcmd-ffi`.

### Callback contract

| return | meaning | Rust-side mapping |
|---|---|---|
| `> 0` | bytes filled; **must be `<= cap`**; partial reads legal, no minimum | `Ok(n)` from `Read::read` |
| `0` | end of stream | `Ok(0)` (EOF) |
| `-1` (`STREAM_TIMED_OUT`) | host stopped feeding (timeout / interrupt) | `io::Error` kind `TimedOut`; the run **returns status 124** |
| `-2` (`STREAM_IO_ERROR`) | host read failure (other negatives reserved, same treatment) | `io::Error` kind `Other`; propagates per the command's own contract (e.g. `tac` -> rc 1; a `?`-propagating command -> 125) |

The `StreamReader` wrapper clamps an out-of-contract `n > cap` to `cap`
(defensive; a host that does that is already broken) and never calls the
callback with `cap == 0`. Bytes beyond the returned `n` are not read.

**Timeout convention: 124.** When the host reports `-1` at any point, the
run returns **124** regardless of the command's own status (a command may
have swallowed the read error internally — the flag is tracked at the
wrapper, not inferred from the status). 124 is GNU's convention: coreutils
`timeout(1)` exits 124 when the child times out
(`winux_core::stream::STREAM_TIMEOUT_STATUS`). Partial output written before
the stop stays in `stdout_buf`; the host decides whether to keep it.

### Buffer ownership, lifetime, thread model

* **Ownership.** The callback's `buf` is host-owned memory. Rust borrows it
  for the duration of the single callback invocation and never stores the
  pointer; `user_data` is passed through opaquely and never dereferenced by
  Rust. Output buffers (`stdout_buf`/`stderr_buf`) stay Rust-owned until
  `winuxcmd_free_buf`, exactly as in v1.
* **Lifetime.** The reader lives only inside one `winuxcmd_run_stream`
  call. The host must keep `stdin_read`/`stdin_user_data` valid for the
  whole call and must not free or mutate the buffer concurrently.
* **Threading model: single-threaded.** The callback is invoked
  synchronously on the caller's thread, in the order the command reads.
  `StreamReader` holds a raw pointer, so it is `!Send + !Sync` at compile
  time — the streaming run cannot be moved across threads by construction.
  A host that wants a producer thread owns it behind its own callback; the
  ABI makes no thread demands and takes no locks.

### The Windows handle red line (#1487 lesson)

**Rust never touches a CRT fd, process stdio, or a console/pipe HANDLE.**
The host reads its own console/pipe/file and hands over plain byte
buffers; the Rust side sees only the callback and the buffer contents. No
`GetStdHandle`, no `std::io::stdin`, no fd 0 in the library or ABI layer —
the only place process stdio is touched is the CLI driver, whose adapter
exists precisely to convert process handles into the explicit-buffer
protocol (the #1487/#727 root cause was exactly a load-time CRT stdio
binding across the boundary; the streaming ABI adds no new binding).

### How to switch a command

Change the handler's `run_command_with_stdin(name, args)` call to
`run_command_with_stdin_streaming(name, args)` (identical signature, same
args span), update the registration comment, and extend the
`rust2-stream-driver` lanes to cover the command. The C++ host entry is the
bridge's streaming convenience call (`run_command_with_stdin_streaming`, real
fd 0 through the `file_io` read path) plus the generic
`run_command_with_stdin_source` / `run_command_with_stdin_bytes`.

**Wired through the streaming entry:** `b2sum`, `sha224sum`, `sha384sum`,
`tac`. Every other rust2 command keeps its v1 behavior until its wave.

Regression net: `rust2-stream-driver` ctest lanes
(`rust2-stream-segments/-errors/-stdin/-mem`,
`tests/bridge/stream_abi_driver.cpp`) — 189 checks across
tac/base64/b2sum/sha224sum/sha384sum.

### Output side

stdout/stderr keep the v1 shape: Rust-owned `CBuf`s the host drains after
the call. Output capture is already the v1 contract; a streaming *write*
callback can be added later as another additive symbol if a host
demonstrates the need.

### Validation evidence

* Byte equality: segmented feeding (1 B / 7 B / 64 KiB uneven) is
  byte-identical to whole-buffer v1 across ASCII, multi-byte UTF-8 split
  mid-character, no-trailing-newline and empty inputs; host-stops-feeding
  returns 124; host read failure surfaces as the command's own rc.
* 64 MiB ledger (`bash gate/stream-check.sh`): 21/21 `cmp`-identical
  comparisons; ABI copy removal measured at ~2x input for `tac` (392.7 ->
  264.5 MiB peak pagefile) and `base64` (473.3 -> 344.1 MiB).
* Digest-family adoption: 189 driver checks PASS, 24/24 GNU byte equality
  (WSL 9.4), memory peaks `tac` 396.6 -> 312.7 MiB (pagefile).

## 6. I18N: the tr! three-layer migration

Every user-visible natural-language string in a wired command must survive
this ladder; a command is not wired until all three layers are done.

**Layer 1 — stable key + English fallback in source.** The fallback is the
GNU `error()`/`quotef()` wording, with the program-name prefix left in code:

```rust
tr!(ctx, "command.<cmd>.error.<what>", "cannot open '{name}' for reading", name = name)
```

Rules: `command.<cmd>.<surface>.<what>` key style; named `{field}`
placeholders only (no positions, format specs, or implicit captures);
`{{`/`}}` for literal braces; every formatted value is computed once by the
caller (`value = format!("{:.2}", x)`); file contents and paths never enter
the catalog text — they are placeholders. A catalog template that is
missing, empty, malformed, or does not consume every binding falls back to
the English string; it never panics.

**Layer 2 — host injection at the call site.** The translation arrives
through `Ctx::host` (`HostServices::message(key)`), which the C++ bridge
fills from the loaded catalog for that invocation. Rust never reads
`WINUX_LANG`, a file, or a process-global table; `Ctx::new` (no host) is
English-only, and tests use `MapHost`. The bridge's message callback does the
same `catalog().messages.find(key)` lookup as `i18n.cppm`, returns a stable
pointer to process-static JSON storage, and returns non-zero on a miss. Do
**not** reuse `translate_legacy`'s FNV-1a reverse lookup for runtime
messages.

**Layer 3 — catalog publication (separate repository).** After the source
change:

```sh
python scripts/i18n_batch.py extract     # regenerate the English catalog
python scripts/i18n_batch.py validate    # must pass before publishing
```

then update `unixwin/winuxcmd-i18n` in the same task: edit
`catalogs/zh-CN/catalog.json` preserving stable keys and valid JSON, **bump
that repository's `VERSION`** (CI blocks the PR otherwise; WPM treats an
unchanged version as already installed), commit the catalog separately, and
do not push from an agent lane — report the commit to the captain. English
fallbacks live in WinuxCmd; the locale catalog lives in the I18N repository
and is never committed here (`.wpm/i18n/` is local test output only).

**Retained C++ surfaces keep their existing key naming.** `grep -P` and the
other retained faces still use the C++ catalog path; do not rename their
keys during a wire — a split-engine command has two message surfaces by
design, and `i18n-drift` covers both.

**Do not translate:** option names, paths, URLs, hashes, protocol fields,
numeric values, shell syntax, user file contents, and the `Try '<cmd>
--help'` tail's program name (the name stays in code, the tail text is a
key).

## 7. Retained C++ faces and exemptions

The recipe deletes the 1.x body; it does not delete capabilities.

| retained face | why | anchor |
|---|---|---|
| `grep -P` (PCRE2) | Perl syntax is out of scope for winux-regex; PCRE2 is an existing pinned native dependency, not a Rust-ecosystem one | `DOCS/2.0-regex-engine-decision.md` §6 item 4 |
| `!<fifo>` / `#1038` marker bridge | Windows named-pipe rendezvous stays in the C++ `file_io` layer; Rust commands call through it | `rust2/GATE-REPORT.md` wave5/wave6 |
| `REGISTER_COMMAND` shell | help/man/completion metadata and the dispatcher contract | §1 |
| brand help/version interception | the multicall's shared surface | §1 |

Exemptions are recorded per command in `DOCS/2.0-inventory.tsv`'s `anchor`
column. **Never add a `link` command** (MSVC's `link.exe` name collision;
`CMakeLists.txt` `WINUXCMD_BANNED_COMMANDS`).

## 8. One-time work still required before some waves

- **Raw output path.** The bridge drains Rust stdout through `safePrint`,
  which applies the i18n legacy catalog lookup (`translate_legacy`) to the
  whole string when a catalog is loaded. Harmless for diagnostics, but a
  data-printing command (`basename`, `dirname`, `printenv`, ...) needs a
  non-translating console-layer writer first. (Tracked as part of the i18n
  prerequisite wave; do not silently widen `translate_legacy`.)
- **Host metadata channel.** Input classification that cannot be observed
  through injected IO (tail's terminal/pipe/disk class) travels as explicit
  metadata (`tail::StdinMeta` today; the ABI `stdin_meta` callback is the
  host_v4 item, #1487). Until it lands, the registry defaults to the pipe
  class and the driver composes the real class — do not read a process
  handle from the library to "fix" a gate row.
- **Release / unity build** was exercised once in the pilot wave
  (`-Configuration Release`: BUILD_MODE=RELEASE, unity ON, /LTCG with the
  non-/GL Rust objects): links clean, `truefalse-matrix.sh` old-vs-new
  44/44/44. Re-check it whenever the bridge or the CMake block changes.

## 9. Definition of done (per command)

- [ ] `bash rust2/gate/run-gate.sh <cmd>` green with `RUST_BIN` = wired exe.
- [ ] Every `r=c:no` row explained in the PR (gate c), and the old-exe
      matrix covers main options + error paths + boundaries.
- [ ] Full `ctest` failure set == baseline; retired extensions re-pinned to
      the GNU contract.
- [ ] `CMakeLists.txt` union updated; default **and** full editions build.
- [ ] tr! keys + English fallback; `extract`/`validate` pass; I18N repo
      catalog + `VERSION` bump committed separately (not pushed by the lane).
- [ ] `DOCS/2.0-inventory.tsv` row flipped to `rust2-wired` with an anchor;
      `rust2/GATE-REPORT.md` updated with the measured rows.
- [ ] If an option retires: `DOCS/2.0-MIGRATION.md` §3 entry with the GNU
      option-table citation.
