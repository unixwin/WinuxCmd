#!/usr/bin/env python3
"""ConPTY probe for nohup's terminal faces (rust2 StdioMeta lane).

The differential gate harness (run-gate.sh) always redirects the three
streams, so no tty exists in-harness — this probe allocates a REAL
Windows pseudo-console (pywinpty PtyProcess) and runs winuxcmd-driver.exe
nohup inside it, once per face combination. The driver's stdio_meta()
then reports console facts and the rust2 body applies the GNU nohup.c
redirect rules for real.

Scenarios (cwd is a fresh scratch dir per run):

  all-console     stdin/stdout/stderr all console
                  -> "ignoring input and appending output to 'nohup.out'"
                  -> child stdout+stderr bytes land in ./nohup.out
  stdout-file     `> captured.txt` on the console cmd line
                  -> "ignoring input and redirecting stderr to stdout"
                  -> child stdout+stderr bytes land in captured.txt
  stdin-file      `< in.txt` on the console cmd line
                  -> "appending output to 'nohup.out'"
                  -> child stdout+stderr bytes land in ./nohup.out
  outputs-file    `> o.txt 2> e.txt` on the console cmd line
                  -> "ignoring input" on the console, files keep the
                     plain child split (stdin face only)

Requires: pywinpty (evidence base 3.0.5), the wired winuxcmd-driver.exe,
cmd.exe on PATH. Usage:

  python gate/conpty-nohup.py --driver target/debug/winuxcmd-driver.exe \
      --out <evidence-dir>

Exit 0 when every scenario matches the GNU shape; the evidence dir keeps
per-scenario console captures, the redirected files and a summary.json.
"""
import argparse
import json
import os
import sys
import time

try:
    from winpty import PtyProcess
except Exception as exc:  # pragma: no cover - environment gate
    print(f"FATAL: pywinpty is required: {exc}", file=sys.stderr)
    sys.exit(2)

# child.bat prints one line to each stream; the faces decide where each
# lands. & echo ERR joined so the redirect applies to the second echo.
CHILD_BAT = "@echo OUT\r\n@echo ERR >&2\r\n"
READ_SPAN = 0.4
DEADLINE_S = 20.0


def read_console(proc, deadline=DEADLINE_S):
    """Drain the console stream until the process exits (bounded)."""
    out = ""
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        try:
            chunk = proc.read(4096)
        except (EOFError, OSError):
            break
        except Exception:
            time.sleep(0.05)
            continue
        if chunk:
            out += chunk
        if not proc.isalive():
            # let the peer push the remaining bytes
            time.sleep(READ_SPAN)
            try:
                while True:
                    chunk = proc.read(4096)
                    if not chunk:
                        break
                    out += chunk
            except Exception:
                pass
            break
    return out


def run_scenario(workdir, argv, env):
    """Spawn argv under ConPTY with cwd=workdir; return console text."""
    proc = PtyProcess.spawn(argv, cwd=workdir, env=env, dimensions=(50, 200))
    try:
        text = read_console(proc)
    finally:
        try:
            proc.terminate(force=True)
        except Exception:
            pass
    return text


def scenario_line(driver, name, workdir, env):
    """One face combination; returns the evidence dict."""
    os.makedirs(workdir, exist_ok=True)
    child = os.path.join(workdir, "child.bat")
    with open(child, "w", newline="") as fh:
        fh.write(CHILD_BAT)

    if name == "all-console":
        argv = [driver, "nohup", "cmd", "/d", "/s", "/c", "child.bat"]
    elif name == "stdout-file":
        # stdout redirected to a file; stdin/stderr stay console.
        argv = ["cmd", "/d", "/s", "/c",
                "%s nohup cmd /d /s /c child.bat > captured.txt" % driver]
    elif name == "stdin-file":
        with open(os.path.join(workdir, "in.txt"), "w", newline="") as fh:
            fh.write("typed line\n")
        argv = ["cmd", "/d", "/s", "/c",
                "%s nohup cmd /d /s /c child.bat < in.txt" % driver]
    elif name == "outputs-file":
        argv = ["cmd", "/d", "/s", "/c",
                "%s nohup cmd /d /s /c child.bat > o.txt 2> e.txt" % driver]
    else:
        raise ValueError(name)

    console = run_scenario(workdir, argv, env)
    files = {}
    for fname in ("nohup.out", "captured.txt", "o.txt", "e.txt"):
        path = os.path.join(workdir, fname)
        if os.path.exists(path):
            with open(path, "rb") as fh:
                files[fname] = fh.read().decode("utf-8", "replace")
        else:
            files[fname] = None
    return {"console": console, "files": files}


