# WinuxCmd vs Microsoft Coreutils Compatibility Matrix

Generated from live command directories and \--help\ output.

Generated at: 2026-06-10T13:42:20.1585983+08:00
Winux directory: C:\Users\cmx\repo\WinuxCmd\.winuxcmd\bin
Microsoft directory: C:\Users\cmx\repo\_winget_coreutils_install\bin

## Summary

| Metric | Value |
| --- | ---: |
| Winux commands | 153 |
| Microsoft commands | 77 |
| Shared commands | 77 |
| Winux-only commands | 76 |
| Microsoft-only commands | 0 |
| Shared commands with identical option tokens | 18 |
| Shared commands where Winux has extra option tokens only | 13 |
| Shared commands where Microsoft has extra option tokens only | 27 |
| Shared commands with mixed option-token differences | 19 |

## Winux-only Commands

cal, chcon, chgrp, chmod, chown, chroot, clear, cmp, col, column, cpio, cygpath, d2u, dd, diff, diff3, dir, dircolors, dos2unix, expand, file, free, getconf, groups, hexdump, hmac256, hostid, id, infocmp, install, jq, kill, less, locale, logname, lsof, man, mkfifo, mknod, more, mpicalc, nice, nohup, paste, patch, pinky, ps, reset, rev, runcon, sdiff, sed, shred, stdbuf, strings, stty, sync, test_bracket, tic, timeout, toe, top, tput, tree, tty, tzset, u2d, uname, unix2dos, users, vdir, watch, which, who, whoami, xxd

## Microsoft-only Commands

-

## Shared Command Summary

| Command | Class | Winux opts | Microsoft opts | Winux-only | Microsoft-only |
| --- | --- | ---: | ---: | ---: | ---: |
| arch | same | 4 | 4 | 0 | 0 |
| b2sum | different | 18 | 19 | 4 | 5 |
| base32 | microsoft-superset | 7 | 10 | 0 | 3 |
| base64 | same | 10 | 10 | 0 | 0 |
| basename | same | 10 | 10 | 0 | 0 |
| basenc | different | 8 | 19 | 1 | 12 |
| cat | same | 18 | 18 | 0 | 0 |
| cksum | microsoft-superset | 4 | 23 | 0 | 19 |
| comm | microsoft-superset | 10 | 13 | 0 | 3 |
| cp | microsoft-superset | 33 | 44 | 0 | 11 |
| csplit | microsoft-superset | 16 | 18 | 0 | 2 |
| cut | microsoft-superset | 15 | 20 | 0 | 5 |
| date | different | 10 | 20 | 2 | 12 |
| df | microsoft-superset | 11 | 25 | 0 | 14 |
| dirname | same | 6 | 6 | 0 | 0 |
| du | microsoft-superset | 19 | 39 | 0 | 20 |
| echo | winux-superset | 10 | 6 | 4 | 0 |
| env | microsoft-superset | 14 | 19 | 0 | 5 |
| expr | winux-superset | 4 | 2 | 2 | 0 |
| factor | microsoft-superset | 4 | 5 | 0 | 1 |
| false | winux-superset | 4 | 2 | 2 | 0 |
| find | different | 6 | 1 | 6 | 1 |
| fmt | microsoft-superset | 18 | 27 | 0 | 9 |
| fold | microsoft-superset | 10 | 12 | 0 | 2 |
| grep | winux-superset | 71 | 68 | 3 | 0 |
| head | winux-superset | 14 | 13 | 1 | 0 |
| hostname | different | 11 | 12 | 1 | 2 |
| join | different | 12 | 18 | 2 | 8 |
| link | winux-superset | 5 | 4 | 1 | 0 |
| ln | microsoft-superset | 11 | 25 | 0 | 14 |
| ls | different | 49 | 65 | 1 | 17 |
| md5sum | different | 16 | 17 | 4 | 5 |
| mkdir | microsoft-superset | 10 | 11 | 0 | 1 |
| mktemp | microsoft-superset | 12 | 14 | 0 | 2 |
| mv | different | 23 | 25 | 1 | 3 |
| nl | microsoft-superset | 9 | 24 | 0 | 15 |
| nproc | same | 6 | 6 | 0 | 0 |
| numfmt | microsoft-superset | 10 | 23 | 0 | 13 |
| od | microsoft-superset | 10 | 28 | 0 | 18 |
| pathchk | same | 6 | 6 | 0 | 0 |
| pr | different | 27 | 38 | 1 | 12 |
| printenv | same | 6 | 6 | 0 | 0 |
| printf | winux-superset | 4 | 2 | 2 | 0 |
| ptx | different | 34 | 30 | 5 | 1 |
| pwd | same | 8 | 8 | 0 | 0 |
| readlink | same | 19 | 19 | 0 | 0 |
| realpath | microsoft-superset | 18 | 21 | 0 | 3 |
| rm | different | 16 | 17 | 1 | 2 |
| rmdir | same | 8 | 8 | 0 | 0 |
| seq | microsoft-superset | 10 | 12 | 0 | 2 |
| sha1sum | different | 16 | 17 | 4 | 5 |
| sha224sum | different | 16 | 17 | 4 | 5 |
| sha256sum | different | 16 | 17 | 4 | 5 |
| sha384sum | different | 16 | 17 | 4 | 5 |
| sha512sum | different | 16 | 17 | 4 | 5 |
| shuf | microsoft-superset | 14 | 18 | 0 | 4 |
| sleep | same | 4 | 4 | 0 | 0 |
| sort | microsoft-superset | 35 | 47 | 0 | 12 |
| split | different | 13 | 23 | 1 | 11 |
| stat | microsoft-superset | 9 | 13 | 0 | 4 |
| sum | winux-superset | 8 | 7 | 1 | 0 |
| tac | microsoft-superset | 4 | 10 | 0 | 6 |
| tail | different | 21 | 22 | 1 | 2 |
| tee | winux-superset | 10 | 9 | 1 | 0 |
| test | winux-superset | 23 | 0 | 23 | 0 |
| touch | same | 16 | 16 | 0 | 0 |
| tr | same | 12 | 12 | 0 | 0 |
| true | winux-superset | 4 | 2 | 2 | 0 |
| truncate | microsoft-superset | 10 | 12 | 0 | 2 |
| tsort | same | 4 | 4 | 0 | 0 |
| unexpand | microsoft-superset | 8 | 12 | 0 | 4 |
| uniq | winux-superset | 22 | 20 | 2 | 0 |
| unlink | winux-superset | 5 | 4 | 1 | 0 |
| uptime | same | 8 | 8 | 0 | 0 |
| wc | same | 15 | 15 | 0 | 0 |
| xargs | microsoft-superset | 15 | 27 | 0 | 12 |
| yes | same | 4 | 4 | 0 | 0 |

