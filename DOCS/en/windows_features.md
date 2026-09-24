# WinuxCmd Windows Features & WPM Guide

## Overview

WinuxCmd provides Windows-native implementations of Unix commands, with
additional Windows-specific features and a package manager (WPM).

## Windows-Specific Commands

### winuxcmd (main executable)
The main entry point that dispatches to individual commands.
Supports 169 commands across GNU Coreutils, BSD, Cygwin, and custom extensions.

### wpm (WinuxCmd Package Manager)
Package manager for installing and managing external tools.

#### Commands
- `wpm install <pkg>` - Install a package
- `wpm uninstall <pkg>` - Uninstall a package
- `wpm list` - List available packages
- `wpm installed` - List installed packages
- `wpm search <query>` - Search packages
- `wpm info <pkg>` - Show package info
- `wpm update` - Update package index
- `wpm outdated` - Show outdated packages
- `wpm index` - Show/update package index

#### Package Sources
WPM uses the wpm-source repository as its package index:
- Official: https://github.com/unixwin/wpm-source
- CDN: https://cdn.jsdelivr.net/gh/unixwin/wpm-source@main/index.json

#### Available Packages (via wpm-source)
- goawk (awk implementation)
- bsdtar (BSD tar)
- gzip (GNU compression)
- openssh (SSH client)
- make (GNU make)
- neovim (text editor)
- curl (URL transfer)
- wget (network downloader)

## Windows-Specific Behavior

### Path Handling
- WinuxCmd accepts both / and \ as path separators
- Paths are normalized to Windows format internally
- UNC paths (\\server\share) are supported

### Line Endings
- Input: Auto-detects CRLF/LF/CR
- Output: Uses system default (CRLF on Windows)
- Use -z/--zero for NUL-separated output

### Permissions
- Windows ACLs mapped to Unix permission model
- Read-only files mapped to -w (not writable)
- Directories always appear executable

### Signals
- Windows does not have Unix signals
- kill/killall use TerminateProcess API
- Signal names are mapped to Windows equivalents

### File Types
- .exe, .bat, .cmd, .ps1 are recognized as executable
- -x/--executable checks file extension
- Symlinks use Windows reparse points

### Environment Variables
- Windows env vars are case-insensitive
- PATH uses ; separator (not :)
- HOME mapped to USERPROFILE

## Build Information

### Compiler
- MSVC (Visual Studio)
- C++23 with modules
- Ninja build system

### Build Command
```powershell
./scripts/build-with-vs.ps1
```

### Test Command
```powershell
./scripts/build-with-vs.ps1 -Target winuxcmd-tests
build-vs/tests/winuxcmd-tests.exe
```

## Known Limitations

1. **SELinux**: Not available on Windows (chcon, runcon are stubs)
2. **Process groups**: killall/pkill use substring match by default
3. **Terminal features**: Some stty/tput options not supported
4. **File systems**: FAT32/exFAT lack Unix permissions
5. **Hard links**: Limited support on some Windows versions
6. **FIFOs**: Named pipes use different Windows API
