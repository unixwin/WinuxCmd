#!/usr/bin/env bash
# =============================================================================
# run_gnu_tests.sh — GNU Coreutils upstream test runner for WinuxCmd
# =============================================================================
#
# Inspired by uutils/coreutils approach:
#   - Runs GNU coreutils shell-based test scripts against WinuxCmd binaries
#   - Captures pass/fail/skip results for each test
#   - Generates a summary report comparing WinuxCmd vs GNU baseline
#
# Prerequisites:
#   1. WSL2 with GNU coreutils source installed at /opt/coreutils-9.11/
#      (see DOCS/en/gnu_test_baseline.md for setup instructions)
#   2. WinuxCmd binaries accessible from WSL2 (via /mnt/c/ or mounted path)
#   3. Standard GNU test dependencies: bash, perl, diffutils, gawk, grep
#
# Usage:
#   ./scripts/run_gnu_tests.sh [OPTIONS]
#
# Options:
#   -w, --winux-dir DIR      Path to WinuxCmd binaries (default: auto-detect)
#   -g, --gnu-dir DIR        Path to GNU coreutils source (default: /opt/coreutils-9.11)
#   -c, --command CMD        Run tests for a single command (e.g., "cat", "ls")
#   -o, --output-dir DIR     Output directory for reports (default: ./test-reports)
#   -j, --jobs N             Number of parallel test jobs (default: 1)
#   -s, --skip LIST          Comma-separated list of commands to skip
#   -v, --verbose            Verbose output
#   -h, --help               Show this help message
#
# Examples:
#   ./scripts/run_gnu_tests.sh                          # Run all tests
#   ./scripts/run_gnu_tests.sh -c cat                   # Run cat tests only
#   ./scripts/run_gnu_tests.sh -w /mnt/c/build/vs/bin   # Custom WinuxCmd path
#   ./scripts/run_gnu_tests.sh -s "chroot,chcon,mknod"  # Skip unsupported cmds
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Color helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

info()  { printf "${BLUE}[INFO]${NC}  %s\n" "$*"; }
ok()    { printf "${GREEN}[OK]${NC}    %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
fail()  { printf "${RED}[FAIL]${NC}  %s\n" "$*"; }
header(){ printf "\n${BOLD}=== %s ===${NC}\n" "$*"; }

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
WINUX_DIR=""
GNU_DIR="/opt/coreutils-9.11"
SINGLE_COMMAND=""
OUTPUT_DIR="./test-reports"
JOBS=1
SKIP_LIST=""
VERBOSE=0

# ---------------------------------------------------------------------------
# Commands known not to work on Windows (skip by default)
# ---------------------------------------------------------------------------
WINDOWS_UNSUPPORTED="chroot,chcon,mknod,mkfifo,runcon"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
usage() {
    sed -n '2,/^# ====/{/^# /s/^# //p}' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -w|--winux-dir)   WINUX_DIR="$2"; shift 2 ;;
        -g|--gnu-dir)     GNU_DIR="$2"; shift 2 ;;
        -c|--command)     SINGLE_COMMAND="$2"; shift 2 ;;
        -o|--output-dir)  OUTPUT_DIR="$2"; shift 2 ;;
        -j|--jobs)        JOBS="$2"; shift 2 ;;
        -s|--skip)        SKIP_LIST="$2"; shift 2 ;;
        -v|--verbose)     VERBOSE=1; shift ;;
        -h|--help)        usage ;;
        *)                fail "Unknown option: $1"; usage ;;
    esac
done

# ---------------------------------------------------------------------------
# Auto-detect WinuxCmd directory
# ---------------------------------------------------------------------------
detect_winux_dir() {
    local candidates=(
        "../build-vs/usr/bin"
        "../.winuxcmd/bin"
        "../build-vs-stage-final/usr/bin"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -d "$candidate" ]]; then
            cd "$candidate" && pwd && cd - > /dev/null
            return
        fi
    done

    # Check Windows paths via WSL mount
    if [[ -d "/mnt/c/repo/unixwin-winuxcmd/build-vs/usr/bin" ]]; then
        echo "/mnt/c/repo/unixwin-winuxcmd/build-vs/usr/bin"
        return
    fi

    fail "Could not auto-detect WinuxCmd binary directory."
    fail "Use -w/--winux-dir to specify the path."
    exit 1
}

if [[ -z "$WINUX_DIR" ]]; then
    WINUX_DIR="$(detect_winux_dir)"
fi

info "WinuxCmd binary directory: $WINUX_DIR"
info "GNU coreutils source dir: $GNU_DIR"

