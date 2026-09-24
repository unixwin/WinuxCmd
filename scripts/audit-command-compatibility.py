#!/usr/bin/env python3
"""Generate a command compatibility matrix from the WinuxCmd source tree.

The matrix is intentionally mechanical: it lists command entry points, declared
options, obvious unsupported/placeholder option text, test coverage counts, and
the upstream family we should compare against. It is not a substitute for
behavior probes, but it gives release work a stable checklist.
"""

from __future__ import annotations

import argparse
import ast
import json
import re
import warnings
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


COREUTILS = {
    "[",
    "arch",
    "b2sum",
    "base32",
    "base64",
    "basename",
    "basenc",
    "cat",
    "chcon",
    "chgrp",
    "chmod",
    "chown",
    "chroot",
    "cksum",
    "comm",
    "cp",
    "csplit",
    "cut",
    "date",
    "dd",
    "df",
    "dir",
    "dircolors",
    "dirname",
    "du",
    "echo",
    "env",
    "expand",
    "expr",
    "factor",
    "false",
    "fmt",
    "fold",
    "groups",
    "head",
    "hostid",
    "hostname",
    "id",
    "install",
    "join",
    "kill",
    "ln",
    "logname",
    "ls",
    "md5sum",
    "mkdir",
    "mkfifo",
    "mknod",
    "mktemp",
    "mv",
    "nice",
    "nl",
    "nohup",
    "nproc",
    "numfmt",
    "od",
    "paste",
    "pathchk",
    "pinky",
    "pr",
    "printenv",
    "printf",
    "ptx",
    "pwd",
    "readlink",
    "realpath",
    "rm",
    "rmdir",
    "runcon",
    "seq",
    "sha1sum",
    "sha224sum",
    "sha256sum",
    "sha384sum",
    "sha512sum",
    "shred",
    "shuf",
    "sleep",
    "sort",
    "split",
    "stat",
    "stdbuf",
    "sum",
    "stty",
    "sync",
    "tac",
    "tail",
    "tee",
    "test",
    "test_bracket",
    "timeout",
    "touch",
    "tr",
    "true",
    "truncate",
    "tsort",
    "tty",
    "uname",
    "unexpand",
    "uniq",
    "unlink",
    "uptime",
    "users",
    "vdir",
    "wc",
    "who",
    "whoami",
    "yes",
}

DIFFUTILS = {"cmp", "diff", "diff3", "sdiff"}
GREP_FAMILY = {"grep", "egrep", "fgrep"}
SED_FAMILY = {"sed"}
LESS_FAMILY = {"less"}
UTIL_LINUX = {
    "cal",
    "col",
    "column",
    "hexdump",
    "look",
    "more",
    "rev",
    "getopt",
    "dmesg",
    "findmnt",
}
PROCPS_NG = {"free", "ps", "top", "watch"}
NCURSES = {"clear", "infocmp", "reset", "tic", "toe", "tput"}
DOS2UNIX = {"d2u", "dos2unix", "u2d", "unix2dos"}
FINDUTILS = {"find", "xargs"}
FILE_FAMILY = {"file"}
TREE_FAMILY = {"tree"}
XXD_FAMILY = {"xxd"}
PATCH_FAMILY = {"patch"}
MAN_FAMILY = {"man"}
LSOF_FAMILY = {"lsof"}
CYGWIN_FAMILY = {"getconf", "locale"}
GNU_WHICH_FAMILY = {"which"}
GNU_CPIO_FAMILY = {"cpio"}
CYGPATH_FAMILY = {"cygpath"}
BINUTILS = {"strings"}
LIBGCRYPT = {"hmac256", "mpicalc"}

P0_HOT = {
    "[",
    "cat",
    "cp",
    "cut",
    "date",
    "diff",
    "dirname",
    "du",
    "echo",
    "env",
    "find",
    "grep",
    "egrep",
    "fgrep",
    "head",
    "less",
    "ln",
    "ls",
    "mkdir",
    "more",
    "mv",
    "pwd",
    "rm",
    "rmdir",
    "sed",
    "sort",
    "tail",
    "tee",
    "test",
    "test_bracket",
    "touch",
    "tr",
    "wc",
    "xargs",
}

