# Mechanical replacement recipe (WinuxCmd 2.0, #1488)

How to move one applet from its C++ implementation to the rust2 library
inside `winuxcmd.exe`. Distilled from the first real replacement, `true` +
`false` (branch `rust2/wire-true-false`). Rules from #1488 that this recipe
enforces: **mechanical replacement, the old implementation is deleted in the
same change (no long-lived dual track), and every replaced command passes
three gates** — GNU oracle, existing tests, byte equality with the previous
exe.

## 1. Decisions fixed by the pilot wave (one-time, already done)

| decision | choice | why |
|---|---|---|
| link shape | **staticlib** (`winuxcmd_ffi.lib`, crate `winuxcmd-ffi` with `crate-type = ["cdylib", "staticlib"]`) | #1487 / niubash#727 traced the FFI breakage to the DLL boundary (load-time CRT stdio binding in a separately linked module). A staticlib is folded into winuxcmd.exe by the same link: one image, one CRT, no load-time binding. The cdylib stays for host experiments only. |
| CRT | Rust built with `-C target-feature=+crt-static` | rustc's MSVC default is `/defaultlib:msvcrt` (/MD); winuxcmd.exe is `/MT` (`USE_STATIC_CRT=ON`). With the flag the archive asks for `libcmt`. Verified: no LNK4098 in the link. With `USE_STATIC_CRT=OFF` CMake drops the flag. |
| ABI | existing `winuxcmd_run` (v1): argv/envp/cwd as UTF-8 C strings, stdin as a buffer, stdout/stderr returned as Rust-owned buffers, `catch_unwind` -> 125 | #1488 "ABI 面只传显式句柄/缓冲". C mirror: `src/shared/rust2_abi.h` (change together with `lib.rs`, bump the version). |
| C++/Rust boundary | C++ keeps the `REGISTER_COMMAND` registration (help/man/completion metadata) and the dispatcher's shared pre-handler machinery (sole `--help`/`--version` interception, rewrite hooks, option policy). The **handler body** becomes one call into Rust. | The product's help/version text is WinuxCmd-branded for all 176 applets; moving it per command would break gate 3 and fork the help system. What gets deleted is the C++ code that implements the command. |
| bridge | `src/bridge/rust2_bridge.cppm`, module `rust2_bridge`, `winux::rust2::run_command(name, ctx.raw_args)` | The only C++ that touches the ABI. Snapshots env (`GetEnvironmentStringsW`, skipping `=C:` pseudo-vars) and cwd (`GetCurrentDirectoryW`), no exceptions (winuxcmd builds without /EHsc), drains stdout/stderr through `safePrint`/`safeErrorPrint` so capture handles and console handling stay on the one output exit. |
| CMake | `WINUXCMD_ENABLE_RUST2` (default ON iff `cargo` is found), list `WINUXCMD_RUST2_COMMANDS`, custom command `cargo rustc --release --locked -p winuxcmd-ffi --lib --crate-type staticlib` with `CARGO_TARGET_DIR=<build>/rust2-target`, imported static lib + pinned native libs | A machine without Rust configures to OFF and still builds; it then ships **without** the listed commands (their sources and the bridge are filtered out). That is the price of "no dual track"; there is no C++ fallback body. |

Native libs pinned in CMake (`advapi32 kernel32 ntdll userenv ws2_32
dbghelp`) come from
`cargo rustc --release -p winuxcmd-ffi --lib --crate-type staticlib -- -C target-feature=+crt-static --print native-static-libs`.
Re-run it and update the list if a rust2 change makes the link fail with
LNK2019. Symbol check: `dumpbin /LINKERMEMBER:1 winuxcmd_ffi.lib` lists
`winuxcmd_run`, `winuxcmd_free_buf`, `winuxcmd_abi_version`,
`winuxcmd_has_command` unmangled.

Cost observed: winuxcmd.exe (Debug) 20,887,552 -> 21,923,328 bytes (+1.0 MB,
Rust std incl. backtrace support; one-time, shared by later commands). First
cargo build of the workspace is seconds (std only, no external crates).

## 2. Per-command steps (repeat for each applet)

Preconditions — stop and fix first if any fails:

- The command is in `winux-cmds::registry()` and `bash rust2/gate/run-gate.sh <cmd>` is green.
- Inventory `io_idiom` shows it **does not read stdin** (ABI v1 has no streaming stdin, see section 4).
- Read the dispatcher's behavior for it (`src/core/dispatcher.cppm` `behavior_for()`,
  `src/core/command_context.cppm` `option_policy_for_command()`): rewrite hooks,
  abbreviation expansion, sole-argument help/version, and the macro's
  `make_context` parse all run **before** the handler. The Rust function
  receives `ctx.raw_args` after those steps. If C++ can reject an argument
  first (an option parse error), that diagnostic stays C++ — decide whether
  the C++ option policy must become permissive for this command.

Steps:

1. **Baseline (before touching anything).** Build
   (`powershell -ExecutionPolicy Bypass -File scripts/build-with-vs.ps1`),
   copy `build-vs/winuxcmd.exe` to a scratch dir **keeping the name
   `winuxcmd.exe`** (the multicall routes by argv[0]; a copy named
   `winuxcmd-old.exe` answers `command not found: winuxcmd-old`), run the full
   `ctest` in `build-vs`, save the failing set.
2. **Library check.** Re-run the rust2 gate for the command and re-read its
   `note`/`win` rows: a `note` row can hide a pilot divergence (see 4).
   Fix the Rust side first, in its own commit.
3. **Edit `src/commands/<cmd>.cpp`.** Keep the `REGISTER_COMMAND` metadata
   byte-identical (help/man text is generated from it), add
   `import rust2_bridge;`, replace the handler body with
   `return winux::rust2::run_command("<cmd>", ctx.raw_args);`, delete every
   helper, include and static the old body used.
4. **Append `<cmd>` to `WINUXCMD_RUST2_COMMANDS`** in `CMakeLists.txt`.
5. **Build**, then run the gates (section 3).
6. **Record**: update `rust2/GATE-REPORT.md` if case rows changed, note the
   replacement in the inventory, commit (Rust fix / wiring / docs as separate
   commits).

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
(`rust2/gate/wire/truefalse-matrix.sh` + `compare.sh` are the true/false
instance; copy the matrix for the next command). GNU side runs under WSL
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

## 5. One-time work still required before some waves

- **Streaming stdin (ABI v2).** ~~v1 passes stdin as a whole buffer; the
  bridge forwards none.~~ **Adopted (additive), #1488 v2 bridge adoption:**
  `winuxcmd_run_stream` (pull callback `winuxcmd_stream_read_fn` + user_data
  -> `impl Read`; host-stops-feeding convention is status 124; `-2` is the
  host read-failure channel). The C++ bridge exposes it as
  `winux::rust2::run_command_with_stdin_streaming(name, args)`
  (`src/bridge/rust2_bridge.cppm`; real fd 0 read in chunks through the
  same `file_io` read path as the v1 slurp — binary mode, cooked-console
  ^D records, broken-pipe EOF — so no whole-buffer input residency), plus
  the generic forms `run_command_with_stdin_source` (host-supplied
  callback, NULL = empty stdin) and `run_command_with_stdin_bytes` (v1
  whole buffer from bytes) for tests and A/B comparisons.
  `winuxcmd_abi_version` is 2 since that adoption. **No wired command uses
  v2 yet** — the v1 behaviors (`run_command`, `run_command_with_stdin`,
  the `kRust2StdinCommands` slurp) stay untouched for stability; the next
  wave wires the first genuinely-streaming stdin reader (`cat`, `tr`,
  `sort`, ...) by switching its handler to `run_command_with_stdin_streaming`,
  which is now a plain mechanical replacement. Regression net: the
  `rust2-stream-driver` ctest lanes (`rust2-stream-segments/-errors/
  -stdin/-mem`, `tests/bridge/stream_abi_driver.cpp`). Contract, ownership
  and thread model: [README.md](README.md) "Streaming stdin ABI (v2)".
- **Raw output path.** The bridge drains Rust stdout through `safePrint`,
  which applies the i18n legacy catalog lookup (`translate_legacy`) to the
  whole string when a catalog is loaded. Harmless for true/false (no output)
  and for diagnostics, but a data-printing command (`basename`, `dirname`,
  `printenv`, ...) needs a non-translating console-layer writer first.
- **Release / unity build** was exercised once in this wave
  (`-Configuration Release`: BUILD_MODE=RELEASE, unity ON, /LTCG with the
  non-/GL Rust objects): links clean, `truefalse-matrix.sh` old-vs-new
  44/44/44. Re-check it whenever the bridge or the CMake block changes.
