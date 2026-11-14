#!/usr/bin/env python3
"""ConPTY driver for unixwin/WinuxCmd#1487 gate ③ (niubash#727 gate 3).

Adapted from the 2026-11-01 evidence driver
`D:\\repo\\nbbuild\\evidence\\727\\m3\\gate3_final.py` + `pty_shim.py`
(pywinpty 3.0.5).  Changes: parameters instead of hard-coded paths, no
unbounded console materialization, a JSON summary, and an optional flood
phase that counts bytes instead of printing them.

Requires: pywinpty 3.0.5, a 1.4.x niu.exe with the FFI route module, and a
winuxcore.dll ABI v2 FFI tree installed under the product root.

The console stream is never echoed.  Only a bounded tail is written to
gate3-console.log; the full byte count is recorded in the summary.
"""
import argparse
import json
import os
import queue
import re
import sys
import threading
import time

try:
    from winpty import PtyProcess
except Exception as exc:  # pragma: no cover - environment gate
    print(f"FATAL: pywinpty is required: {exc}", file=sys.stderr)
    sys.exit(2)

TAIL_LIMIT = 256 * 1024


class Session:
    def __init__(self, exe, cwd, env_extra, dimensions=(50, 200)):
        full = {**os.environ, **env_extra}
        for key in ("BASH_ENV", "WINUXSH_ROOT", "TERM", "WINUXCMD_PATH",
                    "NIU_ROOT", "WINUXCMD_HOME"):
            full.pop(key, None)
        self.proc = PtyProcess.spawn(exe, cwd=cwd, env=full, dimensions=dimensions)
        self.queue = queue.Queue()
        self.alive = True
        self._tail = bytearray()
        self.total_bytes = 0
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self):
        while True:
            try:
                chunk = self.proc.read(4096)
            except (EOFError, OSError):
                self.queue.put(None)
                self.alive = False
                return
            except Exception:
                time.sleep(0.05)
                continue
            if not chunk:
                continue
            self.queue.put(chunk)

    def _absorb(self, data, tail_only_env):
        raw = data.encode("utf-8", "replace")
        self.total_bytes += len(raw)
        self._tail.extend(raw)
        if len(self._tail) > TAIL_LIMIT:
            del self._tail[:-TAIL_LIMIT]
        if tail_only_env:
            # Flood phase: never keep the whole stream in a Python string.
            return data[-8192:] if len(data) > 8192 else data
        return data

    def drain(self, span=0.4, tail_only=False):
        out = ""
        deadline = time.monotonic() + span
        while time.monotonic() < deadline:
            try:
                chunk = self.queue.get(timeout=0.05)
            except queue.Empty:
                continue
            if chunk is None:
                break
            out += self._absorb(chunk, tail_only)
            if len(out) > TAIL_LIMIT:
                out = out[-TAIL_LIMIT:]
        return out

    def read_until(self, markers, timeout, tail_only=False):
        buf = ""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = self.queue.get(timeout=max(0.02, deadline - time.monotonic()))
            except queue.Empty:
                continue
            if chunk is None:
                return buf, None
            buf += self._absorb(chunk, tail_only)
            if len(buf) > TAIL_LIMIT:
                buf = buf[-TAIL_LIMIT:]
            for marker in markers:
                if marker in buf:
                    return buf, marker
        return buf, None

    def write(self, text):
        if not self.proc.isalive():
            raise EOFError("pty closed")
        self.proc.write(text)

    def isalive(self):
        return self.proc.isalive()

    def tail_text(self):
        return self._tail.decode("utf-8", "replace")

    def close(self):
        try:
            self.proc.terminate(force=True)
        except Exception:
            pass


def token_re(tag):
    return re.compile(re.escape(tag) + r"(\d+)")


def parse_token(text, tag):
    match = token_re(tag).search(text)
    return int(match.group(1)) if match else None


