#!/usr/bin/env python3
"""Focused xargs regression probe: literal argv and binary-safe evidence.

Invoke Windows executables directly, and GNU xargs with wsl.exe --exec.
No shell, TSV escaping, command substitution, or text decoding is involved.
This supplements, and does not replace, the full migration gates.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def binary_identity(path):
    resolved = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with resolved.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": str(resolved), "sha256": digest.hexdigest()}


def invoke(argv, data, env):
    try:
        result = subprocess.run(
            argv, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env, timeout=20, check=False,
        )
        return {
            "argv": argv,
            "argv_utf8_hex": [arg.encode("utf-8").hex() for arg in argv],
            "rc": result.returncode,
            "stdout_hex": result.stdout.hex(),
            "stderr_hex": result.stderr.hex(),
        }
    except subprocess.TimeoutExpired as error:
        return {
            "argv": argv, "timeout": True,
            "stdout_hex": (error.stdout or b"").hex(),
            "stderr_hex": (error.stderr or b"").hex(),
        }
    except OSError as error:
        return {"argv": argv, "launch_error": str(error)}


def matches(result, rc, stdout, stderr, gnu=False):
    if result.get("rc") != rc:
        return False
    actual_err = bytes.fromhex(result["stderr_hex"])
    # Only normalize GNU's executable-name prefix on diagnostics, never data.
    if gnu and actual_err.startswith(b"/usr/bin/xargs: "):
        actual_err = b"xargs: " + actual_err[len(b"/usr/bin/xargs: "):]
    return (
        bytes.fromhex(result["stdout_hex"]) == stdout
        and actual_err == stderr
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rust", required=True, help="Rust driver executable")
    parser.add_argument("--cpp", required=True, help="Old C++ product executable")
    parser.add_argument("--out", required=True, help="JSON evidence file")
    options = parser.parse_args()
    destination = Path(options.out).resolve()
    if not destination.parent.is_dir():
        parser.error("--out parent directory must already exist")

    env = os.environ.copy()
    env.update({"WINUX_LANG": "off", "LC_ALL": "C", "LANG": "C"})
    env.pop("POSIXLY_CORRECT", None)
    rust = binary_identity(options.rust)
    cpp = binary_identity(options.cpp)
    comspec = env.get("COMSPEC", env.get("ComSpec", "C:/Windows/System32/cmd.exe"))
    literal = b"one&echo.XARGS_REVIEW_INJECTED"
    # Each expectation is independent of the implementation under test.
    # The final field enables the GNU oracle for platform-neutral rows.
    cases = [
        ("hex-delimiter", ["-d", r"\x41"], b"aAbAc", 0, b"a b c\n", b"", True),
        ("octal-delimiter", ["-d", r"\012"], b"a\nb", 0, b"a b\n", b"", True),
        (
            "invalid-delimiter", ["-d", r"\q"], b"a", 1, b"",
            b"xargs: Invalid escape sequence \\q in input delimiter specification.\n",
            True,
        ),
        ("raw-default-echo", ["-0"], b"\xff\0", 0, b"\xff\n", b"", True),
        ("raw-no-newline", ["-0"], b"-n\0\xff\0", 0, b"\xff", b"", True),
        ("raw-with-escape", ["-0"], b"-e\0\\x41\xff\0", 0, b"A\xff\n", b"", True),
        ("nul-output", ["-0"], b"-e\0\\x00\\xff\0", 0, b"\0\xff\n", b"", True),
        (
            "cmd-literal-tail", ["-0", "cmd", "/c", "echo"], literal + b"\0",
            0, literal + b"\r\n", b"", False,
        ),
        (
            "cmd-fullpath-tail", ["-0", comspec, "/C", "echo"], literal + b"\0",
            0, literal + b"\r\n", b"", False,
        ),
    ]
    report = {
        "scope": "Focused regressions only; not full migration acceptance",
        "binaries": {"rust": rust, "cpp": cpp},
        "cases": [],
    }
    for name, args, data, rc, stdout, stderr, use_gnu in cases:
        row = {
            "id": name, "input_hex": data.hex(),
            "expected": {"rc": rc, "stdout_hex": stdout.hex(), "stderr_hex": stderr.hex()},
            "rust": invoke([rust["path"], "xargs", *args], data, env),
            "cpp": invoke([cpp["path"], "xargs", *args], data, env),
        }
        row["rust_matches_expected"] = matches(row["rust"], rc, stdout, stderr)
        if use_gnu:
            row["gnu"] = invoke(
                [
                    "wsl.exe", "--exec", "/usr/bin/env", "-u", "POSIXLY_CORRECT",
                    "LC_ALL=C", "LANG=C", "/usr/bin/xargs", *args,
                ],
                data, env,
            )
            row["gnu_matches_expected"] = matches(row["gnu"], rc, stdout, stderr, gnu=True)
        # The short-name case is also the established old-product contract.
        # Full-path cmd lacks that protection in the old builder, so its
        # output is evidence, not the expected value for the repaired path.
        if name == "cmd-literal-tail":
            row["cpp_matches_expected"] = matches(row["cpp"], rc, stdout, stderr)
        row["passed"] = (
            row["rust_matches_expected"]
            and row.get("gnu_matches_expected", True)
            and row.get("cpp_matches_expected", True)
        )
        report["cases"].append(row)

    report["passed"] = all(row["passed"] for row in report["cases"])
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    for row in report["cases"]:
        print(f"{'PASS' if row['passed'] else 'FAIL'} {row['id']}")
    print(f"Evidence: {destination}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
