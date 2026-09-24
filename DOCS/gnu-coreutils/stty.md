# stty

Print or change terminal characteristics.

## Synopsis

```
stty [-a|--all] [-g|--save] [SETTING...]
stty [-F DEVICE] [SETTING...]
```

## Description

With no arguments, `stty` prints all terminal settings in human-readable form.

## Options

| Option | Long | Description | Status |
|--------|------|-------------|--------|
| `-a` | `--all` | Print all current settings in human-readable form | Done |
| `-g` | `--save` | Print all current settings in a stty-readable form | Done |
| `-F` | `--file` | Open and use the specified device instead of stdin | Done |

## Settings

### Combination settings

| Setting | Description | Status |
|---------|-------------|--------|
| `sane` | Reset all settings to reasonable defaults | Done |
| `raw` | Disable all input processing | Done |
| `cooked` | Enable standard input processing | Done |
| `cbreak` | Like -icanon with min=1 | Done |

### Control settings

| Setting | Description | Status |
|---------|-------------|--------|
| `echo` | Echo input characters | Done |
| `-echo` | Do not echo input characters | Done |
| `icanon` | Enable canonical (line) mode | Done |
| `-icanon` | Disable canonical mode | Done |
| `isig` | Enable interrupt/quit signals | Done |
| `-isig` | Disable interrupt/quit signals | Done |
| `echoe` | Echo erase characters as BS-SP-BS | Done (no-op) |
| `echok` | Echo NL after kill character | Done (no-op) |
| `echonl` | Echo NL even without echo | Done (no-op) |
| `noflsh` | Disable flush after interrupt/quit | Done (no-op) |

### Special characters

| Setting | Description | Status |
|---------|-------------|--------|
| `intr CHAR` | Send interrupt signal (default: ^C) | Accepted (no-op) |
| `quit CHAR` | Send quit signal (default: ^\) | Accepted (no-op) |
| `erase CHAR` | Erase previous character (default: ^?) | Accepted (no-op) |
| `kill CHAR` | Erase current line (default: ^U) | Accepted (no-op) |
| `eof CHAR` | End of file (default: ^D) | Accepted (no-op) |
| `start CHAR` | Restart output after stop (default: ^Q) | Accepted (no-op) |
| `stop CHAR` | Stop output (default: ^S) | Accepted (no-op) |
| `susp CHAR` | Send suspend signal (default: ^Z) | Accepted (no-op) |

## Windows Limitations

On Windows, only `echo`, `icanon`, and `isig` settings have actual effect. These map to Windows console mode flags:
- `echo` → `ENABLE_ECHO_INPUT`
- `icanon` → `ENABLE_LINE_INPUT`
- `isig` → `ENABLE_PROCESSED_INPUT`

Other settings are accepted but silently ignored.

## Examples

```bash
stty              # Show all settings
stty -a           # Show all settings
stty -g           # Show machine-readable settings
stty sane         # Reset to defaults
stty -echo        # Disable echo
stty raw          # Set raw mode
stty cooked       # Set cooked mode
```

## See Also

- `tty(1)` - Print terminal name
