# GNU Coreutils Upstream Test Baseline

> This document explains how to run GNU coreutils upstream tests against WinuxCmd,
> interpret the results, and understand known differences.
>
> Inspired by the [uutils/coreutils](https://github.com/uutils/coreutils) testing approach.

## Overview

WinuxCmd aims for behavioral compatibility with GNU coreutils 9.11. To measure
progress and catch regressions, we run the **actual GNU test scripts** against
WinuxCmd binaries. This approach catches discrepancies that unit tests written
from scratch might miss, because the GNU tests encode decades of edge-case knowledge.

### How It Works (following uutils pattern)

| Step | Description |
|------|-------------|
| 1 | Install GNU coreutils 9.11 source in WSL2 (`/opt/coreutils-9.11/`) |
| 2 | Build WinuxCmd and copy binaries to a WSL2-accessible path |
| 3 | Prepend WinuxCmd to `PATH` so GNU test scripts invoke WinuxCmd instead of GNU |
| 4 | Run GNU test scripts (`tests/by-util/<cmd>/test-*.sh`) |
| 5 | Capture exit codes, stdout, stderr for each test |
| 6 | Classify as **pass** (exit 0), **skip** (exit 77), or **fail** |
| 7 | Generate a summary report with per-command breakdown |

## Prerequisites

### 1. WSL2 Setup

```bash
# Verify WSL2 is available
wsl --version

# Launch WSL2
wsl

# Install build dependencies
sudo apt update && sudo apt install -y \
    build-essential autoconf automake texinfo \
    gettext perl python3 gawk diffutils \
    gcc make git
```

### 2. Install GNU Coreutils 9.11 Source

```bash
# Download and build GNU coreutils 9.11
cd /tmp
curl -LO https://ftp.gnu.org/gnu/coreutils/coreutils-9.11.tar.xz
tar xf coreutils-9.11.tar.xz
cd coreutils-9.11

# Configure with test infrastructure
./configure --prefix=/opt/coreutils-9.11
make -j$(nproc)
make install

# Verify test infrastructure exists
ls /opt/coreutils-9.11/tests/by-util/

# Alternatively, just build tests without full install:
cd /tmp/coreutils-9.11
./configure
make check-TESTS  # Quick sanity check
```

### 3. Make WinuxCmd Accessible from WSL2

```bash
# Option A: Build WinuxCmd on Windows, access via /mnt/c/
# The build outputs are at: build-vs/usr/bin/

# Option B: Copy binaries into WSL2
cp -r /mnt/c/repo/unixwin-winuxcmd/build-vs/usr/bin/ ~/winuxcmd-bin/

# Option C: Create a symlink
ln -s /mnt/c/repo/unixwin-winuxcmd/build-vs/usr/bin ~/winuxcmd-bin
```

## Running the Tests

### Full Test Suite

```bash
# From the WinuxCmd repo root (in WSL2)
bash scripts/run_gnu_tests.sh -v

# With custom paths
bash scripts/run_gnu_tests.sh \
    -w ~/winuxcmd-bin \
    -g /opt/coreutils-9.11 \
    -v
```

### Single Command

```bash
# Test only "cat"
bash scripts/run_gnu_tests.sh -c cat -v

# Test only "sort"
bash scripts/run_gnu_tests.sh -c sort -v
```

### Skip Unsupported Commands

```bash
# Skip commands that cannot work on Windows
bash scripts/run_gnu_tests.sh -s "chroot,chcon,mknod,mkfifo,runcon" -v
```

### Output Comparison Mode

```bash
# Compare individual command outputs (GNU vs WinuxCmd)
bash scripts/compare_outputs.sh -v

# Compare a single command
bash scripts/compare_outputs.sh -c cat -v

# Use specific paths
bash scripts/compare_outputs.sh \
    -w ~/winuxcmd-bin \
    -g /usr/bin \
    -v
```

## Output Directory Structure

Both scripts generate reports under `test-reports/`. The structure mirrors
uutils' `tests/by-util/` convention:

```
test-reports/
  gnu-test-run-YYYYMMDD-HHMMSS/
    summary.json              # Machine-readable overall summary
    REPORT.md                 # Human-readable Markdown report
    full-test.log             # Complete log output
    by-util/
      cat/
        _summary.json         # Per-command pass/fail counts
        test-cat.stdout       # stdout capture per test
        test-cat.stderr       # stderr capture per test
        test-cat.exit         # Exit code per test
        test-cat.status       # Classification: pass/fail/skip/timeout
      ls/
        _summary.json
        ...
      sort/
        ...
  compare/
    YYYYMMDD-HHMMSS/
      results.csv             # Machine-readable comparison results
      COMPARISON_REPORT.md    # Markdown diff report
      cat/
        echo_hello.gnu.stdout
        echo_hello.winux.stdout
        echo_hello.diff       # Unified diff of outputs
        ...
```

## Interpreting Results

### Test Classifications

| Exit Code | Classification | Meaning |
|-----------|---------------|---------|
| 0 | **pass** | WinuxCmd output matches GNU expected output |
| 77 | **skip** | Test skipped (incompatible environment, known gap) |
| 124 | **timeout** | Test exceeded 300s time limit |
| other | **fail** | WinuxCmd output differs from GNU expected output |

### Reading the Markdown Report

The `REPORT.md` file contains:

1. **Summary table** — overall pass/skip/fail counts and pass rate
2. **Per-command results** — pass rate broken down by command
3. **Failed tests** — detailed list with stderr snippets for debugging

### Reading the Comparison Report

The `COMPARISON_REPORT.md` file contains:

1. **Match rate** — percentage of tests where GNU and WinuxCmd produce identical output
2. **Results table** — per-test comparison with stdout/stderr/exit code match flags
3. **Diff details** — unified diffs showing exactly what differs

### Pass Rate Thresholds

| Rate | Assessment |
|------|-----------|
| >= 90% | Good progress; focus on remaining edge cases |
| >= 70% | Moderate; core functionality works but gaps remain |
| < 70% | Significant gaps; prioritize missing options/features |

## Known Differences and Limitations

### Platform-Inherent Differences

These differences are expected and cannot be resolved without changes to the
Windows platform itself:

| Category | Examples | Impact |
|----------|----------|--------|
| **POSIX permissions** | `chmod`, `chown` | Windows uses ACLs, not Unix modes |
| **Symbolic links** | `ln -s` | Requires Developer Mode or admin privileges |
| **File system** | Case sensitivity, path separators, reserved chars | Affects all file operations |
| **Devices/nodes** | `mknod`, `mkfifo` | Windows lacks POSIX device nodes and FIFOs |
| **SELinux** | `chcon`, `runcon` | Not applicable to Windows |
| **Process signals** | `kill`, `nice`, `nohup` | Different signal model on Windows |
| **User identity** | `id`, `groups`, `whoami` | Different user/group model |
| **Locale** | `LC_*` environment variables | Windows uses different locale system |

### Option Implementation Gaps

Some GNU options are not yet implemented in WinuxCmd. See:

- `DOCS/generated/gnu_option_gap_audit.md` — Full option-by-option gap analysis
- `DOCS/gnu-coreutils/COMPATIBILITY_MATRIX.md` — Per-command compatibility status
- `DOCS/en/gnu_compat_audit_plan.md` — Migration plan and priorities

Current statistics (as of last audit):

| Metric | Value |
|--------|------:|
| GNU options missing in WinuxCmd | ~402 |
| WinuxCmd-only options | ~854 |
| Commands with full GNU option parity | ~15 |

### Output Formatting Differences

Some tests may fail due to minor formatting differences that are functionally
equivalent:

- **Whitespace normalization** — trailing spaces, blank line count
- **Timestamp formatting** — timezone representation, locale-dependent dates
- **Error message wording** — English text may differ slightly
- **Exit code nuances** — some edge cases use different exit codes

### Commands Requiring Special Handling

The following commands have tests that require special treatment:

| Command | Issue | Workaround |
|---------|-------|------------|
| `chroot` | No Windows equivalent | Always skip |
| `chcon` | SELinux only | Always skip |
| `mknod` | No device nodes on Windows | Always skip |
| `mkfifo` | No FIFO on NTFS | Always skip |
| `runcon` | SELinux only | Always skip |
| `stty` | Terminal control differs | Skip ioctl-dependent tests |
| `timeout` | Windows process model differs | Skip signal-based tests |
| `nice` | Different priority model | Skip priority tests |
| `who` | Different login model | Skip utmp-dependent tests |
| `hostname` | Network config differs | May produce different output |

## CI Integration

### GitHub Actions (Planned)

```yaml
gnu-compat-test:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Setup WSL2
      run: wsl --install -d Ubuntu-24.04
    - name: Install GNU coreutils 9.11
      run: |
        wsl -e bash -c "\
          cd /tmp && \
          curl -LO https://ftp.gnu.org/gnu/coreutils/coreutils-9.11.tar.xz && \
          tar xf coreutils-9.11.tar.xz && \
          cd coreutils-9.11 && \
          ./configure --prefix=/opt/coreutils-9.11 && \
          make -j$(nproc) && make install"
    - name: Build WinuxCmd
      run: wsl -e bash -c "cd ~/winuxcmd && make"
    - name: Run GNU tests
      run: |
        wsl -e bash -c "\
          cd ~/winuxcmd && \
          bash scripts/run_gnu_tests.sh -v"
    - name: Upload report
      uses: actions/upload-artifact@v4
      with:
        name: gnu-test-report
        path: test-reports/
```

### Running Locally (Recommended)

For development, run tests locally in WSL2:

```bash
# 1. Open WSL2 terminal
wsl

# 2. Navigate to WinuxCmd repo
cd /mnt/c/repo/unixwin-winuxcmd

# 3. Run comparison tests for quick feedback
bash scripts/compare_outputs.sh -c cat -v
bash scripts/compare_outputs.sh -c sort -v
bash scripts/compare_outputs.sh -c ls -v

# 4. Run full GNU test suite for comprehensive check
bash scripts/run_gnu_tests.sh -v

# 5. Review results
cat test-reports/gnu-test-run-*/REPORT.md
```

## Updating the Baseline

When upgrading the GNU coreutils version:

1. Download the new source tarball
2. Build and install to `/opt/coreutils-<version>/`
3. Update the `-g` flag in the test scripts
4. Re-run the full test suite
5. Update the known-differences section in this document

```bash
# Example: upgrade from 9.11 to 9.12
cd /tmp
curl -LO https://ftp.gnu.org/gnu/coreutils/coreutils-9.12.tar.xz
tar xf coreutils-9.12.tar.xz
cd coreutils-9.12
./configure --prefix=/opt/coreutils-9.12
make -j$(nproc) && make install

# Re-run with new version
bash scripts/run_gnu_tests.sh -g /opt/coreutils-9.12 -v
```

## References

- [GNU Coreutils](https://www.gnu.org/software/coreutils/)
- [GNU Coreutils Tests](https://git.savannah.gnu.org/cgit/coreutils.git/tree/tests)
- [uutils/coreutils](https://github.com/uutils/coreutils) — Rust rewrite that inspired this approach
- [uutils test strategy](https://github.com/uutils/coreutils/tree/main/tests/by-util)
- `DOCS/en/gnu_compat_audit_plan.md` — Full compatibility audit plan
- `DOCS/gnu-coreutils/COMPATIBILITY_MATRIX.md` — Per-command compatibility matrix
- `DOCS/generated/gnu_option_gap_audit.md` — Option gap analysis

---
_Document maintained by WinuxCmd team. Last updated: 2026-08-27_