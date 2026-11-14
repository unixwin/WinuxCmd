<!-- 2.0.0 docs draft: verified against origin/main f0cf6b17 (2026-11-14).
     WP-S must refresh counts, artifact names and links at the release tag. -->

<a id="top"></a>

<div align="center">

<img src=".github/assets/banner.svg" alt="WinuxCmd — Unix commands, native on Windows. 176 commands, one native executable, GNU-behavior differential gate." width="100%">

**Real Unix commands. Real Windows paths. One native executable.**
No WSL · No Cygwin · No MSYS2 · No path-translation pain
**v2.0.0 — the Rust command core is here.** 🎉

**[unixwin.dev](https://unixwin.dev) — Docs & downloads**

[![GitHub release](https://img.shields.io/github/v/release/unixwin/WinuxCmd)](https://github.com/unixwin/WinuxCmd/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/unixwin/WinuxCmd/total)](https://github.com/unixwin/WinuxCmd/releases)
[![Stars](https://img.shields.io/github/stars/unixwin/WinuxCmd/stargazers)](https://github.com/unixwin/WinuxCmd/stargazers)
[![License](https://img.shields.io/github/license/unixwin/WinuxCmd)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-blue)

[💾 Install](#install) · [🚀 What's new in 2.0](#-whats-new-in-20) · [⚡ Quickstart](#-quickstart) · [🔁 1.x → 2.0](#-upgrading-from-1x) · [📚 Docs](#-documentation) · [中文](README-zh.md)

</div>

---

## The problem, in one sentence

You're on Windows and you need `grep -rn`, `sed -i`, `find -exec`, `xargs -0` — and every option so far is a compromise:

- **WSL** — 1 GB+ install, and a VM filesystem boundary between you and your files
- **Cygwin** — path-translation gymnastics and a 2–5 s startup
- **GnuWin32** — abandoned in 2012, stuck at 60% compatibility
- **uutils** — a great Rust project, but ~100 commands and ~600 options

**WinuxCmd skips the compromise.** One native Win32 executable that speaks
GNU syntax on Windows paths: **176 commands**, no runtime, no VM. 2.0 moves
the command core to Rust — behavior still measured against a GNU oracle,
byte for byte.

## 🚀 What's new in 2.0

2.0 is an **implementation** release, not an interface release: the same
commands, names, options, help surface and multicall binary. What changed is
what runs underneath.

```
winuxcmd.exe  (one image, one CRT)
├── C++ front end        registration metadata, dispatcher, option policy,
│                        help/version interception, wpm, retained faces
├── rust2_bridge         the only C++ that touches the ABI
├── winuxcmd_ffi         C ABI v2: winuxcmd_run / winuxcmd_run_stream
└── winux-cmds           library-form commands
    └── winux-core       Io / Ctx / HostServices / path / regex seam
```

- **Library-form commands** — every wired applet is a function:
  `run(args, &mut Io, &Ctx) -> CmdResult`. IO is injected, cwd and
  environment are explicit, there is no process-global mutable state, and a
  command returns a status instead of calling `exit()`. A panic becomes
  status 125 at the ABI boundary; it never unwinds into the dispatcher.
- **One image** — the Rust side is a **static library** folded into
  `winuxcmd.exe` by the same MSVC link. No DLL, no load-time CRT boundary,
  nothing extra to install. This is the fix for the FFI breakage that
  stalled the first Rust attempt (#1487).
- **Streaming stdin** — `winuxcmd_run_stream` feeds input through a pull
  callback, so a command reads incrementally instead of slurping through
  the ABI. A host-stopped read returns GNU's 124; a read failure maps to the
  command's own status.
- **`winux-regex`** — a std-only BRE/ERE engine (GNU extensions,
  backreferences, POSIX leftmost-longest, glibc capture rules) shared by
  `grep`/`sed`/`find`/`tac` and friends. `grep -P` keeps the existing pinned
  PCRE2 face; Perl syntax is not reimplemented.
- **Translation without globals** — runtime messages carry stable keys and
  English fallbacks; a translation is injected per invocation from the
  catalog selected by `WINUX_LANG`. English-only builds are the default.
- **A stricter gate** — every wired command passes three gates: GNU oracle
  byte equality, an unchanged ctest failure set, and an old-vs-new exe byte
  matrix with each divergence explained.

At the 2.0 freeze, **50 applets run the Rust core**, **32 more have a green
differential gate and are queued** for the 2.0.x wiring waves, and the
remaining applets keep the 1.x core until their wave. The authoritative
per-command state is [`DOCS/2.0-inventory.tsv`](DOCS/2.0-inventory.tsv);
the architecture and wiring contract is
[`rust2/REPLACEMENT-RECIPE.md`](rust2/REPLACEMENT-RECIPE.md).

## ⚡ Quickstart

```bash
# Install one of the ways below, then use the commands directly:
ls -la
grep -rn "TODO" src/
sed -i 's/http:/https:/g' config.ini
find . -name "*.tmp" -exec rm {} \;

# Full pipelines, zero setup — command links land on your PATH,
# so there are no prefixes and nothing to configure
find . -name "*.cpp" -print0 | xargs -0 wc -l

# Every applet is reachable through the multicall binary too:
winuxcmd grep -c ERROR app.log
winuxcmd --list-all

# Tools that should not be reimplemented install through WPM:
wpm install jq
```

Verify what you are running:

```bash
winuxcmd --version
winuxcmd <command> --help
```

### Install

| | |
|---|---|
| **winget** | `winget install WinuxCmd` (ID `caomengxuan666.WinuxCmd`) |
| **Installer (recommended)** | `WinuxCmd-<version>-x64-setup.exe` (or the ARM64 setup) from [GitHub Releases](https://github.com/unixwin/WinuxCmd/releases/latest) |
| **Portable** | Unzip `WinuxCmd-<version>-win-x64.zip` anywhere and add it to your `PATH` |
| **Build from source** | VS 2022 + CMake 3.30 + Ninja + Rust — see [Building from source](#️-building-from-source) |

## 🔁 Upgrading from 1.x

The command set is unchanged. The behavior is GNU-truer where 1.x had
invented things. Read the full
**[migration guide](DOCS/2.0-MIGRATION.md)** before upgrading scripts; the
short version:

| Area | 1.x | 2.0 |
|---|---|---|
| Implementation | C++ bodies in `winuxcmd.exe` | Rust library functions in the same image |
| Command set / names / options | 176 applets | **unchanged** (retired 1.x-only option spellings excepted) |
| `--help` / `--version` text | WinuxCmd-branded | **unchanged** (same dispatcher surface) |
| GNU behavior | measured, with tracked deviations | GNU byte-equal on the gated surface; every remaining deviation is classified |
| `sort` speed | reference | 12x faster plain sort on the 64 MiB NTFS benchmark |
| Streaming stdin | whole-buffer ABI | incremental pull callback (v2) |
| Translations | C++ catalog | per-invocation host injection + separate catalog repo |
| Editions / multicall / WPM | default + full | **unchanged** |
| Windows subsets | NTFS modes, ownership, symlinks, SELinux, FIFOs | **unchanged, documented** |

**Retired 1.x extensions** (GNU has no such option, or treats it as data):
`echo -u/--upper`, `echo -r/--repeat`, `fold -c`,
`fmt -m/-P/-x/-X/-T`, `numfmt --unit-separator/-M/-l` and the `Z/Y/R/Q`
suffixes as 1.x accepted them. Each has a GNU citation in the migration
guide §3.

**Not yet changed:** applets still on the 1.x core behave exactly as in
1.2.1. Their GNU gaps are tracked as `compat-gap` issues (#1639–#1657) and
will be fixed by their Rust rewrites, not by 2.0.0.

## 🧰 Command coverage

176 applets across GNU coreutils, findutils, grep/sed, util-linux, BSD tools,
process management, Cygwin/MSYS bridging and WinuxCmd extensions. The
command surface is unchanged from 1.x; what is changing is the engine
behind each applet.

| Family | Examples |
|---|---|
| File operations | `ls` `cp` `mv` `rm` `ln` `mkdir` `install` `stat` `du` `df` |
| Text processing | `cat` `head` `tail` `cut` `tr` `sort` `uniq` `fmt` `paste` `pr` |
| Search & traversal | `find` `xargs` `locate` `updatedb` `whereis` `which` |
| Digests & encoding | `md5sum` `sha*sum` `b2sum` `cksum` `base64` `base32` `basenc` |
| Paths & links | `readlink` `realpath` `basename` `dirname` `pathchk` `cygpath` |
| Process & system | `ps` `top` `kill` `timeout` `nice` `nohup` `free` `uptime` |
| Permissions | `chmod` `chown` `chgrp` `getfacl` `chcon` `runcon` |
| Terminal & docs | `tty` `stty` `tput` `clear` `reset` `man` `less` `more` |
| WinuxCmd extensions | `wpm` `regtool` `mkgroup` `mkpasswd` `d2u` `u2d` |

Per-command support state (Rust-wired / deferred / retained face) and the
issue anchors live in
[`DOCS/2.0-inventory.tsv`](DOCS/2.0-inventory.tsv); the summary is
[`DOCS/2.0-inventory-summary.md`](DOCS/2.0-inventory-summary.md).

## 🧪 Testing and GNU verification

2.0 gates each wired command three ways:

1. **GNU oracle** — the wired product vs WSL GNU (coreutils 9.4, findutils
   4.9.0, util-linux 2.39.3, Debian hostname 3.23), byte-for-byte on stdout
   and exit status for `strict` rows.
2. **Existing tests** — the full `ctest` failure set must equal the
   pre-wiring baseline.
3. **Old vs new exe** — an argument matrix compares rc/stdout/stderr; every
   divergence must be explained as a 1.x deviation the replacement corrects.

Case tables are generated (`rust2/gate/gen-cases.py`); every row is
`strict`/`exit`/`win`/`note`, and a `note` row without a registry entry is
itself a finding. The measured narrative is
[`rust2/GATE-REPORT.md`](rust2/GATE-REPORT.md); the wave5 full run
(2026-11-13) covered 82 commands / 2,444 cases, and xargs adds 141. The
release run replaces those numbers.

Windows-value-model rows (`id`, `uname`, `hostname`, `getconf`, `df`,
`whoami`, `nproc`) gate rc against GNU and stdout against the 1.x value
model; they are recorded, not skipped.

## 📚 Documentation

| Document | Description |
|---|---|
| [Migration guide 1.x → 2.0](DOCS/2.0-MIGRATION.md) | Per-command differences, retired extensions, platform subsets |
| [Release notes 2.0.0](DOCS/2.0-RELEASE-NOTES.md) | Bilingual release notes (draft until the tag) |
| [Command inventory](DOCS/2.0-inventory.tsv) | 176 applets, three states, exemption anchors |
| [Inventory summary](DOCS/2.0-inventory-summary.md) | Method, counts, terminal state |
| [Regex engine decision](DOCS/2.0-regex-engine-decision.md) | Why `winux-regex` is a port, and the `grep -P` exemption |
| [Replacement recipe](rust2/REPLACEMENT-RECIPE.md) | How one applet is wired (three gates, split engine, streaming, I18N) |
| [Gate report](rust2/GATE-REPORT.md) | Per-wave differential results |
| [Compatibility matrix](DOCS/en/command_compatibility_matrix.md) | 1.x-era per-command support notes |
| [WPM guide](DOCS/en/wpm_guide.md) | Package manager user guide |
| [Windows features](DOCS/en/windows_features.md) | Windows-specific behavior |

## 📦 Editions

WinuxCmd ships in two editions built from the same source:

| | **Default edition** | **Full edition** |
|---|---|---|
| Assets | `WinuxCmd-<version>-win-<arch>.*` | `WinuxCmd-<version>-win-<arch>-full.*` |
| Entry links | All commands **except** the full-only names | All entries |
| wpm index | yes | **not indexed** — standalone download only |

**Full-only commands:** the default edition does not create entry links for
**`[`** · **`env`** · **`false`** · **`kill`** · **`test`** · **`time`** ·
**`true`**. Their user-facing semantics are defined by your host shell's
builtins, so a `kill.exe`-style entry on `PATH` advertises a dispatch the
builtin owns. The commands stay fully implemented in the multicall binary —
`winuxcmd kill -9 <pid>` works in both editions. `echo`, `printf` and `pwd`
keep their entries. The definition lives in one place
([`src/core/dispatcher.cppm`](src/core/dispatcher.cppm)
`is_full_only_command`).

## 📦 WPM package manager

```bash
wpm install jq          # JSON processor
wpm install goawk       # awk implementation
wpm install bsdtar      # BSD tar
wpm install openssh     # SSH client
wpm install make        # GNU make
wpm install neovim      # text editor

wpm search json         # discover packages
wpm list --all          # see what's installed
```

Details in the [WPM User Guide](DOCS/en/wpm_guide.md).

## 🐂 Better together: the niubash shell

WinuxCmd gives Windows real Unix **commands**. [niubash](https://github.com/unixwin/niubash)
gives them a real **bash language** to live in:

```bash
for f in *.log; do
  grep -c ERROR "$f" | xargs -I{} echo "$f: {} errors"
done | sort -t: -k2 -rn | head -5
```

niubash injects WinuxCmd's command links onto `PATH` at startup, so the
`grep`, `sed` and `find` above are the real binaries.

## 🛠️ Building from source

**Prerequisites:** Visual Studio 2022+ · CMake 3.30+ · Ninja · Rust (stable)
with both MSVC targets.

```powershell
# Build (always use the script; it sets up the VS environment)
./scripts/build-with-vs.ps1

# Build the full edition (all entry links, "-full" asset names)
cmake -B build-full -G Ninja -DCMAKE_BUILD_TYPE=Release -DWINUXCMD_PACKAGE_EDITION=full

# Tests
./scripts/build-with-vs.ps1 -Target winuxcmd-tests
build-vs/tests/winuxcmd-tests.exe
```

### Rust toolchain (rust2 command library, #1488)

- **Install [rustup](https://rustup.rs) (stable) plus both MSVC targets:**
  `rustup target add x86_64-pc-windows-msvc aarch64-pc-windows-msvc`.
  The `aarch64` target is required even on an x64 host when you cross-build
  arm64: cargo builds the staticlib with `--target` for the C++ target
  architecture. CI pins the toolchain (`RUST2_TOOLCHAIN` in
  `.github/workflows/build-winuxcmd.yml`).
- **`WINUXCMD_ENABLE_RUST2`** defaults **ON** when cargo is found and **OFF**
  when it is not; `-DWINUXCMD_ENABLE_RUST2=ON` without cargo is a
  configure-time error.
- **No C++ fallback exists.** A Rust-less build omits every command in
  `WINUXCMD_RUST2_COMMANDS` (currently 50) from that build; the command is
  never dual-tracked (#1550). Official builds ship Rust.
- **Cross-arch consistency:** CI's `rust2-surface-check` requires x64 and
  arm64 packages to carry the same rust2 surface.
- **Run the Rust tests:**

  ```bash
  cd rust2
  cargo test -p winux-core -p winux-regex -p winux-cmds
  ```

**Profile-Guided Optimization (x64):** release CI builds the published x64
binary with PGO. To reproduce locally:

```powershell
./scripts/build-pgo.ps1          # -> build-pgo/usr/bin/winuxcmd.exe
```

Training workloads are in `scripts/pgo-train.sh`; ARM64 builds are not
PGO'd (profiles must be collected on the target architecture).

## 🤝 Contributing

Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md). For the Rust
core, read [`rust2/REPLACEMENT-RECIPE.md`](rust2/REPLACEMENT-RECIPE.md)
first: a replacement is mechanical, deletes the old body in the same change,
and passes the three gates.

## License

MIT — see [LICENSE](LICENSE). PCRE2 is a pinned static dependency
(pcre2-10.47) and ships its license with the source tree.

---

<div align="center">

**WinuxCmd** — because `ls` shouldn't require a Linux kernel.
Pair it with [**niubash**](https://github.com/unixwin/niubash) — the native bash shell that speaks GNU fluently on Windows.

[⬆ Back to top](#top)

</div>
