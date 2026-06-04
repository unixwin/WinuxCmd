# WinuxCmd Project TODO List

## Completed Items (Phase 1 - Done)

### Documentation ✅
- [x] Create project README.md (English)
- [x] Create Chinese documentation in DOCS/README_zh.md
- [x] Create detailed command compatibility matrix (English and Chinese)
- [x] Update Project_Rules.md according to new requirements

### Build System Setup ✅
- [x] Configure CMake build scripts for C++23 modules
- [x] Set up dual-mode compilation targets (individual commands + combined mode)
- [x] Configure size optimization flags
- [x] Add support for individual commands and combined executable
- [x] Implement build modes (DEV/RELEASE/DEBUG_RELEASE)
- [x] Add CMakePresets.json for easy configuration

### Core Module Foundation ✅
- [x] Create core command processing module
- [x] Implement path resolution (Linux to Windows path conversion)
- [x] Create command parameter parsing tool
- [x] Implement Linux-style formatted console output wrapper
- [x] Add color output support
- [x] Implement pipeline-based architecture

## Command Implementation Status (Phase 2)

### File System Commands ✅
- [x] ls (list directory contents)
  - [x] Basic functionality
  - [x] -l (long format)
  - [x] -a (show hidden files)
  - [x] -h (human-readable sizes)
  - [x] -r (reverse sort)
  - [x] -t (sort by time)
  - [x] --color (color output)
- [x] mkdir (create directory)
  - [x] -p (parents)
  - [x] -v (verbose)
  - [x] -m (mode)
- [x] rmdir (remove directory)
  - [x] --ignore-fail-on-non-empty
  - [x] -p (parents)
  - [x] -v (verbose)
- [x] rm (remove file)
  - [x] -f (force)
  - [x] -i (interactive)
  - [x] -r (recursive)
  - [x] -v (verbose)
- [x] cp (copy file/directory)
  - [x] -i (interactive)
  - [x] -r (recursive)
  - [x] -v (verbose)
  - [x] -f (force)
- [x] mv (move/rename file)
  - [x] -i (interactive)
  - [x] -v (verbose)
  - [x] -f (force)
  - [x] -n (no-clobber)
- [x] touch (create empty file)
  - [x] -a (access time)
  - [x] -m (modification time)
  - [x] -c (no-create)
  - [x] -d (date)
  - [x] -r (reference)

### Text Processing Commands ✅
- [x] cat (concatenate files)
  - [x] -n (number lines)
  - [x] -b (number non-blank)
  - [x] -E (show ends)
  - [x] -s (squeeze blank)
- [x] grep (search text)
  - [x] Basic pattern matching
  - [x] -E (extended regex)
  - [x] -F (fixed strings)
  - [x] -G (basic regex)
  - [x] -i (ignore case)
  - [x] -n (show line numbers)
  - [x] -v (invert match)
  - [x] -c (count)
  - [x] -l (files with matches)
  - [x] -L (files without match)
  - [x] --color (highlight matches)
- [x] head (show first few lines)
  - [x] -n (lines)
  - [x] -c (bytes)
  - [x] -q (quiet)
  - [x] -v (verbose)
- [x] tail (show last few lines)
  - [x] -n (lines)
  - [x] -c (bytes)
  - [x] -z (zero terminated)
  - [x] -f (follow)
- [x] sort (sort lines)
  - [x] -b (ignore blanks)
  - [x] -f (ignore case)
  - [x] -n (numeric)
  - [x] -r (reverse)
  - [x] -u (unique)
  - [x] -k (key)
  - [x] -t (field separator)
- [x] uniq (report or omit duplicate lines)
  - [x] -c (count)
  - [x] -d (duplicates)
  - [x] -f (skip fields)
  - [x] -i (ignore case)
  - [x] -s (skip chars)
  - [x] -u (unique)
  - [x] -w (check chars)