## Shared Command Details

### arch

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### b2sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### base32

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --ignore-garbage, --wrap, -i

### base64

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### basename

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### basenc

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -b
Microsoft-only tokens: --base16, --base2lsbf, --base2msbf, --base32, --base32hex, --base58, --base64, --base64url, --ignore-garbage, --wrap, --z85, -i

### cat

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### cksum

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --algorithm, --base64, --check, --debug, --ignore-missing, --length, --quiet, --raw, --status, --strict, --tag, --untagged, --warn, --zero, -a, -c, -l, -w, -z

### comm

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --total, --zero-terminated, -z

### cp

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --attributes-only, --copy-contents, --debug, --parents, --preserve-default-attributes, --progress, --remove-destination, --sparse, --strip-trailing-slashes, --update, -g

### csplit

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --suppress-matched, -q

### cut

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --bytes, --characters, --complement, -n, -w

### date

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --rfc-2822, --utc
Microsoft-only tokens: --debug, --file, --iso-8601, --reference, --resolution, --rfc-3339, --rfc-email, --set, --universal, -f, -I, -s

### df

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --all, --block-size, --exclude-type, --local, --no-sync, --portability, --sync, --total, --type, -a, -B, -l, -P, -x

### dirname

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### du

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --count-links, --dereference, --dereference-args, --exclude, --exclude-from, --files0-from, --inodes, --no-dereference, --null, --one-file-system, --separate-dirs, --threshold, --time-style, --verbose, -0, -l, -m, -P, -t, -x

### echo

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --repeat, --upper, -r, -u
Microsoft-only tokens: -

### env

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --argv0, --file, --list-signal-handling, -a, -f

### expr

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -h, -V
Microsoft-only tokens: -

