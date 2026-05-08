---
name: winuxcmd
description: Repository-local WinuxCmd workflow for AI agents: download and integrate released WinuxCmd builds, activate .winuxcmd/bin, avoid PowerShell alias collisions, use man.exe, run MSVC builds/tests, and package the skill for release. Use when working in this repository or preparing WinuxCmd release assets and the skill bundle.
---

# WinuxCmd

Use this skill when working inside the WinuxCmd repository.

## Start Here

1. Activate the repository-local command directory for the current shell:

```powershell
.\scripts\activate-workspace.ps1
```

2. If you are in `cmd`, use:

```cmd
scripts\activate-workspace.cmd
```

3. Prefer explicit command names with `.exe` in PowerShell when you need unambiguous behavior:

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

## Workspace Activation

- `scripts/setup-workspace-bin.ps1` creates `.winuxcmd/bin`.
- `scripts/activate-workspace.ps1` prepends that directory to the current
  PowerShell session PATH and clears common alias collisions for the session.
- `scripts/activate-workspace.cmd` does the same for CMD sessions.
- `scripts/install-workspace-profile-hook.ps1` updates the current user's
  PowerShell 7 and Windows PowerShell 5.1 profiles so new shells started in this
  repository auto-activate the workspace.
- If you are integrating a downloaded WinuxCmd release, pass
  `-WinuxCmdPath <path-to-winuxcmd.exe>` to `scripts/setup-workspace-bin.ps1`
  before activating the workspace.

## Build and Test

- Use `scripts/build-with-vs.ps1` when you need an MSVC build/test run.
- Default target: `winuxcmd-tests`.
- Default build directory: `build-vs`.
- Default environment script: `vcvars64.bat`.

## Command Guidance

- After activation, bare `ls`, `cp`, `mv`, `rm`, and similar common commands
  should resolve to WinuxCmd in the current session.
- Use the repo's GNU-parity flags when they reduce AI mistakes:
  - `ls`: `-d`, `-b`, `-f`, `-I`, `-U`, `-X`, `-v`
  - `cp`: `-t`, `-T`, `-n`, `-u`
  - `mv`: `-t`, `-T`, `-n`, `-u`, `-b`
  - `install`: `-D`, `-t`, `-T`
  - `sort`: `-V`
- If a command is already available as a WinuxCmd executable, prefer it over a
  PowerShell alias or external fallback.

## Release Packaging

- When packaging the skill, load `references/release-packaging.md` first.
- The release asset should ship the standalone skill bundle together with the
  Windows binaries.
- Keep the skill bundle rooted at `skills/winuxcmd` with `SKILL.md` at the top
  level of the archive.

## References

- `references/workspace-integration.md`
- `references/download-and-integrate.md`
- `references/command-guidance.md`
- `references/release-packaging.md`