- [x] wc (word, line, character count)
  - [x] -c (bytes)
  - [x] -l (lines)
  - [x] -w (words)
  - [x] -m (chars)
  - [x] -L (max line length)
- [x] cut (cut fields from lines)
  - [x] -d (delimiter)
  - [x] -f (fields)
  - [x] -s (only delimited)
  - [x] -z (zero terminated)
- [x] sed (stream editor)
  - [x] -e (expression)
  - [x] -f (file)
  - [x] -n (quiet)
  - [x] -i (in-place)

### System Information Commands ✅
- [x] which (locate commands in PATH)
  - [x] -a (all matches)
  - [x] --skip-dot
  - [x] --skip-tilde
- [x] env (environment variables)
  - [x] -i (ignore environment)
  - [x] -u (unset)
  - [x] -0 (null separator)
  - [x] -C/--chdir (change working directory)

## Future Enhancements (Phase 3 - Planned)

### Advanced Commands
- [ ] awk (pattern scanning and processing language)
- [x] xargs (build and execute command lines from standard input)
- [x] tee (read from standard input and write to standard output and files)
- [x] find (basic search functionality)
- [ ] find full GNU expression grammar and remaining advanced features (`-H`,
      complete `-printf`, exact action ordering)
- [x] date (basic formatting)
- [ ] date advanced formatting
- [ ] time (measure command execution time)
- [x] grep (pattern matching)
- [x] sed (stream editor)
- [x] sort (text sorting)
- [x] uniq (duplicate detection)
- [x] cut (field extraction)
- [x] wc (word/line/byte count)
- [x] head (output first part)
- [x] tail (output last part)
- [x] cat (concatenate files)
- [x] echo (display text)
- [x] tr (translate characters)
- [x] fold (wrap lines)
- [x] fmt (format text)
- [x] nl (number lines)
- [x] paste (merge lines)
- [x] pr (format for printing)
- [x] rev (reverse lines)
- [x] tac (reverse concatenate)
- [x] shuf (shuffle lines)
- [x] split (split files)
- [x] csplit (split by context)
- [x] join (join lines)
- [x] comm (compare sorted files)
- [x] expand (tabs to spaces)
- [x] unexpand (spaces to tabs)
- [x] ptx (permuted index)
- [x] cmp (byte comparison)
- [x] diff3 (three-way comparison)
- [x] sdiff (side-by-side diff)
- [x] patch (apply patches)
- [x] cksum (checksum)
- [x] md5sum (MD5 checksum)
- [x] sha1sum (SHA1 checksum)
- [x] sha224sum (SHA224 checksum)
- [x] sha256sum (SHA256 checksum)
- [x] sha384sum (SHA384 checksum)
- [x] sha512sum (SHA512 checksum)
- [x] b2sum (BLAKE2 checksum)
- [x] base64 (Base64 encoding/decoding)
- [x] base32 (Base32 encoding/decoding)
- [x] basenc (various encodings)
- [x] xxd (hexdump)
- [x] od (octal dump)
- [x] cpio (archive copying)
- [x] dd (file conversion)
- [x] expr (expression evaluation)
- [x] factor (prime factorization)
- [x] seq (sequence generation)
- [x] yes (repeated output)
- [x] sleep (delay)
- [x] stdbuf (buffer modification)
- [x] shred (secure deletion)
- [x] pathchk (path validation)
- [x] tree (tree display)
- [x] less (file viewer)
- [x] printf (formatted output)
- [x] jq (JSON processor)
- [x] column (column formatting)

### Network Commands
- [ ] ping (send ICMP ECHO_REQUEST to network hosts)
- [ ] curl (transfer data from or to a server)
- [ ] wget (non-interactive network downloader)

### Archive Commands
- [ ] tar (tape archiver)
- [ ] gzip/gunzip (compression/decompression)
- [ ] zip/unzip (ZIP archive manipulation)