# ---------------------------------------------------------------------------
# Validate prerequisites
# ---------------------------------------------------------------------------
if [[ ! -d "$GNU_DIR/tests" ]]; then
    fail "GNU coreutils tests not found at $GNU_DIR/tests"
    fail "Please install GNU coreutils source first."
    fail "See DOCS/en/gnu_test_baseline.md for instructions."
    exit 1
fi

if [[ ! -d "$GNU_DIR/tests/by-util" ]]; then
    fail "GNU test directory structure not found at $GNU_DIR/tests/by-util/"
    fail "This may not be a GNU coreutils source tree."
    exit 1
fi

# ---------------------------------------------------------------------------
# Setup output directory structure (mimicking uutils/tests/by-util/)
# ---------------------------------------------------------------------------
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="$OUTPUT_DIR/gnu-test-run-$TIMESTAMP"
SUMMARY_FILE="$REPORT_DIR/summary.json"
LOG_FILE="$REPORT_DIR/full-test.log"
RESULTS_DIR="$REPORT_DIR/by-util"

mkdir -p "$RESULTS_DIR"

# ---------------------------------------------------------------------------
# Helper: check if a command is in the skip list
# ---------------------------------------------------------------------------
is_skipped() {
    local cmd="$1"
    IFS=',' read -ra skip_items <<< "$SKIP_LIST"
    for item in "${skip_items[@]}"; do
        if [[ "$cmd" == "$item" ]]; then
            return 0
        fi
    done
    return 1
}

# ---------------------------------------------------------------------------
# Helper: check if command has a WinuxCmd binary
# ---------------------------------------------------------------------------
has_winux_binary() {
    local cmd="$1"
    [[ -f "$WINUX_DIR/$cmd" ]] || [[ -f "$WINUX_DIR/$cmd.exe" ]]
}

# ---------------------------------------------------------------------------
# Helper: run a single GNU test script with WinuxCmd in PATH
# ---------------------------------------------------------------------------
run_single_test() {
    local cmd="$1"
    local test_file="$2"
    local test_name
    test_name="$(basename "$test_file" .sh)"

    local test_result_dir="$RESULTS_DIR/$cmd"
    local stdout_file="$test_result_dir/${test_name}.stdout"
    local stderr_file="$test_result_dir/${test_name}.stderr"
    local exit_code_file="$test_result_dir/${test_name}.exit"
    local status_file="$test_result_dir/${test_name}.status"

    mkdir -p "$test_result_dir"

    # Create a temporary test workspace (clean per test)
    local test_workspace
    test_workspace="$(mktemp -d /tmp/winuxcmd-gnu-test-XXXXXX)"

    # Set up environment to intercept GNU commands with WinuxCmd
    # Prepend WinuxCmd dir to PATH so test scripts call WinuxCmd binaries
    export PATH="$WINUX_DIR:$PATH"

    # Set env var to signal test scripts
    export GNU_TEST_MODE=1

    # Run the test script in the workspace with WinuxCmd in PATH
    local exit_code=0
    (
        cd "$test_workspace"
        # Source test infrastructure if available
        if [[ -f "$GNU_DIR/tests/test-lib.sh" ]]; then
            export test_source_dir="$GNU_DIR/tests"
        fi

        timeout 300 bash "$test_file" > "$stdout_file" 2> "$stderr_file" || exit_code=$?
    ) || exit_code=$?

    echo "$exit_code" > "$exit_code_file"

    # Classify result
    local status="fail"
    if [[ $exit_code -eq 0 ]]; then
        status="pass"
    elif [[ $exit_code -eq 77 ]]; then
        status="skip"
    elif [[ $exit_code -eq 124 ]]; then
        status="timeout"
    fi

    echo "$status" > "$status_file"

    # Cleanup
    rm -rf "$test_workspace"

    if [[ "$VERBOSE" -eq 1 ]]; then
        case "$status" in
            pass)    ok "$cmd/$test_name" ;;
            skip)    warn "$cmd/$test_name (skipped)" ;;
            timeout) fail "$cmd/$test_name (timeout)" ;;
            *)       fail "$cmd/$test_name (exit=$exit_code)" ;;
        esac
    fi

    return 0
}

