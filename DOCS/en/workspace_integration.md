# Workspace Integration

This repository supports a local-only activation path for AI agents and
developer shells.

## Goal

- Make WinuxCmd available inside the current workspace.
- Avoid editing user PATH, machine PATH, or PowerShell profiles.
- Avoid admin-only installation steps.
- Keep the repo-local command directory self-contained and disposable.

## How it works

1. `scripts/setup-workspace-bin.ps1` creates `.winuxcmd/bin` in the repository
   root.
2. The directory contains `.exe` hardlinks for `winuxcmd.exe` and every
   command under `src/commands`.
3. `scripts/activate-workspace.ps1` prepends that repo-local directory to the
   current PowerShell process PATH only, sets `WINUXCMD_HOME`, and clears
   common alias collisions for that session.
4. `scripts/activate-workspace.cmd` does the same for CMD.

## Workspace-local bin

`.winuxcmd/bin` is the only path this repo needs for local activation. It is
generated on demand, stays inside the repository tree, and can be removed and
recreated without touching global shell state.

## Recommended usage

PowerShell:

```powershell
.\scripts\activate-workspace.ps1
man.exe ls
winuxcmd.exe help
```

Use `man.exe`, not bare `man`, when you want the WinuxCmd-provided helper in
PowerShell. The activation script clears common alias collisions for the
session, but the explicit `.exe` form is still the least ambiguous call.

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

If you want new PowerShell sessions started inside this repository to
auto-activate WinuxCmd, install the user profile hook. It writes the same hook
block into both the PowerShell 7 profile and the Windows PowerShell 5.1 profile
for the current user, keeps per-file backups before editing, and only activates
when the current location is under this repository root:

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
- Use explicit `.exe` names when you want to stay unambiguous, especially
  `man.exe`.
- The local bin can be removed with `scripts\setup-workspace-bin.ps1 -Remove`.
- The setup script expects an NTFS-style filesystem for hardlinks.
- The repo-local AI usage guide lives in `skills/winuxcmd/SKILL.md`.