### System Commands
- [x] ps (report process status)
- [x] kill (send signal to processes)
- [x] df (report file system disk space usage)
- [x] du (estimate file space usage)
- [x] lsof (report open file)
- [x] free (memory usage)
- [x] uptime (system uptime)
- [x] nice (process priority)
- [x] nohup (immune to hangups)
- [x] renice (alter priority)
- [x] watch (periodic execution)
- [x] timeout (time-limited execution)
- [x] jobs (job status)
- [x] bg (background resume)
- [x] fg (foreground resume)
- [x] disown (remove jobs)
- [x] wait (wait for process)
- [x] env (environment)
- [x] printenv (print environment)
- [x] export (export variable)
- [x] unset (unset variable)
- [x] logname (login name)
- [x] whoami (user ID)
- [x] id (user/group info)
- [x] users (logged-in users)
- [x] who (user information)
- [x] groups (group information)
- [x] hostname (host name)
- [x] hostid (host ID)
- [x] uname (system info)
- [x] arch (architecture)
- [x] nproc (processor count)
- [x] numfmt (number formatting)
- [x] cal (calendar)
- [x] getconf (system config)
- [x] locale (locale info)
- [x] tzset (timezone)
- [x] cygpath (path conversion)
- [x] dos2unix (line ending conversion)
- [x] unix2dos (line ending conversion)
- [x] d2u (alias for dos2unix)
- [x] u2d (alias for unix2dos)

### Testing and Terminal Control
- [x] test (check file types and compare values)
- [x] [ (alias for test)
- [x] true (return success)
- [x] false (return failure)
- [x] clear (clear screen)
- [x] reset (reinitialize terminal)
- [x] tput (terminal capability query)
- [x] tic (terminfo compiler)
- [x] toe (terminfo table)
- [x] tty (terminal file name)
- [x] stty (terminal settings)
- [x] infocmp (terminfo comparison)
- [x] pinky (user information)
- [x] mktemp (temporary file)
- [x] mpicalc (arbitrary precision calculator)
- [x] hmac256 (HMAC computation)

### File Management
- [x] ls (list directory)
- [x] mkdir (create directory)
- [x] rmdir (remove directory)
- [x] rm (remove files)
- [x] cp (copy files)
- [x] mv (move/rename files)
- [x] touch (create empty file)
- [x] basename (strip directory)
- [x] dirname (strip last component)
- [x] readlink (resolve symlinks)
- [x] realpath (resolve absolute path)
- [x] stat (file status)
- [x] link (create links)
- [x] unlink (remove file)
- [x] truncate (file size)
- [x] sync (synchronize)
- [x] install (install files)
- [x] find (search files)
- [x] chmod (change mode)
- [x] chown (change owner)
- [x] chgrp (change group)
- [x] ln (make links)
- [x] diff (compare files)
- [x] file (determine file type)

## Testing and Optimization (Ongoing)

### Compatibility Testing
- [ ] Test each command against Linux native output
- [ ] Verify path conversion edge cases
- [ ] Test parameter combinations
- [ ] Validate error message formatting

### Performance Optimization
- [ ] Profile command execution for bottlenecks
- [ ] Optimize memory usage patterns
- [ ] Improve startup time
- [ ] Reduce I/O overhead

### Build System Improvements
- [ ] Add more build presets
- [ ] Implement cross-compilation support
- [ ] Add static analysis integration
- [ ] Improve CI/CD pipeline

## Documentation Updates
- [ ] Create comprehensive user guide
- [ ] Add advanced usage examples
- [ ] Document internal architecture
- [ ] Create contribution guidelines

## Tracking

- Total commands implemented: 20+
- Phase 1 completion date: 2026
- Phase 2 completion date: 2026
- Phase 3 target date: TBD

## Notes

- All implementations must follow Project_Rules.md
- Prioritize core parameters over rare parameters
- Focus on performance and compatibility throughout development
- Maintain clean, well-documented code
- Use C++23 modules for all new code
- Separate interface (.cppm) and implementation (.cpp) files