def send_command(session, line, token, timeout, retries=2, tail_only=False):
    """Send `line`, wait for `token`. Retries only the token echo, never the
    command (commands here have side effects)."""
    session.drain(0.3, tail_only)
    start = time.monotonic()
    session.write(line + "\r")
    out, hit = session.read_until([token], timeout, tail_only)
    if hit:
        return out, time.monotonic() - start
    for _ in range(retries):
        # The engine can discard a typed line while a foreground applet is
        # still running; re-send only a harmless echo probe.
        session.write("echo " + token.replace("-", "-''") + "$?\r")
        out2, hit = session.read_until([token], max(5.0, timeout / 4), tail_only)
        out += out2
        if hit:
            return out, time.monotonic() - start
    return out, time.monotonic() - start


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--niu", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--big", required=True)
    parser.add_argument("--cancel-after", type=float, default=3.0)
    parser.add_argument("--timeout", type=float, default=240.0)
    parser.add_argument("--flood", action="store_true")
    parser.add_argument("--flood-timeout", type=float, default=300.0)
    parser.add_argument("--expect", choices=("broken", "fixed"), default="broken")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    home = os.path.join(args.out, "home")
    os.makedirs(home, exist_ok=True)
    for name in ("sB.tmp", "sC.tmp", "sD.tmp"):
        try:
            os.remove(os.path.join(args.work, name))
        except OSError:
            pass

    summary = {
        "gate": "727 gate 3 (product-path Ctrl-C)",
        "niu": args.niu,
        "expect": args.expect,
        "big": args.big,
        "big_bytes": os.path.getsize(args.big) if os.path.exists(args.big) else None,
        "warm_rc": None,
        "b_dur": None,
        "b_rc": None,
        "c_dur": None,
        "c_rc": None,
        "c_token_lat": None,
        "c_file_done_lat": None,
        "c_early": False,
        "alive": None,
        "wc_lines": None,
        "wc_rc": None,
        "d_rc": None,
        "cmp1": None,
        "cmp2": None,
        "exit_alive": None,
        "flood": None,
    }
    timeline = []
    t0 = time.monotonic()

    def log(message):
        line = "[%8.3fs] %s" % (time.monotonic() - t0, message)
        timeline.append(line)

    session = Session(args.niu, args.work, {
        "NIU_WINUXCMD_FFI": "1",
        "NIU_WINUXCMD_FFI_LOG": "1",
        "NIU_COMPLETION_STYLE": "gnu",
        "HOME": home,
    })
    try:
        log("spawn interactive niu (FFI=1 LOG=1)")
        session.drain(5.0)

        out, _ = send_command(session, "echo G3-''WARM-$?", "G3-WARM-", 30)
        summary["warm_rc"] = parse_token(out, "G3-WARM-")
        if summary["warm_rc"] is None:
            log("prompt not detected; tail=%r" % out[-400:])
            raise RuntimeError("prompt-fail")
        log("warm rc=%s" % summary["warm_rc"])

        b0 = time.monotonic()
        out, _ = send_command(session, "sort -o sB.tmp big.txt; echo G3-''B-$?",
                              "G3-B-", args.timeout)
        summary["b_dur"] = time.monotonic() - b0
        summary["b_rc"] = parse_token(out, "G3-B-")
        log("baseline %.2fs rc=%s" % (summary["b_dur"], summary["b_rc"]))

        # Cancel case: start the long sort, inject a real ConPTY ^C, then
        # measure how long until the shell answers a queued echo probe.
        c0 = time.monotonic()
        session.drain(0.4)
        session.write("sort -o sC.tmp big.txt\r")
        time.sleep(args.cancel_after)
        session.write("\x03")
        inj = time.monotonic()
        # Queue a probe immediately; the pre-fix engine discards typed input
        # while dispatch runs, so send_command may re-send the probe only.
        probe, _ = send_command(session, "echo G3-''C-$?", "G3-C-", args.timeout,
                                retries=6)
        summary["c_rc"] = parse_token(probe, "G3-C-")
        # Same probe as the 2026-11-01 run's ALIVE assertion: the shell
        # answered after ^C, so it survived (130 when the engine reports a
        # cancelled call; None when the typed probe was discarded).
        summary["alive"] = summary["c_rc"]
        summary["c_token_lat"] = time.monotonic() - inj
        summary["c_dur"] = time.monotonic() - c0

        target = 36000000
        sc = os.path.join(args.work, "sC.tmp")
        done_at = None
        poll_deadline = time.monotonic() + args.timeout
        while time.monotonic() < poll_deadline:
            try:
                size = os.path.getsize(sc)
            except OSError:
                size = -1
            if size == target:
                done_at = time.monotonic()
                break
            if summary["c_token_lat"] is not None and time.monotonic() - inj > summary["c_token_lat"]:
                break
            time.sleep(0.5)
        summary["c_file_done_lat"] = (done_at - inj) if done_at else None
        summary["c_early"] = (summary["c_token_lat"] is not None and
                              summary["c_token_lat"] < 5.0)
        log("cancel token +%.2fs (file %+.2fs), rc=%s, early=%s" % (
            summary["c_token_lat"] or -1,
            (summary["c_file_done_lat"] if summary["c_file_done_lat"] is not None else -1),
            summary["c_rc"], summary["c_early"]))

        out, _ = send_command(session, "wc -l big.txt; echo G3-''WC-$?",
                              "G3-WC-", 120)
        summary["wc_rc"] = parse_token(out, "G3-WC-")
        match = re.search(r"(\d[\d,]*)\s+big\.txt", out)
        summary["wc_lines"] = match.group(1).replace(",", "") if match else None
        log("wc lines=%s rc=%s" % (summary["wc_lines"], summary["wc_rc"]))

        out, _ = send_command(session, "sort -o sD.tmp big.txt; echo G3-''D-$?",
                              "G3-D-", args.timeout)
        summary["d_rc"] = parse_token(out, "G3-D-")
        out, _ = send_command(
            session,
            "cmp -s sB.tmp sC.tmp; echo G3-''C1-$?; cmp -s sB.tmp sD.tmp; echo G3-''C2-$?",
            "G3-C2-", 60)
        summary["cmp1"] = parse_token(out, "G3-C1-")
        summary["cmp2"] = parse_token(out, "G3-C2-")
        log("re-sort rc=%s cmp B/C=%s B/D=%s" % (
            summary["d_rc"], summary["cmp1"], summary["cmp2"]))

        if args.flood:
            # Original m3 flood phase: same command twice; the second one
            # leaks the 36 MB to the console and ^C cannot stop it quickly.
            flood = {}
            t = time.monotonic()
            out, _ = send_command(session, "sort big.txt > /dev/null; echo G3-''FB-$?",
                                  "G3-FB-", args.flood_timeout, tail_only=True)
            flood["baseline_s"] = time.monotonic() - t
            flood["baseline_rc"] = parse_token(out, "G3-FB-")
            flood["baseline_bytes"] = session.total_bytes
            log("flood baseline %.2fs rc=%s" % (flood["baseline_s"], flood["baseline_rc"]))

            before = session.total_bytes
            f0 = time.monotonic()
            session.write("sort big.txt > /dev/null\r")
            time.sleep(args.cancel_after)
            inj2 = time.monotonic()
            session.write("\x03")
            out, _ = send_command(session, "echo G3-''FC-$?", "G3-FC-",
                                  args.flood_timeout, retries=12, tail_only=True)
            flood["cancel_rc"] = parse_token(out, "G3-FC-")
            flood["cancel_to_prompt_s"] = time.monotonic() - inj2
            flood["cancel_bytes"] = session.total_bytes - before
            flood["cancel_dur_s"] = time.monotonic() - f0
            summary["flood"] = flood
            log("flood cancel prompt +%.2fs bytes=%d" % (
                flood["cancel_to_prompt_s"], flood["cancel_bytes"]))

        session.write("exit\r")
        time.sleep(1.0)
        summary["exit_alive"] = session.isalive()
        log("after exit alive=%s total_console_bytes=%d" % (
            summary["exit_alive"], session.total_bytes))
    except Exception as exc:  # keep the partial evidence
        summary["error"] = f"{type(exc).__name__}: {exc}"
        log("ERROR %s" % summary["error"])
    finally:
        summary["console_total_bytes"] = session.total_bytes
        session.close()

    with open(os.path.join(args.out, "gate3-console.log"), "w", encoding="utf-8",
              errors="replace") as handle:
        handle.write("".join(line + "\n" for line in timeline))
        handle.write("\n--- console tail (last 256 KiB, control bytes kept) ---\n")
        handle.write(session.tail_text())
    with open(os.path.join(args.out, "gate3-summary.json"), "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    print(json.dumps({k: summary[k] for k in (
        "warm_rc", "b_dur", "b_rc", "c_dur", "c_rc", "c_token_lat",
        "c_file_done_lat", "c_early", "alive", "wc_lines", "wc_rc", "d_rc",
        "cmp1", "cmp2", "exit_alive", "error") if k in summary}, indent=2))
    return 0 if "error" not in summary else 1


if __name__ == "__main__":
    sys.exit(main())
