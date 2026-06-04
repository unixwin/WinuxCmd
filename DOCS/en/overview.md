# WinuxCmd Overview

## What WinuxCmd Is

WinuxCmd is a native Windows command-line toolset that provides Linux-style command usability without requiring WSL.

Core goals:

- Keep command usage familiar for Linux users
- Keep execution native and lightweight on Windows
- Improve reliability of AI-generated command workflows on Windows

## Key Differentiators

1. Native Windows runtime
2. Linux-style command ergonomics
3. Cross-tool pipelines in Windows terminal workflows
4. Small binary and fast startup

## Shell Behavior (Important)

WinuxCmd REPL supports built-in commands and fallback execution.

- Known WinuxCmd command: dispatched internally
- Unknown command: fallback to native shell
- Fallback shell is selected by parent shell session:
  - entered from PowerShell/pwsh -> fallback uses PowerShell
  - entered from cmd -> fallback uses cmd

This keeps `Get-Process`/`Where-Object` usable when you entered from PowerShell.

## Completion

User completion file:

![Auto Completion Demo](../images/auto.gif)

- default: `%USERPROFILE%\.winuxcmd\completions\user-completions.txt`
- env override: `WINUXCMD_COMPLETION_FILE`

Format:

```text
cmd|<command>|<description>
opt|<command>|<option>|<description>
```

## Shell Integration

- [Shell Integration Guide (EN)](winux_shell_integration_en.md)
- [Shell Integration Guide (ZH)](../zh/winux_shell_integration_zh.md)

## Command Matrix

- [Command Compatibility Matrix](commands_implementation_en.md)
- [GNU Coreutils Parity Audit](gnu_coreutils_parity.md) This includes GNU manual-based option mapping and wildcard expansion rules.

## Build and Test

- Build modes: [build_modes_en.md](build_modes_en.md)
- Testing: [testing_framework_en.md](testing_framework_en.md)
- Workspace integration: [workspace_integration.md](workspace_integration.md)
- AI usage guide: [../../skills/winuxcmd/SKILL.md](../../skills/winuxcmd/SKILL.md)
- GNU coreutils parity audit: [gnu_coreutils_parity.md](gnu_coreutils_parity.md)

## Contribution Reality

The project has many users but relatively few maintainers/contributors.

High-impact contributions:

1. shell compatibility fixes
2. regression tests for REPL and fallback logic
3. documentation improvements
