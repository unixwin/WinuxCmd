# WinuxCmd vs Microsoft Coreutils Compatibility Matrix

Generated from live command directories and \--help\ output.

Generated at: 2026-06-10T07:39:30.8355850+08:00
Winux directory: C:\Users\cmx\repo\WinuxCmd\build-vs-release-tests
Microsoft directory: C:\Users\cmx\repo\_winget_coreutils_install\bin

## Summary

| Metric | Value |
| --- | ---: |
| Winux commands | 153 |
| Microsoft commands | 77 |
| Shared commands | 77 |
| Winux-only commands | 76 |
| Microsoft-only commands | 0 |
| Shared commands with identical option tokens | 25 |
| Shared commands where Winux has extra option tokens only | 24 |
| Shared commands where Microsoft has extra option tokens only | 6 |
| Shared commands with mixed option-token differences | 22 |

## Winux-only Commands

[, cal, chcon, chgrp, chmod, chown, chroot, clear, cmp, col, column, cpio, cygpath, d2u, dd, diff, diff3, dir, dircolors, dos2unix, expand, file, free, getconf, groups, hexdump, hmac256, hostid, id, infocmp, install, jq, kill, less, locale, logname, lsof, man, mkfifo, mknod, more, mpicalc, nice, nohup, paste, patch, pinky, ps, reset, rev, runcon, sdiff, sed, shred, stdbuf, strings, stty, sync, tic, timeout, toe, top, tput, tree, tty, tzset, u2d, uname, unix2dos, users, vdir, watch, which, who, whoami, xxd

## Microsoft-only Commands

-

## Shared Command Summary

| Command | Class | Winux opts | Microsoft opts | Winux-only | Microsoft-only |
| --- | --- | ---: | ---: | ---: | ---: |
| arch | same | 4 | 4 | 0 | 0 |
| b2sum | different | 21 | 19 | 4 | 2 |
| base32 | same | 10 | 10 | 0 | 0 |
| base64 | same | 10 | 10 | 0 | 0 |
| basename | winux-superset | 13 | 10 | 3 | 0 |
| basenc | winux-superset | 20 | 19 | 1 | 0 |
| cat | same | 18 | 18 | 0 | 0 |
| cksum | microsoft-superset | 16 | 23 | 0 | 7 |
| comm | same | 13 | 13 | 0 | 0 |
| cp | different | 44 | 44 | 5 | 5 |
| csplit | winux-superset | 19 | 18 | 1 | 0 |
| cut | different | 21 | 20 | 2 | 1 |
| date | different | 17 | 20 | 2 | 5 |
| df | winux-superset | 26 | 25 | 1 | 0 |
| dirname | same | 6 | 6 | 0 | 0 |
| du | different | 39 | 39 | 1 | 1 |
| echo | winux-superset | 10 | 6 | 4 | 0 |
| env | different | 21 | 19 | 4 | 2 |
| expr | winux-superset | 4 | 2 | 2 | 0 |
| factor | same | 5 | 5 | 0 | 0 |
| false | winux-superset | 4 | 2 | 2 | 0 |
| find | winux-superset | 8 | 1 | 7 | 0 |
| fmt | microsoft-superset | 18 | 27 | 0 | 9 |
| fold | same | 12 | 12 | 0 | 0 |
| grep | winux-superset | 71 | 68 | 3 | 0 |
| head | winux-superset | 14 | 13 | 1 | 0 |
| hostname | winux-superset | 21 | 12 | 9 | 0 |
| join | winux-superset | 20 | 18 | 2 | 0 |
| link | same | 4 | 4 | 0 | 0 |
| ln | winux-superset | 28 | 25 | 3 | 0 |
| ls | different | 67 | 65 | 6 | 4 |
| md5sum | different | 19 | 17 | 4 | 2 |
| mkdir | same | 11 | 11 | 0 | 0 |
| mktemp | microsoft-superset | 13 | 14 | 0 | 1 |
| mv | different | 23 | 25 | 1 | 3 |
| nl | same | 24 | 24 | 0 | 0 |
| nproc | same | 6 | 6 | 0 | 0 |
| numfmt | different | 15 | 23 | 1 | 9 |
| od | microsoft-superset | 11 | 28 | 0 | 17 |
| pathchk | winux-superset | 7 | 6 | 1 | 0 |
| pr | different | 42 | 38 | 8 | 4 |
| printenv | same | 6 | 6 | 0 | 0 |
| printf | winux-superset | 4 | 2 | 2 | 0 |
| ptx | different | 34 | 30 | 5 | 1 |
| pwd | same | 8 | 8 | 0 | 0 |
| readlink | same | 19 | 19 | 0 | 0 |
| realpath | winux-superset | 22 | 21 | 1 | 0 |
| rm | different | 16 | 17 | 1 | 2 |
| rmdir | same | 8 | 8 | 0 | 0 |
| seq | different | 24 | 12 | 14 | 2 |
| sha1sum | different | 19 | 17 | 4 | 2 |
| sha224sum | different | 19 | 17 | 4 | 2 |
| sha256sum | different | 19 | 17 | 4 | 2 |
| sha384sum | different | 19 | 17 | 4 | 2 |
| sha512sum | different | 19 | 17 | 4 | 2 |
| shuf | microsoft-superset | 17 | 18 | 0 | 1 |
| sleep | same | 4 | 4 | 0 | 0 |
| sort | different | 43 | 47 | 1 | 5 |
| split | different | 26 | 23 | 4 | 1 |
| stat | winux-superset | 14 | 13 | 1 | 0 |
| sum | winux-superset | 8 | 7 | 1 | 0 |
| tac | same | 10 | 10 | 0 | 0 |
| tail | different | 22 | 22 | 1 | 1 |
| tee | winux-superset | 11 | 9 | 2 | 0 |
| test | winux-superset | 23 | 0 | 23 | 0 |
| touch | same | 16 | 16 | 0 | 0 |
| tr | same | 12 | 12 | 0 | 0 |
| true | winux-superset | 4 | 2 | 2 | 0 |
| truncate | same | 12 | 12 | 0 | 0 |
| tsort | same | 4 | 4 | 0 | 0 |
| unexpand | microsoft-superset | 9 | 12 | 0 | 3 |
| uniq | winux-superset | 22 | 20 | 2 | 0 |
| unlink | same | 4 | 4 | 0 | 0 |
| uptime | same | 8 | 8 | 0 | 0 |
| wc | winux-superset | 16 | 15 | 1 | 0 |
| xargs | winux-superset | 33 | 27 | 6 | 0 |
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
Microsoft-only tokens: --ignore-missing, -z

### base32

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### base64

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### basename

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --mul, --suf, --ze
Microsoft-only tokens: -

### basenc

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -b
Microsoft-only tokens: -

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
Microsoft-only tokens: --ignore-missing, --quiet, --status, --strict, --warn, -l, -w

### comm

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### cp

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --context, --no-preserve, --preserve, --reflink, -c
Microsoft-only tokens: --debug, --preserve-default-attributes, --progress, --strip-trailing-slashes, -g

### csplit

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --silent
Microsoft-only tokens: -

### cut

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --no-partial, -O
Microsoft-only tokens: -w

### date

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --rfc-2822, --utc
Microsoft-only tokens: --debug, --reference, --resolution, --set, -s

### df

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --output
Microsoft-only tokens: -

### dirname

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### du

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --time
Microsoft-only tokens: --verbose

### echo

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --repeat, --upper, -r, -u
Microsoft-only tokens: -

### env

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --block-signal, --debug, --default-signal, --ignore-signal
Microsoft-only tokens: --file, -f

### expr

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -h, -V
Microsoft-only tokens: -

### factor

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### false

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 1
Winux-only tokens: -h, -V
Microsoft-only tokens: -

### find

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --help, --version, -H, -L, -o, -P, -V
Microsoft-only tokens: -

### fmt

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --exact-prefix, --exact-skip-prefix, --preserve-headers, --quick, --skip-prefix, --tab-width, -m, -q, -X

### fold

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --alias, --all-fqdns, --all-ip-addresses, --file, --node, --yp, -a, -n, -y
Microsoft-only tokens: -

### join

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --empty, --output
Microsoft-only tokens: -

### link

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### ln

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --dereference, --directory, -d
Microsoft-only tokens: -

### ls

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --block-size, --dereference-command-line-symlinks-to-dir, --format, --hyperlink, --long-list, --time
Microsoft-only tokens: --author, --dereference-command-line-symlink-to-dir, --full-time, --long

### md5sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, -z

### mkdir

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### mktemp

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --suffix

### mv

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --context
Microsoft-only tokens: --debug, --progress, -g

### nl

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### nproc

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### numfmt

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -f
Microsoft-only tokens: --debug, --field, --from-unit, --suffix, --to-unit, --unit-separator, --zero-terminated, -M, -z

### od

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --address-radix, --endian, --format, --output-duplicates, --read-bytes, --skip-bytes, --strings, --width, -b, -d, -e, -F, -i, -l, -o, -S, -w

### pathchk

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --posix
Microsoft-only tokens: -

### pr

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --balance-columns, --expand, --join-lines, --output-tabs, --show-control-chars, --show-nonprinting, -b, -c
Microsoft-only tokens: --across, --double-space, --expand-tabs, --pages

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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --no-symlinks
Microsoft-only tokens: -

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

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -a, -e, -i, -n
Microsoft-only tokens: --terminator, -t

### sha1sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, -z

### sha224sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, -z

### sha256sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, -z

### sha384sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, -z

### sha512sum

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: --ignore-missing, -z

### shuf

Classification: microsoft-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: --random-seed

### sleep

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### sort

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --check
Microsoft-only tokens: --batch-size, --check-silent, --compress-program, --parallel, --temporary-directory

### split

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --hex-suffixes, --numeric-suffixes, --unbuffered, -u
Microsoft-only tokens: --verbose

### stat

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --cached
Microsoft-only tokens: -

### sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --bsd
Microsoft-only tokens: -

### tac

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### tail

Classification: different
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --silent
Microsoft-only tokens: --use-polling

### tee

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --diagnose, --output-error
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

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

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
Microsoft-only tokens: --no-utf8, -f, -U

### uniq

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --all-repeated, --group
Microsoft-only tokens: -

### unlink

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### uptime

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### wc

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --debug
Microsoft-only tokens: -

### xargs

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --interactive, --open-tty, --process-slot-var, --replace, --show-limits, -o
Microsoft-only tokens: -

### yes

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

