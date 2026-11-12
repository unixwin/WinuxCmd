#!/usr/bin/env python3
"""shellns_gate.py — the shell-namespace mode of the #1488 gate.

run-gate.sh compares Rust vs C++ vs GNU on platform-neutral spellings;
this mode adds the piece that mode cannot see: the SHELL NAMESPACE. The
runner

  1. configures a fresh shell root (WINUXSH_ROOT set, the higher-priority
     aliases __RUBASH_SHELL_ROOT / NIU_ROOT / RUBASH_ROOT cleared — the
     root chain is __RUBASH_SHELL_ROOT > NIU_ROOT > WINUXSH_ROOT >
     RUBASH_ROOT, native_path.cppm:141 — plus TMPDIR pinned to the root's
     tmp view and HOME inside the root): the exact environment the bundled
     shell hands its coreutils (niubash#346 "coreutils own the shell
     tree");
  2. feeds the wired commands POSIX-spelled operands (/tmp/x, /dev/null,
     /mnt/d/..., the bare "/" tree) — in-process argv through python
     subprocess, because the agent shell's conversion layer rewrites
     POSIX-looking argv for native children (MSYS_NO_PATHCONV does NOT
     disable it; probed 2026-11-12);
  3. asserts the LANDING: the POSIX spelling must name its native path
     inside the configured root (<root>/tmp/x, X:\\..., root-relative),
     and the pre-port wrong landing (the current drive's bare D:\\tmp)
     must stay empty.

Setups run through a tiny python interpreter (native file creation, so
fixtures carry the caller's ownership — WSL-created fixtures come out
root-owned and break chown; probed 2026-11-12). Grammar, `;`-separated:
    mkdir -p PATH
    printf 'TEXT' > PATH        (TEXT: \\n and \\t escapes)

Case table gate/shellns-cases.tsv, TAB-separated:
    id | cmd | setup | stdin | expect_rc | args (SPACE-split) | landing
  landing: semicolon-separated paths, resolved like the shell namespace:
      /tmp/...  -> TMPDIR/...          /var/tmp/... -> TMPDIR/var/tmp/...
      /home/... -> parent(HOME)/...    /mnt/X/...   -> X:\\...
      /...      -> ROOT/...            D:\\...      -> verbatim
    A '!' prefix asserts the path must NOT exist after the run.
  '@TMPDIR@' inside args expands to the pinned POSIX tmp spelling ("/tmp");
  '@ROOT@' in setups expands to the native root.

Run: python rust2/gate/shellns_gate.py [driver.exe]
Artifacts: rust2/target/gate/shellns-ledger.tsv
"""

import os
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RUST2 = os.path.dirname(HERE)
DEFAULT_EXE = os.path.join(RUST2, "target", "debug", "winuxcmd-driver.exe")
ART = os.path.join(RUST2, "target", "gate")


def posix_to_native(spelling, root, tmpdir, home_parent):
    """Resolve a landing spelling the way the shell namespace does."""
    neg = spelling.startswith("!")
    if neg:
        spelling = spelling[1:]
    if spelling.startswith("/var/tmp"):
        native = os.path.join(tmpdir, "var", "tmp",
                              spelling[len("/var/tmp"):].strip("/\\"))
    elif spelling.startswith("/tmp"):
        native = os.path.join(tmpdir, spelling[len("/tmp"):].strip("/\\"))
    elif spelling.startswith("/home"):
        native = os.path.join(home_parent, spelling[len("/home"):].strip("/\\"))
    elif spelling.startswith("/mnt/") and len(spelling) >= 6:
        drive = spelling[5].upper()
        rest = spelling[7:] if len(spelling) > 6 else ""
        native = drive + ":\\" + rest.replace("/", "\\")
    elif spelling.startswith("/"):
        native = os.path.join(root, spelling[1:].replace("/", "\\"))
    else:
        native = spelling
    return neg, native


def native_setup_path(target, tmpdir):
    """A setup path is either native (D:/..., C:\\...) or the pinned tmp
    POSIX spelling (/tmp/...). Resolved natively — the setup layer creates
    fixtures directly, without the shell namespace."""
    if target.startswith("/tmp"):
        return os.path.join(tmpdir,
                            target[len("/tmp"):].strip("/\\").replace("/", "\\"))
    return target.replace("/", "\\")


def run_setup(setup, root, tmpdir, home_parent):
    """The tiny native setup interpreter (see module docstring)."""
    for stmt in setup.split(";"):
        stmt = stmt.strip()
        if not stmt:
            continue
        stmt = (stmt.replace("@ROOT@", root)
                    .replace("@TMPDIR@", tmpdir)
                    .replace("$TMPDIR", tmpdir)
                    .replace("$HOME", os.path.join(home_parent, "gater")))
        words = shlex.split(stmt)
        if not words:
            continue
        if words[0] == "mkdir" and "-p" in words[1:]:
            target = words[-1].strip('"')
            os.makedirs(native_setup_path(target, tmpdir), exist_ok=True)
        elif words[0] == "printf" and ">" in words:
            text = words[1].strip("'\"")
            i = words.index(">")
            if i + 1 >= len(words):
                raise ValueError("printf without target: %r" % stmt)
            target = words[i + 1].strip('"')
            text = text.replace("\\n", "\n").replace("\\t", "\t")
            native = native_setup_path(target, tmpdir)
            os.makedirs(os.path.dirname(native), exist_ok=True)
            with open(native, "w", newline="") as fh:
                fh.write(text)
        else:
            raise ValueError("unsupported setup stmt: %r" % stmt)