# ---------------------------------------------------------------------------
# Helper: run all GNU tests for a single command
# ---------------------------------------------------------------------------
run_command_tests() {
    local cmd="$1"
    local test_dir="$GNU_DIR/tests/by-util/$cmd"

    if [[ ! -d "$test_dir" ]]; then
        if [[ "$VERBOSE" -eq 1 ]]; then
            warn "No GNU test directory for: $cmd"
        fi
        return 0
    fi

    # Count test scripts
    local test_count=0
    for test_file in "$test_dir"/test-*.sh; do
        [[ -f "$test_file" ]] || continue
        test_count=$((test_count + 1))
    done

    if [[ $test_count -eq 0 ]]; then
        if [[ "$VERBOSE" -eq 1 ]]; then
            warn "No test scripts found for: $cmd"
        fi
        return 0
    fi

    info "Running $test_count tests for: $cmd"

    local pass=0 skip=0 fail_count=0 timeout=0

    for test_file in "$test_dir"/test-*.sh; do
        [[ -f "$test_file" ]] || continue
        run_single_test "$cmd" "$test_file"

        local status
        status="$(cat "$RESULTS_DIR/$cmd/$(basename "$test_file" .sh).status")"

        case "$status" in
            pass)    pass=$((pass + 1)) ;;
            skip)    skip=$((skip + 1)) ;;
            timeout) timeout=$((timeout + 1)) ;;
            *)       fail_count=$((fail_count + 1)) ;;
        esac
    done

    # Write per-command summary
    cat > "$RESULTS_DIR/$cmd/_summary.json" <<SUMEOF
{
    "command": "$cmd",
    "total": $test_count,
    "pass": $pass,
    "skip": $skip,
    "fail": $fail_count,
    "timeout": $timeout
}
SUMEOF

    printf "  Results: ${GREEN}%d pass${NC}, ${YELLOW}%d skip${NC}, ${RED}%d fail${NC}, %d timeout\n" \
        "$pass" "$skip" "$fail_count" "$timeout"

    # Return counts as colon-separated for caller
    echo "$pass:$skip:$fail_count:$timeout:$test_count"
}

# ---------------------------------------------------------------------------
# Main execution
# ---------------------------------------------------------------------------
header "GNU Coreutils Upstream Test Runner for WinuxCmd"
info "Timestamp: $TIMESTAMP"
info "Report directory: $REPORT_DIR"

# Determine which commands to test
COMMANDS_TO_TEST=()

if [[ -n "$SINGLE_COMMAND" ]]; then
    COMMANDS_TO_TEST=("$SINGLE_COMMAND")