P1_HOT = {
    "awk",
    "basename",
    "chmod",
    "chown",
    "chgrp",
    "cmp",
    "comm",
    "csplit",
    "dd",
    "df",
    "install",
    "join",
    "kill",
    "mktemp",
    "nl",
    "od",
    "paste",
    "printf",
    "readlink",
    "realpath",
    "split",
    "stat",
    "strings",
    "tac",
    "timeout",
    "uniq",
    "unlink",
}

REFERENCE_URLS = {
    "gnu-coreutils": "https://www.gnu.org/software/coreutils/manual/",
    "gnu-grep": "https://www.gnu.org/software/grep/manual/",
    "gnu-sed": "https://www.gnu.org/software/sed/manual/",
    "gnu-diffutils": "https://www.gnu.org/software/diffutils/manual/",
    "gnu-findutils": "https://www.gnu.org/software/findutils/manual/",
    "less": "https://www.greenwoodsoftware.com/less/",
    "util-linux": "https://mirrors.edge.kernel.org/pub/linux/utils/util-linux/",
    "procps-ng": "https://gitlab.com/procps-ng/procps",
    "ncurses": "https://invisible-island.net/ncurses/",
    "dos2unix": "https://waterlan.home.xs4all.nl/dos2unix.html",
    "file": "https://www.darwinsys.com/file/",
    "tree": "https://oldmanprogrammer.net/source.php?dir=projects/tree",
    "xxd": "https://www.vim.org/",
    "patch": "https://savannah.gnu.org/projects/patch/",
    "man-db": "https://www.nongnu.org/man-db/",
    "lsof": "https://github.com/lsof-org/lsof",
    "cygpath": "https://cygwin.com/cygwin-ug-net/cygpath.html",
    "cygwin": "https://cygwin.com/",
    "gnu-which": "https://savannah.gnu.org/projects/which/",
    "gnu-cpio": "https://www.gnu.org/software/cpio/",
    "gnu-binutils": "https://www.gnu.org/software/binutils/",
    "libgcrypt": "https://gnupg.org/software/libgcrypt/",
    "local": "",
}


@dataclass
class OptionInfo:
    short: str
    long: str
    description: str
    value_type: str
    status: str


@dataclass
class CommandInfo:
    command: str
    source_file: str
    source_line_count: int
    upstream_source_line_count: int
    family: str
    reference_url: str
    priority: str
    option_count: int
    placeholder_count: int
    unsupported_count: int
    test_count: int
    perf_probe_count: int
    perf_failure_count: int
    perf_worst_ratio: float | None
    options: list[OptionInfo]
    notes: list[str]


def find_repo_root(start: Path) -> Path:
    cur = start.resolve()
    for candidate in [cur, *cur.parents]:
        if (candidate / "src" / "commands").is_dir() and (
            candidate / "CMakeLists.txt"
        ).is_file():
            return candidate
    raise SystemExit("Could not locate WinuxCmd repository root")


def command_family(command: str) -> str:
    if command in COREUTILS:
        return "gnu-coreutils"
    if command in GREP_FAMILY:
        return "gnu-grep"
    if command in SED_FAMILY:
        return "gnu-sed"
    if command in DIFFUTILS:
        return "gnu-diffutils"
    if command in FINDUTILS:
        return "gnu-findutils"
    if command in LESS_FAMILY:
        return "less"
    if command in UTIL_LINUX:
        return "util-linux"
    if command in PROCPS_NG:
        return "procps-ng"
    if command in NCURSES:
        return "ncurses"
    if command in DOS2UNIX:
        return "dos2unix"
    if command in FILE_FAMILY:
        return "file"
    if command in TREE_FAMILY:
        return "tree"
    if command in XXD_FAMILY:
        return "xxd"
    if command in PATCH_FAMILY:
        return "patch"
    if command in MAN_FAMILY:
        return "man-db"
    if command in LSOF_FAMILY:
        return "lsof"
    if command in CYGWIN_FAMILY:
        return "cygwin"
    if command in GNU_WHICH_FAMILY:
        return "gnu-which"
    if command in GNU_CPIO_FAMILY:
        return "gnu-cpio"
    if command in CYGPATH_FAMILY:
        return "cygpath"
    if command in BINUTILS:
        return "gnu-binutils"
    if command in LIBGCRYPT:
        return "libgcrypt"
    return "local"


TEST_GROUP_ALIASES = {
    "[": "test_bracket",
    "false": "false_cmd",
    "true": "true_cmd",
}


def priority_for(command: str, option_count: int, test_count: int) -> str:
    if command in P0_HOT:
        return "P0"
    if command in P1_HOT:
        return "P1"
    if option_count >= 20 or test_count >= 20:
        return "P1"
    return "P2"


