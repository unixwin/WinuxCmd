#!/usr/bin/env python3
"""Run black-box command parity and performance probes.

The harness intentionally compares the built WinuxCmd dispatcher against a
locally installed GNU/MSYS toolchain.  It is not a microbenchmark suite; it is a
release-readiness smoke bench that answers three concrete questions:

1. Can we run the same command shape as upstream?
2. Does stdout match for deterministic non-platform-specific cases?
3. How far is the current implementation from the reference tool's wall time?

The generated JSON/Markdown reports are consumed by the compatibility matrix
and should be refreshed after substantial command changes.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import stat
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable


GNU_SEARCH_ROOTS = [
    Path(r"C:\Program Files\JetBrains\CLion 2025.3.3\bin\mingw\bin"),
    Path(r"C:\Program Files\Git\usr\bin"),
    Path(r"C:\msys64\usr\bin"),
    Path(r"C:\cygwin64\bin"),
]


def clear_windows_readonly(path: Path) -> None:
    if os.name == "nt":
        attrs = ctypes.windll.kernel32.GetFileAttributesW(str(path))
        if attrs != 0xFFFFFFFF and attrs & 0x1:
            ctypes.windll.kernel32.SetFileAttributesW(str(path), attrs & ~0x1)
    try:
        os.chmod(path, stat.S_IREAD | stat.S_IWRITE)
    except OSError:
        pass


def remove_tree(path: Path) -> None:
    def onexc(func: Callable[[str], object], failing_path: str, _exc: BaseException) -> None:
        clear_windows_readonly(Path(failing_path))
        func(failing_path)

    try:
        shutil.rmtree(path, onexc=onexc)
    except TypeError:
        def onerror(func: Callable[[str], object], failing_path: str, _exc_info: object) -> None:
            clear_windows_readonly(Path(failing_path))
            func(failing_path)

        shutil.rmtree(path, onerror=onerror)


@dataclass(frozen=True)
class Probe:
    command: str
    name: str
    argv: list[str]
    fixture: str
    stdin: str | None = None
    stdin_file: str | None = None
    compare_stdout: bool = True
    expected_exit: int | None = 0
    isolated: bool = False
    reference_command: str | None = None
    reference_argv: list[str] | None = None
    reference_required: bool = True
    mode_paths: list[str] | None = None
    time_paths: list[str] | None = None
    stdout_regex: str | None = None
    stderr_regex: str | None = None
    compare_exit: bool = True
    stdout_line_limit: int | None = None


@dataclass
class ProbeResult:
    command: str
    name: str
    argv: list[str]
    fixture: str
    status: str
    winux_ms: float | None
    reference_ms: float | None
    ratio: float | None
    stdout_match: bool | None
    raw_stdout_match: bool | None
    normalized_stdout_match: bool | None
    state_match: bool | None
    winux_exit: int | None
    reference_exit: int | None
    winux_stdout_sha256: str | None
    reference_stdout_sha256: str | None
    note: str


PROBES = [
    Probe("cat", "large file passthrough", ["big.txt"], "text"),
    Probe("cat", "number final unterminated line", ["-n", "cat-no-newline.txt"], "text"),
    Probe("cat", "show all visible transform", ["-A", "cat-visible.bin"], "text"),
    Probe("grep", "fixed literal search", ["-F", "needle_999", "big.txt"], "text"),
    Probe("grep", "fixed pattern file many literals", ["-F", "-f", "grep-many-patterns.txt", "big.txt"], "text"),
    Probe("grep", "extended regex alternation", ["-E", "needle_(123|999)", "big.txt"], "text"),
    Probe("grep", "only matching extended regex", ["-E", "-o", "needle_[0-9]+", "big.txt"], "text"),
    Probe("grep", "context custom group separator", ["-C", "1", "--group-separator=@@", "needle", "grep-context.txt"], "text"),
    Probe("grep", "recursive extended size alternation", ["-REIn", "28x12|12x10|16x12", "grep-recursive"], "text"),
    Probe("sed", "global literal substitution", ["s/needle/NEEDLE/g", "big.txt"], "text"),
    Probe("sed", "zero address regex range", ["0,/foo/s/foo/XX/", "sed-zero-range.txt"], "text"),
    Probe("sed", "filename command", ["-n", "F", "sed-file-a.txt", "sed-file-b.txt"], "text"),
    Probe("sed", "substitution write flag", ["s/needle/NEEDLE/w sed-subst-write.out", "sed-subst-write.txt"], "textops", isolated=True),
    Probe("wc", "line byte word counts", ["-lwm", "big.txt"], "text"),
    Probe("wc", "byte count fast path", ["-c", "big.txt"], "text"),
    Probe("wc", "line and byte fast path", ["-lc", "big.txt"], "text"),
    Probe("head", "first 1000 lines", ["-n", "1000", "big.txt"], "text"),
    Probe("head", "all but last 4096 bytes", ["-c", "-4096", "big.txt"], "text"),
    Probe("head", "all but last 200 lines", ["-n", "-200", "big.txt"], "text"),
    Probe("tail", "last 1000 lines", ["-n", "1000", "big.txt"], "text"),
    Probe("tail", "last 4096 bytes", ["-c", "4096", "big.txt"], "text"),
    Probe("tail", "bytes from start", ["-c", "+4096", "big.txt"], "text"),
    Probe("tail", "obsolete from-start line shorthand", ["+1000", "big.txt"], "text"),
    Probe("cut", "delimited field selection", ["-d", ",", "-f", "2,4", "table.csv"], "table"),
    Probe("sort", "large lexical sort", ["words.txt"], "sort"),
    Probe("sort", "zero-terminated records", ["-z", "sort-zero.bin"], "sort"),
    Probe("sort", "field numeric key sort", ["-t", ",", "-k", "2,2n", "sort-fields.csv"], "sort"),
    Probe("sort", "merge sorted streams", ["-m", "sort-merge-left.txt", "sort-merge-right.txt"], "sort"),
    Probe("sort", "merge preserves input stream order", ["-m", "sort-merge-single.txt"], "sort"),
    Probe(
        "sort",
        "check unsorted diagnostic shape",
        ["-c", "sort-check-bad.txt"],
        "sort",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?sort(?:\.exe)?: sort-check-bad\.txt:3: disorder: 1\n",
    ),
    Probe("find", "recursive name predicate", ["tree", "-type", "f", "-name", "*.txt"], "tree"),
    Probe("find", "empty predicate printf", ["tree", "-empty", "-printf", "%P|%y\\n"], "tree"),
    Probe("find", "prune or branch", ["tree", "-name", "dir_010", "-prune", "-o", "-type", "f", "-name", "*.txt", "-printf", "%P\\n"], "tree"),
    Probe("find", "xtype regular predicate", ["tree", "-xtype", "f", "-name", "*.txt"], "tree"),
    Probe("find", "posix extended regex predicate", ["tree", "-regextype", "posix-extended", "-regex", "tree/dir_(000|010)/nested_[0-9]/file_[0-9]{3}_[0-9]{2}\\.txt"], "tree"),
    Probe("find", "fprint0 action state", [".", "-type", "f", "-name", "*.txt", "-fprint0", "find-fprint0.out"], "tree", isolated=True, compare_stdout=False),
    Probe("ls", "recursive tree listing", ["-R", "tree"], "tree"),
    Probe("ls", "size sort single-column", ["-1S", "ls-fixture"], "ls"),
    Probe("ls", "mtime sort single-column", ["-1t", "ls-fixture"], "ls"),
    Probe("ls", "comma format wrapping", ["-m", "-w", "40", "ls-fixture"], "ls"),
    Probe("ls", "horizontal format wrapping", ["-x", "-w", "40", "ls-fixture"], "ls"),
    Probe("tee", "stdin to stdout and literal dash file", ["-"], "text", stdin_file="big.txt"),
    Probe(
        "tee",
        "bare output-error keeps file operand",
        ["--output-error", "tee-out.txt"],
        "fileops",
        stdin_file="tee-input.txt",
        isolated=True,
    ),
    Probe("tr", "translate lowercase to uppercase", ["a-z", "A-Z"], "text", stdin_file="big.txt"),
    Probe("tr", "delete digits", ["-d", "0-9"], "text", stdin_file="big.txt"),
    Probe("tr", "squeeze spaces", ["-s", " "], "text", stdin_file="big.txt"),
    Probe("tr", "complement squeeze words", ["-cs", "[:alpha:]", "\\n"], "text", stdin_file="tr-words.txt"),
    Probe("tr", "truncate set1 translation", ["-t", "abc", "X"], "text", stdin_file="tr-truncate.txt"),
    Probe("xargs", "default echo batches", ["-n", "50"], "xargs", stdin_file="xargs-words.txt"),
    Probe("xargs", "null-delimited echo batches", ["-0", "-n", "40"], "xargs", stdin_file="xargs-nul.txt"),
    Probe("echo", "escape interpretation", ["-e", "alpha\\nbeta", "tail"], "text"),
    Probe("dirname", "multiple operands", ["alpha/beta/file.txt", "plain", "."], "text"),
    Probe(
        "dirname",
        "drive and unc roots",
        ["//", "//server", "//server/share", "C:/", "C:foo"],
        "text",
    ),
    Probe("pwd", "physical current directory", ["-P"], "text", compare_stdout=False),
    Probe("cal", "fixed month layout", ["7", "2026"], "text"),
    Probe("date", "fixed epoch utc format", ["-u", "-d", "@0", "+%Y-%m-%dT%H:%M:%S%z"], "text"),
    Probe("hostname", "current host name", [], "text"),
    Probe("id", "numeric user id", ["-u"], "text"),
    Probe("id", "user name only", ["-un"], "text"),
    Probe("id", "numeric group id", ["-g"], "text"),
    Probe("nice", "current niceness", [], "text"),
    Probe("arch", "machine hardware name", [], "text"),
    Probe("uname", "machine hardware name", ["-m"], "text"),
    Probe("nproc", "ignore one processing unit", ["--ignore=1"], "text"),
    Probe("tty", "non tty diagnostic", [], "text", expected_exit=1),
    Probe("tty", "silent non tty status", ["-s"], "text", expected_exit=1),
    Probe(
        "stty",
        "non tty default diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?stty(?:\.exe)?: 'standard input': Inappropriate ioctl for device\n",
    ),
    Probe(
        "stty",
        "non tty all diagnostic",
        ["-a"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?stty(?:\.exe)?: 'standard input': Inappropriate ioctl for device\n",
    ),
    Probe(
        "stty",
        "non tty save diagnostic",
        ["-g"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?stty(?:\.exe)?: 'standard input': Inappropriate ioctl for device\n",
    ),
    Probe(
        "stty",
        "invalid setting diagnostic",
        ["invalidxyz"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?stty(?:\.exe)?: invalid argument 'invalidxyz'\n"
            r"Try '(?:.*/)?stty(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "stty",
        "missing value diagnostic",
        ["rows"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?stty(?:\.exe)?: missing argument to 'rows'\n"
            r"Try '(?:.*/)?stty(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "stty",
        "missing file device diagnostic",
        ["-F", "missing-device"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?stty(?:\.exe)?: missing-device: No such file or directory\n",
    ),
    Probe(
        "ptx",
        "fixed width kwic shape",
        ["-w", "40", "ptx-basic.txt"],
        "text",
        compare_stdout=False,
        stdout_regex=(
            r"(?s)[^\n]*alpha beta beta/?\n"
            r"[^\n]*alpha\s+beta beta gamma\n"
            r"[^\n]*alpha beta\s+beta gamma\n"
            r"[^\n]*alpha beta beta\s+gamma\n"
        ),
    ),
    Probe(
        "ptx",
        "ignore file kwic shape",
        ["-w", "40", "-i", "ptx-ignore.txt", "ptx-basic.txt"],
        "text",
        compare_stdout=False,
        stdout_regex=(
            r"(?s)[^\n]*alpha\s+beta beta gamma\n"
            r"[^\n]*alpha beta\s+beta gamma\n"
            r"[^\n]*alpha beta beta\s+gamma\n"
        ),
    ),
    Probe(
        "ptx",
        "auto reference kwic shape",
        ["-A", "-w", "60", "ptx-basic.txt"],
        "text",
        compare_stdout=False,
        stdout_regex=(
            r"(?s)ptx-basic\.txt:1:[^\n]*alpha beta beta/?\n"
            r"ptx-basic\.txt:1:[^\n]*alpha\s+beta beta gamma\n"
            r"ptx-basic\.txt:2:[^\n]*alpha beta\s+beta gamma\n"
            r"ptx-basic\.txt:2:[^\n]*alpha beta beta\s+gamma\n"
        ),
    ),
    Probe("reset", "ncurses version", ["-V"], "text"),
    Probe("tic", "ncurses version", ["-V"], "text"),
    Probe(
        "tic",
        "missing source file diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?tic(?:\.exe)?: File name needed\.  Usage:\n"
            r"\ttic \[-e names\] \[-o dir\] \[-R name\] \[-v\[n\]\] \[-V\] \[-w\[n\]\] \[-1aCDcfGgIKLNrsTtUx\] source-file\n"
        ),
    ),
    Probe("toe", "ncurses version", ["-V"], "text"),
    Probe("tzset", "current POSIX timezone", [], "text"),
    Probe(
        "tzset",
        "short help shape",
        ["-h"],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?s)Usage: tzset \[OPTION\]\n.*Print POSIX-compatible timezone ID.*Options:.*",
    ),
    Probe(
        "tzset",
        "version output shape",
        ["-V"],
        "text",
        compare_stdout=False,
        stdout_regex=(
            r"(?s)(?:tzset \(WinuxCmd\) \d+\.\d+\.\d+\n"
            r"|tzset \(cygwin\) \d+\.\d+\.\d+\nPOSIX-timezone generator\n.*)"
        ),
    ),
    Probe(
        "tzset",
        "reject positional timezone",
        ["UTC"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?s)Usage: tzset \[OPTION\]\n(?:\n)?Print POSIX-compatible timezone ID.*",
    ),
    Probe("whoami", "current user name", [], "text"),
    Probe("logname", "login name", [], "text"),
    Probe("cpio", "newc archive create shape", ["-o"], "text", stdin="cat-no-newline.txt\n", compare_stdout=False, stdout_regex=r"(?s)070701.*TRAILER!+.*", reference_required=False),
    Probe("free", "megabytes memory table shape", ["-m"], "text", compare_stdout=False, stdout_regex=r"(?s)\s*total\s+used\s+free\s+available\nMem:\s+\d+\s+\d+\s+\d+\nSwap:\s+\d+\s+\d+\s+\d+\n", reference_required=False),
    Probe("lsof", "field output prefix shape", ["--no-headers", "-F", "-t", "50"], "text", compare_stdout=False, compare_exit=False, expected_exit=None, stdout_regex=r"(?s)(?:[pctn][^\n]*\n|\n)+", stderr_regex=r"(?s)(?:lsof: warning: [^\n]*\n)*", stdout_line_limit=12, reference_required=False),
    Probe("lsof", "attached internet filter shape", ["-iTCP:80", "--no-headers", "-t", "50"], "text", compare_stdout=False, stdout_regex=r"(?s).*", stderr_regex=r"(?s)(?:lsof: warning: [^\n]*\n)*", reference_required=False),
    Probe("man", "command index shape", ["--list"], "text", compare_stdout=False, stdout_regex=r"(?s)Available commands:\n.*\b(?:cat|grep|ls)\b.*", reference_required=False),
    Probe("top", "batch one iteration shape", ["-b", "-n", "1", "--rows", "8"], "text", compare_stdout=False, stdout_regex=r"(?s)top - .*Tasks:.*%Cpu\(s\):.*MiB Mem :.*PID USER.*", reference_required=False),
    Probe("tree", "depth one tree shape", ["-L", "1", "tree"], "tree", compare_stdout=False, stdout_regex=r"(?s).*tree\n.*(?:├──|\+--|`--).*(?:directories|files).*", reference_required=False),
    Probe("uptime", "windows uptime shape", [], "text", compare_stdout=False, stdout_regex=r"(?s)\s*\d{2}:\d{2}:\d{2} up .*,  load average: N/A, N/A, N/A\n", reference_required=False),
    Probe("uptime", "pretty uptime shape", ["-p"], "text", compare_stdout=False, stdout_regex=r"(?s)up \d+ (?:day|days|hour|hours|minute|minutes).*\n", reference_required=False),
    Probe("uptime", "since timestamp shape", ["-s"], "text", compare_stdout=False, stdout_regex=r"(?s)\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\n", reference_required=False),
    Probe("watch", "single iteration command shape", ["-n", "0", "-c", "1", "-t", "cmd", "/c", "echo", "watch-ok"], "text", compare_stdout=False, stdout_regex=r"(?s).*watch-ok.*", reference_required=False),
    Probe("watch", "failing child status", ["-n", "0", "-c", "1", "-t", "cmd", "/c", "exit", "1"], "text", compare_stdout=False, expected_exit=1, stdout_regex=r"(?s).*", reference_required=False),
    Probe("hostid", "hex host id shape", [], "text", compare_stdout=False, stdout_regex=r"[0-9a-f]{8}\n"),
    Probe(
        "groups",
        "current memberships shape",
        [],
        "text",
        compare_stdout=False,
        expected_exit=None,
        stdout_regex=r".+\n",
        compare_exit=False,
    ),
    Probe(
        "who",
        "quiet user count shape",
        ["-q"],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?:[^\n]*\n)?# users=\d+\n",
    ),
    Probe(
        "users",
        "logged-in users shape",
        [],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?:[^\n]*(?: [^\n]+)*\n)?",
    ),
    Probe(
        "ps",
        "default process list shape",
        [],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?s).*PID.*\n(?:.*\n)*",
    ),
    Probe(
        "infocmp",
        "clear capability shape",
        ["xterm"],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?s).*clear=.*",
    ),
    Probe("getconf", "online processor count", ["_NPROCESSORS_ONLN"], "text"),
    Probe(
        "locale",
        "current locale categories shape",
        [],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?s)LANG=.*\nLC_CTYPE=.*\n.*LC_ALL=.*\n",
    ),
    Probe(
        "dircolors",
        "bourne shell output shape",
        ["-b"],
        "text",
        compare_stdout=False,
        stdout_regex=r"(?s)LS_COLORS='.*';\nexport LS_COLORS\n",
    ),
    Probe(
        "file",
        "ascii text classification shape",
        ["big.txt"],
        "text",
        compare_stdout=False,
        stdout_regex=r"big\.txt: .*text.*\n",
    ),
    Probe(
        "which",
        "path lookup true shape",
        ["true"],
        "text",
        compare_stdout=False,
        stdout_regex=r".*true(?:\.exe)?\n",
    ),
    Probe(
        "yes",
        "capped repeated arguments",
        ["alpha", "beta"],
        "text",
        expected_exit=None,
        stdout_line_limit=20000,
    ),
    Probe("clear", "xterm clear sequence", [], "text"),
    Probe("tput", "clear capability sequence", ["clear"], "text"),
    Probe("sleep", "zero interval", ["0"], "text"),
    Probe("sync", "global flush no operands", [], "text"),
    Probe("printenv", "known LC_ALL variable", ["LC_ALL"], "text"),
    Probe("true", "successful no-op", [], "text"),
    Probe("false", "failing no-op", [], "text", expected_exit=1),
    Probe("env", "empty environment assignments", ["-i", "BAR=2", "FOO=1"], "text"),
    Probe("env", "split string reparsed assignments", ["-S", "-i BAR=2 FOO=1"], "text"),
    Probe("expr", "arithmetic precedence", ["2", "+", "3", "*", "4"], "text"),
    Probe("factor", "medium composite factors", ["1234567890"], "text"),
    Probe("numfmt", "from iec stdin", ["--from=iec"], "textops", stdin_file="numfmt-iec.txt"),
    Probe("pathchk", "portable valid path", ["-p", "portable/name"], "text"),
    Probe("tsort", "simple dependency chain", ["tsort-chain.txt"], "textops"),
    Probe(
        "expr",
        "anchored basic regex capture",
        ["abc123", ":", r"[a-z]*\([0-9][0-9]*\)"],
        "text",
    ),
    Probe("rev", "large file line reversal", ["big.txt"], "text"),
    Probe(
        "rev",
        "nul separated records",
        ["-0"],
        "rev",
        stdin_file="rev-nul.txt",
    ),
    Probe("test", "numeric true expression", ["123", "-gt", "45"], "text"),
    Probe("test", "numeric false expression", ["12", "-eq", "13"], "text", expected_exit=1),
    Probe("[", "numeric true expression", ["123", "-gt", "45", "]"], "text"),
    Probe("less", "non-tty passthrough", ["big.txt"], "text"),
    Probe(
        "more",
        "non-tty passthrough",
        ["big.txt"],
        "text",
        reference_command="cat",
        reference_argv=["big.txt"],
    ),
    Probe(
        "more",
        "clean-print passthrough",
        ["-p", "more-small.txt"],
        "text",
        reference_command="cat",
        reference_argv=["more-small.txt"],
    ),
    Probe(
        "more",
        "start at line number",
        ["+3", "more-small.txt"],
        "text",
        reference_command="tail",
        reference_argv=["-n", "+3", "more-small.txt"],
    ),
    Probe(
        "egrep",
        "extended regex alias",
        ["needle_(123|999)", "big.txt"],
        "text",
        reference_command="grep",
        reference_argv=["-E", "needle_(123|999)", "big.txt"],
    ),
    Probe(
        "fgrep",
        "fixed string alias",
        ["needle_999", "big.txt"],
        "text",
        reference_command="grep",
        reference_argv=["-F", "needle_999", "big.txt"],
    ),
    Probe("xxd", "default hex dump", ["hexdump.bin"], "textops"),
    Probe("od", "hex byte limit without addresses", ["-An", "-tx1", "-N16", "big.txt"], "text"),
    Probe("base64", "large encode default wrap", ["big.txt"], "text"),
    Probe("base64", "decode wrapped payload", ["-d", "base64-wrapped.txt"], "textops"),
    Probe("base32", "large encode default wrap", ["big.txt"], "text"),
    Probe("base32", "decode wrapped payload", ["-d", "base32-wrapped.txt"], "textops"),
    Probe("basenc", "base64url encode default wrap", ["--base64url", "big.txt"], "text"),
    Probe("basenc", "base16 decode payload", ["--base16", "-d", "basenc-base16.txt"], "textops"),
    Probe("expand", "custom tab expansion", ["-t", "4", "expand-tabs.txt"], "textops"),
    Probe("unexpand", "all spaces to tabs", ["-a", "-t", "4", "unexpand-spaces.txt"], "textops"),
    Probe("fold", "space-aware wrapping", ["-s", "-w", "20", "fold-long.txt"], "textops"),
    Probe("fmt", "width paragraph refill", ["-w", "40", "fmt-basic.txt"], "textops"),
    Probe(
        "hexdump",
        "canonical first bytes exact",
        ["-C", "-n", "32", "hexdump.bin"],
        "textops",
    ),
    Probe(
        "hexdump",
        "default two-byte hex exact",
        ["hexdump.bin"],
        "textops",
    ),
    Probe("column", "table default whitespace", ["-t", "column-basic.txt"], "textops"),
    Probe("col", "backspace overstrike suppression", ["-b"], "textops", stdin_file="col-backspace.txt"),
    Probe("column", "table separator empty fields", ["-t", "-s", ",", "column-empty.csv"], "textops"),
    Probe("pr", "omit header passthrough", ["-t", "-w", "40", "pr-basic.txt"], "textops"),
    Probe(
        "chmod",
        "numeric owner write removal state",
        ["444", "f.txt"],
        "chmodops",
        compare_stdout=False,
        isolated=True,
        mode_paths=["f.txt"],
    ),
    Probe(
        "chmod",
        "group other write removal keeps owner writable",
        ["go-w", "f.txt"],
        "chmodops",
        compare_stdout=False,
        isolated=True,
        mode_paths=["f.txt"],
    ),
    Probe(
        "install",
        "mode 444 compare reapplies state",
        ["-C", "-m", "444", "source.txt", "dest.txt"],
        "installops",
        compare_stdout=False,
        isolated=True,
        mode_paths=["dest.txt"],
    ),
    Probe(
        "install",
        "mode 644 copy state",
        ["-m", "644", "source.txt", "out.txt"],
        "installops",
        compare_stdout=False,
        isolated=True,
        mode_paths=["out.txt"],
    ),
    Probe(
        "chgrp",
        "reference group no-op",
        ["--reference=reference.txt", "target.txt"],
        "ownerops",
        isolated=True,
    ),
    Probe(
        "chown",
        "reference ownership no-op",
        ["--reference=reference.txt", "target.txt"],
        "ownerops",
        isolated=True,
    ),
    Probe(
        "mktemp",
        "dry run template shape",
        ["-u", "bench-XXXXXX"],
        "text",
        compare_stdout=False,
        stdout_regex=r"bench-[A-Za-z0-9]{6}\n",
    ),
    Probe("dir", "single column directory listing", ["-1", "tree/dir_000/nested_0"], "tree"),
    Probe(
        "vdir",
        "single column directory listing",
        ["--format=single-column", "tree/dir_000/nested_0"],
        "tree",
    ),
    Probe(
        "df",
        "posix output shape",
        ["-P", "."],
        "text",
        compare_stdout=False,
        stdout_regex=(
            r"Filesystem\s+1024-blocks\s+Used\s+Available\s+Capacity\s+Mounted on\n"
            r".+\n"
        ),
    ),
    Probe(
        "df",
        "custom output field shape",
        ["--output=source,size,used,avail,pcent,target", "."],
        "text",
        compare_stdout=False,
        stdout_regex=(
            r"Filesystem\s+1K-blocks\s+Used\s+Avail\s+Use%\s+Mounted on\n"
            r".+\n"
        ),
    ),
    Probe(
        "dd",
        "block copy state",
        ["if=dd-input.bin", "of=dd-output.bin", "bs=4", "count=3", "status=none"],
        "blockops",
        isolated=True,
    ),
    Probe("nl", "pattern body numbering", ["-b", "p^ERR", "-w", "1", "-s", ":", "nl-pattern.txt"], "textops"),
    Probe("tac", "literal colon separator", ["-s", ":", "tac-colon.txt"], "textops"),
    Probe("stat", "format name and size", ["-c", "%n:%s", "stat-file.txt"], "textops"),
    Probe(
        "strings",
        "utf16le with filename and separator",
        ["-a", "-f", "-e", "l", "--output-separator=|", "strings-utf16le.bin"],
        "textops",
    ),
    Probe("kill", "signal name conversion", ["-lHUP"], "text"),
    Probe("kill", "realtime signal number conversion", ["-l34"], "text"),
    Probe(
        "realpath",
        "relative path output",
        ["--relative-to=realpath", "realpath/a/file.txt"],
        "textops",
        compare_stdout=False,
    ),
    Probe(
        "readlink",
        "canonicalize existing path",
        ["-e", "realpath/a/file.txt"],
        "textops",
        compare_stdout=False,
    ),
    Probe("join", "default sorted join", ["join-left.txt", "join-right.txt"], "textops"),
    Probe(
        "diff3",
        "default overlapping conflict",
        ["diff3-mine.txt", "diff3-base.txt", "diff3-yours.txt"],
        "textops",
        expected_exit=0,
        reference_argv=[
            "--diff-program",
            "{ref:diff}",
            "diff3-mine.txt",
            "diff3-base.txt",
            "diff3-yours.txt",
        ],
    ),
    Probe(
        "diff3",
        "merge overlapping conflict",
        ["-m", "diff3-mine.txt", "diff3-base.txt", "diff3-yours.txt"],
        "textops",
        expected_exit=1,
        reference_argv=[
            "--diff-program",
            "{ref:diff}",
            "-m",
            "diff3-mine.txt",
            "diff3-base.txt",
            "diff3-yours.txt",
        ],
    ),
    Probe(
        "sdiff",
        "side by side width 80",
        ["-w", "80", "sdiff-left.txt", "sdiff-right.txt"],
        "textops",
        expected_exit=1,
        reference_argv=[
            "--diff-program",
            "{ref:diff}",
            "-w",
            "80",
            "sdiff-left.txt",
            "sdiff-right.txt",
        ],
    ),
    Probe(
        "shuf",
        "deterministic random source head",
        ["--random-source=shuf-random.bin", "-n", "3", "shuf.txt"],
        "textops",
    ),
    Probe("nohup", "non tty child stdout", ["{ref:printf}", "hi"], "text"),
    Probe("stdbuf", "unbuffered stdout child", ["-o0", "{ref:printf}", "hi"], "text"),
    Probe("timeout", "quick command success", ["5", "{ref:true}"], "text"),
    Probe("cygpath", "default windows to unix", [r"C:\Users\Alice\Documents"], "text"),
    Probe("cygpath", "path list windows to unix", ["-p", "-u", r"C:\A;D:\B"], "text"),
    Probe("cygpath", "path list posix to mixed", ["-p", "-m", "/c/A:/d/B"], "text"),
    Probe("split", "line chunks state", ["-l", "1000", "split-lines.txt", "part"], "textops", isolated=True),
    Probe("csplit", "regex split state", ["-s", "csplit-input.txt", "/^MARK/"], "textops", isolated=True),
    Probe("unlink", "single file removal state", ["unlink-target.txt"], "unlinkops", isolated=True),
    Probe("shred", "one pass zero remove state", ["-n", "1", "-z", "-u", "shred-target.txt"], "shredops", isolated=True),
    Probe("dos2unix", "in-place CRLF to LF state", ["crlf.txt"], "lineendops", compare_stdout=False, isolated=True),
    Probe("d2u", "alias in-place CRLF to LF state", ["crlf.txt"], "lineendops", compare_stdout=False, isolated=True),
    Probe("unix2dos", "in-place LF to CRLF state", ["lf.txt"], "lineendops", compare_stdout=False, isolated=True),
    Probe("u2d", "alias in-place LF to CRLF state", ["lf.txt"], "lineendops", compare_stdout=False, isolated=True),
    Probe("diff", "brief differing files", ["-q", "diff-a.txt", "diff-b.txt"], "fileops", expected_exit=1, isolated=True),
    Probe("du", "apparent bytes single file", ["-b", "du-file.txt"], "fileops", isolated=True),
    Probe("du", "files0 total bytes", ["-b", "-c", "--files0-from", "du-list0.bin"], "fileops", isolated=True),
    Probe("truncate", "shrink existing file state", ["-s", "1024", "du-file.txt"], "fileops", compare_stdout=False, isolated=True),
    Probe("cp", "recursive directory copy", ["-R", "copy-src", "copy-out"], "fileops", isolated=True),
    Probe("cp", "no-clobber keeps destination", ["-n", "copy-new.txt", "copy-existing.txt"], "fileops", isolated=True),
    Probe("cp", "update skips older source", ["-u", "copy-old-src.txt", "copy-newer-dst.txt"], "fileops", isolated=True),
    Probe("cp", "backup custom suffix", ["-b", "-S", ".bak", "copy-new.txt", "copy-existing.txt"], "fileops", isolated=True),
    Probe("cp", "parents path copy", ["--parents", "copy-src/nested/gamma.txt", "parents-out"], "fileops", isolated=True),
    Probe("mkdir", "parents creation", ["-p", "new/a/b"], "fileops", isolated=True),
    Probe("mkdir", "verbose parents", ["-v", "-p", "newv/a/b"], "fileops", isolated=True, compare_stdout=False, stdout_regex=r"(?:.*/)?mkdir(?:\.exe)?: created directory 'newv'\n(?:.*/)?mkdir(?:\.exe)?: created directory 'newv/a'\n(?:.*/)?mkdir(?:\.exe)?: created directory 'newv/a/b'\n"),
    Probe("touch", "create missing file", ["touch-new.txt"], "fileops", isolated=True),
    Probe(
        "touch",
        "fixed utc directory timestamp",
        ["-d", "2024-04-05 06:07:08 UTC", "touch-dir"],
        "fileops",
        compare_stdout=False,
        isolated=True,
        time_paths=["touch-dir"],
    ),
    Probe(
        "touch",
        "reference relative date timestamp",
        ["-r", "touch-ref.txt", "-d", "+1 day", "touch-relative-target.txt"],
        "fileops",
        compare_stdout=False,
        isolated=True,
        time_paths=["touch-relative-target.txt"],
    ),
    Probe("ln", "hard link file", ["link-src.txt", "link-dst.txt"], "fileops", isolated=True),
    Probe("ln", "verbose hard link", ["-v", "link-src.txt", "link-verbose.txt"], "fileops", isolated=True),
    Probe("ln", "backup custom suffix", ["-b", "-S", ".bak", "link-src.txt", "link-existing.txt"], "fileops", isolated=True),
    Probe("mv", "rename file", ["move-src.txt", "move-dst.txt"], "fileops", isolated=True),
    Probe("mv", "no-clobber keeps destination", ["-n", "move-new.txt", "move-existing.txt"], "fileops", isolated=True),
    Probe("mv", "update skips older source", ["-u", "move-old-src.txt", "move-newer-dst.txt"], "fileops", isolated=True),
    Probe("mv", "backup custom suffix", ["-b", "-S", ".bak", "move-new.txt", "move-existing.txt"], "fileops", isolated=True),
    Probe("rm", "recursive removal", ["-r", "remove-tree"], "fileops", isolated=True),
    Probe("rm", "force missing succeeds", ["-f", "missing-file.txt"], "fileops", isolated=True),
    Probe("rm", "dir removes empty directory", ["-d", "remove-empty-dir"], "fileops", isolated=True),
    Probe("rm", "verbose single file removal", ["-v", "remove-verbose.txt"], "fileops", isolated=True),
    Probe("rmdir", "remove empty parents", ["-p", "empty/a/b"], "fileops", isolated=True),
    Probe("rmdir", "ignore nonempty succeeds", ["--ignore-fail-on-non-empty", "remove-nonempty-dir"], "fileops", isolated=True),
    Probe("basename", "multiple suffix stripping", ["-a", "-s", ".txt", "/tmp/alpha.txt", "beta.txt"], "text"),
    Probe("cmp", "quiet equal large files", ["-s", "big.txt", "big-copy.txt"], "text"),
    Probe("comm", "three-column sorted comparison", ["comm-a.txt", "comm-b.txt"], "text"),
    Probe(
        "comm",
        "default unsorted warning",
        ["comm-unsorted-a.txt", "comm-unsorted-b.txt"],
        "text",
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?comm(?:\.exe)?: file 1 is not in sorted order\n"
            r"(?:.*/)?comm(?:\.exe)?: input is not in sorted order\n"
        ),
    ),
    Probe("paste", "parallel two files", ["paste-left.txt", "paste-right.txt"], "text"),
    Probe("printf", "format reuse", ["%s:%04d\\n", "alpha", "7", "beta", "42"], "text"),
    Probe("seq", "integer fast path", ["1", "10000"], "text"),
    Probe("uniq", "count adjacent duplicates", ["-c", "dupes.txt"], "text"),
    Probe("uniq", "duplicate and unique flags suppress all", ["-d", "-u", "dupes.txt"], "text"),
    Probe("uniq", "all repeated with unique flag emits later repeats", ["-D", "-u", "dupes.txt"], "text"),
    Probe(
        "uniq",
        "count all repeated conflict",
        ["-c", "-D", "dupes.txt"],
        "text",
        compare_stdout=False,
        expected_exit=1,
    ),
    Probe("sum", "sysv large file", ["-s", "big.txt"], "text"),
    Probe("cksum", "crc large file", ["big.txt"], "text"),
    Probe("md5sum", "md5 large file", ["big.txt"], "text"),
    Probe("sha1sum", "sha1 large file", ["big.txt"], "text"),
    Probe("sha256sum", "sha256 large file", ["big.txt"], "text"),
    Probe("hmac256", "hmac sha256 large file", ["release-key", "big.txt"], "text"),
    Probe("hmac256", "hmac sha256 stdin", ["release-key"], "text", stdin_file="big.txt"),
    Probe(
        "patch",
        "unified diff file apply state",
        ["-p0", "-i", "change.diff"],
        "patchops",
        compare_stdout=False,
        isolated=True,
    ),
    Probe(
        "mpicalc",
        "hex rpn arithmetic stdin",
        [],
        "text",
        stdin="2 3 + p\n0a 5 * p\n10 3 / p\n10 3 % p\n",
    ),
    Probe(
        "mpicalc",
        "hex rpn modular stack stdin",
        [],
        "text",
        stdin="2 8 11 ^ p\n2 8 11 m p\n0ff b p\n2 3 r f\n",
    ),
    Probe("pinky", "short heading no utmp", [], "text"),
    Probe("pinky", "short format omit heading", ["-f"], "text"),
    Probe("pinky", "long unknown user", ["-l", "nosuchuser"], "text"),
    Probe(
        "chroot",
        "missing operand diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=125,
        stderr_regex=(
            r"(?:.*/)?chroot(?:\.exe)?: missing operand\n"
            r"Try '(?:.*/)?chroot(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "chroot",
        "missing directory diagnostic",
        ["definitely-missing-chroot-root"],
        "text",
        compare_stdout=False,
        expected_exit=125,
        stderr_regex=(
            r"(?:.*/)?chroot(?:\.exe)?: cannot change root directory to "
            r"'definitely-missing-chroot-root': No such file or directory\n"
        ),
    ),
    Probe(
        "runcon",
        "current context unsupported diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?runcon(?:\.exe)?: failed to get current context: Not supported\n",
    ),
    Probe(
        "runcon",
        "missing command diagnostic",
        ["system_u:system_r:httpd_t:s0"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?runcon(?:\.exe)?: no command specified\n"
            r"Try '(?:.*/)?runcon(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "chcon",
        "missing operand diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?chcon(?:\.exe)?: missing operand\n"
            r"Try '(?:.*/)?chcon(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "chcon",
        "missing file operand after context",
        ["system_u:object_r:user_home_t:s0"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?chcon(?:\.exe)?: missing operand after "
            r"'system_u:object_r:user_home_t:s0'\n"
            r"Try '(?:.*/)?chcon(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "mkfifo",
        "missing operand diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?mkfifo(?:\.exe)?: missing operand\n"
            r"Try '(?:.*/)?mkfifo(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "mkfifo",
        "existing path diagnostic",
        ["cat-no-newline.txt"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?mkfifo(?:\.exe)?: cannot create fifo "
            r"'cat-no-newline\.txt': File exists\n"
        ),
    ),
    Probe(
        "mknod",
        "missing operand diagnostic",
        [],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?mknod(?:\.exe)?: missing operand\n"
            r"Try '(?:.*/)?mknod(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "mknod",
        "missing type diagnostic",
        ["nodeonly"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?mknod(?:\.exe)?: missing operand after 'nodeonly'\n"
            r"Try '(?:.*/)?mknod(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "mknod",
        "fifo extra device numbers diagnostic",
        ["pnode", "p", "1", "2"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=(
            r"(?:.*/)?mknod(?:\.exe)?: extra operand '1'\n"
            r"Fifos do not have major and minor device numbers\.\n"
            r"Try '(?:.*/)?mknod(?:\.exe)? --help' for more information\.\n"
        ),
    ),
    Probe(
        "mknod",
        "existing path diagnostic",
        ["cat-no-newline.txt", "p"],
        "text",
        compare_stdout=False,
        expected_exit=1,
        stderr_regex=r"(?:.*/)?mknod(?:\.exe)?: cat-no-newline\.txt: File exists\n",
    ),
    Probe("sha224sum", "sha224 large file", ["big.txt"], "text"),
    Probe("sha384sum", "sha384 large file", ["big.txt"], "text"),
    Probe("sha512sum", "sha512 large file", ["big.txt"], "text"),
    Probe("b2sum", "blake2 large file", ["big.txt"], "text"),
]


def find_repo_root(start: Path) -> Path:
    cur = start.resolve()
    for candidate in [cur, *cur.parents]:
        if (candidate / "src" / "commands").is_dir() and (
            candidate / "CMakeLists.txt"
        ).is_file():
            return candidate
    raise SystemExit("Could not locate WinuxCmd repository root")


def detect_reference_bin(command: str, preferred_root: Path | None) -> Path | None:
    roots = [preferred_root] if preferred_root else []
    roots.extend(GNU_SEARCH_ROOTS)
    seen: set[Path] = set()
    for root in roots:
        if root is None or root in seen:
            continue
        seen.add(root)
        candidate = root / f"{command}.exe"
        if candidate.is_file():
            return candidate
    found = shutil.which(command)
    if not found:
        return None
    path = Path(found)
    # Avoid Windows built-ins and WinuxCmd copies when probing references.
    lowered = str(path).lower()
    if (
        r"\windows\system32\\" in lowered
        or r"\winuxsh\winuxcmd\\" in lowered
        or r"\winuxcmd\build-" in lowered
    ):
        return None
    return path


def command_exe(build_dir: Path, command: str) -> Path | None:
    candidate = build_dir / f"{command}.exe"
    return candidate if candidate.is_file() else None


def dispatcher_exe(build_dir: Path) -> Path | None:
    candidate = build_dir / "winuxcmd.exe"
    return candidate if candidate.is_file() else None


def materialize_probe_argv(
    argv: list[str], build_dir: Path, reference_root: Path | None
) -> list[str]:
    materialized: list[str] = []
    for arg in argv:
        if arg.startswith("{ref:") and arg.endswith("}"):
            command = arg[len("{ref:") : -1]
            exe = detect_reference_bin(command, reference_root)
            materialized.append(str(exe.resolve()) if exe else arg)
            continue
        if arg.startswith("{winux:") and arg.endswith("}"):
            command = arg[len("{winux:") : -1]
            exe = command_exe(build_dir, command)
            materialized.append(str(exe.resolve()) if exe else arg)
            continue
        materialized.append(arg)
    return materialized


def detect_cmake_build_type(build_dir: Path) -> str:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return "unknown"
    for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:"):
            _, value = line.split("=", 1)
            return value or "unknown"
    return "unknown"


def write_text_fixture(root: Path) -> None:
    visible_path = root / "cat-visible.bin"
    expected_visible = b"A\tB\r\n\x7f\x80\xff\n\n\nZ\n" * 8192
    if not visible_path.is_file() or visible_path.read_bytes() != expected_visible:
        visible_path.write_bytes(expected_visible)

    path = root / "big.txt"
    if not path.is_file() or path.stat().st_size <= 1_000_000:
        lines = []
        for i in range(120_000):
            lines.append(
                f"{i:06d} alpha beta needle_{i % 1000} field_{i % 97} "
                f"value_{(i * 17) % 65536}\n"
            )
        path.write_text("".join(lines), encoding="utf-8", newline="\n")

    copy = root / "big-copy.txt"
    if not copy.is_file() or copy.stat().st_size != path.stat().st_size:
        shutil.copyfile(path, copy)

    small_files: dict[str, str] = {
        "comm-a.txt": "apple\nbanana\ncarrot\nkiwi\n",
        "comm-b.txt": "banana\ncarrot\ndate\nkiwi\n",
        "comm-unsorted-a.txt": "banana\napple\n",
        "comm-unsorted-b.txt": "carrot\n",
        "paste-left.txt": "left1\nleft2\nleft3\n",
        "paste-right.txt": "right1\nright2\nright3\n",
        "dupes.txt": "alpha\nalpha\nbeta\nbeta\nbeta\ngamma\n",
        "tr-words.txt": "alpha, beta  123\nGAMMA\n",
        "tr-truncate.txt": "abc cab\n",
        "more-small.txt": "one\ntwo\nthree\nfour\n",
        "cat-no-newline.txt": "tail",
        "grep-context.txt": "pre1\nneedle one\npost1\ngap1\ngap2\npre2\nneedle two\npost2\n",
        "sed-file-a.txt": "one\n",
        "sed-file-b.txt": "two\n",
        "sed-zero-range.txt": "bar\nfoo\nfoo\n",
        "ptx-basic.txt": "alpha beta\nbeta gamma\n",
        "ptx-ignore.txt": "alpha\n",
    }
    many_patterns = root / "grep-many-patterns.txt"
    many_pattern_text = "".join(
        f"absent_fixed_{i:04d}\n" for i in range(1024)
    ) + "needle_999\n"
    if (
        not many_patterns.is_file()
        or many_patterns.read_text(encoding="utf-8") != many_pattern_text
    ):
        many_patterns.write_text(many_pattern_text, encoding="utf-8", newline="\n")
    for name, content in small_files.items():
        target = root / name
        if not target.is_file() or target.read_text(encoding="utf-8") != content:
            target.write_text(content, encoding="utf-8", newline="\n")

    grep_recursive = root / "grep-recursive"
    grep_recursive_files: dict[str, str] = {
        "root.txt": "hero 30x20\nplain 24x24\n",
        "keep/a.txt": "sprite 28x12\ntile 16x12\n",
        "keep/b.txt": "button 12x10\nlarge 40x40\n",
    }
    for name, content in grep_recursive_files.items():
        target = grep_recursive / name
        if not target.is_file() or target.read_text(encoding="utf-8") != content:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8", newline="\n")

def write_table_fixture(root: Path) -> None:
    path = root / "table.csv"
    if path.is_file() and path.stat().st_size > 500_000:
        return
    lines = [
        f"{i},name_{i % 200},region_{i % 13},{(i * 31) % 100000},tail_{i % 7}\n"
        for i in range(80_000)
    ]
    path.write_text("".join(lines), encoding="utf-8", newline="\n")


def write_sort_fixture(root: Path) -> None:
    path = root / "words.txt"
    if not (path.is_file() and path.stat().st_size > 500_000):
        value = 0x12345678
        lines = []
        for i in range(80_000):
            value = (1103515245 * value + 12345) & 0x7FFFFFFF
            lines.append(f"key_{value:010d}_{i % 97:02d}\n")
        path.write_text("".join(lines), encoding="utf-8", newline="\n")

    zero_path = root / "sort-zero.bin"
    expected_zero = b"b\0a\0c\0"
    if not zero_path.is_file() or zero_path.read_bytes() != expected_zero:
        zero_path.write_bytes(expected_zero)

    fields_path = root / "sort-fields.csv"
    expected_fields = "a,10\nb,2\nc,1\n"
    if not fields_path.is_file() or fields_path.read_text(encoding="utf-8") != expected_fields:
        fields_path.write_text(expected_fields, encoding="utf-8", newline="\n")

    check_path = root / "sort-check-bad.txt"
    expected_check = "10\n2\n1\n-5\n"
    if not check_path.is_file() or check_path.read_text(encoding="utf-8") != expected_check:
        check_path.write_text(expected_check, encoding="utf-8", newline="\n")

    merge_left_path = root / "sort-merge-left.txt"
    expected_merge_left = "".join(f"{i}\n" for i in range(0, 60000, 2))
    if not merge_left_path.is_file() or merge_left_path.read_text(encoding="utf-8") != expected_merge_left:
        merge_left_path.write_text(expected_merge_left, encoding="utf-8", newline="\n")

    merge_right_path = root / "sort-merge-right.txt"
    expected_merge_right = "".join(f"{i}\n" for i in range(1, 60000, 2))
    if not merge_right_path.is_file() or merge_right_path.read_text(encoding="utf-8") != expected_merge_right:
        merge_right_path.write_text(expected_merge_right, encoding="utf-8", newline="\n")

    merge_single_path = root / "sort-merge-single.txt"
    expected_merge_single = "2\na\n1\nb\n"
    if not merge_single_path.is_file() or merge_single_path.read_text(encoding="utf-8") != expected_merge_single:
        merge_single_path.write_text(expected_merge_single, encoding="utf-8", newline="\n")


def write_tree_fixture(root: Path) -> None:
    tree = root / "tree"
    marker = tree / ".fixture-complete"
    def ensure_template() -> None:
        template = root / "tree-template"
        template_marker = template / ".fixture-complete"
        if template_marker.is_file():
            return
        if template.exists():
            remove_tree(template)
        shutil.copytree(tree, template)
    if marker.is_file():
        (tree / "empty_dir").mkdir(parents=True, exist_ok=True)
        empty_file = tree / "empty.txt"
        if not empty_file.is_file() or empty_file.stat().st_size != 0:
            empty_file.write_bytes(b"")
        ensure_template()
        return
    if tree.exists():
        remove_tree(tree)
    for d in range(60):
        subdir = tree / f"dir_{d:03d}" / f"nested_{d % 5}"
        subdir.mkdir(parents=True, exist_ok=True)
        for f in range(20):
            suffix = "txt" if f % 3 else "log"
            (subdir / f"file_{d:03d}_{f:02d}.{suffix}").write_text(
                f"{d},{f},needle_{(d * f) % 1000}\n",
                encoding="utf-8",
                newline="\n",
            )
    (tree / "empty_dir").mkdir(parents=True, exist_ok=True)
    (tree / "empty.txt").write_bytes(b"")
    marker.write_text("ok\n", encoding="utf-8")
    ensure_template()


def write_ls_fixture(root: Path) -> None:
    ls_dir = root / "ls-fixture"
    marker = ls_dir / ".fixture-complete"
    expected = {
        "alpha.txt": (b"a", 1_700_000_000),
        "beta.log": (b"b" * 200, 1_710_000_000),
        "gamma.bin": (b"g" * 20, 1_720_000_000),
        "README": (b"readme", 1_705_000_000),
    }
    if marker.is_file() and all((ls_dir / name).is_file() for name in expected):
        return
    if ls_dir.exists():
        remove_tree(ls_dir)
    ls_dir.mkdir(parents=True, exist_ok=True)
    for name, (payload, ts) in expected.items():
        path = ls_dir / name
        path.write_bytes(payload)
        os.utime(path, (ts, ts))
    marker.write_text("ok\n", encoding="utf-8", newline="\n")
def write_xargs_fixture(root: Path) -> None:
    item_count = 5_000
    words = root / "xargs-words.txt"
    if (
        not words.is_file()
        or words.stat().st_size < 40_000
        or words.stat().st_size > 80_000
    ):
        lines = []
        for i in range(item_count):
            lines.append(f"arg_{i:05d}\n")
        words.write_text("".join(lines), encoding="utf-8", newline="\n")

    nul = root / "xargs-nul.txt"
    if (
        not nul.is_file()
        or nul.stat().st_size < 40_000
        or nul.stat().st_size > 80_000
    ):
        payload = b"".join(
            f"nul_{i:05d}".encode("ascii") + b"\0" for i in range(item_count)
        )
        nul.write_bytes(payload)


def write_rev_fixture(root: Path) -> None:
    path = root / "rev-nul.txt"
    expected_min = 20_000
    if not path.is_file() or path.stat().st_size < expected_min:
        payload = b"".join(
            f"record_{i:05d}_tail".encode("ascii") + b"\0" for i in range(4_000)
        )
        path.write_bytes(payload)


def write_textops_fixture(root: Path) -> None:
    small_files: dict[str, str] = {
        "nl-pattern.txt": "ERR first\nok\nERR second\n",
        "tac-colon.txt": "one:two:three:",
        "stat-file.txt": "hello",
        "column-basic.txt": "name\tage\tcity\nAlice\t30\tNY\nBob\t25\tLA\n",
        "column-empty.csv": "a,,c\nlong,b,\n",
        "pr-basic.txt": "alpha\nbeta\ngamma\n",
        "join-left.txt": "1 A\n2 B\n4 D\n",
        "join-right.txt": "1 alpha\n2 beta\n3 gamma\n",
        "shuf.txt": "a\nb\nc\nd\ne\n",
        "base64-wrapped.txt": "aGVsbG8gd29ybGQK\n",
        "base32-wrapped.txt": "NBSWY3DPEB3W64TMMQ======\n",
        "basenc-base16.txt": "68656C6C6F20776F726C640A\n",
        "expand-tabs.txt": "a\tb\tc\nab\tcd\tef\n",
        "unexpand-spaces.txt": "    alpha    beta\nxx  yy    zz\n",
        "fold-long.txt": "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron\n",
        "fmt-basic.txt": "This paragraph starts on one long line and should be reflowed into a stable forty column shape for parity testing.\n",
        "numfmt-iec.txt": "1.5K\n2.0M\n",
        "tsort-chain.txt": "a b\nb c\n",
        "diff3-mine.txt": "same\nmine\n",
        "diff3-base.txt": "same\nbase\n",
        "diff3-yours.txt": "same\nyours\n",
        "sdiff-left.txt": "same\nleft\n",
        "sdiff-right.txt": "same\nright\n",
        "col-backspace.txt": "a\bB\n",
    }
    for name, content in small_files.items():
        target = root / name
        if not target.is_file() or target.read_text(encoding="utf-8") != content:
            target.write_text(content, encoding="utf-8", newline="\n")

    random_source = root / "shuf-random.bin"
    expected_random = bytes(range(1, 65))
    if not random_source.is_file() or random_source.read_bytes() != expected_random:
        random_source.write_bytes(expected_random)

    strings_utf16le = root / "strings-utf16le.bin"
    expected_strings_utf16le = b"w\0o\0r\0l\0d\0\0"
    if (
        not strings_utf16le.is_file()
        or strings_utf16le.read_bytes() != expected_strings_utf16le
    ):
        strings_utf16le.write_bytes(expected_strings_utf16le)
    hexdump_bin = root / "hexdump.bin"
    expected_hexdump = b"0123456789abcdefABCDEFGHIJKLMNOP"
    if not hexdump_bin.is_file() or hexdump_bin.read_bytes() != expected_hexdump:
        hexdump_bin.write_bytes(expected_hexdump)

    rp_dir = root / "realpath" / "a"
    rp_dir.mkdir(parents=True, exist_ok=True)
    rp_file = rp_dir / "file.txt"
    if not rp_file.is_file() or rp_file.read_text(encoding="utf-8") != "rp\n":
        rp_file.write_text("rp\n", encoding="utf-8", newline="\n")

    template = root / "textops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "sed-subst-write.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)

    split_lines = [
        f"line_{i:05d} field_{i % 97:02d} payload_{(i * 17) % 65536:05d}\n"
        for i in range(20_000)
    ]
    (template / "split-lines.txt").write_text(
        "".join(split_lines), encoding="utf-8", newline="\n"
    )
    (template / "csplit-input.txt").write_text(
        "intro one\nintro two\nMARK first\nbody one\nMARK second\nbody two\n",
        encoding="utf-8",
        newline="\n",
    )
    (template / "sed-subst-write.txt").write_text(
        "needle one\nother\nneedle two\n", encoding="utf-8", newline="\n"
    )
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


def write_patchops_fixture(root: Path) -> None:
    template = root / "patchops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "target.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "target.txt").write_text(
        "alpha\nbeta\ngamma\n", encoding="utf-8", newline="\n"
    )
    (template / "change.diff").write_text(
        "--- target.txt\n"
        "+++ target.txt\n"
        "@@ -1,3 +1,3 @@\n"
        " alpha\n"
        "-beta\n"
        "+BETA\n"
        " gamma\n",
        encoding="utf-8",
        newline="\n",
    )
    marker.write_text("ok\n", encoding="utf-8", newline="\n")
def write_unlinkops_fixture(root: Path) -> None:
    template = root / "unlinkops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "unlink-target.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "unlink-target.txt").write_text(
        "remove me\n", encoding="utf-8", newline="\n"
    )
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


def write_blockops_fixture(root: Path) -> None:
    template = root / "blockops-template"
    marker = template / ".fixture-complete"
    expected = b"abcdefghijklmnopqrstuvwxyz"
    if marker.is_file() and (template / "dd-input.bin").is_file():
        if (template / "dd-input.bin").read_bytes() == expected:
            return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "dd-input.bin").write_bytes(expected)
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


def write_chmodops_fixture(root: Path) -> None:
    template = root / "chmodops-template"
    marker = template / ".fixture-complete"
    expected = "chmod payload\n"
    if marker.is_file() and (template / "f.txt").is_file():
        if (template / "f.txt").read_text(encoding="utf-8") == expected:
            return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "f.txt").write_text(expected, encoding="utf-8", newline="\n")
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


def write_installops_fixture(root: Path) -> None:
    template = root / "installops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "source.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "source.txt").write_text(
        "install payload\n", encoding="utf-8", newline="\n"
    )
    (template / "dest.txt").write_text(
        "install payload\n", encoding="utf-8", newline="\n"
    )
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


def write_ownerops_fixture(root: Path) -> None:
    template = root / "ownerops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "reference.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "reference.txt").write_text(
        "owner reference\n", encoding="utf-8", newline="\n"
    )
    (template / "target.txt").write_text(
        "owner target\n", encoding="utf-8", newline="\n"
    )
    marker.write_text("ok\n", encoding="utf-8", newline="\n")

def write_lineendops_fixture(root: Path) -> None:
    template = root / "lineendops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "crlf.txt").is_file() and (template / "lf.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "crlf.txt").write_bytes(b"alpha\r\nbeta\r\n")
    (template / "lf.txt").write_bytes(b"alpha\nbeta\n")
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


def write_shredops_fixture(root: Path) -> None:
    template = root / "shredops-template"
    marker = template / ".fixture-complete"
    if marker.is_file() and (template / "shred-target.txt").is_file():
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)
    (template / "shred-target.txt").write_text("shred payload", encoding="utf-8")
    marker.write_text("ok", encoding="utf-8")
def write_fileops_fixture(root: Path) -> None:
    tee_input = root / "tee-input.txt"
    if not tee_input.is_file() or tee_input.read_text(encoding="utf-8") != "tee payload\n":
        tee_input.write_text("tee payload\n", encoding="utf-8", newline="\n")

    template = root / "fileops-template"
    marker = template / ".fixture-complete"
    required_paths = [
        template / "touch-dir",
        template / "touch-ref.txt",
        template / "touch-relative-target.txt",
        template / "copy-existing.txt",
        template / "copy-new.txt",
        template / "copy-old-src.txt",
        template / "copy-newer-dst.txt",
        template / "parents-out",
        template / "move-existing.txt",
        template / "move-new.txt",
        template / "move-old-src.txt",
        template / "move-newer-dst.txt",
        template / "remove-empty-dir",
        template / "remove-verbose.txt",
        template / "remove-nonempty-dir",
        template / "link-existing.txt",
        template / "du-list0.bin",
    ]
    if marker.is_file() and all(p.exists() for p in required_paths):
        return
    if template.exists():
        remove_tree(template)
    template.mkdir(parents=True, exist_ok=True)

    (template / "copy-src" / "nested").mkdir(parents=True, exist_ok=True)
    (template / "copy-src" / "alpha.txt").write_text(
        "alpha\nbeta\n", encoding="utf-8", newline="\n"
    )
    (template / "copy-src" / "nested" / "gamma.txt").write_text(
        "gamma\nneedle\n", encoding="utf-8", newline="\n"
    )

    (template / "remove-tree" / "child").mkdir(parents=True, exist_ok=True)
    (template / "remove-tree" / "child" / "victim.txt").write_text(
        "remove me\n", encoding="utf-8", newline="\n"
    )
    (template / "empty" / "a" / "b").mkdir(parents=True, exist_ok=True)

    (template / "copy-existing.txt").write_text(
        "old destination\n", encoding="utf-8", newline="\n"
    )
    (template / "copy-new.txt").write_text(
        "new source\n", encoding="utf-8", newline="\n"
    )
    (template / "copy-old-src.txt").write_text(
        "old source\n", encoding="utf-8", newline="\n"
    )
    (template / "copy-newer-dst.txt").write_text(
        "newer destination\n", encoding="utf-8", newline="\n"
    )
    (template / "parents-out").mkdir(parents=True, exist_ok=True)
    old_ts = 1_700_000_000
    new_ts = 1_710_000_000
    os.utime(template / "copy-old-src.txt", (old_ts, old_ts))
    os.utime(template / "copy-newer-dst.txt", (new_ts, new_ts))
    (template / "move-src.txt").write_text(
        "move payload\n", encoding="utf-8", newline="\n"
    )
    (template / "move-existing.txt").write_text(
        "old move destination\n", encoding="utf-8", newline="\n"
    )
    (template / "move-new.txt").write_text(
        "new move source\n", encoding="utf-8", newline="\n"
    )
    (template / "move-old-src.txt").write_text(
        "old move source\n", encoding="utf-8", newline="\n"
    )
    (template / "move-newer-dst.txt").write_text(
        "newer move destination\n", encoding="utf-8", newline="\n"
    )
    os.utime(template / "move-old-src.txt", (old_ts, old_ts))
    os.utime(template / "move-newer-dst.txt", (new_ts, new_ts))
    (template / "remove-empty-dir").mkdir(parents=True, exist_ok=True)
    (template / "remove-verbose.txt").write_text(
        "remove verbose\n", encoding="utf-8", newline="\n"
    )
    (template / "remove-nonempty-dir").mkdir(parents=True, exist_ok=True)
    (template / "remove-nonempty-dir" / "keep.txt").write_text(
        "keep\n", encoding="utf-8", newline="\n"
    )
    (template / "link-src.txt").write_text(
        "link payload\n", encoding="utf-8", newline="\n"
    )
    (template / "link-existing.txt").write_text(
        "existing link target\n", encoding="utf-8", newline="\n"
    )
    (template / "du-list0.bin").write_bytes(b"du-file.txt\0link-src.txt\0")
    (template / "diff-a.txt").write_text(
        "same\nleft\n", encoding="utf-8", newline="\n"
    )
    (template / "diff-b.txt").write_text(
        "same\nright\n", encoding="utf-8", newline="\n"
    )
    (template / "du-file.txt").write_bytes(b"x" * 16384)
    (template / "touch-dir").mkdir(parents=True, exist_ok=True)
    (template / "touch-ref.txt").write_text(
        "reference touch time\n", encoding="utf-8", newline="\n"
    )
    (template / "touch-relative-target.txt").write_text(
        "relative target\n", encoding="utf-8", newline="\n"
    )
    ref_ts = 1_704_251_045
    os.utime(template / "touch-ref.txt", (ref_ts, ref_ts))
    os.utime(template / "touch-relative-target.txt", (old_ts, old_ts))
    marker.write_text("ok\n", encoding="utf-8", newline="\n")


FIXTURE_WRITERS: dict[str, Callable[[Path], None]] = {
    "text": write_text_fixture,
    "table": write_table_fixture,
    "sort": write_sort_fixture,
    "tree": write_tree_fixture,
    "ls": write_ls_fixture,
    "xargs": write_xargs_fixture,
    "rev": write_rev_fixture,
    "textops": write_textops_fixture,
    "patchops": write_patchops_fixture,
    "unlinkops": write_unlinkops_fixture,
    "blockops": write_blockops_fixture,
    "chmodops": write_chmodops_fixture,
    "installops": write_installops_fixture,
    "ownerops": write_ownerops_fixture,
    "shredops": write_shredops_fixture,
    "lineendops": write_lineendops_fixture,
    "fileops": write_fileops_fixture,
}


def prepare_fixtures(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    run_root = root / ".runs"
    if run_root.exists():
        remove_tree(run_root)
    for writer in FIXTURE_WRITERS.values():
        writer(root)


def slug(text: str) -> str:
    return "".join(ch if ch.isalnum() else "-" for ch in text).strip("-")


def isolated_run_cwd(
    probe: Probe, fixture_dir: Path, label: str, run_index: int
) -> Path:
    template = fixture_dir / f"{probe.fixture}-template"
    if not template.is_dir():
        raise RuntimeError(f"missing isolated fixture template: {template}")
    run_root = (
        fixture_dir
        / ".runs"
        / f"{probe.command}-{slug(probe.name)}-{label}-{run_index}"
    )
    if run_root.exists():
        remove_tree(run_root)
    shutil.copytree(template, run_root)
    return run_root


def tree_fingerprint(root: Path) -> str:
    h = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root).as_posix()
        if rel.startswith(".runs/"):
            continue
        if path.is_dir():
            h.update(b"D\0")
            h.update(rel.encode("utf-8", "surrogateescape"))
            h.update(b"\0")
        elif path.is_file():
            h.update(b"F\0")
            h.update(rel.encode("utf-8", "surrogateescape"))
            h.update(b"\0")
            h.update(str(path.stat().st_size).encode("ascii"))
            h.update(b"\0")
            h.update(sha256(path.read_bytes()).encode("ascii"))
            h.update(b"\0")
    return h.hexdigest()


def windows_readonly(path: Path) -> bool | None:
    if os.name != "nt":
        return None
    attrs = ctypes.windll.kernel32.GetFileAttributesW(str(path))
    if attrs == 0xFFFFFFFF:
        return None
    return bool(attrs & 0x1)


def mode_fingerprint(root: Path, relative_paths: list[str] | None) -> str:
    h = hashlib.sha256()
    for rel in relative_paths or []:
        path = root / rel
        h.update(rel.encode("utf-8", "surrogateescape"))
        h.update(b"\0")
        if not path.exists():
            h.update(b"M\0")
            continue
        h.update(b"D\0" if path.is_dir() else b"F\0")
        h.update(oct(stat.S_IMODE(path.stat().st_mode)).encode("ascii"))
        h.update(b"\0")
        readonly = windows_readonly(path)
        if readonly is None:
            h.update(b"R?\0")
        else:
            h.update(b"R1\0" if readonly else b"R0\0")
    return h.hexdigest()


def timestamp_fingerprint(root: Path, relative_paths: list[str] | None) -> str:
    h = hashlib.sha256()
    for rel in relative_paths or []:
        path = root / rel
        h.update(rel.encode("utf-8", "surrogateescape"))
        h.update(b"\0")
        try:
            st = path.stat(follow_symlinks=False)
        except OSError:
            h.update(b"M\0")
            continue
        h.update(str(int(st.st_mtime)).encode("ascii"))
        h.update(b"\0")
    return h.hexdigest()


def isolated_state_matches(probe: Probe, winux_cwd: Path, reference_cwd: Path) -> bool:
    if tree_fingerprint(winux_cwd) != tree_fingerprint(reference_cwd):
        return False
    if probe.mode_paths and mode_fingerprint(
        winux_cwd, probe.mode_paths
    ) != mode_fingerprint(reference_cwd, probe.mode_paths):
        return False
    if probe.time_paths and timestamp_fingerprint(
        winux_cwd, probe.time_paths
    ) != timestamp_fingerprint(reference_cwd, probe.time_paths):
        return False
    return True


def run_once(
    exe: Path,
    argv: list[str],
    cwd: Path,
    stdin: bytes | None,
    env: dict[str, str],
    stdout_line_limit: int | None,
) -> tuple[int | None, bytes, bytes, float]:
    start = time.perf_counter()
    if stdout_line_limit is None:
        completed = subprocess.run(
            [str(exe), *argv],
            cwd=str(cwd),
            input=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            check=False,
        )
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        return completed.returncode, completed.stdout, completed.stderr, elapsed_ms

    proc = subprocess.Popen(
        [str(exe), *argv],
        cwd=str(cwd),
        stdin=subprocess.PIPE if stdin is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    out = bytearray()
    err = b""
    try:
        if stdin is not None and proc.stdin is not None:
            proc.stdin.write(stdin)
            proc.stdin.close()
        assert proc.stdout is not None
        for _ in range(stdout_line_limit):
            chunk = proc.stdout.readline()
            if not chunk:
                break
            out.extend(chunk)
        proc.kill()
        _remaining_out, err = proc.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        _remaining_out, err = proc.communicate()
    finally:
        if proc.stdout is not None:
            proc.stdout.close()
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    # The process is intentionally terminated after capturing a stable prefix.
    # Do not compare platform-specific termination statuses for infinite tools.
    return None, bytes(out), err, elapsed_ms


def probe_stdin_bytes(probe: Probe, fixture_dir: Path) -> bytes | None:
    if probe.stdin_file is not None:
        return (fixture_dir / probe.stdin_file).read_bytes()
    if probe.stdin is not None:
        return probe.stdin.encode("utf-8")
    return None


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalize_text_stdout(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def median_ms(samples: list[float]) -> float:
    return round(statistics.median(samples), 3)


def run_probe(
    probe: Probe,
    build_dir: Path,
    fixture_dir: Path,
    reference_root: Path | None,
    iterations: int,
    warmups: int,
) -> ProbeResult:
    winux = dispatcher_exe(build_dir)
    reference_command = probe.reference_command or probe.command
    reference_argv = probe.reference_argv or probe.argv
    reference = detect_reference_bin(reference_command, reference_root) if probe.reference_required else None
    has_reference = reference is not None
    if winux is None:
        return ProbeResult(
            probe.command,
            probe.name,
            probe.argv,
            probe.fixture,
            "missing-winux",
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            "missing built executable for winuxcmd",
        )
    if reference is None and probe.reference_required:
        return ProbeResult(
            probe.command,
            probe.name,
            probe.argv,
            probe.fixture,
            "missing-reference",
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            None,
            f"missing GNU/MSYS reference executable for {reference_command}",
        )

    winux_argv = [probe.command, *materialize_probe_argv(probe.argv, build_dir, reference_root)]
    reference_argv = materialize_probe_argv(reference_argv, build_dir, reference_root)

    env = os.environ.copy()
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    env["TERM"] = "xterm"
    # Git/MSYS expands wildcard-looking argv for native subprocess callers
    # unless disabled.  We want command operands like find -name '*.txt' to be
    # seen by the reference command, not expanded by the MSYS runtime boundary.
    env["MSYS"] = "noglob"

    winux_times: list[float] = []
    reference_times: list[float] = []
    winux_stdout = b""
    reference_stdout = b""
    winux_stderr = b""
    reference_stderr = b""
    winux_exit = reference_exit = None
    state_match = None
    note = ""
    stdin_bytes = probe_stdin_bytes(probe, fixture_dir)

    total_runs = warmups + iterations
    for run_index in range(total_runs):
        if probe.isolated:
            winux_cwd = isolated_run_cwd(probe, fixture_dir, "winux", run_index)
            reference_cwd = isolated_run_cwd(
                probe, fixture_dir, "reference", run_index
            )
        else:
            winux_cwd = fixture_dir
            reference_cwd = fixture_dir

        wx_exit, wx_out, wx_err, wx_ms = run_once(
            winux, winux_argv, winux_cwd, stdin_bytes, env, probe.stdout_line_limit
        )
        if has_reference:
            assert reference is not None
            ref_exit, ref_out, ref_err, ref_ms = run_once(
                reference, reference_argv, reference_cwd, stdin_bytes, env, probe.stdout_line_limit
            )
        else:
            ref_exit, ref_out, ref_err, ref_ms = None, b"", b"", None
        if run_index >= warmups:
            winux_times.append(wx_ms)
            if ref_ms is not None:
                reference_times.append(ref_ms)
            if probe.isolated:
                run_state_match = isolated_state_matches(
                    probe, winux_cwd, reference_cwd
                )
                state_match = (
                    run_state_match
                    if state_match is None
                    else state_match and run_state_match
                )
        winux_exit, reference_exit = wx_exit, ref_exit
        winux_stdout, reference_stdout = wx_out, ref_out
        winux_stderr, reference_stderr = wx_err, ref_err
        if has_reference and probe.compare_exit and wx_exit != ref_exit:
            note = (
                f"exit mismatch; winux stderr={wx_err[:160]!r}; "
                f"reference stderr={ref_err[:160]!r}"
            )

    stdout_match = None
    raw_stdout_match = None
    normalized_stdout_match = None
    status = "ok"
    if probe.compare_stdout and has_reference:
        raw_stdout_match = winux_stdout == reference_stdout
        normalized_stdout_match = (
            normalize_text_stdout(winux_stdout)
            == normalize_text_stdout(reference_stdout)
        )
        stdout_match = raw_stdout_match or normalized_stdout_match
        if raw_stdout_match:
            status = "ok"
        elif normalized_stdout_match:
            status = "ok-normalized-newlines"
            note = "stdout differs only by CRLF/LF normalization"
        else:
            status = "stdout-mismatch"
            note = "stdout differs from reference"
    elif probe.stdout_regex is not None:
        pattern = re.compile(probe.stdout_regex)
        winux_shape = pattern.fullmatch(
            normalize_text_stdout(winux_stdout).decode("utf-8", "replace")
        )
        reference_shape = True
        if has_reference:
            reference_shape = pattern.fullmatch(
                normalize_text_stdout(reference_stdout).decode("utf-8", "replace")
            )
        stdout_match = bool(winux_shape and reference_shape)
        if stdout_match:
            status = "ok"
            note = "stdout shape matched regex" if has_reference else "stdout shape matched regex; reference not required"
        else:
            status = "stdout-mismatch"
            note = "stdout shape differs from expected regex"

    if probe.stderr_regex is not None:
        pattern = re.compile(probe.stderr_regex)
        winux_shape = pattern.fullmatch(
            normalize_text_stdout(winux_stderr).decode("utf-8", "replace")
        )
        reference_shape = True
        if has_reference:
            reference_shape = pattern.fullmatch(
                normalize_text_stdout(reference_stderr).decode("utf-8", "replace")
            )
        if winux_shape and reference_shape and status.startswith("ok"):
            note = note or "stderr shape matched regex"
        elif not (winux_shape and reference_shape):
            status = "stderr-mismatch"
            note = "stderr shape differs from expected regex"
    if has_reference and probe.compare_exit and winux_exit != reference_exit:
        status = "exit-mismatch"
    elif probe.compare_exit and probe.expected_exit is not None and winux_exit != probe.expected_exit:
        status = "unexpected-exit"
        note = f"expected exit {probe.expected_exit}, got {winux_exit}"
    elif state_match is False and status.startswith("ok"):
        status = "state-mismatch"
        note = "isolated filesystem state differs from reference"

    wx_median = median_ms(winux_times)
    ref_median = median_ms(reference_times) if reference_times else None
    ratio = round(wx_median / ref_median, 3) if ref_median and ref_median > 0 else None

    return ProbeResult(
        probe.command,
        probe.name,
        probe.argv,
        probe.fixture,
        status,
        wx_median,
        ref_median,
        ratio,
        stdout_match,
        raw_stdout_match,
        normalized_stdout_match,
        state_match,
        winux_exit,
        reference_exit,
        sha256(winux_stdout),
        sha256(reference_stdout) if has_reference else None,
        note,
    )


def render_markdown(
    results: list[ProbeResult],
    reference_root: Path | None,
    build_dir: Path,
    build_type: str,
) -> str:
    ok = sum(1 for r in results if r.status.startswith("ok"))
    compared = sum(1 for r in results if r.stdout_match is not None)
    matched = sum(1 for r in results if r.stdout_match is True)
    raw_matched = sum(1 for r in results if r.raw_stdout_match is True)
    normalized_only = sum(
        1
        for r in results
        if r.raw_stdout_match is False and r.normalized_stdout_match is True
    )
    state_compared = sum(1 for r in results if r.state_match is not None)
    state_matched = sum(1 for r in results if r.state_match is True)
    slow = [r for r in results if r.ratio is not None and r.ratio >= 5.0]

    lines = [
        "# Command Performance Baseline",
        "",
        "Generated by `scripts/benchmark-command-parity.py`.",
        "",
        "This is a black-box smoke benchmark against a local GNU/MSYS reference "
        "when one is available. Dynamic Windows-only probes record Winux shape "
        "and timing without inventing a reference runtime. It complements unit "
        "tests; it does not replace source review.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Probes | {len(results)} |",
        f"| Passing probes | {ok} |",
        f"| Stdout-compared probes | {compared} |",
        f"| Stdout matches | {matched} |",
        f"| Raw stdout matches | {raw_matched} |",
        f"| Newline-normalized-only matches | {normalized_only} |",
        f"| Filesystem-state-compared probes | {state_compared} |",
        f"| Filesystem state matches | {state_matched} |",
        f"| >=5x slower probes | {len(slow)} |",
        "",
        f"Build dir: `{build_dir}`",
        "",
        f"Build type: `{build_type}`",
        "",
        f"Reference root: `{reference_root or 'auto-detected'}`",
        "",
        "## Results",
        "",
        "| Command | Probe | Status | Winux ms | Reference ms | Ratio | Stdout | Raw | Normalized | State | Args | Note |",
        "| --- | --- | --- | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |",
    ]
    for r in results:
        stdout = (
            "n/a"
            if r.stdout_match is None
            else ("match" if r.stdout_match else "DIFF")
        )
        raw = (
            "n/a"
            if r.raw_stdout_match is None
            else ("match" if r.raw_stdout_match else "DIFF")
        )
        normalized = (
            "n/a"
            if r.normalized_stdout_match is None
            else ("match" if r.normalized_stdout_match else "DIFF")
        )
        state = (
            "n/a"
            if r.state_match is None
            else ("match" if r.state_match else "DIFF")
        )
        ratio = "" if r.ratio is None else f"{r.ratio:.3f}x"
        winux_ms = "" if r.winux_ms is None else f"{r.winux_ms:.3f}"
        ref_ms = "" if r.reference_ms is None else f"{r.reference_ms:.3f}"
        args = " ".join(r.argv).replace("|", "\\|")
        note = r.note.replace("|", "\\|")
        lines.append(
            f"| `{r.command}` | {r.name} | {r.status} | {winux_ms} | "
            f"{ref_ms} | {ratio} | {stdout} | {raw} | {normalized} | "
            f"{state} | `{args}` | {note} |"
        )

    if slow:
        lines.extend(["", "## Slow Probes", ""])
        for r in sorted(slow, key=lambda item: item.ratio or 0, reverse=True):
            lines.append(
                f"- `{r.command}` / {r.name}: {r.ratio:.3f}x slower "
                f"({r.winux_ms:.3f} ms vs {r.reference_ms:.3f} ms)."
            )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--fixture-dir", type=Path)
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--commands", help="comma-separated command names to run")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument(
        "--fail-on-mismatch",
        action="store_true",
        help="exit non-zero when any probe reports a mismatch",
    )
    args = parser.parse_args()

    repo = find_repo_root(args.repo)
    build_dir = args.build_dir or repo / "build-vs"
    build_type = detect_cmake_build_type(build_dir)
    fixture_dir = args.fixture_dir or build_dir / "command-bench-fixtures"
    json_path = args.json or repo / "DOCS" / "generated" / "command_performance_baseline.json"
    markdown_path = (
        args.markdown
        or repo / "DOCS" / "generated" / "command_performance_baseline.md"
    )

    prepare_fixtures(fixture_dir)
    selected_probes = PROBES
    if args.commands:
        wanted = {name.strip() for name in args.commands.split(",") if name.strip()}
        selected_probes = [probe for probe in PROBES if probe.command in wanted]
        found = {probe.command for probe in selected_probes}
        missing = sorted(wanted - found)
        if missing:
            raise SystemExit("No probes registered for: " + ",".join(missing))
    results = [
        run_probe(
            probe,
            build_dir=build_dir,
            fixture_dir=fixture_dir,
            reference_root=args.reference_root,
            iterations=args.iterations,
            warmups=args.warmups,
        )
        for probe in selected_probes
    ]

    payload = {
        "build_dir": str(build_dir),
        "build_type": build_type,
        "fixture_dir": str(fixture_dir),
        "reference_root": str(args.reference_root) if args.reference_root else None,
        "iterations": args.iterations,
        "warmups": args.warmups,
        "results": [asdict(r) for r in results],
    }
    json_path.parent.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    markdown_path.write_text(
        render_markdown(results, args.reference_root, build_dir, build_type),
        encoding="utf-8",
        newline="\n",
    )

    failures = [r for r in results if not r.status.startswith("ok")]
    print(f"probes={len(results)}")
    print(f"json={json_path}")
    print(f"markdown={markdown_path}")
    if failures:
        print("failures=" + ",".join(f"{r.command}:{r.status}" for r in failures))
    if failures and args.fail_on_mismatch:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