# GNU nohup.c expected shapes per scenario: console notice, which files
# hold which child bytes, and whether nohup.out must exist.
EXPECT = {
    "all-console": {
        "notice": "ignoring input and appending output to 'nohup.out'",
        "child_on_console": False,
        "want": {"nohup.out": ("OUT", "ERR")},
        "absent": (),
    },
    "stdout-file": {
        "notice": "ignoring input and redirecting stderr to stdout",
        "child_on_console": False,
        "want": {"captured.txt": ("OUT", "ERR")},
        "absent": ("nohup.out",),
    },
    "stdin-file": {
        "notice": "appending output to 'nohup.out'",
        "child_on_console": False,
        "want": {"nohup.out": ("OUT", "ERR")},
        "absent": (),
    },
    "outputs-file": {
        # nohup's own notice is a stderr write; with 2> e.txt it lands
        # in e.txt, ahead of the child's ERR line.
        "notice": "ignoring input",
        "notice_in": "e.txt",
        "child_on_console": False,
        "want": {"o.txt": ("OUT",), "e.txt": ("ERR",)},
        "absent": ("nohup.out",),
    },
}


def judge(name, got):
    exp = EXPECT[name]
    checks = []
    console = got["console"]
    if "notice_in" in exp:
        body = got["files"].get(exp["notice_in"]) or ""
        checks.append(("notice", exp["notice"] in body))
        checks.append(("no-ignoring-input-when-unexpected",
                       "ignoring input and" not in body))
    else:
        checks.append(("notice", exp["notice"] in console))
    # the exact GNU wording must not come with an extra prefix
    checks.append(("no-ignoring-input-when-unexpected",
                   exp["notice"] != "ignoring input"
                   or "ignoring input and" not in console))
    if not exp["child_on_console"]:
        # the child must NOT leak onto the console (its bytes went to the
        # redirected destination); the cmd /c echo of nothing else helps.
        checks.append(("child-off-console",
                       "OUT" not in console.replace("o.txt", "")
                       .replace("child.bat", "")))
    for fname, needles in exp["want"].items():
        body = got["files"].get(fname)
        checks.append((f"{fname}-exists", body is not None))
        if body is not None:
            for n in needles:
                checks.append((f"{fname}-has-{n}", n in body))
    for fname in exp["absent"]:
        checks.append((f"{fname}-absent", got["files"].get(fname) is None))
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", required=True,
                        help="winuxcmd-driver.exe path")
    parser.add_argument("--out", required=True, help="evidence directory")
    args = parser.parse_args()

    driver = os.path.abspath(args.driver)
    if not os.path.exists(driver):
        print(f"FATAL: driver not found: {driver}", file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)

    env = dict(os.environ)
    # A clean HOME keeps the two-step nohup.out fallback deterministic:
    # HOME points into the evidence dir, so a local-open failure would
    # land there (and be visible), never in the operator's profile.
    home = os.path.join(args.out, "home")
    os.makedirs(home, exist_ok=True)
    env["HOME"] = home

    summary = {"driver": driver, "scenarios": {}}
    ok_all = True
    for name in ("all-console", "stdout-file", "stdin-file", "outputs-file"):
        workdir = os.path.join(args.out, name)
        got = scenario_line(driver, name, workdir, env)
        checks = judge(name, got)
        passed = all(ok for _, ok in checks)
        ok_all = ok_all and passed
        summary["scenarios"][name] = {
            "pass": passed,
            "checks": [{"name": n, "ok": ok} for n, ok in checks],
            "console": got["console"],
            "files": got["files"],
        }
        with open(os.path.join(workdir, "console.txt"), "w",
                  encoding="utf-8", errors="replace") as fh:
            fh.write(got["console"])
        print(f"[{'PASS' if passed else 'FAIL'}] {name}")
        for n, ok in checks:
            if not ok:
                print(f"        missing: {n}")

    with open(os.path.join(args.out, "summary.json"), "w",
              encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
