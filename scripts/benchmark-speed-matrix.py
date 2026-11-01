#!/usr/bin/env python3
"""Speed-matrix benchmark for the #1127 P4 performance round.

Compares the built WinuxCmd dispatcher against the two reference
implementations this project measures itself against — GNU coreutils
(MSYS/Git toolchain) and uutils coreutils — on the hot-path workloads
named by DOCS/performance-roadmap.md: large-file cat (passthrough and
-v transform), cp of a large file, wc -c and full wc, md5/sha256 of a
large file, sort of 1M lines (default and forced-tiny -S), and uniq
over long duplicate runs.

Methodology (deliberately boring, in the spirit of
benchmark-command-parity.py):

* Every workload's output is verified before its timings are trusted:
  stdout bytes (or the cp destination, or the parsed wc tuple) must
  hash identically across every implementation that ran. A workload
  whose outputs disagree is reported as MISMATCH and its ratios are
  flagged, never silently averaged in.
* Timings are medians of repeated runs after one warmup, wall clock,
  stdout redirected to a file (never a pipe buffer in memory).
* Fixtures are deterministic (a fixed-seed LCG), generated once per
  session under --work-dir.

Baseline handling: --record-baseline writes the winux medians plus an
environment block to a JSON file (the checked-in seed lives at
tests/benchmark/baseline.json). --baseline FILE compares a fresh run
against a recorded baseline and prints per-workload deltas. The
comparison is REPORT-ONLY unless --fail-on-regression PCT is passed:
wall-clock medians on shared CI runners swing far more than the
regressions this gate would want to catch, so the hard gate stays off
until the project has a seeded baseline and a variance history (see
tests/benchmark/README.md).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

GNU_SEARCH_ROOTS = [
    Path(r"C:\Program Files\Git\usr\bin"),
    Path(r"C:\msys64\usr\bin"),
    Path(r"C:\cygwin64\bin"),
]

MIB = 1024 * 1024


# ---------------------------------------------------------------------------
# Deterministic fixtures
# ---------------------------------------------------------------------------

class Lcg:
    """Small deterministic PRNG so fixtures are byte-stable across runs."""

    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFFFFFFFFFF

    def next(self) -> int:
        self.state = (self.state * 6364136223846793005 + 1442695040888963407) & (
            (1 << 64) - 1
        )
        return self.state >> 33

    def byte(self) -> int:
        return self.next() & 0xFF


def write_binary_fixture(path: Path, size: int, seed: int) -> None:
    rng = Lcg(seed)
    block = bytes(rng.byte() for _ in range(64 * 1024))
    with path.open("wb") as fh:
        remaining = size
        while remaining > 0:
            chunk = block[: min(remaining, len(block))]
            fh.write(chunk)
            remaining -= len(chunk)


def write_mixed_text_fixture(path: Path, size: int, seed: int) -> None:
    """Text with control bytes, tabs and >= 0x80 bytes sprinkled in, so
    transform paths (cat -v) cannot take a pure-ASCII shortcut on the
    reference side either."""
    rng = Lcg(seed)
    words = [b"alpha", b"beta", b"gamma", b"delta", b"epsilon", b"zeta"]
    specials = [b"\x01", b"\x07", b"\x1b", b"\x7f", b"\xc3\xa9", b"\xe2\x82\xac", b"\t"]
    with path.open("wb") as fh:
        written = 0
        line_no = 0
        while written < size:
            parts = []
            for _ in range(8):
                parts.append(words[rng.next() % len(words)])
                if rng.next() % 23 == 0:
                    parts.append(specials[rng.next() % len(specials)])
            line = b" ".join(parts) + b"\n"
            fh.write(line)
            written += len(line)
            line_no += 1


def write_sort_fixture(path: Path, lines: int, seed: int) -> None:
    rng = Lcg(seed)
    with path.open("wb") as fh:
        for i in range(lines):
            fh.write(f"key{rng.next() % 100000:05d} value-{i:07d} payload\n".encode())


def write_uniq_fixture(path: Path, seed: int) -> None:
    """Long duplicate runs (the streaming-window stress shape) mixed
    with unique lines."""
    rng = Lcg(seed)
    with path.open("wb") as fh:
        for group in range(2000):
            line = f"group-{group:05d} shared line\n".encode()
            fh.write(line * (1 + rng.next() % 50))
            fh.write(f"solo-{group:05d}\n".encode())


# ---------------------------------------------------------------------------
# Implementation resolution
# ---------------------------------------------------------------------------

@dataclass
class Impl:
    name: str  # "winux" | "gnu" | "uutils"
    prefix: list[str]  # command prefix; the workload argv is appended

    def argv(self, args: list[str]) -> list[str]:
        return [*self.prefix, *args]


def find_exe(root: Path, names: list[str]) -> Path | None:
    for name in names:
        candidate = root / name
        if candidate.is_file():
            return candidate
    return None


def resolve_winux(build_dir: Path) -> Impl | None:
    exe = find_exe(build_dir, ["winuxcmd.exe"])
    if exe is None:
        for candidate in build_dir.rglob("winuxcmd.exe"):
            exe = candidate
            break
    if exe is None:
        return None
    return Impl("winux", [str(exe)])


def resolve_gnu(command: str, root: Path | None) -> Impl | None:
    roots = [root] if root else GNU_SEARCH_ROOTS
    for base in roots:
        if base is None:
            continue
        exe = find_exe(base, [f"{command}.exe", command])
        if exe is not None:
            return Impl("gnu", [str(exe)])
    return None


def resolve_uutils(command: str, root: Path | None) -> Impl | None:
    if root is None:
        return None
    exe = find_exe(root, [f"uu-{command}.exe", f"uu_{command}.exe"])
    if exe is not None:
        return Impl("uutils", [str(exe)])
    multicall = find_exe(root, ["coreutils.exe", "uu-coreutils.exe"])
    if multicall is not None:
        return Impl("uutils", [str(multicall), command])
    exe = find_exe(root, [f"{command}.exe"])
    if exe is not None:
        return Impl("uutils", [str(exe)])
    return None


# ---------------------------------------------------------------------------
# Workloads
# ---------------------------------------------------------------------------

@dataclass
class Workload:
    key: str
    command: str
    title: str
    # argv_for(fixture_dir) -> argv appended after the impl prefix
    argv_for: object
    # "stdout" hashes stdout; "cp-dest" hashes the produced dst.bin;
    # "wc-tuple" parses leading numbers out of stdout; "first-token"
    # compares only stdout's first token (hash digests, whose hex is
    # not all digits).
    verify: str = "stdout"
    stdin_file: str | None = None


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(MIB), b""):
            h.update(chunk)
    return h.hexdigest()


def run_timed(argv: list[str], cwd: Path, stdout_path: Path,
              stdin_path: Path | None) -> tuple[int, float]:
    stdin = stdin_path.open("rb") if stdin_path else subprocess.DEVNULL
    try:
        with stdout_path.open("wb") as out:
            start = time.perf_counter()
            proc = subprocess.run(
                argv, cwd=str(cwd), stdin=stdin, stdout=out,
                stderr=subprocess.PIPE, check=False,
            )
            elapsed = (time.perf_counter() - start) * 1000.0
        return proc.returncode, elapsed
    finally:
        if stdin_path:
            stdin.close()


@dataclass
class WorkloadResult:
    key: str
    title: str
    status: str  # ok | mismatch | error
    medians_ms: dict[str, float | None] = field(default_factory=dict)
    ratios_vs_winux: dict[str, float | None] = field(default_factory=dict)
    note: str = ""


def run_workload(wl: Workload, impls: dict[str, Impl | None],
                 fixture_dir: Path, run_dir: Path,
                 iterations: int) -> WorkloadResult:
    result = WorkloadResult(wl.key, wl.title, "ok")
    digests: dict[str, str] = {}
    argv = wl.argv_for(fixture_dir)

    for name, impl in impls.items():
        if impl is None:
            result.medians_ms[name] = None
            continue
        # One working directory per (workload, implementation); only the
        # fixtures this workload names are copied in, once.
        impl_dir = run_dir / wl.key / name
        impl_dir.mkdir(parents=True, exist_ok=True)
        for token in argv:
            source = fixture_dir / token
            if source.is_file() and not (impl_dir / token).is_file():
                (impl_dir / token).write_bytes(source.read_bytes())
        samples: list[float] = []
        digest = None
        error = ""
        for attempt in range(iterations + 1):
            out_path = impl_dir / "stdout.bin"
            dest = impl_dir / "dst.bin"
            if dest.is_file():
                dest.unlink()  # cp destination must start absent
            rc, elapsed = run_timed(
                impl.argv(argv), impl_dir, out_path,
                (impl_dir / wl.stdin_file) if wl.stdin_file else None,
            )
            if rc != 0:
                error = f"{name} exited {rc}"
                break
            if wl.verify == "cp-dest":
                digest = sha256_file(dest) if dest.is_file() else "missing"
            elif wl.verify == "wc-tuple":
                numbers = []
                for token in out_path.read_bytes().split():
                    if token.isdigit():
                        numbers.append(token)
                    else:
                        break
                digest = hashlib.sha256(b" ".join(numbers)).hexdigest()
            elif wl.verify == "first-token":
                tokens = out_path.read_bytes().split()
                digest = hashlib.sha256(tokens[0] if tokens else b"").hexdigest()
            else:
                digest = sha256_file(out_path)
            if attempt > 0:
                samples.append(elapsed)
        if error:
            result.status = "error"
            result.note = error
            result.medians_ms[name] = None
            continue
        digests[name] = digest or ""
        result.medians_ms[name] = round(statistics.median(samples), 3) if samples else None

    distinct = set(digests.values())
    if len(distinct) > 1:
        result.status = "mismatch"
        result.note = "outputs disagree across implementations: " + ", ".join(
            f"{k}={v[:12]}" for k, v in sorted(digests.items())
        )
    winux_ms = result.medians_ms.get("winux")
    for name, ms in result.medians_ms.items():
        if ms and winux_ms:
            result.ratios_vs_winux[name] = round(winux_ms / ms, 3)
    return result


def build_workloads() -> list[Workload]:
    return [
        Workload("cat-passthrough", "cat", "cat 64 MiB binary passthrough",
                 lambda d: ["big.bin"]),
        Workload("cat-transform-v", "cat", "cat -v 16 MiB mixed text",
                 lambda d: ["-v", "mixed.txt"]),
        Workload("cp-large", "cp", "cp 128 MiB file",
                 lambda d: ["cp-src.bin", "dst.bin"], verify="cp-dest"),
        Workload("wc-c", "wc", "wc -c 64 MiB binary",
                 lambda d: ["-c", "big.bin"], verify="wc-tuple"),
        Workload("wc-full", "wc", "wc 16 MiB mixed text",
                 lambda d: ["mixed.txt"], verify="wc-tuple"),
        Workload("md5-large", "md5sum", "md5sum 64 MiB binary",
                 lambda d: ["big.bin"], verify="first-token"),
        Workload("sha256-large", "sha256sum", "sha256sum 64 MiB binary",
                 lambda d: ["big.bin"], verify="first-token"),
        Workload("sort-1m", "sort", "sort 1,000,000 lines",
                 lambda d: ["sort.txt"]),
        Workload("sort-1m-tiny-buffer", "sort",
                 "sort 1,000,000 lines with -S64K (multi-run merge)",
                 lambda d: ["-S64K", "sort.txt"]),
        Workload("uniq-plain", "uniq", "uniq over long duplicate runs",
                 lambda d: ["uniq.txt"]),
        Workload("uniq-count", "uniq", "uniq -c over long duplicate runs",
                 lambda d: ["-c", "uniq.txt"]),
    ]


def prepare_fixtures(fixture_dir: Path) -> None:
    fixture_dir.mkdir(parents=True, exist_ok=True)
    targets = {
        "big.bin": lambda p: write_binary_fixture(p, 64 * MIB, seed=0xB16),
        "cp-src.bin": lambda p: write_binary_fixture(p, 128 * MIB, seed=0xC09),
        "mixed.txt": lambda p: write_mixed_text_fixture(p, 16 * MIB, seed=0x111),
        "sort.txt": lambda p: write_sort_fixture(p, 1_000_000, seed=0x507),
        "uniq.txt": lambda p: write_uniq_fixture(p, seed=0x222),
    }
    for name, writer in targets.items():
        path = fixture_dir / name
        if not path.is_file():
            writer(path)


# ---------------------------------------------------------------------------
# Baseline record / compare
# ---------------------------------------------------------------------------

def environment_block() -> dict:
    return {
        "recorded_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": platform.node(),
        "os": platform.platform(),
        "cpu_count": os.cpu_count(),
        "python": platform.python_version(),
    }


def compare_to_baseline(results: list[WorkloadResult],
                        baseline: dict) -> list[str]:
    lines = []
    entries = baseline.get("entries", {})
    for r in results:
        base = entries.get(r.key, {}).get("winux_ms")
        current = r.medians_ms.get("winux")
        if base is None or current is None:
            lines.append(f"- `{r.key}`: no baseline/current pair "
                         f"(baseline={base}, current={current})")
            continue
        delta = (current - base) / base * 100.0
        flag = "REGRESSION" if delta > 15 else ("improved" if delta < -15 else "stable")
        lines.append(f"- `{r.key}`: {base:.1f} ms -> {current:.1f} ms "
                     f"({delta:+.1f}%, {flag})")
    return lines


def render_markdown(results: list[WorkloadResult],
                    baseline_lines: list[str] | None) -> str:
    lines = [
        "# Speed Matrix (WinuxCmd vs GNU vs uutils)",
        "",
        "Generated by `scripts/benchmark-speed-matrix.py`. Ratios are "
        "winux median / implementation median for the same workload; "
        "outputs are hash-verified across implementations before any "
        "timing is trusted.",
        "",
        "| Workload | Status | Winux ms | GNU ms | uutils ms | vs GNU | vs uutils | Note |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for r in results:
        def fmt(value: float | None) -> str:
            return "" if value is None else f"{value:.1f}"

        def ratio(name: str) -> str:
            value = r.ratios_vs_winux.get(name)
            return "" if value is None else f"{value:.2f}x"

        lines.append(
            f"| {r.title} | {r.status} | {fmt(r.medians_ms.get('winux'))} | "
            f"{fmt(r.medians_ms.get('gnu'))} | {fmt(r.medians_ms.get('uutils'))} | "
            f"{ratio('gnu')} | {ratio('uutils')} | {r.note} |"
        )
    if baseline_lines:
        lines += ["", "## Baseline comparison (winux column)", "",
                  *baseline_lines]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path,
                        help="directory containing winuxcmd.exe")
    parser.add_argument("--work-dir", type=Path,
                        help="scratch dir for fixtures and runs")
    parser.add_argument("--gnu-root", type=Path)
    parser.add_argument("--uutils-root", type=Path,
                        help="directory with uu-<cmd>.exe or a coreutils.exe multicall")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--record-baseline", type=Path,
                        help="write the winux medians as a baseline file")
    parser.add_argument("--baseline", type=Path,
                        help="compare against a recorded baseline (report-only)")
    parser.add_argument("--fail-on-regression", type=float, default=None,
                        metavar="PCT",
                        help="exit 1 when a winux median regresses more than "
                             "PCT percent vs --baseline (off by default)")
    args = parser.parse_args()

    repo = args.repo
    build_dir = args.build_dir or repo / "build-vs"
    work_dir = args.work_dir or build_dir / "speed-matrix-work"
    fixture_dir = work_dir / "fixtures"
    run_dir = work_dir / "runs"

    winux = resolve_winux(build_dir)
    if winux is None:
        print(f"winuxcmd.exe not found under {build_dir}", file=sys.stderr)
        return 2

    prepare_fixtures(fixture_dir)
    workloads = build_workloads()
    results: list[WorkloadResult] = []
    for wl in workloads:
        impls = {
            "winux": Impl("winux", [*winux.prefix, wl.command]),
            "gnu": resolve_gnu(wl.command, args.gnu_root),
            "uutils": resolve_uutils(wl.command, args.uutils_root),
        }
        results.append(run_workload(wl, impls, fixture_dir, run_dir,
                                    args.iterations))

    baseline_lines = None
    regression_failed = False
    if args.baseline:
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        baseline_lines = compare_to_baseline(results, baseline)
        if args.fail_on_regression is not None:
            entries = baseline.get("entries", {})
            for r in results:
                base = entries.get(r.key, {}).get("winux_ms")
                current = r.medians_ms.get("winux")
                if base and current:
                    delta = (current - base) / base * 100.0
                    if delta > args.fail_on_regression:
                        regression_failed = True

    payload = {
        "environment": environment_block(),
        "build_dir": str(build_dir),
        "iterations": args.iterations,
        "results": [asdict(r) for r in results],
    }
    json_path = args.json or repo / "DOCS" / "generated" / "speed_matrix.json"
    markdown_path = args.markdown or repo / "DOCS" / "generated" / "speed_matrix.md"
    json_path.parent.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    markdown_path.write_text(render_markdown(results, baseline_lines),
                             encoding="utf-8", newline="\n")

    if args.record_baseline:
        baseline_payload = {
            "schema": 1,
            "seeded": True,
            "environment": environment_block(),
            "note": "Winux medians recorded by scripts/benchmark-speed-matrix.py "
                    "--record-baseline. Compare with --baseline (report-only "
                    "unless --fail-on-regression is given).",
            "entries": {
                r.key: {"winux_ms": r.medians_ms.get("winux"), "status": r.status}
                for r in results
            },
        }
        args.record_baseline.parent.mkdir(parents=True, exist_ok=True)
        args.record_baseline.write_text(
            json.dumps(baseline_payload, indent=2) + "\n", encoding="utf-8")

    print(f"workloads={len(results)}")
    print(f"json={json_path}")
    print(f"markdown={markdown_path}")
    bad = [r for r in results if r.status != "ok"]
    if bad:
        print("non-ok=" + ",".join(f"{r.key}:{r.status}" for r in bad))
    if regression_failed:
        print("regression beyond threshold vs baseline")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
