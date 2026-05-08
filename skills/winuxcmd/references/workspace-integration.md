# Workspace Integration

Use the repository-local activation path when working in WinuxCmd. It keeps the
tooling inside the current checkout and avoids editing user or machine `PATH`.

## Activation

1. `scripts/setup-workspace-bin.ps1` creates `.winuxcmd/bin`.
2. `scripts/activate-workspace.ps1` prepends that directory to the current
   PowerShell session only and removes common alias collisions for that session.
3. `scripts/activate-workspace.cmd` performs the same setup for `cmd`.

## Persistent shells

- `scripts/install-workspace-profile-hook.ps1` adds a per-user profile hook for
  PowerShell 7 and Windows PowerShell 5.1.
- The hook backs up each profile file before editing.
- Use `-Remove` to uninstall the hook and restore the default shell behavior.

## Practical rule

If a shell was already open before activation, run the activation script in that
shell once. New shells started from the repository can auto-activate when the
profile hook is installed.
