<a id="top"></a>

<div align="center">

<img src=".github/assets/banner.svg" alt="WinuxCmd — Unix commands, native on Windows. 176 commands, 1924 options, 93% differential pass rate (178 cases / 13 tracked platform diffs)." width="100%">

**Real Unix commands. Real Windows paths. One ~3 MB executable.**
No WSL · No Cygwin · No MSYS2 · No path-translation pain
**v1.0.0 stable is out.** 🎉

[![GitHub release](https://img.shields.io/github/v/release/unixwin/WinuxCmd)](https://github.com/unixwin/WinuxCmd/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/unixwin/WinuxCmd/total)](https://github.com/unixwin/WinuxCmd/releases)
[![Stars](https://img.shields.io/github/stars/unixwin/WinuxCmd)](https://github.com/unixwin/WinuxCmd/stargazers)
[![License](https://img.shields.io/github/license/unixwin/WinuxCmd)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-blue)

[💾 Install](#-install) · [⚡ Demo](#-unix-muscle-memory-on-windows) · [🐂 niubash](#-better-together-the-niubash-shell) · [📦 WPM](#-wpm-package-manager) · [🆚 Compare](#-how-it-compares) · [📚 Docs](#-documentation) · [中文](README-zh.md)

</div>

---

## The problem, in one sentence

You're on Windows and you need `grep -rn`, `sed -i`, `find -exec`, `xargs -0` — and every option so far is a compromise:

- **WSL** — 1 GB+ install, and a VM filesystem boundary between you and your files
- **Cygwin** — path-translation gymnastics and a 2–5 s startup
- **GnuWin32** — abandoned in 2012, stuck at 60% compatibility
- **uutils** — a great Rust project, but ~100 commands and ~600 options

**WinuxCmd skips the compromise.** A single native Win32 executable that speaks GNU syntax on Windows paths: **176 commands, 1,924 options** — with ongoing differential testing against a GNU coreutils 9.7 oracle (178 cases, 165 pass, 13 tracked platform diffs).

| | | | | |
|:---:|:---:|:---:|:---:|:---:|
| **176** | **1,924** | **178 cases** | **2,420** | **~4 MB** |
| commands | options¹ | GNU diff · 93% pass | unit tests · 99.6% pass | single binary |

> ¹ 1,924 = `OPTION(` macro count in `src/commands/`. Command count excludes the internal `wpm` manager and the `[` bracket alias of `test`. Differential corpus: 178 cases across 84 commands (165 pass / 13 known platform diffs, `tests/differential/baseline.json`). See the [GNU comparison report](DOCS/en/gnu_comparison_report.md).

---

## ⚡ Unix muscle memory on Windows

```bash
# Every GNU flag you know, on real Windows paths
ls -la
grep -rn "TODO" src/
sed -i 's/http:/https:/g' config.ini
find . -name "*.tmp" -exec rm {} \;

# Full pipelines, zero setup — command links land on your PATH,
# so there are no prefixes and nothing to configure
find . -name "*.cpp" -print0 | xargs -0 wc -l

# And when a tool shouldn't be reimplemented, WPM installs the real thing
wpm install jq
```

## 💾 Install

| | |
|---|---|
| **Installer (recommended)** | Grab `WinuxCmd-<version>-x64-setup.exe` (or the ARM64 setup) from [GitHub Releases](https://github.com/unixwin/WinuxCmd/releases/latest) |
| **Portable** | Unzip `WinuxCmd-<version>-win-x64.zip` anywhere and add it to your `PATH` |
| **Build from source** | VS 2022 + CMake 3.30 + Ninja — see [Building from source](#️-building-from-source) |

## 🚀 Why WinuxCmd

- 🪟 **Native, not emulated** — talks to Win32 APIs directly. Understands `C:\`, UNC paths and NTFS ACLs (with `cygpath` and `getfacl` for bridging). No VM, no runtime DLLs, instant startup.
- 🧠 **GNU where it counts** — `find` alone implements 88 options (full expression parser, `-exec`/`-execdir`/`-ok`, `-printf`); `grep` ships PCRE2; `sed` supports in-place `-i` editing.
- 📦 **WPM built in** — a package manager for the tools that shouldn't be reimplemented: jq, ripgrep, fd, fzf, bat, make, neovim, curl, wget…
- 🧪 **Tested like it matters** — 2,742 unit tests, plus a 178-case differential suite byte-compared against authoritative upstream sources (GNU coreutils 9.7, findutils 4.10.0, grep 3.12, sed 4.9, patch 2.7.6, util-linux 2.41, procps-ng 4.0.4). Every command audited line-by-line; reports in `tests/differential/audit-*.md`.
- ⚡ **Small, fast, PGO-built** — 3.79 MB for 176 commands, statically linked, zero dependencies, ~19 ms cold start. Published x64 binaries are Profile-Guided Optimization builds trained on real workloads (sort −18%, nl −17% vs the same code without PGO). Methodology and data: `docs/performance-roadmap.md`.

## 🐂 Better together: the niubash shell

WinuxCmd gives Windows real Unix **commands**. [niubash](https://github.com/unixwin/niubash) gives them a real **bash language** to live in — the same unixwin org, two halves of one workflow:

```bash
# In niubash (native bash on Windows — no WSL):
for f in *.log; do
  grep -c ERROR "$f" | xargs -I{} echo "$f: {} errors"
done | sort -t: -k2 -rn | head -5
```

- **Real bash semantics** — `if`/`for`/functions/arrays/pipelines run on [niubash](https://github.com/unixwin/niubash), powered by its [rubash](https://github.com/unixwin/rubash) engine, green across the GNU Bash upstream test suite (86/86).
- **Agent-friendly** — `niu -c` is quiet and deterministic: no banners, stable stdout/stderr, exact exit codes. The shell your AI tooling already speaks.
- **Zero glue** — niubash injects WinuxCmd's command links onto the `PATH` at startup, so `grep`, `sed` and `find` above are the real binaries you're looking at right now.

Ship WinuxCmd alone in a 3 MB exe, or drop in [niubash](https://github.com/unixwin/niubash) (v1.0.0) and get the whole bash workflow.

## 📦 WPM package manager

```bash
wpm install jq          # JSON processor
wpm install goawk       # awk implementation
wpm install bsdtar      # BSD tar
wpm install openssh     # SSH client
wpm install make        # GNU make
wpm install neovim      # text editor
wpm install curl        # URL transfer
wpm install wget        # network downloader

wpm search json         # discover packages
wpm list --all          # see what's installed
```

Details in the [WPM User Guide](DOCS/en/wpm_guide.md).

## 🆚 How it compares

| Feature | WinuxCmd | uutils (Rust) | GnuWin32 | Cygwin | busybox |
|---------|:--------:|:-------------:|:--------:|:------:|:-------:|
| **Commands** | **176** | ~100 | ~90 | ~200 | ~300 |
| **Options** | **1,924** | ~600 | ~200 | Full | ~500 |
| **GNU compat** | **81% diff pass** | 95% | 60% | 99% | 70% |
| **Native Win32** | ✅ | ❌ | ✅ | ❌ | ❌ |
| **Package manager** | ✅ WPM | ❌ | ❌ | apt-cyg | ❌ |
| **Test cases** | **2,420** | ~2,000 | 0 | — | ~100 |
| **Binary size** | **~3 MB** | ~5 MB | — | 1 GB+ | — |
| **Startup** | **Instant** | Instant | — | 2–5 s | — |
| **Maintained** | ✅ 2026 | ✅ | ❌ since 2012 | ✅ | ❌ |

<details>
<summary><b>Deep dive: vs uutils / GnuWin32 / Cygwin</b></summary>

### vs uutils/coreutils (Rust)

| Aspect | WinuxCmd | uutils |
|--------|----------|--------|
| Language | C++23 | Rust |
| Commands | 176 | ~100 |
| Options | 1,924 | ~600 |
| Binary size | ~3 MB | ~5 MB |
| Dependencies | None | Rust runtime |
| Build time | 2 min | 15 min |
| Package manager | WPM built-in | None |

### vs GnuWin32

| Aspect | WinuxCmd | GnuWin32 |
|--------|----------|----------|
| Maintenance | Active | Abandoned |
| Last update | 2026 | 2012 |
| Windows support | Win10/11 | WinXP+ |
| Modern toolchain | C++23, CMake | C, autotools |

### vs Cygwin

| Aspect | WinuxCmd | Cygwin |
|--------|----------|--------|
| Install size | ~3 MB | 1 GB+ |
| Startup time | Instant | 2–5 s |
| Path handling | Native Windows | Unix emulation |
| Dependencies | None | MSYS2 runtime |
| Package manager | WPM | apt-cyg |

</details>

## 🧰 Command coverage

<details>
<summary><b>176 commands — full coverage table (click to expand)</b></summary>

### GNU Coreutils (83 commands)

| Category | Commands | Differential status |
|----------|----------|---------------------|
| File Ops | cp, mv, rm, ln, install, mkdir, rmdir, touch, unlink | ✅ mostly verified; cp has 9+ options rejected on Windows (issue-140) |
| Text Processing | cat, echo, head, tail, sort, uniq, cut, tr, wc, fold, fmt, join, comm | ✅ diff PASS except **fmt** (OUT_DIFF, issue-140) |
| Directory Listing | ls, dir, vdir | ✅ ls PASS; `dir` Windows-columnar by design |
| Search | find, xargs | ✅ |
| Crypto/Hash | base64, base32, basenc, md5sum, sha1sum, sha256sum, sha384sum, sha512sum, b2sum, cksum, sum | ✅ |
| Date/Time | date, touch, time, timeout | ✅ |
| System Info | uname, hostname, id, whoami, users, groups, nproc, uptime, arch | ⚠️ **whoami/users/groups** thin stubs (≤1 declared option each) |
| Disk | df, du, stat | ⚠️ **stat** deep-path gap (OUT_DIFF, issue-140) |
| Env/Expr | env, printenv, expr, seq, yes, true, false | ✅ |
| Text Format | pr, nl, expand, unexpand, column, paste, tsort, ptx | ⚠️ **tsort** and **ptx** output-format gaps (OUT_DIFF, issue-140) |
| File Info | file, stat, readlink, realpath, dirname, basename, pathchk, sync | ⚠️ **stat** deep-path; **file** no-magic stub |
| Process | nice, nohup, stdbuf | ⚠️ **nice** thin (1 option); **stdbuf** EXIT_DIFF (issue-140) |
| Permissions | chmod, chown, chgrp, chroot | ⚠️ Linux ownership/SELinux options rejected on Windows |
| Other | shred, factor, kill, truncate, fmt, numfmt, mktemp, dircolors, sum, csplit, split | ⚠️ **fmt** OUT_DIFF gap |

### GNU findutils/grep/sed (3 commands)

| Command | Options | Features |
|---------|---------|----------|
| **find** | 88 | Full expression parser, -exec/-execdir/-ok, -printf |
| **grep** | 49 | PCRE2 support, --color, --exclude patterns |
| **sed** | 17 | In-place editing, extended regex, --posix |

### BSD tools (15 commands)

cal, column, hexdump, logger, tree, less, more, strings, rev, tsort, seq, sleep, nohup, watch, tput

### Process management (13 commands)

ps, top, kill, killall, pgrep, pkill, pidof, pldd, free, uptime, renice, stdbuf, timeout

### Cygwin/MSYS2 (14 commands)

cygpath, dos2unix, unix2dos, d2u, u2d

### System info (16 commands)

hostname, id, who, pinky, stty, infocmp, tic, toe, locale, tput, getconf, getfacl, ldd, lsof, file, man

### Custom extensions (10+ commands)

wpm (package manager), mpicalc, regtool, mkpasswd, mkgroup, mkfifo, mknod, clear, reset, tzset

</details>

## 🏃 Benchmarks

| Test | WinuxCmd | uutils | GNU (WSL2) |
|------|----------|--------|------------|
| cat (100 MB) | 0.8 s | 0.9 s | 0.7 s |
| sort (1M lines) | 2.1 s | 2.3 s | 1.9 s |
| grep (100 MB) | 1.2 s | 1.1 s | 1.0 s |
| find (10K files) | 0.3 s | 0.4 s | 0.2 s |

Consistently in the same league as uutils — and within ~10–15% of native GNU running under WSL2, without booting a VM.

*Benchmark environment: Windows 11, Intel i7-13700K, 32 GB RAM, NVMe SSD*

## 🧪 Testing and GNU verification

- **2,420 automated unit tests** — **99.6% pass rate**
- **Differential corpus**: 178 test cases (131 corpus + 47 regressions, covering **84 commands**), executed against a GNU coreutils 9.7 oracle with identical inputs — **165 pass / 13 tracked diffs (93%)**, machine-checked in `tests/differential/baseline.json`
- Automated GNU comparison: `scripts/compare_outputs.sh` and `gnu_comparison_tests.sh`; per-case runner: `tests/differential/runner.sh`

| Tracked diffs (13) | Command / case | Reason |
|-----------|---------|--------|
| `id -g` | regressions/21-id-group | platform: Windows has no POSIX gid; prints primary-group RID (197121) |
| `cp -l`, `ln` | regressions/23-cp-link-option, ln/35-hard | environment: hardlinks across `\\wsl.localhost` 9p unsupported in the WSL-side runner; verified on native NTFS |
| `mkdir` exists | mkdir/30-exists | format: GNU uses locale-dependent curly quotes (U+2018/2019); winuxcmd uses ASCII `'` |
| dd, envsubst, mktemp, namei, readlink, which, seq | 9 remaining cases | output-format/platform differences; per-case state lives in `tests/differential/baseline.json` |

The eight former issue-140 gaps (dd, diff -u, tsort, fmt, stat, sdiff, ptx, stdbuf) all pass; normal-format `diff` hunk-header, hash-family separator, cksum line-ending, realpath forward-slash, and od -c octal-escaping gaps were all found and fixed in 2026-09.

See the [GNU Comparison Report](DOCS/en/gnu_comparison_report.md).

## 📚 Documentation

| Document | Description |
|----------|-------------|
| [Compatibility Matrix](DOCS/en/command_compatibility_matrix.md) | Support status of all 176 commands |
| [GNU Comparison Report](DOCS/en/gnu_comparison_report.md) | Differential testing vs a GNU coreutils 9.7 oracle |
| [Windows Features](DOCS/en/windows_features.md) | Windows-specific behavior |
| [WPM Guide](DOCS/en/wpm_guide.md) | Package manager user guide |
| [GNU Test Baseline](DOCS/en/gnu_test_baseline.md) | GNU test framework |

## 🛠️ Building from source

**Prerequisites:** Visual Studio 2022+ · CMake 3.30+ · Ninja

```bash
# Build
./scripts/build-with-vs.ps1

# Run tests
./scripts/build-with-vs.ps1 -Target winuxcmd-tests
build-vs/tests/winuxcmd-tests.exe
```

**Profile-Guided Optimization (x64):** release CI builds the published x64
binary with PGO automatically — instrumented build, training workloads,
then an `/USEPROFILE` relink. To reproduce locally:

```bash
./scripts/build-pgo.ps1          # -> build-pgo/usr/bin/winuxcmd.exe
```

Training workloads (`scripts/pgo-train.sh`) are ported from
uutils/coreutils' `build-pgo.sh`, so profiles stay machine-independent.
ARM64 builds are not PGO'd: profile collection must run on the target
architecture.

## 🤝 Contributing

Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT — see [LICENSE](LICENSE).

---

<div align="center">

**WinuxCmd** — because `ls` shouldn't require a Linux kernel.
Pair it with [**niubash**](https://github.com/unixwin/niubash) — the native bash shell that speaks GNU fluently on Windows.

[⬆ Back to top](#top)

</div>
