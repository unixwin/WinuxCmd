# WinuxCmd Shell Integration (PowerShell / CMD / Windows Terminal)

## Recommended Startup

### PowerShell command activation

PowerShell aliases can shadow common GNU names such as `ls`, `cat`, `rm`, and
`man`. Run this in the current session to make those bare commands use
WinuxCmd:

```powershell
winux activate
```

This also works in Windows PowerShell 5.1. To restore the original PowerShell
aliases:

```powershell
winux deactivate
```

To auto-activate in every new interactive PowerShell session, install the
`winux` profile wrapper first, then add this after the wrapper block in
`$PROFILE`:

```powershell
if (Get-Command winux -ErrorAction SilentlyContinue) {
    winux activate 6>$null
}

If `winux activate` reports `winux.ps1 not found`, the usual cause is a stale
older wrapper still left in `$PROFILE` from a previous install path such as an
old Scoop location. Re-run `winux-activate.ps1` from the current WinuxCmd
install to rewrite the wrapper against the latest `%LOCALAPPDATA%\WinuxCmd`
runtime.
```

### CMD / Windows Terminal

Add the WinuxCmd `bin` directory to PATH or run `create_links.ps1` in the
release `bin` directory. Shell parsing, pipelines, redirection, and unknown
commands remain handled by cmd or PowerShell.

## Troubleshooting

1. `rm`, `cat`, or `man` still run the PowerShell command:
   - Run `winux activate`, or add the auto-activation snippet above to `$PROFILE`.
2. Profile script not loaded:
   - verify execution policy and profile path with `echo $PROFILE`.
3. `ls` seems to be hijacked even though `Get-Command ls` still shows the PowerShell alias:
   - another tool may have installed a `PSConsoleHostReadLine` rewrite in `$PROFILE`; Microsoft `coreutils` does this, so inspect your profile for injected wrapper blocks.