def test_group_for(command: str) -> str:
    return TEST_GROUP_ALIASES.get(command, command)


def collect_invocations(text: str, name: str) -> list[str]:
    invocations: list[str] = []
    token = name + "("
    pos = 0
    while True:
        start = text.find(token, pos)
        if start < 0:
            break
        i = start + len(token)
        depth = 1
        in_string = False
        escape = False
        while i < len(text) and depth:
            ch = text[i]
            if in_string:
                if escape:
                    escape = False
                elif ch == "\\":
                    escape = True
                elif ch == '"':
                    in_string = False
            else:
                if ch == '"':
                    in_string = True
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
            i += 1
        if depth == 0:
            invocations.append(text[start + len(token) : i - 1])
        pos = max(i, start + len(token))
    return invocations


STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')
TYPE_TOKENS = [
    "TERMINATED_STRING_TYPE",
    "OPTIONAL_STRING_TYPE",
    "STRING_TYPE",
    "INT_TYPE",
    "BOOL_TYPE",
]


def decode_c_string(token: str) -> str:
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", SyntaxWarning)
            value = ast.literal_eval(token)
    except (SyntaxError, ValueError):
        return token[1:-1]
    return value if isinstance(value, str) else str(value)


def option_status(description: str) -> str:
    lower = description.lower()
    if "not support" in lower or "unsupported" in lower:
        return "unsupported"
    if (
        "ignored" in lower
        or "placeholder" in lower
        or "no-op" in lower
        or "windows limitation" in lower
    ):
        return "placeholder"
    return "declared"


def parse_options(text: str) -> list[OptionInfo]:
    options: list[OptionInfo] = []
    for call in collect_invocations(text, "OPTION"):
        strings = [decode_c_string(m.group(0)) for m in STRING_RE.finditer(call)]
        if len(strings) < 3:
            continue
        value_type = "flag"
        for token in TYPE_TOKENS:
            if token in call:
                value_type = token
                break
        description = " ".join(strings[2:])
        options.append(
            OptionInfo(
                short=strings[0],
                long=strings[1],
                description=description,
                value_type=value_type,
                status=option_status(description),
            )
        )
    return options


def parse_command_names(source_path: Path, text: str) -> list[str]:
    names: list[str] = []
    for call in collect_invocations(text, "REGISTER_COMMAND"):
        strings = [decode_c_string(m.group(0)) for m in STRING_RE.finditer(call)]
        if strings:
            names.append(strings[0])
            continue
        first = call.split(",", 1)[0].strip()
        if first:
            names.append(first)
    if not names:
        names.append(source_path.stem)
    return names


