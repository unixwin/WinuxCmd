"""Paired, validated recursive-grep timings for two WinuxCmd executables."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import time


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def measure(binary, fixture, expected):
    env = dict(os.environ, HOME=str(fixture), USERPROFILE=str(fixture), LC_ALL="C")
    env.pop("WINUX_LANG", None)
    started = time.perf_counter()
    result = subprocess.run(
        [str(binary), "grep", "-rl", "needle", "data"],
        cwd=fixture,
        input=b"",
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        timeout=15,
    )
    elapsed = (time.perf_counter() - started) * 1000
    assert result.returncode == 0, result.stderr
    assert result.stderr == b"", result.stderr
    actual = sorted(result.stdout.decode("utf-8").splitlines())
    assert actual == expected, "recursive grep did not return every fixture file"
    return elapsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--samples", type=int, default=30)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")
    binaries = {arm: getattr(args, arm).resolve(strict=True) for arm in ("before", "after")}
    for binary in binaries.values():
        if binary.stem.lower() != "winuxcmd":
            parser.error("keep the executable name winuxcmd.exe for multicall dispatch")
    fixture = Path(tempfile.mkdtemp(prefix="winux-grep-walk-")).resolve()
    (fixture / "data").mkdir()
    expected = []
    for index in range(2000):
        name = f"f{100 + index}.txt" if index < 100 else f"g{index:04d}.txt"
        (fixture / "data" / name).write_bytes(f"needle {index}\n".encode())
        expected.append(f"data/{name}")
    expected.sort()
    report = {
        "kind": "winuxcmd-grep-walk-paired",
        "host": {"os": platform.platform(), "cpu": platform.processor(), "python": platform.python_version()},
        "fixture": {"path": str(fixture), "files": len(expected)},
        "binaries": {arm: {"path": str(file), "sha256": digest(file)} for arm, file in binaries.items()},
        "limitations": ["Direct process timings include startup and output collection, not the shell or full host.",
                        "Two warmup pairs; alternating order; all filenames checked on every invocation.",
                        "Run without concurrent local builds or tests. Fixture retained at the recorded path."],
        "samplesMs": {arm: [] for arm in binaries},
        "ok": False,
    }
    try:
        for pair in range(-2, args.samples):
            for arm in (("before", "after") if pair % 2 == 0 else ("after", "before")):
                elapsed = measure(binaries[arm], fixture, expected)
                if pair >= 0:
                    report["samplesMs"][arm].append(elapsed)
        report["stats"] = {}
        for arm, values in report["samplesMs"].items():
            ordered = sorted(values)
            report["stats"][arm] = {"n": len(values), "p50": ordered[math.ceil(len(values) * 0.5) - 1],
                                    "p95": ordered[math.ceil(len(values) * 0.95) - 1]}
        report["ok"] = True
        print(json.dumps(report["stats"]))
    except Exception as error:
        report["error"] = repr(error)
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
