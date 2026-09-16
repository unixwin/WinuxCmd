"""Record GNU results, or compare a native dispatcher with those exact bytes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", type=Path)
    mode.add_argument("--compare", type=Path)
    program = parser.add_mutually_exclusive_group(required=True)
    program.add_argument("--gnu-bin", type=Path)
    program.add_argument("--winuxcmd", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--cases", type=Path, default=Path(__file__).with_name("master-cases.json"))
    args = parser.parse_args()
    if args.record and (not args.gnu_bin or not args.revision):
        parser.error("recording requires --gnu-bin and --revision")
    data = args.cases.read_bytes()
    cases = json.loads(data)
    ids = [case["id"] for case in cases]
    if not ids or len(ids) != len(set(ids)):
        parser.error("cases must be nonempty with unique IDs")
    fingerprint = hashlib.sha256(data).hexdigest()
    expected = None
    if args.compare:
        expected = json.loads(args.compare.read_text(encoding="utf-8"))
        if expected["cases_sha256"] != fingerprint or set(expected["results"]) != set(ids):
            parser.error("oracle corpus does not match the checked-out cases")
        if args.revision and expected["revision"] != args.revision:
            parser.error("oracle revision does not match the requested GNU revision")
    env = dict(os.environ, LC_ALL="C", LANG="C", TZ="UTC0", WINUX_LANG="en", TERM="xterm", COLORTERM="")
    results = {}
    failures = []
    for case in cases:
        command = ([str(args.winuxcmd.resolve()), case["command"]] if args.winuxcmd
                   else [str(args.gnu_bin.resolve() / case["command"])])
        try:
            result = subprocess.run(command + case["args"], input=bytes.fromhex(case.get("stdin_hex", "")),
                                    capture_output=True, timeout=30, env=env, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"ERROR {case['id']}: {error}", file=sys.stderr)
            return 2
        actual = {"exit": result.returncode, "stdout_hex": result.stdout.hex(), "stderr_hex": result.stderr.hex()}
        if args.record and result.returncode != 0:
            print(f"ERROR {case['id']}: GNU positive fixture failed: {actual}", file=sys.stderr)
            return 2
        results[case["id"]] = actual
        if expected and actual != expected["results"][case["id"]]:
            failures.append(case["id"])
            print(f"FAIL {case['id']}: expected={expected['results'][case['id']]} actual={actual}")
    if args.record:
        args.record.write_text(json.dumps({"revision": args.revision, "cases_sha256": fingerprint,
                                          "results": results}, indent=2) + "\n", encoding="utf-8")
    print(f"Cases: {len(cases)}; differences: {len(failures)}")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