### factor

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --exponents

### false

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 1
Winux-only tokens: -h, -V
Microsoft-only tokens: -

### find

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --help, --version, -H, -L, -P, -V
Microsoft-only tokens: -a

### fmt

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --exact-prefix, --exact-skip-prefix, --preserve-headers, --quick, --skip-prefix, --tab-width, -m, -q, -X

### fold

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --characters, -c

### grep

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --color, --colour, --silent
Microsoft-only tokens: -

### head

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --silent
Microsoft-only tokens: -

### hostname

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --all-ip-addresses
Microsoft-only tokens: --domain, -d

### join

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --empty, --output
Microsoft-only tokens: --check-order, --header, --ignore-case, --nocheck-order, --zero-terminated, -a, -i, -z

### link

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --verbose
Microsoft-only tokens: -

### ln

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --backup, --interactive, --logical, --no-target-directory, --physical, --relative, --suffix, --target-directory, -b, -i, -L, -P, -r, -T

### ls

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --long-list
Microsoft-only tokens: --author, --dereference-command-line, --dereference-command-line-symlink-to-dir, --dired, --file-type, --full-time, --group-directories-first, --hide, --ignore, --indicator-style, --long, --no-group, --quoting-style, --show-control-chars, --si, --time-style, --zero

### md5sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### mkdir

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --context

### mktemp

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --suffix, -p

### mv

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --context
Microsoft-only tokens: --debug, --progress, -g

### nl

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --footer-numbering, --header-numbering, --join-blank-lines, --line-increment, --no-renumber, --number-format, --number-separator, --number-width, --section-delimiter, --starting-line-number, -d, -f, -l, -n, -p

### nproc

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### numfmt

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --debug, --field, --format, --from-unit, --grouping, --header, --invalid, --suffix, --to-unit, --unit-separator, --zero-terminated, -M, -z

### od

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --address-radix, --endian, --format, --output-duplicates, --read-bytes, --skip-bytes, --strings, --traditional, --width, -b, -d, -e, -F, -i, -l, -o, -S, -w

### pathchk

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### pr

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --expand
Microsoft-only tokens: --across, --column, --date-format, --double-space, --expand-tabs, --first-line-number, --merge, --pages, --sep-string, -i, -J, -m

### printenv

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### printf

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -h, -V
Microsoft-only tokens: -

### ptx

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --break, --copyright, --format, --tabs, -C
Microsoft-only tokens: --break-file

### pwd

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### readlink

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### realpath

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --canonicalize, --relative-base, --relative-to

### rm

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --interactive
Microsoft-only tokens: --progress, -g

### rmdir

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### seq

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --terminator, -t

### sha1sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### sha224sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### sha256sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### sha384sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### sha512sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, --strict, --tag, --zero, -z

### shuf

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --output, --random-seed, --random-source, -o

### sleep

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### sort

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --batch-size, --buffer-size, --check-silent, --compress-program, --debug, --files0-from, --parallel, --random-source, --sort, --temporary-directory, --version-sort, -C

### split

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --numeric-suffixes
Microsoft-only tokens: --additional-suffix, --elide-empty-files, --filter, --line-bytes, --number, --separator, --verbose, -e, -n, -t, -x

### stat

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --dereference, --file-system, --printf, --terse

### sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --bsd
Microsoft-only tokens: -

### tac

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --before, --regex, --separator, -b, -r, -s

### tail

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --silent
Microsoft-only tokens: --debug, --use-polling

### tee

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --diagnose
Microsoft-only tokens: -

### test

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --help, --version, -a, -b, -c, -d, -e, -f, -g, -h, -k, -L, -n, -O, -p, -r, -S, -t, -u, -V, -w, -x, -z
Microsoft-only tokens: -

### touch

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### tr

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### true

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -h, -V
Microsoft-only tokens: -

### truncate

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --io-blocks, -o

### tsort

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### unexpand

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --first-only, --no-utf8, -f, -U

### uniq

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --all-repeated, --group
Microsoft-only tokens: -

### unlink

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --verbose
Microsoft-only tokens: -

### uptime

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### wc

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### xargs

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --arg-file, --delimiter, --eof, --exit, --max-chars, --max-lines, -a, -d, -E, -l, -s, -x

### yes

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