else
    # Use GNU test directory listing (most reliable)
    for test_dir in "$GNU_DIR/tests/by-util"/*/; do
        [[ -d "$test_dir" ]] || continue
        local_cmd="$(basename "$test_dir")"
        COMMANDS_TO_TEST+=("$local_cmd")
    done
fi

total_pass=0
total_skip=0
total_fail=0
total_timeout=0
total_tests=0
total_commands=0
skipped_commands=0

header "Test Plan"
info "Commands to test: ${#COMMANDS_TO_TEST[@]}"
info "Skip list: ${SKIP_LIST:-none}"
echo ""

for cmd in "${COMMANDS_TO_TEST[@]}"; do
    # Check skip list
    if is_skipped "$cmd"; then
        if [[ "$VERBOSE" -eq 1 ]]; then
            warn "Skipping (unsupported on Windows): $cmd"
        fi
        skipped_commands=$((skipped_commands + 1))
        continue
    fi

    # Check if WinuxCmd binary exists
    if ! has_winux_binary "$cmd"; then
        if [[ "$VERBOSE" -eq 1 ]]; then
            warn "No WinuxCmd binary for: $cmd"
        fi
        skipped_commands=$((skipped_commands + 1))
        continue
    fi

    # Run tests
    result="$(run_command_tests "$cmd" 2>&1 | tail -1)"

    # Parse colon-separated result
    IFS=':' read -r cmd_pass cmd_skip cmd_fail cmd_timeout cmd_total <<< "$result"

    total_pass=$((total_pass + cmd_pass))
    total_skip=$((total_skip + cmd_skip))
    total_fail=$((total_fail + cmd_fail))
    total_timeout=$((total_timeout + cmd_timeout))
    total_tests=$((total_tests + cmd_total))
    total_commands=$((total_commands + 1))

    echo ""
done

# ---------------------------------------------------------------------------
# Generate summary report
# ---------------------------------------------------------------------------
header "Overall Results"

printf "  ${GREEN}Pass:      %d${NC}\n" "$total_pass"
printf "  ${YELLOW}Skip:      %d${NC}\n" "$total_skip"
printf "  ${RED}Fail:      %d${NC}\n" "$total_fail"
printf "  Timeout:   %d\n" "$total_timeout"
printf "  Total:     %d\n" "$total_tests"
echo ""
printf "  Commands tested:  %d\n" "$total_commands"
printf "  Commands skipped: %d\n" "$skipped_commands"

# Calculate pass rate
if [[ $total_tests -gt 0 ]]; then
    pass_rate=$((total_pass * 100 / total_tests))
    echo ""
    if [[ $pass_rate -ge 90 ]]; then
        ok "Pass rate: ${pass_rate}%"
    elif [[ $pass_rate -ge 70 ]]; then
        warn "Pass rate: ${pass_rate}%"
    else
        fail "Pass rate: ${pass_rate}%"
    fi
fi

# Write JSON summary
cat > "$SUMMARY_FILE" <<JSONEOF
{
    "timestamp": "$(date -Iseconds)",
    "winux_dir": "$WINUX_DIR",
    "gnu_dir": "$GNU_DIR",
    "commands_tested": $total_commands,
    "commands_skipped": $skipped_commands,
    "total_tests": $total_tests,
    "pass": $total_pass,
    "skip": $total_skip,
    "fail": $total_fail,
    "timeout": $total_timeout
}
JSONEOF

info "Summary written to: $SUMMARY_FILE"
info "Full log: $LOG_FILE"
info "Per-command results: $RESULTS_DIR/"

# ---------------------------------------------------------------------------
# Generate Markdown report
# ---------------------------------------------------------------------------
MD_FILE="$REPORT_DIR/REPORT.md"

cat > "$MD_FILE" <<MDEOF
# GNU Coreutils Upstream Test Report

**Generated**: $(date -Iseconds)
**WinuxCmd binary dir**: `$WINUX_DIR`
**GNU coreutils source**: `$GNU_DIR`

## Summary

| Metric | Value |
|--------|------:|
| Commands tested | $total_commands |
| Total tests | $total_tests |
| Pass | $total_pass |
| Skip | $total_skip |
| Fail | $total_fail |
| Timeout | $total_timeout |
| Pass rate | $(( total_tests > 0 ? total_pass * 100 / total_tests : 0 ))% |

## Per-Command Results

| Command | Total | Pass | Skip | Fail | Timeout |
|---------|------:|-----:|-----:|-----:|--------:|
MDEOF

# Append per-command rows
for cmd_dir in "$RESULTS_DIR"/*/; do
    [[ -d "$cmd_dir" ]] || continue
    cmd_name="$(basename "$cmd_dir")"
    [[ "$cmd_name" == "_"* ]] && continue

    if [[ -f "$cmd_dir/_summary.json" ]]; then
        c_total="$(grep -o '"total": *[0-9]*' "$cmd_dir/_summary.json" | grep -o '[0-9]*')"
        c_pass="$(grep -o '"pass": *[0-9]*' "$cmd_dir/_summary.json" | grep -o '[0-9]*')"
        c_skip="$(grep -o '"skip": *[0-9]*' "$cmd_dir/_summary.json" | grep -o '[0-9]*')"
        c_fail="$(grep -o '"fail": *[0-9]*' "$cmd_dir/_summary.json" | grep -o '[0-9]*')"
        c_timeout="$(grep -o '"timeout": *[0-9]*' "$cmd_dir/_summary.json" | grep -o '[0-9]*')"

        printf "| %s | %s | %s | %s | %s | %s |\n" \
            "$cmd_name" "$c_total" "$c_pass" "$c_skip" "$c_fail" "$c_timeout" >> "$MD_FILE"
    fi
done

# Append failed test details
echo "" >> "$MD_FILE"
echo "## Failed Tests" >> "$MD_FILE"
echo "" >> "$MD_FILE"

has_failures=0
for cmd_dir in "$RESULTS_DIR"/*/; do
    [[ -d "$cmd_dir" ]] || continue
    cmd_name="$(basename "$cmd_dir")"
    [[ "$cmd_name" == "_"* ]] && continue

    for status_file in "$cmd_dir"/*.status; do
        [[ -f "$status_file" ]] || continue
        test_name="$(basename "$status_file" .status)"
        [[ "$test_name" == "_"* ]] && continue

        status="$(cat "$status_file")"
        if [[ "$status" == "fail" ]] || [[ "$status" == "timeout" ]]; then
            echo "- `$cmd_name/$test_name\` — $status" >> "$MD_FILE"
            stderr_file="$cmd_dir/${test_name}.stderr"
            if [[ -f "$stderr_file" ]] && [[ -s "$stderr_file" ]]; then
                echo '  ```' >> "$MD_FILE"
                head -20 "$stderr_file" >> "$MD_FILE" 2>/dev/null || true
                echo '  ```' >> "$MD_FILE"
            fi
            has_failures=1
        fi
    done
done

if [[ $has_failures -eq 0 ]]; then
    echo "_No failures detected._" >> "$MD_FILE"
fi

echo "" >> "$MD_FILE"
echo "---" >> "$MD_FILE"
echo "_Report generated by `scripts/run_gnu_tests.sh`_" >> "$MD_FILE"

info "Markdown report: $MD_FILE"
echo ""
ok "Done. Full results in: $REPORT_DIR"