#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build WinuxCmd under a Visual Studio developer environment.

Python equivalent of scripts/build-with-vs.ps1: locates vcvars64.bat via
vswhere, initializes the VS environment inside cmd.exe, then configures and
builds the selected CMake target in one shell session.

Examples:
    python scripts/build.py                          # build winuxcmd-tests
    python scripts/build.py --target winuxcmd        # build the main binary
    python scripts/build.py --skip-configure         # incremental rebuild only
    python scripts/build.py --target winuxcmd-tests --run-tests
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_vs_env_script() -> str:
    """Locate vcvars64.bat / VsDevCmd.bat using vswhere, then known paths."""
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        try:
            result = subprocess.run(
                [
                    str(vswhere),
                    "-latest",
                    "-products",
                    "*",
                    "-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-find",
                    r"VC\Auxiliary\Build\vcvars64.bat",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            candidates = [line.strip() for line in result.stdout.splitlines() if line.strip()]
            if candidates and Path(candidates[0]).is_file():
                return candidates[0]
        except OSError:
            pass

    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    fallbacks = [
        Path(program_files) / "Microsoft Visual Studio" / "18" / "Community" / "VC" / "Auxiliary" / "Build" / "vcvars64.bat",
        Path(program_files) / "Microsoft Visual Studio" / "17" / "Community" / "VC" / "Auxiliary" / "Build" / "vcvars64.bat",
        Path(program_files_x86) / "Microsoft Visual Studio" / "17" / "BuildTools" / "VC" / "Auxiliary" / "Build" / "vcvars64.bat",
    ]
    for candidate in fallbacks:
        if candidate.is_file():
            return str(candidate)
    print("error: Visual Studio environment script not found", file=sys.stderr)
    sys.exit(2)


def quote_cmd(value: str) -> str:
    return '"' + value.replace('"', '\\"') + '"'


def run_cmd(command: str) -> int:
    """Run a command line through cmd.exe without re-quoting it.

    A single string is passed to CreateProcess so the quoting produced by
    quote_cmd() reaches cmd.exe verbatim (a list argument would be re-quoted
    by subprocess.list2cmdline and corrupt paths containing spaces).
    """
    cmd_exe = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"),
                           "System32", "cmd.exe")
    return subprocess.run(f"{cmd_exe} /d /s /c {command}", check=False).returncode


def _latest_version_dir(root: Path) -> Path | None:
    """Return the highest-versioned child directory of root, if any."""
    if not root.is_dir():
        return None

    def version_key(p: Path):
        try:
            return [int(x) for x in p.name.split(".")]
        except ValueError:
            return None

    numbered = [(version_key(p), p) for p in root.iterdir() if p.is_dir()]
    numbered = [(k, p) for k, p in numbered if k is not None]
    if not numbered:
        return None
    return max(numbered, key=lambda item: item[0])[1]


def discover_msvc_env() -> dict[str, str] | None:
    """Manually construct MSVC/INCLUDE/LIB/PATH when vcvars64.bat fails.

    vcvars64.bat can fail silently on machines where a security tool blocks
    helper programs it invokes (e.g. reg.exe), leaving INCLUDE without the
    Windows SDK so cl.exe cannot find winsock2.h. This fallback locates the
    newest MSVC toolset and Windows SDK directly.
    """
    vs_roots = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio",
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio",
    ]
    msvc_dir = None
    for vs_root in vs_roots:
        if not vs_root.is_dir():
            continue
        for edition in vs_root.glob("*/**/VC/Tools/MSVC"):
            found = _latest_version_dir(edition)
            if found and (found / "lib" / "x64").is_dir():
                msvc_dir = found
                break
        if msvc_dir:
            break
    if msvc_dir is None:
        return None

    sdk_root = (Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
                / "Windows Kits" / "10")
    sdk_inc = _latest_version_dir(sdk_root / "Include")
    sdk_lib = _latest_version_dir(sdk_root / "Lib")
    if sdk_inc is None or sdk_lib is None:
        return None

    include = ";".join(str(p) for p in (
        msvc_dir / "include",
        sdk_inc / "ucrt",
        sdk_inc / "um",
        sdk_inc / "shared",
        sdk_inc / "winrt",
        sdk_inc / "cppwinrt",
    ) if p.is_dir())
    lib = ";".join(str(p) for p in (
        msvc_dir / "lib" / "x64",
        sdk_lib / "ucrt" / "x64",
        sdk_lib / "um" / "x64",
    ) if p.is_dir())
    if not include or not lib:
        return None

    env = dict(os.environ)
    env["INCLUDE"] = include
    env["LIB"] = lib
    cl_bindir = msvc_dir / "bin" / "Hostx64" / "x64"
    if cl_bindir.is_dir():
        env["PATH"] = str(cl_bindir) + os.pathsep + env.get("PATH", "")
    return env


