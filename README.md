# WinuxCmd: Linux Commands for Windows × Cross-Shell Pipelines

[English](README.md) | [中文](README-zh.md)

> Native Windows Linux-style commands | ~900KB single binary | No WSL required | Windows commands and Linux-style filters work together

![GitHub release (latest by date)](https://img.shields.io/github/v/release/caomengxuan666/WinuxCmd)
![GitHub all releases](https://img.shields.io/github/downloads/caomengxuan666/WinuxCmd/total)
![GitHub stars](https://img.shields.io/github/stars/caomengxuan666/WinuxCmd)
![GitHub license](https://img.shields.io/github/license/caomengxuan666/WinuxCmd)
![Windows Support](https://img.shields.io/badge/platform-Windows-blue)

## Why This Project

WinuxCmd is built for one practical goal: make common Linux command workflows work naturally on Windows terminals.

- Native Windows executable (`winuxcmd.exe`), no WSL required
- Linux-style commands and flags (`ls`, `grep`, `sed`, `ps`, `lsof`, ...)
- Fast startup and small footprint
- Works well with AI-generated shell commands
- Cross-tool pipelines between Windows and Linux-style commands

## Core Strengths

| Feature | Value |
|---|---|
| Windows × Linux pipeline interoperability | `netstat -ano \| grep 8080` works out of the box |
| Small footprint | ~900KB single executable with on-demand links |
| Fast execution | Millisecond-level command execution in common workflows |

## Quick Start

### Install (recommended)

```powershell
irm https://dl.caomengxuan666.com/install.ps1 | iex
```

### Manual install

1. Download from [Releases](https://github.com/caomengxuan666/WinuxCmd/releases)
2. Extract to any folder
3. Enter `bin` and run:

```powershell
.\create_links.ps1
```

Optional:

```powershell
.\create_links.ps1 -UseSymbolicLinks
.\create_links.ps1 -Remove
```

## Usage Modes

### 1) Direct command mode

```powershell
winuxcmd ls -la
winuxcmd grep -n "TODO" README.md
winuxcmd help
winuxcmd help sort
```

### 2) Linked command mode

After links are created, you can call commands directly:

```powershell
ls -la
grep -n "TODO" README.md
```

### 3) PowerShell alias activation

PowerShell aliases such as `ls`, `cat`, `rm`, and `man` can shadow the linked
GNU commands, especially in Windows PowerShell 5.1. Use `winux activate` in the
current session to point those names at WinuxCmd, and `winux deactivate` to
restore the original aliases:

```powershell
winux activate
rm old.txt
cat README.md
man ls
```

To auto-activate in new interactive PowerShell sessions, install the `winux`
profile wrapper and add this after it in `$PROFILE`:

```powershell
if (Get-Command winux -ErrorAction SilentlyContinue) {
    winux activate 6>$null
}
```

## FFI API

FFI is kept in the tree for future experimentation, but it is not part of the
default build or release path right now.

- Default builds keep `BUILD_FFI=OFF`
- Release-oriented modes force FFI off
- No FFI binary is shipped in the current release flow

The headers and example remain in the repo for reference, but treat them as
inactive unless you are explicitly reviving that path locally.

## Workspace Integration

Use the repository-local activation scripts when you want WinuxCmd available
inside this folder without touching global `PATH`:

```powershell
.\scripts\activate-workspace.ps1
man.exe ls
winuxcmd.exe help
```

For persistent interactive shells, install the optional user profile hook:

```powershell
.\scripts\install-workspace-profile-hook.ps1
```

The local guidance for AI usage lives in `skills/winuxcmd/SKILL.md`.
GitHub releases also publish a standalone `WinuxCmd-skill-v<version>.zip`
bundle alongside the Windows binaries.


## Pipeline Examples

```bash
netstat -ano | grep 8080
tasklist | grep -i chrome
ipconfig | grep -i "ipv4"
ps -ef | grep winuxcmd
```

## Implemented Commands

148 commands are currently implemented, including wildcard (glob) support for 48 commands:

- `ls`, `cat`, `grep`, `sed`, `head`, `tail`, `sort`, `wc`, `cut`, `rm`, `stat`, `md5sum`, `find`, `tree`, `more`, `hexdump`, `strings`, `col`, `stty`, `dir`, `vdir`, `dircolors`, `chgrp`, and more
- Wildcard patterns: `*`, `?`, `[abc]`, `[a-z]` character classes
- Cross-shell pipelines: `netstat -ano | grep 8080` works out of the box

See full compatibility and option details:

- [Command Compatibility Matrix (EN)](DOCS/en/commands_implementation_en.md)
- [命令兼容性矩阵 (ZH)](DOCS/zh/commands_implementation.md)

## Performance Comparison

Full execution benchmark (startup + execution + exit), 1000-file directory, 20 runs per command.

| Command | WinuxCmd (ms) | uutils (Rust) (ms) | Winner |
|---------|---------------|--------------------|--------|
| ls | 6.30 | 7.27 | WinuxCmd |
| cat | 6.19 | 7.01 | WinuxCmd |
| head | 6.27 | 6.79 | WinuxCmd |
| tail | 6.34 | 6.84 | WinuxCmd |
| grep | 6.42 | 5.99 | uutils |
| sort | 6.31 | 7.27 | WinuxCmd |
| uniq | 6.23 | 6.84 | WinuxCmd |
| wc | 6.21 | 6.81 | WinuxCmd |

Summary:

- WinuxCmd wins in 7/8 commands
- Average time: WinuxCmd 6.28ms vs uutils 6.85ms
- Overall: about 1.09x faster

Benchmark details and notes:

- [Build Modes](DOCS/en/build_modes_en.md)
- [Custom Containers and Benchmarks](DOCS/en/custom_containers.md)

## Roadmap

### Phase 1: Core and Compatibility

- Stabilize Linux-style core commands on Windows
- Keep binary size and startup performance targets

### Phase 2: Shell and Tooling

- More compatibility fixes for mixed Windows/Linux pipelines
- Improved test coverage for shell-edge scenarios

### Phase 3: Ecosystem

- Better integration docs and templates
- Contributor tooling and automation improvements
- Long-term cross-platform abstraction planning

## Q&A

### Q: Is this a replacement for PowerShell?

A: No. WinuxCmd complements PowerShell. Use whichever syntax is best for the task.

### Q: Why does output sometimes show access-denied warnings (for example in lsof)?

A: Windows handle/process visibility depends on privilege level. WinuxCmd degrades gracefully and reports partial-result warnings instead of crashing.

## Project Characteristics

- Many users in daily usage scenarios
- Limited contributor capacity
- Strong need for focused, high-quality contributions

If you want to help, the most valuable areas are:

1. Bug fixes for shell compatibility
2. Test coverage for command behavior on Windows
3. Documentation clarity and examples

## Contributing

- [CONTRIBUTING.md](CONTRIBUTING.md)

## Documentation

- [Overview (EN)](DOCS/en/overview.md)
- [Shell Integration (EN)](DOCS/en/winux_shell_integration_en.md)
- [概览 (ZH)](DOCS/zh/overview_zh.md)
- [Shell 集成 (ZH)](DOCS/zh/winux_shell_integration_zh.md)
- [Build Modes](DOCS/en/build_modes_en.md)
- [Testing Framework](DOCS/en/testing_framework_en.md)

## License

MIT License (c) 2026 caomengxuan666. See [LICENSE](LICENSE).

