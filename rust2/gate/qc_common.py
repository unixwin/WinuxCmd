#!/usr/bin/env python3
"""Shared helpers for the gate QC layer (#1488).

Parses gate/cases/<cmd>.tsv case tables (same column contract as
run-gate.sh) and runs one case against the oracle sides:

  rust : winuxcmd-driver.exe  (spawned natively, cwd = per-case scratch)
  cpp  : build-vs/usr/bin/winuxcmd.exe (1.x C++ dispatcher)
  gnu  : WSL coreutils via `timeout 10 env K=V cmd args`, native ext4
         scratch under /tmp/winuxcmd-gate-qc (never touches the gate's
         /tmp/winuxcmd-gate), artifacts mirrored back to
         rust2/target/gate-qc/gnu/<cmd>/<id>/

Side-running semantics deliberately mirror run-gate.sh:
  - @EMPTY@ encodes a literal empty-string argument
  - env column: comma-separated K=V assignments
  - setup: POSIX sh snippet run with bash inside the scratch dir
  - 10s timeout per side

Path contract note (docs/windows-path-contract.md): this script is a
repo-local tool; it derives the WSL view of the tree from the location of
this file (/d/repo/x -> /mnt/d/repo/x) exactly like run-gate.sh, overridable
with TREE_MNT.
"""

import hashlib
import os
import shutil
import subprocess
import time

GATE_DIR = os.path.dirname(os.path.abspath(__file__))
RUST2_DIR = os.path.dirname(GATE_DIR)
TREE_DIR = os.path.dirname(RUST2_DIR)
CASES_DIR = os.path.join(GATE_DIR, "cases")
CASES_POSIX_DIR = os.path.join(GATE_DIR, "cases-posix")
QC_ART = os.path.join(RUST2_DIR, "target", "gate-qc")
GNU_REMOTE_ROOT = "/tmp/winuxcmd-gate-qc"
GNU_MIRROR = os.path.join(QC_ART, "gnu")
SIDE_TIMEOUT = 10


def posix_fixture():
    """Shell-namespace fixture env for POSIX-spelling cases (#1488 gate posix
    round), mirroring run-gate.sh --posix-mode: __RUBASH_SHELL_ROOT (highest
    root priority in native_path.cppm shell_tree_map_w, so no host NIU_ROOT
    can shadow it), TMPDIR (/tmp and /var/tmp), HOME (/home maps to HOME's
    parent). All under target/gate-qc/posix/, created on demand. Python
    subprocess passes argv verbatim, so no conversion-layer detour is needed
    (see PROBE-METHODOLOGY.md)."""
    art = os.path.join(QC_ART, "posix")
    root = os.path.join(art, "root")
    tmp = os.path.join(art, "tmp")
    home = os.path.join(art, "home", "u")
    for d in (root, os.path.join(root, "usr", "bin"), tmp, os.path.dirname(home)):
        os.makedirs(d, exist_ok=True)
    return {
        "__RUBASH_SHELL_ROOT": root,
        "TMPDIR": tmp,
        "HOME": home,
    }


def tree_mnt():
    """WSL view of this checkout, mirroring run-gate.sh's derivation."""
    env = os.environ.get("TREE_MNT")
    if env:
        return env
    t = TREE_DIR.replace("\\", "/")
    if len(t) >= 2 and t[1] == ":":
        return "/mnt/" + t[0].lower() + t[2:]
    if t.startswith("/"):
        return "/mnt/" + t[1].lower() + t[2:] if t[1].isupper() else t
    return t


def _native(p):
    """Convert an MSYS-style /d/repo/... path to a Windows-native D:/repo/..."""
    if len(p) >= 3 and p[0] == "/" and p[2] == "/" and p[1].isalpha():
        return p[1].upper() + ":" + p[2:]
    return p


def _bin(envkey, default_rel):
    """Resolve a side binary: env override, else the worktree default."""
    v = os.environ.get(envkey)
    if v:
        return _native(v.replace("\\", "/"))
    return os.path.join(RUST2_DIR, default_rel)


def rust_bin():
    return _bin("RUST_BIN", os.path.join("target", "debug", "winuxcmd-driver.exe"))


def cpp_bin():
    return _bin("CPP_BIN", os.path.join("build-vs", "usr", "bin", "winuxcmd.exe"))


