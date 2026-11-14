#!/usr/bin/env python3
"""gate/matrix-summary.py — merge the rust2-gate matrix shards into a dashboard.

The rust2-gate CI job is a matrix: every shard runs
`bash rust2/gate/run-gate.sh <commands>` into its own `GATE_ARTIFACT_DIR`
and uploads that directory as `rust2-gate-<shard>`. This script consumes
the downloaded artifacts (one subdirectory per artifact, as
actions/download-artifact@v8 lays them out) and emits a single markdown
dashboard for the run's step summary plus a machine-readable totals line.

The verdict is recomputed from the ledger's own recorded comparisons with
the same rules as run-gate.sh / gen-cases._ledger_verdict — the ledger has
no verdict column on purpose, so a stale summary can never disagree with
the raw evidence.

Guards (exit 1):
  * a shard artifact has no ledger at all (silently missing coverage);
  * the matrix stopped covering a case table (new table landed, shard list
    not updated);
  * any shard reports a FAIL row;
  * any shard reports missing-or-invalid-gnu-status (the oracle leg never
    came up — the #1632 vacuous-green class).

Usage: matrix-summary.py <artifacts-dir> [--out FILE]
"""

import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_ledger(path):
    rows = []
    if not os.path.isfile(path):
        return rows
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line:
                continue
            f = line.split("|")
            if len(f) < 6:
                continue
            rcs = f[3].split("=", 1)[-1].split("/")
            out = f[4]
            rows.append({
                "cmd": f[0],
                "id": f[1],
                "mode": f[2],
                "r_rc": rcs[0] if len(rcs) > 0 else "?",
                "g_rc": rcs[1] if len(rcs) > 1 else "?",
                "c_rc": rcs[2] if len(rcs) > 2 else "?",
                "g_out_eq": "r=g:yes" in out,
                "c_eq": "r=c:yes" in out,
                "note": f[5],
            })
    return rows


def verdict(row):
    """ok / fail / pending / record / skipped for one ledger row."""
    note = row["note"]
    if note.startswith("skipped"):
        return "skipped"
    if "missing-or-invalid-gnu-status" in note:
        return "oracle-missing"
    mode = row["mode"]
    if mode == "note":
        return "record"
    if mode == "strict":
        ok = row["g_out_eq"] and row["r_rc"] == row["g_rc"]
    elif mode == "exit":
        ok = row["r_rc"] == row["g_rc"]
    elif mode == "win":
        ok = row["r_rc"] == row["c_rc"]
    elif mode in ("posix", "posix-exit"):
        ok = row["r_rc"] == row["g_rc"]
        if mode == "posix":
            ok = ok and row["g_out_eq"]
        if not ok and "posix-pending" in note:
            return "pending"
        return "posix-ok" if ok else "fail"
    else:
        return "fail"
    return "ok" if ok else "fail"


def class_of(row):
    mode = row["mode"]
    if mode == "win":
        return "win"
    if mode in ("posix", "posix-exit"):
        return "posix"
    if mode == "note":
        return "record"
    return "gnu"


def table_stems(subdir):
    names = set()
    for path in glob.glob(os.path.join(HERE, subdir, "*.tsv")):
        names.add(os.path.basename(path)[:-4])
    return names