def count_tests(repo: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    for path in (repo / "tests" / "unit").rglob("*.cpp"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for group in re.findall(r"\bTEST\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,", text):
            counts[group] = counts.get(group, 0) + 1
    return counts


def load_perf_summary(repo: Path) -> dict[str, dict[str, object]]:
    path = repo / "DOCS" / "generated" / "command_performance_baseline.json"
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}

    summary: dict[str, dict[str, object]] = {}
    for result in data.get("results", []):
        command = str(result.get("command", ""))
        if not command:
            continue
        item = summary.setdefault(
            command,
            {"probe_count": 0, "failure_count": 0, "worst_ratio": None},
        )
        item["probe_count"] = int(item["probe_count"]) + 1
        status = str(result.get("status", ""))
        if not status.startswith("ok"):
            item["failure_count"] = int(item["failure_count"]) + 1
        ratio = result.get("ratio")
        if isinstance(ratio, (int, float)):
            current = item["worst_ratio"]
            item["worst_ratio"] = (
                float(ratio)
                if current is None
                else max(float(current), float(ratio))
            )
    return summary


def upstream_source_candidates(command: str, family: str) -> list[Path]:
    upstream = Path(__file__).resolve().parents[2] / "_upstream_refs"
    if family == "gnu-coreutils":
        checksum_wrappers = {
            "md5sum": "coreutils-md5sum",
            "sha1sum": "coreutils-sha1sum",
            "sha224sum": "coreutils-sha224sum",
            "sha256sum": "coreutils-sha256sum",
            "sha384sum": "coreutils-sha384sum",
            "sha512sum": "coreutils-sha512sum",
        }
        if command == "cksum":
            return [
                upstream / "gnu-coreutils" / "src" / "cksum.c",
                upstream / "gnu-coreutils" / "src" / "cksum_crc.c",
            ]
        if command == "b2sum":
            return [
                upstream / "gnu-coreutils" / "src" / "cksum.c",
                upstream / "gnu-coreutils" / "src" / "blake2" / "b2sum.c",
                upstream
                / "gnu-coreutils"
                / "src"
                / "blake2"
                / "blake2b-ref.c",
            ]
        if command in checksum_wrappers:
            return [
                upstream / "gnu-coreutils" / "src" / "cksum.c",
                upstream / "gnu-coreutils" / "src" / f"{checksum_wrappers[command]}.c",
            ]
        aliases = {
            "[": "test",
            "base32": "basenc",
            "base64": "basenc",
            "arch": "uname",
            "false": "true",
            "dir": "ls",
            "vdir": "ls",
            "test_bracket": "test",
            "chgrp": "chown-chgrp",
        }
        if command == "kill":
            return [
                upstream / "gnu-coreutils" / "src" / "kill.c",
                upstream / "procps-ng" / "src" / "kill.c",
                upstream / "procps-ng" / "local" / "signals.c",
                upstream / "cygwin-full" / "winsup" / "utils" / "kill.cc",
                upstream
                / "cygwin-full"
                / "winsup"
                / "cygwin"
                / "include"
                / "cygwin"
                / "signal.h",
                upstream / "cygwin-full" / "winsup" / "cygwin" / "strsig.cc",
            ]
        name = aliases.get(command, command)
        return [upstream / "gnu-coreutils" / "src" / f"{name}.c"]
    if family == "gnu-grep":
        return [upstream / "gnu-grep" / "src" / "grep.c"]
    if family == "gnu-sed":
        return [
            upstream / "gnu-sed" / "sed" / "compile.c",
            upstream / "gnu-sed" / "sed" / "execute.c",
        ]
    if family == "gnu-findutils":
        if command == "find":
            return [
                upstream / "gnu-findutils" / "find" / "parser.c",
                upstream / "gnu-findutils" / "find" / "pred.c",
                upstream / "gnu-findutils" / "find" / "ftsfind.c",
            ]
        if command == "xargs":
            return [upstream / "gnu-findutils" / "xargs" / "xargs.c"]
    if family == "gnu-diffutils":
        if command in {"cmp", "diff", "diff3", "sdiff"}:
            return [upstream / "gnu-diffutils" / "src" / f"{command}.c"]
    if family == "less":
        return [
            upstream / "less" / "main.c",
            upstream / "less" / "command.c",
            upstream / "less" / "forwback.c",
            upstream / "less" / "input.c",
            upstream / "less" / "line.c",
        ]
    if family == "util-linux":
        util_linux_paths = {
            "cal": upstream / "util-linux" / "misc-utils" / "cal.c",
            "col": upstream / "util-linux" / "text-utils" / "col.c",
            "column": upstream / "util-linux" / "text-utils" / "column.c",
            "getopt": upstream / "util-linux" / "misc-utils" / "getopt.c",
            "hexdump": upstream / "util-linux" / "text-utils" / "hexdump.c",
            "more": upstream / "util-linux" / "text-utils" / "more.c",
            "rev": upstream / "util-linux" / "text-utils" / "rev.c",
        }
        if command in util_linux_paths:
            return [util_linux_paths[command]]
    if family == "man-db":
        return [upstream / "man-db" / "src" / "man.c"]
    if family == "cygwin":
        cygwin_paths = {
            "getconf": upstream / "cygwin-full" / "winsup" / "utils" / "getconf.c",
            "locale": upstream / "cygwin-full" / "winsup" / "utils" / "locale.cc",
        }
        if command in cygwin_paths:
            return [cygwin_paths[command]]
    if family == "gnu-which":
        if command == "which":
            return [upstream / "gnu-which" / "which.c"]
    if family == "gnu-cpio":
        if command == "cpio":
            return [
                upstream / "gnu-cpio" / "src" / "main.c",
                upstream / "gnu-cpio" / "src" / "copyin.c",
                upstream / "gnu-cpio" / "src" / "copyout.c",
                upstream / "gnu-cpio" / "src" / "copypass.c",
                upstream / "gnu-cpio" / "src" / "util.c",
            ]
    if family == "cygpath":
        return [upstream / "cygwin" / "winsup" / "utils" / "cygpath.cc"]
    if family == "ncurses":
        ncurses_paths = {
            "clear": upstream / "ncurses" / "progs" / "clear.c",
            "infocmp": upstream / "ncurses" / "progs" / "infocmp.c",
            "reset": upstream / "ncurses" / "progs" / "reset_cmd.c",
            "tic": upstream / "ncurses" / "progs" / "tic.c",
            "toe": upstream / "ncurses" / "progs" / "toe.c",
            "tput": upstream / "ncurses" / "progs" / "tput.c",
        }
        if command in ncurses_paths:
            return [ncurses_paths[command]]
    if family == "dos2unix":
        if command in {"d2u", "dos2unix", "u2d", "unix2dos"}:
            return [
                upstream / "dos2unix" / "dos2unix" / "dos2unix.c",
                upstream / "dos2unix" / "dos2unix" / "common.c",
            ]
    if family == "file":
        if command == "file":
            return [
                upstream / "file" / "src" / "file.c",
                upstream / "file" / "src" / "softmagic.c",
                upstream / "file" / "src" / "apprentice.c",
            ]
    if family == "tree":
        if command == "tree":
            return [upstream / "tree" / "tree.c", upstream / "tree" / "tree.h"]
    if family == "xxd":
        if command == "xxd":
            return [upstream / "vim" / "src" / "xxd" / "xxd.c"]
    if family == "lsof":
        if command == "lsof":
            return [
                upstream / "lsof" / "lib" / "lsof.c",
                upstream / "lsof" / "lib" / "proc.c",
                upstream / "lsof" / "lib" / "print.c",
                upstream / "lsof" / "lib" / "misc.c",
            ]
    if family == "procps-ng":
        procps_paths = {
            "free": [upstream / "procps-ng" / "src" / "free.c"],
            "ps": [
                upstream / "procps-ng" / "src" / "ps" / "parser.c",
                upstream / "procps-ng" / "src" / "ps" / "output.c",
            ],
            "top": [upstream / "procps-ng" / "src" / "top" / "top.c"],
            "watch": [upstream / "procps-ng" / "src" / "watch.c"],
        }
        if command in procps_paths:
            return procps_paths[command]
    if family == "gnu-binutils":
        if command == "strings":
            return [upstream / "gnu-binutils" / "binutils" / "strings.c"]
    if family == "patch":
        return [
            upstream / "gnu-patch" / "src" / "patch.c",
            upstream / "gnu-patch" / "src" / "pch.c",
        ]
    if family == "libgcrypt":
        if command in {"hmac256", "mpicalc"}:
            return [upstream / "libgcrypt" / "src" / f"{command}.c"]
    return []


def upstream_source_line_count(command: str, family: str) -> int:
    total = 0
    for path in upstream_source_candidates(command, family):
        if path.is_file():
            total += count_source_lines(path.read_text(encoding="utf-8", errors="ignore"))
    return total


def notes_for(
    command: str,
    options: list[OptionInfo],
    perf_probe_count: int,
    perf_failure_count: int,
    perf_worst_ratio: float | None,
    upstream_loc: int,
) -> list[str]:
    notes: list[str] = []
    if command == "wpm":
        notes.append("Excluded from release parity scope by policy.")
    if command in {"grep", "egrep", "fgrep", "sed", "find", "csplit", "nl", "tac"}:
        notes.append("Regex behavior is command-owned, not shell-owned.")
    if command in {"less", "more"}:
        notes.append("Interactive terminal behavior needs PTY/manual coverage.")
    if command == "kill":
        notes.append(
            "Signal conversion follows GNU/procps behavior; Windows signal "
            "numbers are aligned to Cygwin/MSYS local references."
        )
    unsupported = [o for o in options if o.status == "unsupported"]
    placeholders = [o for o in options if o.status == "placeholder"]
    if unsupported:
        notes.append("Has explicitly unsupported declared option(s).")
    if placeholders:
        notes.append("Has compatibility placeholder/no-op option(s).")
    if upstream_loc == 0 and command_family(command) != "local":
        notes.append("No local upstream source file mapped yet.")
    if perf_probe_count:
        notes.append(f"Performance probes: {perf_probe_count}.")
    if perf_failure_count:
        notes.append(f"Performance/parity probe failures: {perf_failure_count}.")
    if perf_worst_ratio is not None and perf_worst_ratio >= 5.0:
        notes.append(f"Slowest probe is {perf_worst_ratio:.1f}x reference.")
    return notes


def count_source_lines(text: str) -> int:
    return sum(1 for line in text.splitlines() if line.strip())


def format_command_list(commands: list[CommandInfo]) -> str:
    if not commands:
        return "_None._"
    return ", ".join(f"`{c.command}`" for c in commands)


def iter_commands(repo: Path) -> Iterable[CommandInfo]:
    test_counts = count_tests(repo)
    perf = load_perf_summary(repo)
    for source in sorted((repo / "src" / "commands").glob("*.cpp")):
        text = source.read_text(encoding="utf-8", errors="ignore")
        options = parse_options(text)
        for command in parse_command_names(source, text):
            if command == "wpm":
                continue
            family = command_family(command)
            test_count = test_counts.get(test_group_for(command), 0)
            option_count = len(options)
            priority = priority_for(command, option_count, test_count)
            perf_item = perf.get(command, {})
            perf_probe_count = int(perf_item.get("probe_count", 0))
            perf_failure_count = int(perf_item.get("failure_count", 0))
            perf_worst_ratio_raw = perf_item.get("worst_ratio")
            perf_worst_ratio = (
                float(perf_worst_ratio_raw)
                if isinstance(perf_worst_ratio_raw, (int, float))
                else None
            )
            upstream_loc = upstream_source_line_count(command, family)
            yield CommandInfo(
                command=command,
                source_file=str(source.relative_to(repo)).replace("\\", "/"),
                source_line_count=count_source_lines(text),
                upstream_source_line_count=upstream_loc,
                family=family,
                reference_url=REFERENCE_URLS[family],
                priority=priority,
                option_count=option_count,
                placeholder_count=sum(1 for o in options if o.status == "placeholder"),
                unsupported_count=sum(1 for o in options if o.status == "unsupported"),
                test_count=test_count,
                perf_probe_count=perf_probe_count,
                perf_failure_count=perf_failure_count,
                perf_worst_ratio=perf_worst_ratio,
                options=options,
                notes=notes_for(
                    command,
                    options,
                    perf_probe_count,
                    perf_failure_count,
                    perf_worst_ratio,
                    upstream_loc,
                ),
            )


def is_p0_thin_vs_upstream(c: CommandInfo) -> bool:
    if c.priority != "P0" or c.source_line_count >= 150:
        return False
    if c.upstream_source_line_count <= 0:
        return True
    return c.source_line_count < c.upstream_source_line_count


def render_md(commands: list[CommandInfo]) -> str:
    total = len(commands)
    by_priority = {
        p: sum(1 for c in commands if c.priority == p) for p in ["P0", "P1", "P2"]
    }
    unsupported = sum(c.unsupported_count for c in commands)
    placeholders = sum(c.placeholder_count for c in commands)
    untested = sum(1 for c in commands if c.test_count == 0)
    thin_hot = sum(1 for c in commands if is_p0_thin_vs_upstream(c))
    upstream_mapped = sum(1 for c in commands if c.upstream_source_line_count > 0)
    perf_probed = sum(1 for c in commands if c.perf_probe_count > 0)
    perf_failures = sum(c.perf_failure_count for c in commands)
    perf_slow = sum(
        1
        for c in commands
        if c.perf_worst_ratio is not None and c.perf_worst_ratio >= 5.0
    )
    p0_thin_commands = sorted(
        [c for c in commands if is_p0_thin_vs_upstream(c)],
        key=lambda c: c.command,
    )
    untested_commands = sorted(
        [c for c in commands if c.test_count == 0],
        key=lambda c: (c.priority, c.command),
    )
    placeholder_commands = sorted(
        [c for c in commands if c.placeholder_count or c.unsupported_count],
        key=lambda c: (c.priority, c.command),
    )
    p0_without_perf = sorted(
        [c for c in commands if c.priority == "P0" and c.perf_probe_count == 0],
        key=lambda c: c.command,
    )

    lines: list[str] = [
        "# Command Compatibility Matrix",
        "",
        "Generated by `scripts/audit-command-compatibility.py` from command source, declared options, and unit test names.",
        "",
        "Scope excludes `wpm` by release policy.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Commands | {total} |",
        f"| P0 hot commands | {by_priority['P0']} |",
        f"| P1 commands | {by_priority['P1']} |",
        f"| P2 commands | {by_priority['P2']} |",
        f"| Declared unsupported options | {unsupported} |",
        f"| Declared placeholder/no-op options | {placeholders} |",
        f"| Commands without direct unit tests | {untested} |",
        f"| P0 commands thin vs upstream | {thin_hot} |",
        f"| Commands with mapped local upstream source | {upstream_mapped} |",
        f"| Commands with performance probes | {perf_probed} |",
        f"| Performance/parity probe failures | {perf_failures} |",
        f"| Commands with >=5x slowest probe | {perf_slow} |",
        "",
        "## Regex And Shell Boundary",
        "",
        "- Shell parsing owns quoting, pipes, redirects, variable expansion, and shell glob expansion.",
        "- Winuxsh may expand shell globs before invoking commands; PowerShell often passes native wildcard-looking arguments through, so WinuxCmd keeps command-level wildcard expansion only for file-like operands.",
        "- Command regex is owned by the command: `grep`, `sed`, `find -regex`, `csplit`, `nl`, and `tac -r` must compile and evaluate patterns internally.",
        "- Regex/script operands must stay literal and must not be reinterpreted as file globs. Examples: `grep -E 'a|b'`, `sed 's/a/b/'`, and `find . -name '*.cpp'` are command data, not path expansion requests.",
        "",
        "## Focus Lists",
        "",
        f"- P0 commands thin vs upstream: {format_command_list(p0_thin_commands)}.",
        f"- Commands without direct unit tests: {format_command_list(untested_commands)}.",
        f"- Commands with unsupported or placeholder options: {format_command_list(placeholder_commands)}.",
        f"- P0 commands without performance probes yet: {format_command_list(p0_without_perf)}.",
        "",
        "## Matrix",
        "",
        "| Priority | Command | Family | Local LOC | Upstream LOC | Options | Unsupported | Placeholder | Tests | Perf probes | Worst perf | Perf fails | Source | Notes |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]

    for c in sorted(commands, key=lambda x: (x.priority, x.family, x.command)):
        notes = "<br>".join(c.notes)
        ref = f"[{c.family}]({c.reference_url})" if c.reference_url else c.family
        worst_perf = (
            ""
            if c.perf_worst_ratio is None
            else f"{c.perf_worst_ratio:.1f}x"
        )
        lines.append(
            f"| {c.priority} | `{c.command}` | {ref} | {c.source_line_count} | "
            f"{c.upstream_source_line_count} | {c.option_count} | "
            f"{c.unsupported_count} | {c.placeholder_count} | {c.test_count} | "
            f"{c.perf_probe_count} | {worst_perf} | {c.perf_failure_count} | "
            f"`{c.source_file}` | {notes} |"
        )

    lines.extend(
        [
            "",
            "## Release Use",
            "",
            "- P0 commands must have behavior probes against their upstream family before release.",
            "- Unsupported options may stay declared only when the command exits with a clear diagnostic or the option is documented as a compatibility placeholder.",
            "- Placeholder/no-op options should be kept if upstream accepts them and Windows cannot implement the behavior, but their source comments and docs must say so.",
            "- Source LOC is a rough toy-implementation signal only. Behavior tests and option parity decide release readiness.",
            "- Upstream LOC is counted only for local upstream source files already pulled under `_upstream_refs`; a zero means the reference still needs to be pulled or mapped.",
            "- Performance probes come from `DOCS/generated/command_performance_baseline.md`; they are smoke baselines, not exhaustive microbenchmarks.",
            "- Commands with zero direct tests should get at least smoke tests before release, even if they are P2.",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--json",
        type=Path,
        default=None,
        help="Output JSON path. Defaults to DOCS/generated/command_compatibility_matrix.json.",
    )
    parser.add_argument(
        "--markdown",
        type=Path,
        default=None,
        help="Output Markdown path. Defaults to DOCS/generated/command_compatibility_matrix.md.",
    )
    args = parser.parse_args()

    repo = find_repo_root(args.repo)
    json_path = args.json or repo / "DOCS" / "generated" / "command_compatibility_matrix.json"
    md_path = args.markdown or repo / "DOCS" / "generated" / "command_compatibility_matrix.md"

    commands = list(iter_commands(repo))
    json_path.parent.mkdir(parents=True, exist_ok=True)
    md_path.parent.mkdir(parents=True, exist_ok=True)

    json_path.write_text(
        json.dumps([asdict(c) for c in commands], indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    md_path.write_text(render_md(commands), encoding="utf-8")

    print(f"commands={len(commands)}")
    print(f"json={json_path}")
    print(f"markdown={md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
