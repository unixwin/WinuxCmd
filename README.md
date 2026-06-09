# WinuxCmd

[English](README.md) | [中文](README-zh.md)

> GNU-style commands for real Windows shells.

![GitHub release (latest by date)](https://img.shields.io/github/v/release/caomengxuan666/WinuxCmd)
![GitHub all releases](https://img.shields.io/github/downloads/caomengxuan666/WinuxCmd/total)
![GitHub stars](https://img.shields.io/github/stars/caomengxuan666/WinuxCmd)
![GitHub license](https://img.shields.io/github/license/caomengxuan666/WinuxCmd)
![Windows Support](https://img.shields.io/badge/platform-Windows-blue)

WinuxCmd brings commands like `grep`, `sed`, `find`, `xargs`, `ls`, `cat`, and `rm` to Windows in a form that works with PowerShell, `cmd`, and Windows Terminal instead of asking you to leave them.

It is built for the very common Windows situation where Linux-flavored snippets come from AI, docs, CI logs, issue comments, or muscle memory, but the machine in front of you is still Windows.

![Windows Terminal](DOCS/images/WindowsTerminal.png)

## Install

WinuxCmd is available on winget:

```powershell
winget install caomengxuan666.WinuxCmd
```

If you prefer a zip package, download the latest build from [Releases](https://github.com/caomengxuan666/WinuxCmd/releases).

This project is also practical in conservative Windows environments that still expose Windows PowerShell 5.1, which makes it a good fit for many AI sandbox and automation hosts.

## Why WinuxCmd

- Native Windows first. No WSL, VM, or shell replacement.
- Built for mixed pipelines where Windows tools and GNU-style filters need to coexist.
- Started on January 23, 2026, before Microsoft's current Windows coreutils repository appeared on May 15, 2026.
- Friendly to PowerShell 5.1 environments, including AI sandboxes such as Codex that may not expose PowerShell 7.

That last point is a real product difference, not just a timeline note. Microsoft's current [Coreutils for Windows](https://github.com/microsoft/coreutils) README explicitly says PowerShell 7.4 or newer is required, while WinuxCmd is designed to remain usable in PowerShell 5.1-hosted environments.

## What It Feels Like

```bash
netstat -ano | grep 8080
tasklist | grep -i chrome
ipconfig | grep -i ipv4
dir /b | grep -E "\.cpp$" | sort | uniq
find . -name "*.cpp" -print0 | xargs -0 grep -n TODO
```

The point is not to turn Windows into Linux. The point is to let familiar GNU-style text workflows land cleanly in Windows terminals you already use.

## Use It Your Way

Prefer WinuxCmd names inside the current PowerShell session:

```powershell
winux activate
cat README.md
rm old.txt
man ls
```

Restore the original PowerShell behavior with:

```powershell
winux deactivate
```

If you want activation in each interactive PowerShell session, add this after the `winux` wrapper in `$PROFILE`:

```powershell
if (Get-Command winux -ErrorAction SilentlyContinue) {
    winux activate 6>$null
}
```

## Where It Fits

WinuxCmd is a strong fit when:

- AI gives you GNU-style one-liners but your host is Windows.
- You want `grep | sort | xargs | find` style workflows without opening WSL.
- You need native Windows commands like `tasklist`, `netstat`, `sc`, or `ipconfig` to flow into familiar text filters.
- Your environment is older or locked down and Windows PowerShell 5.1 is still what you actually get.

## Command Coverage

WinuxCmd currently implements 148 commands, including practical coverage across:

- file tools like `ls`, `cp`, `mv`, `rm`, `mkdir`, `ln`, `stat`, `readlink`, and `realpath`
- text tools like `cat`, `grep`, `sed`, `sort`, `uniq`, `cut`, `head`, `tail`, and `wc`
- search tools like `find` and `xargs`
- Windows-friendly utilities like `ps`, `lsof`, `which`, `tree`, `hexdump`, and `strings`

Detailed compatibility references live here:

- [Command Compatibility Matrix (EN)](DOCS/en/commands_implementation_en.md)
- [GNU Parity Ledger (EN)](DOCS/en/gnu_coreutils_parity.md)
- [命令兼容性矩阵 (ZH)](DOCS/zh/commands_implementation.md)
- [GNU 兼容清单 (ZH)](DOCS/zh/gnu_coreutils_parity.md)

## Windows Notes

- Unknown commands fall back through the parent shell, so WinuxCmd complements PowerShell and `cmd` instead of trying to replace them.
- After `winux activate`, `man` can directly override the PowerShell mapping in the current session.
- For interactive `cmd` use, launching Windows Terminal with `%SystemRoot%\System32\cmd.exe /k winuxcmd` is safer than a global `AutoRun` hook.

## More

- [Contributing Guide (EN)](CONTRIBUTING.md)
- [Contributing Guide (ZH)](CONTRIBUTING_ZH.MD)
- [Build Modes](DOCS/en/build_modes_en.md)
- [Custom Containers and Benchmarks](DOCS/en/custom_containers.md)
- [Local AI workflow for this repo](skills/winuxcmd/SKILL.md)