def gate_env(root, tmpdir, home_parent):
    env = dict(os.environ)
    # the root priority chain is __RUBASH_SHELL_ROOT > NIU_ROOT >
    # WINUXSH_ROOT > RUBASH_ROOT (native_path.cppm:141): the higher aliases
    # must be cleared or the temp root never wins on a dev box that has
    # niubash installed (probed 2026-11-12: WINUXSH_ROOT alone was shadowed)
    for name in ("__RUBASH_SHELL_ROOT", "NIU_ROOT", "RUBASH_ROOT"):
        env.pop(name, None)
    env["WINUXSH_ROOT"] = root
    env["TMPDIR"] = tmpdir
    env["HOME"] = os.path.join(home_parent, "gater")
    env["USERPROFILE"] = env["HOME"]
    return env


def run_case(exe, root, tmpdir, home_parent, cid, cmd, setup, stdin,
             expect_rc, args, landings):
    scratch = os.path.join(root, "scratch", cid)
    os.makedirs(scratch, exist_ok=True)
    if setup:
        try:
            run_setup(setup, root, tmpdir, home_parent)
        except Exception as exc:  # reported per case
            return "setup-failed: %s" % exc

    expanded = [a.replace("@TMPDIR@", "/tmp") for a in args]
    r = subprocess.run([exe, cmd] + expanded, cwd=scratch,
                       env=gate_env(root, tmpdir, home_parent),
                       input=stdin, capture_output=True, text=True,
                       timeout=30)
    notes = []
    if str(r.returncode) != expect_rc:
        notes.append("rc=%d want %s (%s)" % (r.returncode, expect_rc,
                                             (r.stderr or r.stdout).strip()[:80]))
    for landing in landings:
        neg, native = posix_to_native(landing, root, tmpdir, home_parent)
        if not os.path.isabs(native):
            native = os.path.join(scratch, native)
        exists = os.path.exists(native)
        if neg and exists:
            notes.append("forbidden landing exists: %s" % native)
        if not neg and not exists:
            notes.append("landing missing: %s" % native)
    return "ok" if not notes else "; ".join(notes)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_EXE
    cases_file = os.path.join(HERE, "shellns-cases.tsv")
    root = os.path.join(tempfile.gettempdir(), "shellns-gate-root")
    shutil.rmtree(root, ignore_errors=True)
    tmpdir = os.path.join(root, "tmp")
    os.makedirs(tmpdir, exist_ok=True)
    os.makedirs(os.path.join(root, "var", "tmp"), exist_ok=True)
    os.makedirs(os.path.join(root, "scratch"), exist_ok=True)
    home_parent = os.path.join(root, "home")
    os.makedirs(home_parent, exist_ok=True)
    # the shared /mnt/d landing yard the mnt-variant cases write into —
    # cleared per run so a rerun never sees last run's links (a leftover
    # dst makes `ln src dst` a same-inode relink, which GNU refuses too;
    # probed 2026-11-12)
    shutil.rmtree(r"D:\tmp\shellns-mnt", ignore_errors=True)
    os.makedirs(r"D:\tmp\shellns-mnt", exist_ok=True)

    os.makedirs(ART, exist_ok=True)
    ledger = open(os.path.join(ART, "shellns-ledger.tsv"), "w",
                  encoding="utf-8", newline="\n")
    ledger.write("id\tcmd\tstatus\tnote\n")

    total = passed = 0
    failed = []
    with open(cases_file, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            cid, cmd, setup, stdin, expect_rc = fields[:5]
            args = fields[5].split() if len(fields) > 5 and fields[5] else []
            landings = ([x for x in fields[6].split(";") if x]
                        if len(fields) > 6 else [])
            total += 1
            note = run_case(exe, root, tmpdir, home_parent, cid, cmd, setup,
                            stdin, expect_rc, args, landings)
            ok = note == "ok"
            passed += ok
            if not ok:
                failed.append("%s(%s): %s" % (cid, cmd, note))
            ledger.write("%s\t%s\t%s\t%s\n" %
                         (cid, cmd, "PASS" if ok else "FAIL", note))
    ledger.close()

    print("=" * 20 + " SHELLNS GATE SUMMARY " + "=" * 20)
    print("cases: %d  pass: %d  fail: %d" % (total, passed, total - passed))
    for f in failed:
        print("  FAIL " + f)
    print("ledger: " + os.path.join(ART, "shellns-ledger.tsv"))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