# Wave2 commands (#1488 second batch): their Rust implementations live on the
# wave2 lanes (rust2/check-mechanism and successors), NOT on main yet — the
# main-built driver exits 127 for them. QC tools run wave2 commands through
# the wave2 lane driver (RUST_BIN_W2 env) and wave1 commands through the
# main-built driver (RUST_BIN, which carries the truefalse sole-argument fix
# the wave2 driver predates). Disclosed in every QC report header.
WAVE2_COMMANDS = frozenset(
    ["whoami", "nproc", "unlink", "sleep", "base64", "base32", "b2sum",
     "sha224sum", "sha384sum", "tac", "truncate", "uname", "id", "hostname",
     "getconf"]
)


def rust_bin_for(cmd):
    if cmd in WAVE2_COMMANDS:
        v = os.environ.get("RUST_BIN_W2")
        if not v:
            raise SystemExit(
                "RUST_BIN_W2 not set: wave2 commands (%s...) need the wave2 "
                "lane driver; export RUST_BIN_W2=/path/to/winuxcmd-driver.exe "
                "from a wave2 worktree" % sorted(WAVE2_COMMANDS)[0])
        return _native(v.replace("\\", "/"))
    return rust_bin()


def setup_bash():
    """bash used for setup snippets (same PATH resolution as run-gate.sh)."""
    return os.environ.get("QC_SETUP_BASH") or shutil.which("bash") or "bash"


class Case:
    __slots__ = ("cmd", "cid", "mode", "setup", "env", "stdin", "args")

    def __init__(self, cmd, cid, mode, setup, env, stdin, args):
        self.cmd = cmd
        self.cid = cid
        self.mode = mode
        self.setup = setup
        self.env = env  # dict K->V ("" value allowed)
        self.stdin = stdin  # str, no trailing newline added
        self.args = args  # list[str], @EMPTY@ already decoded

    def key(self):
        return (tuple(self.args), self.stdin, self.env, self.setup)


def parse_env(s):
    out = {}
    if not s:
        return out
    for kv in s.split(","):
        if "=" not in kv:
            continue  # malformed, no '=' (run-gate.sh skips these too)
        k, v = kv.split("=", 1)
        out[k] = v
    return out


def parse_cases(cmd, cases_dir=None):
    """Parse gate/cases/<cmd>.tsv into Case objects (skips # comments)."""
    path = os.path.join(cases_dir or CASES_DIR, cmd + ".tsv")
    cases = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")  # split on tabs PRESERVING empty fields
            cid, mode = fields[0], fields[1]
            setup = fields[2] if len(fields) > 2 else ""
            envstr = fields[3] if len(fields) > 3 else ""
            stdin = fields[4] if len(fields) > 4 else ""
            args = [("" if a == "@EMPTY@" else a) for a in fields[5:]]
            cases.append(Case(cmd, cid, mode, setup, parse_env(envstr), stdin, args))
    return cases


def list_commands(cases_dir=None):
    d = cases_dir or CASES_DIR
    return sorted(f[:-4] for f in os.listdir(d) if f.endswith(".tsv"))


def list_posix_commands():
    """Commands with a shell-namespace (POSIX-spelling) skeleton table."""
    return list_commands(CASES_POSIX_DIR)


def parse_posix_cases(cmd):
    return parse_cases(cmd, CASES_POSIX_DIR)


def qsh(s):
    """Quote a word for single-quoted shell embedding (same as run-gate.sh)."""
    return "'" + s.replace("'", "'\\''") + "'"


class SideResult:
    __slots__ = ("rc", "stdout", "stderr", "error")

    def __init__(self, rc, stdout, stderr, error=""):
        self.rc = rc
        self.stdout = stdout
        self.stderr = stderr
        self.error = error  # harness-level failure (missing exe, timeout)

    @property
    def out_sha(self):
        return hashlib.sha256(self.stdout).hexdigest()[:16] if self.stdout else "-"

    def out_repr(self, limit=60):
        s = self.stdout.decode("utf-8", "replace")
        s = s.replace("\n", "\\n")
        return s[:limit] + ("..." if len(s) > limit else "")


def _run_setup(case, sdir):
    if case.setup:
        subprocess.run(
            [setup_bash(), "-c", case.setup],
            cwd=sdir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=SIDE_TIMEOUT,
        )


