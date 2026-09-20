# Differential Exemption Manifest

Commands intentionally excluded from the GNU differential corpus. Reason per entry.
Total differentially tested: 71 / 169. These 98 are exempt for the reasons below.

## Windows-specific / no GNU counterpart (test here, not vs GNU)
cygpath, regtool, wpm, chcon, chattr, lsattr, getfacl, setfacl, chgrp, chown, runcon, chroot, mkgroup, mkpasswd, man.

## Process / terminal output — platform-nondeterministic
ps, top, watch, uptime, free, lsof, ldd, pldd, pgrep, pidof, pinky, pkill, kill, killall, nice, renice, kill, stty, tty, whoami, groups, logname, id (except exit-code checks), who, users, login, pldd, hostid, hostname (env-dependent).

## Terminfo / ncurses utilities
infocmp, tic, toe, tput, reset, clear.

## Locale / time zone
tzset, locale (partially tested), date (time-dependent — covered for relative offsets only).

## Not yet covered (has GNU counterpart, slated for future corpus batch)
arch, basenc, cal, column, cpio, csplit, d2u, df, diff3, dir, dircolors, dos2unix, envsubst, file, free, getconf, getopt, hexdump, hmac256, install, less, logger, lsattr, mkfifo, mknod, mpicalc, more, nproc, patch, pathchk, readlink, rev, runcon, sha224sum, sha384sum, shuf, sleep, strings, sync, test_bracket, time, timeout, tree, truncate, u2d, uname, unlink, vdir, which, yes, d2u, col, join(covered), paste(covered).

Notes:
- Commands whose output is genuinely environment-dependent (processes, terminal, locale, time) cannot be byte-compared across Windows/WSL in a stable way; they belong in functional/unit tests, not this differential.
- "Not yet covered" is the honest backlog for future corpus expansion — most have clear GNU semantics and are good candidates for round 5.
