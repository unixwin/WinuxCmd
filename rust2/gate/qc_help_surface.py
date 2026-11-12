#!/usr/bin/env python3
"""gate/qc_help_surface.py — help/version/invalid-arg surface sweep (#1488 QC round 2).

The #1550 pilot bug (`false --help` returning 0) and the #1563 class (13
commands printing NOTHING for --help/--version) were both found by sweeping
every implemented command's help surface against GNU. This tool makes that
sweep standing instead of ad hoc:

For every command in gen-cases collect() (i.e. every implemented command the
gate covers) and every probe in [--help, --version, -h, --bogus]:

  gnu  : WSL `timeout 5 env <cmd> <probe>` (batched, one script per command,
         native ext4 scratch under /tmp/winuxcmd-gate-qc-help)
  rust : the main-built winuxcmd-driver.exe (native spawn, cwd = scratch)

Classifications:
  ALIGNED      stdout bytes AND rc equal GNU.
  RC-ONLY      stdout bytes equal, rc differs.
  EMPTY-HELP   GNU prints text on stdout, rust prints nothing (#1563 class).
  TEXT-DIFF    both print, bytes differ (wording/brand surface).
  BOTH-EMPTY   neither prints (recorded; only interesting with rc mismatch).
  PROBE-FAIL   harness error (timeout, missing exe).

Run:  python gate/qc_help_surface.py   # writes gate/HELP-SURFACE.md
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qc_common as q  # noqa: E402
import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location(
    "gen_cases", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "gen-cases.py"))
gen_cases = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen_cases)  # implemented-command list = collect()

OUT = os.path.join(q.GATE_DIR, "HELP-SURFACE.md")
PROBES = ["--help", "--version", "-h", "--bogus"]
GNU_REMOTE_ROOT = "/tmp/winuxcmd-gate-qc-help"
GNU_MIRROR = os.path.join(q.QC_ART, "help-gnu")
TIMEOUT = 5


def qsh(s):
    return "'" + s.replace("'", "'\\''") + "'"


def gnu_batch(cmd):
    """Run all probes for one command in a single WSL script; mirror back."""
    cdir = GNU_REMOTE_ROOT + "/" + cmd
    mirror = os.path.join(GNU_MIRROR, cmd)
    os.makedirs(mirror, exist_ok=True)
    scase = os.path.join(q.QC_ART, "scratch", "help-gnu", cmd)
    os.makedirs(scase, exist_ok=True)
    script = os.path.join(scase, "gnu.sh")
    with open(script, "w", encoding="utf-8", newline="\n") as fh:
        w = fh.write
        w("#!/bin/bash\n")
        w("rm -rf %s; mkdir -p %s || exit 97\ncd %s || exit 97\n"
          % (qsh(cdir), qsh(cdir), qsh(cdir)))
        for i, probe in enumerate(PROBES):
            w("timeout %d env %s %s > .out%d 2> .err%d; echo $? > .rc%d\n"
              % (TIMEOUT, qsh(cmd), qsh(probe), i, i, i))
        w("cp .out* .err* .rc* %s/%s/\n" % (
            qsh(q.to_mnt(GNU_MIRROR)),
            cmd))
    subprocess.run(["wsl", "bash", q.to_mnt(script)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=60,
                   env={"MSYS_NO_PATHCONV": "1",
                        "SystemRoot": os.environ.get("SystemRoot", "")})
    # wsl.exe can return before the artifact copy settles
    import time
    for _ in range(25):
        if os.path.isfile(os.path.join(mirror, ".rc0")):
            break
        time.sleep(0.2)
    return mirror


def main():
    cmds = [cmd for cmd, _ in gen_cases.collect()]
    rows = []
    findings = 0
    for cmd in cmds:
        mirror = gnu_batch(cmd)
        for i, probe in enumerate(PROBES):
            g_out = _read(mirror, ".out%d" % i)
            g_rc = _read(mirror, ".rc%d" % i)
            sdir = os.path.join(q.QC_ART, "scratch", "help-rust", cmd)
            os.makedirs(sdir, exist_ok=True)
            exe = q.rust_bin_for(cmd)
            if not os.path.isfile(exe):
                rows.append((cmd, probe, "PROBE-FAIL", "missing exe: " + exe))
                findings += 1
                continue
            try:
                p = subprocess.run([exe, cmd, probe], cwd=sdir,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   timeout=q.SIDE_TIMEOUT)
                r_rc, r_out = str(p.returncode), p.stdout
            except subprocess.TimeoutExpired:
                r_rc, r_out = "TIMEOUT", b""
            rows.append((cmd, probe) + _classify(g_rc, g_out, r_rc, r_out))
            if rows[-1][2] in ("EMPTY-HELP", "RC-ONLY"):
                findings += 1
        print("swept %-12s (gnu rc h/v/-h/bogus = %s/%s/%s/%s)" % (
            cmd, _read(mirror, ".rc0"), _read(mirror, ".rc1"),
            _read(mirror, ".rc2"), _read(mirror, ".rc3")))

    _write_report(cmds, rows)
    print("\nwrote %s" % OUT)
    print("commands: %d, probes: %d, findings (EMPTY-HELP/RC-ONLY): %d"
          % (len(cmds), len(rows), findings))
    return 0


def _read(mirror, name):
    path = os.path.join(mirror, name)
    if not os.path.isfile(path):
        return "MISSING"
    with open(path, "rb") as fh:
        data = fh.read()
    if name.startswith(".rc"):
        return data.decode("ascii", "replace").strip() or "MISSING"
    return data


def _classify(g_rc, g_out, r_rc, r_out):
    if g_rc in ("MISSING", "TIMEOUT") or g_rc == "124":
        return ("PROBE-FAIL", "gnu rc=%s" % g_rc, "")
    if g_out == r_out and g_rc == r_rc:
        return ("ALIGNED", "", "")
    if g_out and not r_out:
        return ("EMPTY-HELP", "gnu rc=%s rust rc=%s" % (g_rc, r_rc),
                "gnu out1=%r" % g_out[:50])
    if g_out == r_out:
        return ("RC-ONLY", "gnu rc=%s rust rc=%s" % (g_rc, r_rc), "")
    if not g_out and not r_out:
        return ("BOTH-EMPTY", "gnu rc=%s rust rc=%s" % (g_rc, r_rc), "")
    return ("TEXT-DIFF", "gnu rc=%s rust rc=%s" % (g_rc, r_rc), "")


def _write_report(cmds, rows):
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        w = fh.write
        w("# Help-surface sweep — HELP-SURFACE.md\n\n")
        w("Every implemented command (the %d gen-cases collect() tables) x\n"
          % len(cmds))
        w("`--help` / `--version` / `-h` / `--bogus`, rust2 driver vs GNU\n")
        w("coreutils 9.4 (WSL oracle, batched per-command scratch\n")
        w("`/tmp/winuxcmd-gate-qc-help`). Generated by\n")
        w("`python gate/qc_help_surface.py`.\n\n")
        w("Driver provenance: `%s`\n\n" % q.provenance())
        w("The #1563 class (13 commands with empty --help/--version) and the\n")
        w("#1550 pilot bug (`false --help` rc 0) are the failure modes this\n")
        w("sweep exists to catch: EMPTY-HELP is the empty-output regression,\n")
        w("RC-ONLY is a silent status flip on the help/usage path.\n\n")
        w("## Findings\n\n")
        w("| command | probe | verdict | detail |\n")
        w("|---|---|---|---|\n")
        bad = 0
        for cmd, probe, verdict, detail, ev in rows:
            if verdict in ("ALIGNED", "TEXT-DIFF", "BOTH-EMPTY"):
                continue
            w("| %s | `%s` | %s | %s %s |\n" % (cmd, probe, verdict, detail, ev))
            bad += 1
        if not bad:
            w("- none\n")
        w("\n## Full matrix (verdicts; TEXT-DIFF/BOTH-EMPTY are recorded surfaces)\n\n")
        w("| command | `--help` | `--version` | `-h` | `--bogus` |\n")
        w("|---|---|---|---|---|\n")
        by_cmd = {}
        for cmd, probe, verdict, detail, ev in rows:
            by_cmd.setdefault(cmd, {})[probe] = (verdict, detail)
        for cmd in cmds:
            cells = []
            for probe in PROBES:
                v, d = by_cmd[cmd][probe]
                cells.append("%s%s" % (v, (" (%s)" % d) if v == "RC-ONLY" else ""))
            w("| %s | %s |\n" % (cmd, " | ".join(cells)))
        w("\nTEXT-DIFF rows are expected where the product surfaces its own\n")
        w("branded help text on commands the gate deliberately leaves win\n")
        w("(true/false precedent); EMPTY-HELP and RC-ONLY are the gap classes.\n")


if __name__ == "__main__":
    sys.exit(main())
