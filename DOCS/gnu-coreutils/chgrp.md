# chgrp

Change the group of each FILE to GROUP.

## Synopsis

```
chgrp [OPTION]... GROUP FILE...
chgrp [OPTION]... --reference=RFILE FILE...
```

## Description

`chgrp` changes the group ownership of each given file. On Windows, this maps to NTFS group ownership via the Windows security API.

## Options

| Option | Long | Description | Status |
|--------|------|-------------|--------|
| `-c` | `--changes` | Like verbose but report only when a change is made | Done |
| `-f` | `--silent` | Suppress most error messages | Done |
| `-f` | `--quiet` | Suppress most error messages | Done |
| `-v` | `--verbose` | Output a diagnostic for every file processed | Done |
| `-R` | `--recursive` | Operate on files and directories recursively | Done |
| `-h` | `--no-dereference` | Affect symbolic links instead of any referenced file | Done |
| | `--dereference` | Affect the referent of each symbolic link (default) | Done |
| `-H` | | Command line symbolic links are followed | Done |
| `-L` | | Indirect symbolic links are followed | Done |
| `-P` | | No symbolic links are followed (default) | Done |
| | `--reference=RFILE` | Use RFILE's group instead of specifying GROUP | Done |
| | `--preserve-root` | Fail to operate recursively on '/' | Done |
| | `--no-preserve-root` | Do not treat '/' specially (default) | Done |

## Windows Limitations

On Windows, changing group ownership requires:
1. Administrator privileges
2. The GROUP must be a valid Windows group name
3. Uses `LookupAccountName` and `SetNamedSecurityInfo` APIs

## Examples

```bash
chgrp staff file.txt          # Change group to 'staff'
chgrp -R staff /tmp/dir       # Change group recursively
chgrp --reference=file f2     # Use file's group for f2
chgrp -v Users *.txt          # Verbose mode
```

## Exit Status

- 0: Success
- 1: Error (permission denied, invalid group, file not found)

## See Also

- `chown(1)` - Change file owner
- `chmod(1)` - Change file permissions
