# Download and Integrate WinuxCmd

Use this when you need a released WinuxCmd binary inside the current
repository without changing global `PATH`.

## Download

- Download the matching release archive from GitHub Releases.
- Extract it to a local folder.

## Integrate into the workspace

If you have a `winuxcmd.exe` path from a release or a local build, point the
workspace setup script at it:

```powershell
.\scripts\setup-workspace-bin.ps1 -WinuxCmdPath "C:\path\to\winuxcmd.exe"
.\scripts\activate-workspace.ps1
```

If you are already inside the extracted release folder, you can also generate
the command links there with `scripts\create_links.ps1`.

Use `-UseSymbolicLinks` if hardlinks are not available on that filesystem.

## Persistent shells

To make new PowerShell sessions auto-activate inside this repository, install
the profile hook:

```powershell
.\scripts\install-workspace-profile-hook.ps1
```

The hook backs up the profile files before editing them. Use `-Remove` to
uninstall it later.