def run_win_side(side, case):
    """Run a case under the Rust driver or the C++ dispatcher (native spawn).

    Mirrors run-gate.sh's windows sides: setup in scratch dir, stdin from
    the case table, K=V assignments in the child env, 10s timeout.
    """
    exe = rust_bin_for(case.cmd) if side == "rust" else cpp_bin()
    if not os.path.isfile(exe):
        return SideResult(None, b"", b"", "missing exe: " + exe)
    sdir = os.path.join(QC_ART, "scratch", side, case.cmd, case.cid)
    os.makedirs(sdir, exist_ok=True)
    try:
        _run_setup(case, sdir)
    except subprocess.TimeoutExpired:
        return SideResult(None, b"", b"", "setup timeout")
    env = dict(os.environ)
    env.update(case.env)
    try:
        p = subprocess.run(
            [exe, case.cmd] + case.args,
            cwd=sdir,
            input=case.stdin.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=SIDE_TIMEOUT,
            env=env,
        )
        return SideResult(p.returncode, p.stdout, p.stderr)
    except subprocess.TimeoutExpired:
        return SideResult(None, b"", b"", "exec timeout")


def run_gnu_side(case):
    """Run a case under WSL GNU coreutils (native ext4 scratch).

    Same script shape as run-gate.sh's GNU side: `timeout 10 env K=V cmd
    args < .stdin > .out 2> .err`, artifacts copied back to the mirror.
    """
    gdir = GNU_REMOTE_ROOT + "/" + case.cmd + "/" + case.cid
    mirror = os.path.join(GNU_MIRROR, case.cmd, case.cid)
    os.makedirs(mirror, exist_ok=True)
    scase = os.path.join(QC_ART, "scratch", "gnu", case.cmd, case.cid)
    os.makedirs(scase, exist_ok=True)
    script = os.path.join(scase, "gnu.sh")
    with open(script, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("#!/bin/bash\n")
        fh.write("rm -rf %s; mkdir -p %s || exit 97\n" % (qsh(gdir), qsh(gdir)))
        fh.write("cd %s || exit 97\n" % qsh(gdir))
        if case.setup:
            fh.write(case.setup + "\n")
        fh.write("printf %%s %s > .stdin\n" % qsh(case.stdin))
        fh.write("timeout %d env" % SIDE_TIMEOUT)
        for k, v in case.env.items():
            fh.write(" %s" % qsh(k + "=" + v))
        fh.write(" %s" % case.cmd)
        for a in case.args:
            fh.write(" %s" % qsh(a))
        fh.write(" < .stdin > .out 2> .err\n")
        fh.write("echo $? > .rc\n")
        fh.write("cp .out .err .rc %s%s/%s/%s/\n"
                 % (tree_mnt(), GNU_MIRROR.replace("\\", "/")[len(TREE_DIR.replace("\\", "/")):],
                    case.cmd, case.cid))
    mnt = tree_mnt()
    script_fwd = script.replace("\\", "/")
    tree_fwd = TREE_DIR.replace("\\", "/")
    script_mnt = (
        mnt + script_fwd[len(tree_fwd):] if script_fwd.startswith(tree_fwd) else script_fwd
    )
    subprocess.run(
        ["wsl", "bash", script_mnt],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=SIDE_TIMEOUT * 3,
        env={"MSYS_NO_PATHCONV": "1", "SystemRoot": os.environ.get("SystemRoot", "")},
    )
    # wsl.exe can return before the artifact copy settles (run-gate.sh:150)
    rc_p = os.path.join(mirror, ".rc")
    for _ in range(15):
        if os.path.isfile(rc_p):
            break
        time.sleep(0.2)
    if not os.path.isfile(rc_p):
        return SideResult(None, b"", b"", "gnu artifacts missing")
    with open(rc_p, "rb") as fh:
        rc = int(fh.read().strip() or -1)
    out = _read_opt(os.path.join(mirror, ".out"))
    err = _read_opt(os.path.join(mirror, ".err"))
    return SideResult(rc, out, err)


def _read_opt(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError:
        return b""


def run_side(side, case):
    if side in ("rust", "cpp"):
        return run_win_side(side, case)
    if side == "gnu":
        return run_gnu_side(case)
    raise ValueError("unknown side " + side)


def provenance():
    """One-line disclosure of which binaries this QC run used."""
    r = rust_bin()
    try:
        rw2 = rust_bin_for("uname")
    except SystemExit:
        rw2 = "(RUST_BIN_W2 unset)"
    return (
        "drivers: rust(main)=%s; rust(wave2)=%s; cpp=%s; bash(setup)=%s; "
        "gnu=WSL /usr/bin coreutils"
        % (r, rw2, cpp_bin(), setup_bash())
    )
