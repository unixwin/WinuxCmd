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