def read_timing(path):
    """command<TAB>rows<TAB>elapsed_ms; returns (rows, ms)."""
    rows = 0
    ms = 0
    if not os.path.isfile(path):
        return rows, ms
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            f = line.split("\t")
            if len(f) < 3:
                continue
            try:
                rows += int(f[1])
                ms += int(f[2])
            except ValueError:
                continue
    return rows, ms


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: matrix-summary.py <artifacts-dir> [--out FILE]\n")
        return 2
    root = argv[1]
    out_path = None
    if "--out" in argv:
        out_path = argv[argv.index("--out") + 1]

    shards = []
    for entry in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        sdir = os.path.join(root, entry)
        if not os.path.isdir(sdir):
            continue
        ledger = None
        for cand in ("ledger.tsv", "ledger-posix.tsv"):
            p = os.path.join(sdir, cand)
            if os.path.isfile(p):
                ledger = p
                break
        if ledger is None:
            shards.append({"name": entry, "missing": True})
            continue
        rows = load_ledger(ledger)
        counts = {}
        for r in rows:
            key = class_of(r) + ":" + verdict(r)
            counts[key] = counts.get(key, 0) + 1
        trows, ms = read_timing(os.path.join(sdir, "timing.tsv"))
        cap = ""
        cap_path = os.path.join(sdir, "symlink-capability.txt")
        if os.path.isfile(cap_path):
            with open(cap_path, "r", encoding="utf-8", errors="replace") as fh:
                cap = "yes" if "yes" in fh.read() else "no"
        shards.append({
            "name": entry, "missing": False, "rows": rows,
            "counts": counts, "cmds": sorted({r["cmd"] for r in rows}),
            "timing_rows": trows, "ms": ms, "symlink": cap,
            "is_posix": os.path.basename(ledger) == "ledger-posix.tsv",
        })

    lines = []
    lines.append("## rust2 differential gate - full corpus")
    lines.append("")

    total = {"gnu": 0, "gnu_fail": 0, "win": 0, "win_fail": 0,
             "posix_ok": 0, "pending": 0, "posix_fail": 0,
             "skipped": 0, "record": 0, "oracle_missing": 0, "rows": 0}
    bad = []
    if not shards:
        bad.append("no shard artifacts were downloaded - every gate job "
                   "failed before writing a ledger")
    lines.append("| shard | tables | rows | GNU pass | GNU fail | win pass | win fail | "
                 "posix ok/pend/fail | skipped | record | wall |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for s in shards:
        if s["missing"]:
            lines.append("| %s | - | - | - | - | - | - | - | - | - | **no ledger** |"
                         % s["name"])
            bad.append("%s: no ledger artifact" % s["name"])
            continue
        c = s["counts"]
        gnu_fail = c.get("gnu:fail", 0) + c.get("record:fail", 0)
        win_fail = c.get("win:fail", 0)
        p_fail = c.get("posix:fail", 0)
        p_ok = c.get("posix:posix-ok", 0)
        p_pend = c.get("posix:pending", 0)
        lines.append(
            "| %s | %d | %d | %d | %d | %d | %d | %d/%d/%d | %d | %d | %ds |"
            % (s["name"], len(s["cmds"]), len(s["rows"]),
               c.get("gnu:ok", 0), gnu_fail,
               c.get("win:ok", 0), win_fail,
               p_ok, p_pend, p_fail,
               c.get("gnu:skipped", 0) + c.get("win:skipped", 0) +
               c.get("posix:skipped", 0) + c.get("record:skipped", 0),
               c.get("record:record", 0),
               (s["ms"] + 999) // 1000))
        total["rows"] += len(s["rows"])
        total["gnu"] += c.get("gnu:ok", 0)
        total["gnu_fail"] += gnu_fail
        total["win"] += c.get("win:ok", 0)
        total["win_fail"] += win_fail
        total["posix_ok"] += p_ok
        total["pending"] += p_pend
        total["posix_fail"] += p_fail
        total["skipped"] += (c.get("gnu:skipped", 0) + c.get("win:skipped", 0) +
                             c.get("posix:skipped", 0) + c.get("record:skipped", 0))
        total["record"] += c.get("record:record", 0)
        total["oracle_missing"] += c.get("gnu:oracle-missing", 0) + \
            c.get("win:oracle-missing", 0) + c.get("posix:oracle-missing", 0)
        if gnu_fail or win_fail or p_fail:
            bad.append("%s: %d gate failure(s)" % (s["name"], gnu_fail + win_fail + p_fail))
    if total["oracle_missing"]:
        bad.append("%d row(s) reported a missing/invalid GNU status - "
                   "the oracle leg did not come up" % total["oracle_missing"])

    lines.append("| **total** | | **%d** | **%d** | **%d** | **%d** | **%d** | "
                 "**%d/%d/%d** | **%d** | **%d** | |"
                 % (total["rows"], total["gnu"], total["gnu_fail"],
                    total["win"], total["win_fail"],
                    total["posix_ok"], total["pending"], total["posix_fail"],
                    total["skipped"], total["record"]))
    lines.append("")

    # ---- coverage guard: every case table must be claimed by a shard ----
    regular = set()
    posix = set()
    for s in shards:
        if s.get("missing"):
            continue
        if s.get("is_posix"):
            posix |= set(s["cmds"])
        else:
            regular |= set(s["cmds"])
    tables = table_stems("cases")
    posix_tables = table_stems("cases-posix")
    uncovered = sorted(tables - regular)
    uncovered_posix = sorted(posix_tables - posix)
    lines.append("tables: %d regular + %d posix-mode" % (len(tables), len(posix_tables)))
    if uncovered:
        lines.append("")
        lines.append("**UNCOVERED case tables (shard list is stale):** " + ", ".join(uncovered))
        bad.append("%d case table(s) not run by any shard: %s"
                   % (len(uncovered), ", ".join(uncovered)))
    if uncovered_posix:
        lines.append("")
        lines.append("**UNCOVERED posix-mode tables:** " + ", ".join(uncovered_posix))
        bad.append("%d posix-mode table(s) not run: %s"
                   % (len(uncovered_posix), ", ".join(uncovered_posix)))
    lines.append("")

    skips = []
    fails = []
    for s in shards:
        if s.get("missing"):
            continue
        for r in s["rows"]:
            v = verdict(r)
            if v == "skipped":
                skips.append("%s/%s (%s)" % (r["cmd"], r["id"], s["name"]))
            elif v in ("fail", "oracle-missing"):
                fails.append("%s/%s(%s) r_rc=%s g_rc=%s c_rc=%s note=%s [%s]"
                             % (r["cmd"], r["id"], r["mode"], r["r_rc"],
                                r["g_rc"], r["c_rc"], r["note"], s["name"]))
    lines.append("FAIL rows (%d): %s" % (len(fails), "none" if not fails else ""))
    for f in fails:
        lines.append("- " + f)
    lines.append("")
    lines.append("SKIPPED rows (%d, host cannot create the symlink fixture): %s"
                 % (len(skips), "none" if not skips else ""))
    for s in skips:
        lines.append("- " + s)
    lines.append("")
    lines.append("oracle: " + ("OK on every shard that produced rows"
                               if not total["oracle_missing"]
                               else "**MISSING on at least one shard**"))
    for s in shards:
        if not s.get("missing") and s.get("symlink"):
            lines.append("symlink capability on %s: %s" % (s["name"], s["symlink"]))

    if bad:
        lines.append("")
        lines.append("## VERDICT: RED")
        for b in bad:
            lines.append("- " + b)
    else:
        lines.append("")
        lines.append("## VERDICT: GREEN")

    md = "\n".join(lines) + "\n"
    sys.stdout.write(md)
    if out_path:
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(md)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
