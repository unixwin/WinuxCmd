# dir

List information about files (the current directory by default).

## Synopsis

```
dir [OPTION]... [FILE]...
```

## Description

`dir` is equivalent to `ls -C`; it lists files in columns. This is the default behavior regardless of whether output is to a terminal or a pipe.

`dir` is a thin wrapper around `ls` that passes `-C` (columns) as the default format. All `ls` options are supported.

## Options

All options from `ls` are supported. See [ls documentation](ls.md) for the complete list.

Key differences from `ls`:
- Default format is `-C` (columns) regardless of output destination
- All other behavior identical to `ls`

## Examples

```bash
dir              # List files in columns (default)
dir -l           # List in long format
dir -a           # Show hidden files
dir -R           # Recursive listing
dir -lh          # Long format with human-readable sizes
```

## See Also

- `ls(1)` - List directory contents
- `vdir(1)` - List in long format