def vs_env_probe_ok(vs_env_script: str) -> bool:
    """Check whether calling the VS env script really yields the Windows SDK.

    vcvars64.bat may print 'Environment initialized' yet fail to set INCLUDE
    when one of its helper programs is blocked; detect that by inspecting the
    resulting INCLUDE value.
    """
    cmd_exe = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"),
                           "System32", "cmd.exe")
    probe = f'call "{vs_env_script}" && echo ===WCBUILD-ENV=== && set INCLUDE'
    try:
        result = subprocess.run(f'{cmd_exe} /d /s /c "{probe}"',
                                capture_output=True, text=True, check=False,
                                timeout=120)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return "Windows Kits" in result.stdout


def run_steps(steps: list[str], env: dict[str, str] | None) -> int:
    """Run cmake steps either chained after vcvars in cmd.exe (env=None) or
    directly with a prepared environment (env=manual)."""
    if env is None:
        return run_cmd(" && ".join(steps))
    for step in steps:
        print(f"> {step}")
        code = subprocess.run(step, check=False, env=env,
                              shell=True).returncode
        if code != 0:
            return code
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(ROOT))
    parser.add_argument("--build-dir", default="build-vs")
    parser.add_argument("--target", default="winuxcmd-tests")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--run-tests", action="store_true",
                        help="run ctest after a successful build")
    parser.add_argument("--cmake-extra-args", nargs="*", default=[])
    parser.add_argument("--vs-env-script", default="")
    args = parser.parse_args()

    root_path = Path(args.root).resolve()
    build_path = root_path / args.build_dir
    vs_env_script = args.vs_env_script or find_vs_env_script()

    steps: list[str] = []
    if not args.skip_configure:
        cmake_args = [
            "-S", str(root_path),
            "-B", str(build_path),
            "-G", args.generator,
            f"-DCMAKE_BUILD_TYPE={args.configuration}",
            *args.cmake_extra_args,
        ]
        steps.append("cmake " + " ".join(quote_cmd(a) for a in cmake_args))
    if not args.configure_only:
        steps.append(
            "cmake --build "
            + quote_cmd(str(build_path))
            + " --target "
            + quote_cmd(args.target)
        )

    if not steps:
        print("Nothing to do. Use --configure-only, omit --skip-configure, or both.")
        return 0

    vs_call = "call " + quote_cmd(vs_env_script)
    script_name = Path(vs_env_script).name.lower()
    if script_name == "vsdevcmd.bat":
        vs_call += " -arch=x64"
    elif script_name == "vcvarsall.bat":
        vs_call += " x64"

    # If the VS environment script fails to produce the Windows SDK (e.g.
    # reg.exe blocked by a security tool), fall back to a manually
    # constructed MSVC/SDK environment instead of failing on winsock2.h.
    manual_env = None
    if not vs_env_probe_ok(vs_env_script):
        manual_env = discover_msvc_env()
        if manual_env is not None:
            print("warning: VS env script did not initialize the Windows SDK")
            print("warning: using manual MSVC/SDK environment fallback")
        else:
            print("warning: VS env probe failed and no MSVC/SDK fallback found;"
                  " trying the VS env script anyway")

    print(f"Root: {root_path}")
    print(f"BuildDir: {build_path}")
    print(f"Target: {args.target}")
    print(f"VS env: {vs_env_script}")
    print()

    if manual_env is not None:
        exit_code = run_steps(steps, manual_env)
    else:
        command = vs_call + " && " + " && ".join(steps)
        exit_code = run_cmd(command)

    if exit_code == 0 and args.run_tests:
        print("\n=== Running tests ===")
        if manual_env is not None:
            test_steps = ["ctest -C " + quote_cmd(args.configuration)
                          + " --output-on-failure ."]
            exit_code = run_steps(
                ["cd /d " + quote_cmd(str(build_path))] + test_steps,
                manual_env)
        else:
            test_command = "ctest -C " + quote_cmd(args.configuration) + " --output-on-failure ."
            test_shell = (
                "call " + quote_cmd(vs_env_script) + " && cd /d "
                + quote_cmd(str(build_path)) + " && " + test_command
            )
            exit_code = run_cmd(test_shell)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
