#!/usr/bin/env python3
# 64 MiB sed scan benchmark — the scan-perf lane's instrument (#1488).
#
# Same input generator and WSL-GNU-on-ext4 oracle as gate/sed-bench.py, but
# focused on the three cases the lane moves (`s/foo/bar/g`, `-n /re/p`,
# `y///`) plus the capture-group substitution and the `-n p` I/O floor, and
# it prints the machine/software environment it measured on. Every case is
# best-of-3 wall clock; the sha1 column proves every side emitted GNU's
# bytes.
#
#   cargo build --release -p winuxcmd-driver
#   python gate/sed-scanperf.py <label> [RUST_EXE]
import hashlib
import os
import platform
import random
import subprocess
import sys
import time

GATE = os.path.dirname(os.path.abspath(__file__))
RUST2 = os.path.dirname(GATE)
HERE = os.path.join(RUST2, 'target', 'bench')
os.makedirs(HERE, exist_ok=True)
IN = os.path.join(HERE, 'in64.txt')
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'
RUST = sys.argv[2] if len(sys.argv) > 2 else os.path.join(RUST2, 'target', 'release', 'winuxcmd-driver.exe')
BEST_OF = 3

CASES = [
    ('s/foo/bar/g', ['s/foo/bar/g']),
    ('-n /re/p', ['-n', '/lorem.*amet/p']),
    ('y/abc/xyz/', ['y/abc/xyz/']),
    ('s/\\(a\\)\\(b\\)/\\2\\1/', ['s/\\(a\\)\\(b\\)/\\2\\1/']),
    ('-n p (I/O floor)', ['-n', 'p']),
]


def mnt(p):
    p = os.path.abspath(p).replace('\\', '/')
    return '/mnt/' + p[0].lower() + p[2:]


def make_input():
    if os.path.exists(IN):
        return
    random.seed(1488)
    words = ["the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog", "foo", "bar",
             "abc", "cab", "ab", "baz", "lorem", "ipsum", "dolor", "sit", "amet", "re",
             "x1y2", "42"]
    out = []
    size = 0
    target = 64 * 1024 * 1024
    while size < target:
        line = " ".join(random.choice(words) for _ in range(random.randint(4, 14))) + "\n"
        out.append(line)
        size += len(line)
    data = "".join(out)[:target]
    with open(IN, 'w', newline='\n') as f:
        f.write(data[:data.rfind("\n") + 1])


def run_local(cmd, out):
    best = None
    for _ in range(BEST_OF):
        with open(out, 'wb') as fo:
            t = time.perf_counter()
            subprocess.run(cmd, stdout=fo, check=False)
            dt = time.perf_counter() - t
        best = dt if best is None else min(best, dt)
    with open(out, 'rb') as f:
        return best, hashlib.sha1(f.read()).hexdigest()[:10]


def run_gnu():
    script = os.path.join(HERE, 'gnu_scan.sh')
    with open(script, 'w', newline='\n') as f:
        f.write(f'cp {mnt(IN)} /tmp/in64.txt\n')
        for idx, (_, args) in enumerate(CASES):
            q = ' '.join("'" + a.replace("'", "'\\''") + "'" for a in args)
            f.write(
                f'best=999; for i in 1 2 3; do s=$(date +%s.%N); sed {q} /tmp/in64.txt > /tmp/o.txt; '
                f'e=$(date +%s.%N); best=$(echo "$e - $s" | bc -l | awk -v b=$best \'{{print ($1<b)?$1:b}}\'); '
                f'done; echo "{idx}|$best|$(sha1sum /tmp/o.txt | cut -c1-10)"\n'
            )
    out = subprocess.run(['wsl', 'bash', mnt(script)], capture_output=True, text=True,
                         env=dict(os.environ, MSYS_NO_PATHCONV='1')).stdout
    return {l.split('|')[0]: l.split('|')[1:] for l in out.strip().splitlines() if '|' in l}


def env_report():
    def sh(cmd, wsl=False):
        argv = ['wsl', 'bash', '-c', cmd] if wsl else ['powershell', '-NoProfile', '-Command', cmd]
        try:
            return subprocess.run(argv, capture_output=True, text=True, timeout=60,
                                  env=dict(os.environ, MSYS_NO_PATHCONV='1')).stdout.strip()
        except Exception as e:  # noqa: BLE001 - environment probe only
            return f'<{e}>'
    cpu = sh('(Get-CimInstance Win32_Processor).Name')
    cores = sh('(Get-CimInstance Win32_Processor).NumberOfCores')
    threads = sh('(Get-CimInstance Win32_Processor).NumberOfLogicalProcessors')
    ram = sh('[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)')
    build = sh('(Get-CimInstance Win32_OperatingSystem).Version')
    rustc = sh('rustc --version')
    gnu = sh('sed --version | head -1', wsl=True)
    kernel = sh('uname -r', wsl=True)
    fs = sh('df -T /tmp | tail -1 | awk \'{print $2}\'', wsl=True)
    return [f'label={LABEL}', f'cpu={cpu}', f'cores={cores} threads={threads} ram_gib={ram}',
            f'windows={build}', f'{rustc}', f'gnu: {gnu}', f'wsl_kernel={kernel}',
            f'gnu_fs=/tmp {fs}', f'case_count={len(CASES)} best_of={BEST_OF}',
            f'input={IN} bytes={os.path.getsize(IN)}']


make_input()
for line in env_report():
    print(line)
print()
print(f"{'case':28} {LABEL:>8} {'GNU':>8}  outputs-equal")
gnu = run_gnu()
rows = []
for idx, (name, args) in enumerate(CASES):
    r, rh = run_local([RUST, 'sed'] + args + [IN], os.path.join(HERE, 'o_rust.txt'))
    gt, gh = gnu.get(str(idx), ['nan', '-'])
    rows.append((name, r, float(gt), rh == gh))
    print(f"{name:28} {r:8.2f} {float(gt):8.2f}  rust=gnu:{rh == gh}")
if any(not ok for *_, ok in rows):
    sys.exit(1)
