---
name: winuxcmd
description: Use when working in the WinuxCmd repository and needing repository-local command activation, GNU-like command usage on Windows, PowerShell alias collision handling, man.exe help lookup, or Visual Studio build/test commands.
metadata:
  short-description: Activate and use WinuxCmd in-repo
---

# WinuxCmd

Use this workspace-local guide when interacting with the WinuxCmd repository.

## What to do first

1. Activate the repository-local command directory for the current shell only:

```powershell
.\scripts\activate-workspace.ps1
```

2. If you are in `cmd`, use:

```cmd
scripts\activate-workspace.cmd
```

3. Prefer explicit command names with `.exe` in PowerShell to avoid aliases:

```powershell
man.exe ls
grep.exe -n TODO README.md
winuxcmd.exe help sort
```

## Rules

- Do not modify user PATH or machine PATH.
- Do not rely on PowerShell aliases such as `man` or `ls`.
- Use `man.exe` explicitly when asking for command help.
- Use the repository-local `.winuxcmd\bin` directory for command execution.
- If a command exists both as a PowerShell alias and as a WinuxCmd executable,
  always call the executable with `.exe`.

## Repository-local activation

- `scripts/setup-workspace-bin.ps1` creates `.winuxcmd/bin`.
- `scripts/activate-workspace.ps1` prepends that directory to the current
  PowerShell session PATH and clears common alias collisions for the session.
- `scripts/activate-workspace.cmd` does the same for CMD sessions.

## Visual Studio build

- Use `scripts/build-with-vs.ps1` when you need an MSVC build/test run.
- Default target: `winuxcmd-tests`.
- Default build directory: `build-vs`.
- Default environment script: `vcvars64.bat`.

## Persistent PowerShell activation

- If you need bare `ls`/`man`/`cp` to keep working in new PowerShell sessions
  started from this repository, install the optional profile hook with
  `scripts/install-workspace-profile-hook.ps1`.
- The hook updates both the PowerShell 7 profile and the Windows PowerShell
  5.1 profile for the current user, and backs up each file before editing.
- Remove it with `-Remove` when you want to restore the default shell.

## Bare-command behavior

- After PowerShell activation, bare `ls`, `cp`, `mv`, `rm`, and similar common
  commands should resolve to WinuxCmd in the current session.
- Use `man.exe` when you want the safest unambiguous help lookup.

## Useful references

- `DOCS/en/workspace_integration.md`
- `DOCS/en/commands_implementation_en.md`
- `DOCS/en/overview.md`
