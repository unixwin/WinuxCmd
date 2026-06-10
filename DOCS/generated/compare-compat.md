# WinuxCmd vs Microsoft Coreutils Compatibility Matrix

Generated from live command directories and \--help\ output.

Generated at: 2026-06-10T23:25:38.3375658+08:00
Winux directory: C:\Users\cmx\repo\WinuxCmd\build-gnu-maint-vs-bigobj
Microsoft directory: C:\Users\cmx\repo\_winget_coreutils_install\bin

## Summary

| Metric | Value |
| --- | ---: |
| Winux commands | 154 |
| Microsoft commands | 77 |
| Shared commands | 77 |
| Winux-only commands | 77 |
| Microsoft-only commands | 0 |
| Shared commands with identical option tokens | 28 |
| Shared commands where Winux has extra option tokens only | 37 |
| Shared commands where Microsoft has extra option tokens only | 2 |
| Shared commands with mixed option-token differences | 10 |

## Winux-only Commands

[, argv0check, cal, chcon, chgrp, chmod, chown, chroot, clear, cmp, col, column, cpio, cygpath, d2u, dd, diff, diff3, dir, dircolors, dos2unix, expand, file, free, getconf, groups, hexdump, hmac256, hostid, id, infocmp, install, jq, kill, less, locale, logname, lsof, man, mkfifo, mknod, more, mpicalc, nice, nohup, paste, patch, pinky, ps, reset, rev, runcon, sdiff, sed, shred, stdbuf, strings, stty, sync, tic, timeout, toe, top, tput, tree, tty, tzset, u2d, uname, unix2dos, users, vdir, watch, which, who, whoami, xxd

## Microsoft-only Commands

-

## Shared Command Summary

| Command | Class | Winux opts | Microsoft opts | Winux-only | Microsoft-only |
| --- | --- | ---: | ---: | ---: | ---: |
| arch | same | 4 | 4 | 0 | 0 |
| b2sum | winux-superset | 23 | 19 | 4 | 0 |
| base32 | same | 10 | 10 | 0 | 0 |
| base64 | same | 10 | 10 | 0 | 0 |
| basename | winux-superset | 13 | 10 | 3 | 0 |
| basenc | winux-superset | 20 | 19 | 1 | 0 |
| cat | same | 18 | 18 | 0 | 0 |
| cksum | winux-superset | 25 | 23 | 2 | 0 |
| comm | same | 13 | 13 | 0 | 0 |
| cp | different | 45 | 44 | 5 | 4 |
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
| ls | winux-superset | 71 | 65 | 6 | 0 |
| md5sum | winux-superset | 21 | 17 | 4 | 0 |
| mkdir | same | 11 | 11 | 0 | 0 |
| mktemp | same | 14 | 14 | 0 | 0 |
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
| seq | winux-superset | 26 | 12 | 14 | 0 |
| sha1sum | winux-superset | 21 | 17 | 4 | 0 |
| sha224sum | winux-superset | 21 | 17 | 4 | 0 |
| sha256sum | winux-superset | 21 | 17 | 4 | 0 |
| sha384sum | winux-superset | 21 | 17 | 4 | 0 |
| sha512sum | winux-superset | 21 | 17 | 4 | 0 |
| shuf | same | 18 | 18 | 0 | 0 |
| sleep | same | 4 | 4 | 0 | 0 |
| sort | winux-superset | 48 | 47 | 1 | 0 |
| split | winux-superset | 27 | 23 | 4 | 0 |
| stat | winux-superset | 14 | 13 | 1 | 0 |
| sum | winux-superset | 8 | 7 | 1 | 0 |
| tac | same | 10 | 10 | 0 | 0 |
| tail | winux-superset | 23 | 22 | 1 | 0 |
| tee | winux-superset | 11 | 9 | 2 | 0 |
| test | winux-superset | 23 | 0 | 23 | 0 |
| touch | same | 16 | 16 | 0 | 0 |
| tr | same | 12 | 12 | 0 | 0 |
| true | winux-superset | 4 | 2 | 2 | 0 |
| truncate | same | 12 | 12 | 0 | 0 |
| tsort | same | 4 | 4 | 0 | 0 |
| unexpand | same | 12 | 12 | 0 | 0 |
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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -q, -s
Microsoft-only tokens: -

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
Microsoft-only tokens: --debug, --preserve-default-attributes, --progress, -g

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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --block-size, --dereference-command-line-symlinks-to-dir, --format, --hyperlink, --long-list, --time
Microsoft-only tokens: -

### md5sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

### mkdir

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### mktemp

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -a, -E, -i, -n
Microsoft-only tokens: -

### sha1sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

### sha224sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

### sha256sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

### sha384sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

### sha512sum

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --binary, -b, -q, -s
Microsoft-only tokens: -

### shuf

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### sleep

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

### sort

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --check
Microsoft-only tokens: -

### split

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --hex-suffixes, --numeric-suffixes, --unbuffered, -u
Microsoft-only tokens: -

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

Classification: winux-superset
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: --silent
Microsoft-only tokens: -

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

Classification: same
Winux help exit code: 0
Microsoft help exit code: 0
Winux-only tokens: -
Microsoft-only tokens: -

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

