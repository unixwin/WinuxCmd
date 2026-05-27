# vdir

List information about files (the current directory by default).

## Synopsis

```
vdir [OPTION]... [FILE]...
```

## Description

`vdir` is equivalent to `ls -l`; it uses long listing format by default.

`vdir` is a thin wrapper around `ls` that passes `-l` (long format) as the default. All `ls` options are supported.

## Options

All options from `ls` are supported. See [ls documentation](ls.md) for the complete list.

Key differences from `ls`:
- Default format is `-l` (long listing) regardless of output destination
- All other behavior identical to `ls`

## Examples

```bash
vdir              # List files in long format (default)
vdir -a           # Show hidden files
vdir -h           # Human-readable sizes
vdir -R           # Recursive listing
vdir --color      # Colorized output
```

## See Also

- `ls(1)` - List directory contents
- `dir(1)` - List in columns
