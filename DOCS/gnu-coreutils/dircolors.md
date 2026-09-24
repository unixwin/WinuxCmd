# dircolors

Output commands to set the LS_COLORS environment variable.

## Synopsis

```
dircolors [OPTION]... [FILE]
```

## Description

`dircolors` outputs shell commands to set the `LS_COLORS` environment variable, which is used by `ls --color` to colorize file listings.

If `FILE` is specified, read it to determine which colors to use for which file types and extensions. Otherwise, a precompiled database is used.

## Options

| Option | Long | Description | Status |
|--------|------|-------------|--------|
| `-b` | `--sh` | Output Bourne shell code to set LS_COLORS (default) | Done |
| `-c` | `--csh` | Output C shell code to set LS_COLORS | Done |
| `-p` | `--print-database` | Output defaults | Done |
| | `--print-ls-colors` | Output fully escaped colors for display | Done |

## Default Color Database

The default color assignments include:

| Extension | Color | Description |
|-----------|-------|-------------|
| `di` | `01;34` | Directory |
| `ln` | `01;36` | Symbolic link |
| `pi` | `40;33` | Named pipe (FIFO) |
| `so` | `01;35` | Socket |
| `bd` | `40;33;01` | Block device |
| `cd` | `40;33;01` | Character device |
| `ex` | `01;32` | Executable file |
| `tar` | `01;31` | Tar archive |
| `gz` | `01;31` | Gzip archive |
| `zip` | `01;31` | Zip archive |
| `jpg` | `01;35` | JPEG image |
| `png` | `01;35` | PNG image |
| `mp3` | `00;36` | MP3 audio |

## Examples

```bash
# Set LS_COLORS in Bourne shell
eval "$(dircolors)"

# Set LS_COLORS in C shell
eval "`dircolors -c`"

# Print the default color database
dircolors -p

# Print just the color codes
dircolors --print-ls-colors
```

## See Also

- `ls(1)` - List directory contents
