# Workspace Integration

This repository supports a local-only activation path for AI agents and
developer shells.

## Goal

- Make WinuxCmd available inside the current workspace.
- Avoid editing user PATH, machine PATH, or PowerShell profiles.
- Avoid admin-only installation steps.

## How it works

1. `scripts/setup-workspace-bin.ps1` creates `.winuxcmd/bin`.
2. The directory contains `.exe` hardlinks for every command.
3. `scripts/activate-workspace.ps1` prepends that directory to the current
   PowerShell process PATH only and clears common alias collisions for that
   session.
4. `scripts/activate-workspace.cmd` does the same for CMD.

## Recommended usage

PowerShell:

```powershell
.\scripts\activate-workspace.ps1
man.exe ls
winuxcmd.exe help
```

CMD:

```cmd
scripts\activate-workspace.cmd
man.exe ls
winuxcmd.exe help
```

## Build with Visual Studio

Use this script when you need an MSVC build environment without changing the
global shell setup:

```powershell
.\scripts\build-with-vs.ps1 -Target winuxcmd-tests
```

It defaults to a fresh `build-vs` directory and uses `vcvars64.bat` unless you
override the environment script.

## Optional persistent activation

If you want new PowerShell sessions started inside this repository to auto-load
WinuxCmd, install the user profile hook. It updates both the PowerShell 7
profile and the Windows PowerShell 5.1 profile for the current user, and keeps
per-file backups before editing:

```powershell
.\scripts\install-workspace-profile-hook.ps1
```

Remove it with:

```powershell
.\scripts\install-workspace-profile-hook.ps1 -Remove
```

## Notes

- PowerShell activation clears common alias collisions for the session, so
  bare `ls`, `cp`, `mv`, `rm`, and similar commands resolve to WinuxCmd.
- Use explicit `.exe` names when you want to stay unambiguous.
- The local bin can be removed with `scripts\setup-workspace-bin.ps1 -Remove`.
- The setup script expects an NTFS-style filesystem for hardlinks.
- The repo-local AI usage guide lives in `skills/winuxcmd/SKILL.md`.
