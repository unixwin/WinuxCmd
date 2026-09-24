#!/usr/bin/env bash
# =============================================================================
# compare_outputs.sh — Compare GNU vs WinuxCmd command outputs
# =============================================================================
#
# For each command, runs both the GNU version and the WinuxCmd version
# with the same inputs, captures stdout/stderr/exit code, and produces
# a unified diff report.
#
# Inspired by uutils/coreutils comparison approach:
#   1. Create a clean sandbox per command
#   2. Run GNU version, capture output
#   3. Run WinuxCmd version, capture output
#   4. Diff the results, classify as same/different/unsupported
#
# Usage:
#   ./scripts/compare_outputs.sh [OPTIONS]
#
# Options:
#   -w, --winux-dir DIR     Path to WinuxCmd binaries (default: auto-detect)
#   -g, --gnu-dir DIR       Path to GNU binaries (default: /usr/bin)
#   -c, --command CMD       Compare a single command
#   -o, --output-dir DIR    Output directory (default: ./test-reports/compare)
#   -v, --verbose           Verbose output
#   -h, --help              Show this help message
# =============================================================================

set -euo pipefail

# Color helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

info()  { printf "${BLUE}[INFO]${NC}  %s\n" "$*"; }
ok()    { printf "${GREEN}[OK]${NC}    %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
fail()  { printf "${RED}[FAIL]${NC}  %s\n" "$*"; }
header() { printf "\n${BOLD}=== %s ===${NC}\n" "$*"; }

# Defaults
WINUX_DIR=""
GNU_DIR="/usr/bin"
SINGLE_COMMAND=""
OUTPUT_DIR="./test-reports/compare"
VERBOSE=0

# ---------------------------------------------------------------------------
# Test cases for each command
# Format per entry: "test_name|args|stdin|description"
# Multiple test cases separated by ;;
# ---------------------------------------------------------------------------
declare -A TEST_CASES

# --- File output commands ---
TEST_CASES["cat"]="echo_hello|sample.txt||Echo a file"
TEST_CASES["tac"]="reverse_lines|sample.txt||Reverse lines of a file"
TEST_CASES["nl"]="number_lines|sample.txt||Number lines"
TEST_CASES["head"]="head_2_lines|-n 2 sample.txt||First 2 lines"
TEST_CASES["tail"]="tail_2_lines|-n 2 sample.txt||Last 2 lines"
TEST_CASES["cut"]="cut_field|-d, -f2 csv.txt||Cut second CSV field"
TEST_CASES["paste"]="paste_side|sample_a.txt sample_b.txt||Side-by-side paste"
TEST_CASES["expand"]="expand_tabs|tabbed.txt||Expand tabs to spaces"
TEST_CASES["unexpand"]="unexpand_tabs|spaces.txt||Unexpand spaces to tabs"
TEST_CASES["fmt"]="fmt_wrap|long_line.txt||Reformat paragraph"
TEST_CASES["fold"]="fold_wrap|-w 20 long_line.txt||Fold at 20 columns"

# --- Sorting/uniq ---
TEST_CASES["sort"]="sort_alpha|sortable.txt||Sort alphabetically"
TEST_CASES["sort"]+=";;sort_numeric|-n numsort.txt||Sort numerically"
TEST_CASES["sort"]+=";;sort_reverse|-r sortable.txt||Sort in reverse"
TEST_CASES["uniq"]="uniq_basic|preuniq.txt||Filter adjacent duplicates"
TEST_CASES["uniq"]+=";;uniq_count|-c preuniq.txt||Count duplicates"
TEST_CASES["shuf"]="shuf_seed|-e -n 3 --random-source=/dev/urandom 1 2 3 4 5||Shuffle with seed"
TEST_CASES["comm"]="comm_basic|sorted_a.txt sorted_b.txt||Compare sorted files"
TEST_CASES["tsort"]="tsort_basic|tsort_input.txt||Topological sort"

# --- Summarizing ---
TEST_CASES["wc"]="wc_lines|sample.txt||Count lines/words/chars"
TEST_CASES["wc"]+=";;wc_bytes|-c sample.txt||Count bytes"
TEST_CASES["sum"]="sum_basic|sample.txt||BSD checksum"
TEST_CASES["cksum"]="cksum_basic|sample.txt||POSIX checksum"
TEST_CASES["md5sum"]="md5sum_basic|sample.txt||MD5 digest"
TEST_CASES["sha256sum"]="sha256sum_basic|sample.txt||SHA256 digest"

# --- Encoding ---
TEST_CASES["base64"]="base64_enc|sample.txt||Base64 encode"
TEST_CASES["base32"]="base32_enc|sample.txt||Base32 encode"
TEST_CASES["base64"]+=";;base64_dec|b64_sample.txt|-d|Base64 decode"

# --- File operations ---
TEST_CASES["touch"]="touch_create|touch_test.txt||Create a file"
TEST_CASES["mkdir"]="mkdir_p|-p a/b/c||Create nested dirs"
TEST_CASES["ln"]="ln_basic|sample.txt link_sample.txt||Create a link"
TEST_CASES["cp"]="cp_basic|sample.txt cp_out.txt||Copy a file"
TEST_CASES["mv"]="mv_basic|sample.txt mv_out.txt||Move a file"
TEST_CASES["rm"]="rm_basic|rm_test.txt||Remove a file"
TEST_CASES["rmdir"]="rmdir_basic|rmdir_test||Remove a directory"
TEST_CASES["install"]="install_basic|sample.txt install_out.txt||Install a file"
TEST_CASES["chmod"]="chmod_basic|sample.txt||Change file mode"

# --- Text processing ---
TEST_CASES["tr"]="tr_upper|[a-z] [A-Z] < sample.txt||Translate lowercase to upper"
TEST_CASES["tr"]+=";;tr_delete|-d [aeiou] < sample.txt||Delete vowels"
TEST_CASES["sed"]="sed_substitute|s/alpha/beta/g sample.txt||Substitute pattern"
TEST_CASES["sed"]+=";;sed_delete|1d sample.txt||Delete first line"
TEST_CASES["grep"]="grep_basic|alpha sample.txt||Search for pattern"
TEST_CASES["grep"]+=";;grep_line|-n alpha sample.txt||Show line numbers"
TEST_CASES["grep"]+=";;grep_invert|-v alpha sample.txt||Invert match"
TEST_CASES["expr"]="expr_arith|1 + 2||Arithmetic expression"

# --- File info ---
TEST_CASES["ls"]="ls_basic|||List directory"
TEST_CASES["ls"]+=";;ls_long|-la .||Long listing"
TEST_CASES["ls"]+=";;ls_human|-lh .||Human-readable sizes"
TEST_CASES["du"]="du_basic|du_test||Disk usage"
TEST_CASES["stat"]="stat_basic|sample.txt||File status"
TEST_CASES["file"]="file_basic|sample.txt||File type detection"
TEST_CASES["readlink"]="readlink_basic|readlink_test||Read symlink target"
TEST_CASES["realpath"]="realpath_basic|sample.txt||Resolve path"
TEST_CASES["dirname"]="dirname_basic|/path/to/file.txt||Directory component"
TEST_CASES["basename"]="basename_basic|/path/to/file.txt||Base component"
TEST_CASES["basename"]+=";;basename_ext|/path/to/file.txt .txt||Strip suffix"
TEST_CASES["pathchk"]="pathchk_basic|sample.txt||Check path validity"

# --- System ---
TEST_CASES["uname"]="uname_basic|||System info"
TEST_CASES["uname"]+=";;uname_a|-a||All system info"
TEST_CASES["hostname"]="hostname_basic|||Show hostname"
TEST_CASES["nproc"]="nproc_basic|||Count CPUs"
TEST_CASES["whoami"]="whoami_basic|||Current user"
TEST_CASES["id"]="id_basic|||User identity"
TEST_CASES["uptime"]="uptime_basic|||System uptime"
TEST_CASES["printenv"]="printenv_basic|||Print environment"
TEST_CASES["yes"]="yes_basic|-n 3 ||Repeat y"

# --- Misc ---
TEST_CASES["numfmt"]="numfmt_basic|--to=iec 1024||Format numbers"
TEST_CASES["paste"]+=";;paste_delim|-d, sample_a.txt sample_b.txt||Paste with delimiter"
TEST_CASES["pr"]="pr_basic|sample.txt||Paginate"
TEST_CASES["ptx"]="ptx_basic|sample.txt||Permuted index"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        -w|--winux-dir)   WINUX_DIR="$2"; shift 2 ;;
        -g|--gnu-dir)     GNU_DIR="$2"; shift 2 ;;
        -c|--command)     SINGLE_COMMAND="$2"; shift 2 ;;
        -o|--output-dir)  OUTPUT_DIR="$2"; shift 2 ;;
        -v|--verbose)     VERBOSE=1; shift ;;
        -h|--help)
            sed -n '2,/^# ====/{/^# /s/^# //p}' "$0"
            exit 0 ;;
        *) fail "Unknown option: $1"; exit 1 ;;
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
            (cd "$candidate" && pwd)
            return
        fi
    done

    if [[ -d "/mnt/c/repo/unixwin-winuxcmd/build-vs/usr/bin" ]]; then
        echo "/mnt/c/repo/unixwin-winuxcmd/build-vs/usr/bin"
        return
    fi

    fail "Could not auto-detect WinuxCmd binary directory."
    exit 1
}

if [[ -z "$WINUX_DIR" ]]; then
    WINUX_DIR="$(detect_winux_dir)"
fi

info "WinuxCmd dir: $WINUX_DIR"
info "GNU dir:      $GNU_DIR"

# ---------------------------------------------------------------------------
# Setup sandbox and prepare test fixtures
# ---------------------------------------------------------------------------
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="$OUTPUT_DIR/$TIMESTAMP"
mkdir -p "$REPORT_DIR"

setup_sandbox() {
    local sandbox="$1"
    mkdir -p "$sandbox"

    # Create common test fixture files
    printf "alpha\nbeta\nalpha beta\ngamma\n" > "$sandbox/sample.txt"
    printf "pear\napple\nbanana\n" > "$sandbox/sortable.txt"
    printf "10\n20\n3\n1\n5\n" > "$sandbox/numsort.txt"
    printf "alpha\nalpha\nbeta\nalpha\nbeta\nbeta\n" > "$sandbox/preuniq.txt"
    printf "apple\ncherry\nbanana\n" > "$sandbox/sorted_a.txt"
    printf "apricot\nbanana\ncherry\nfig\n" > "$sandbox/sorted_b.txt"
    printf "hello\tworld\tfoo\n" > "$sandbox/tabbed.txt"
    printf "one     two     three\n" > "$sandbox/spaces.txt"
    printf "this is a very long line that needs to be wrapped at some point\n" > "$sandbox/long_line.txt"
    printf "a,b,c\n1,2,3\nx,y,z\n" > "$sandbox/csv.txt"
    printf "a\nb\na\nb\na\n" > "$sandbox/sample_a.txt"
    printf "c\nd\ne\nf\ng\n" > "$sandbox/sample_b.txt"
    printf "hello    world\nfoo   bar\n" > "$sandbox/squeeze.txt"
    printf "alpha beta gamma delta\n" > "$sandbox/expr_input.txt"

    # Tsort input
    printf "a b\nb c\nc d\n" > "$sandbox/tsort_input.txt"

    # base64 encoded sample
    echo "SGVsbG8sIHdvcmxkIQ==" > "$sandbox/b64_sample.txt"

    # Create directories for rmdir test
    mkdir -p "$sandbox/rmdir_test"

    # Create files for various tests
    touch "$sandbox/rm_test.txt"
    touch "$sandbox/touch_test.txt"

    # symlink for readlink test
    ln -sf sample.txt "$sandbox/readlink_test" 2>/dev/null || true

    # du test directory
    mkdir -p "$sandbox/du_test/sub"
    dd if=/dev/zero of="$sandbox/du_test/bigfile" bs=1024 count=10 2>/dev/null || true
    dd if=/dev/zero of="$sandbox/du_test/sub/small" bs=256 count=1 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Run a command and capture output
# ---------------------------------------------------------------------------
run_cmd() {
    local binary="$1"
    local args="$2"
    local stdin_data="$3"
    local workdir="$4"
    local out_file="$5"
    local err_file="$6"
    local exit_file="$7"

    local exit_code=0

    if [[ -n "$stdin_data" ]]; then
        echo "$stdin_data" | timeout 30 "$binary" $args > "$out_file" 2> "$err_file" || exit_code=$?
    else
        timeout 30 "$binary" $args > "$out_file" 2> "$err_file" || exit_code=$?
    fi

    echo "$exit_code" > "$exit_file"
}

# ---------------------------------------------------------------------------
# Normalize output for comparison
# ---------------------------------------------------------------------------
normalize() {
    local file="$1"
    if [[ -f "$file" ]]; then
        tr -d '\r' < "$file" | sed 's/[[:space:]]*$//'
    fi
}

# ---------------------------------------------------------------------------
# Compare a single command
# ---------------------------------------------------------------------------
compare_command() {
    local cmd="$1"
    local test_spec="$2"

    # Parse test specification
    IFS='|' read -r test_name test_args test_stdin test_desc <<< "$test_spec"

    # Resolve binaries
    local gnu_bin="$GNU_DIR/$cmd"
    local winux_bin="$WINUX_DIR/$cmd"

    if [[ ! -x "$gnu_bin" ]]; then
        gnu_bin="$GNU_DIR/$cmd.exe"
    fi
    if [[ ! -x "$gnu_bin" ]]; then
        warn "GNU binary not found: $cmd -- skipping $test_name"
        return 0
    fi

    if [[ ! -f "$winux_bin" ]] && [[ ! -f "$winux_bin.exe" ]]; then
        warn "WinuxCmd binary not found: $cmd -- skipping $test_name"
        return 0
    fi

    # Create sandbox
    local sandbox
    sandbox="$(mktemp -d /tmp/winuxcmd-compare-XXXXXX)"
    setup_sandbox "$sandbox"

    # Output files
    local cmd_dir="$REPORT_DIR/$cmd"
    mkdir -p "$cmd_dir"
    local gnu_stdout="$cmd_dir/${test_name}.gnu.stdout"
    local gnu_stderr="$cmd_dir/${test_name}.gnu.stderr"
    local gnu_exit="$cmd_dir/${test_name}.gnu.exit"
    local winux_stdout="$cmd_dir/${test_name}.winux.stdout"
    local winux_stderr="$cmd_dir/${test_name}.winux.stderr"
    local winux_exit="$cmd_dir/${test_name}.winux.exit"

    # Run GNU version
    (cd "$sandbox" && run_cmd "$gnu_bin" "$test_args" "$test_stdin" "$sandbox" \
        "$gnu_stdout" "$gnu_stderr" "$gnu_exit")

    # Run WinuxCmd version
    (cd "$sandbox" && run_cmd "$winux_bin" "$test_args" "$test_stdin" "$sandbox" \
        "$winux_stdout" "$winux_stderr" "$winux_exit")

    # Compare
    local same_stdout=0 same_stderr=0 same_exit=1

    if diff <(normalize "$gnu_stdout") <(normalize "$winux_stdout") > /dev/null 2>&1; then
        same_stdout=1
    fi
    if diff <(normalize "$gnu_stderr") <(normalize "$winux_stderr") > /dev/null 2>&1; then
        same_stderr=1
    fi

    local gnu_exit_code winux_exit_code
    gnu_exit_code="$(cat "$gnu_exit" 2>/dev/null || echo 127)"
    winux_exit_code="$(cat "$winux_exit" 2>/dev/null || echo 127)"

    if [[ "$gnu_exit_code" -eq "$winux_exit_code" ]]; then
        same_exit=1
    fi

    # Classify
    local classification="same"
    if [[ $same_stdout -eq 0 ]] || [[ $same_stderr -eq 0 ]] || [[ $same_exit -eq 0 ]]; then
        classification="different"
    fi

    # Generate diff
    local diff_file="$cmd_dir/${test_name}.diff"
    {
        echo "=== GNU stdout vs WinuxCmd stdout ==="
        diff -u <(normalize "$gnu_stdout") <(normalize "$winux_stdout") || true
        echo ""
        echo "=== GNU stderr vs WinuxCmd stderr ==="
        diff -u <(normalize "$gnu_stderr") <(normalize "$winux_stderr") || true
        echo ""
        echo "=== Exit codes ==="
        echo "GNU:    $gnu_exit_code"
        echo "Winux:  $winux_exit_code"
    } > "$diff_file"

    # Output result line
    printf "%s|%s|%s|%s|%s|%s|%s\n" \
        "$cmd" "$test_name" "$classification" "$same_stdout" "$same_stderr" "$same_exit" "$test_desc"

    if [[ "$VERBOSE" -eq 1 ]]; then
        if [[ "$classification" == "same" ]]; then
            ok "$cmd/$test_name -- $test_desc"
        else
            fail "$cmd/$test_name -- $test_desc"
            if [[ $same_stdout -eq 0 ]]; then
                warn "  stdout differs (see $diff_file)"
            fi
            if [[ $same_exit -eq 0 ]]; then
                warn "  exit code differs: GNU=$gnu_exit_code, Winux=$winux_exit_code"
            fi
        fi
    fi

    # Cleanup sandbox
    rm -rf "$sandbox"
}

# ---------------------------------------------------------------------------
# Main execution
# ---------------------------------------------------------------------------
info "Output directory: $REPORT_DIR"

RESULTS_FILE="$REPORT_DIR/results.csv"
echo "command|test|classification|same_stdout|same_stderr|same_exit|description" > "$RESULTS_FILE"

total=0
same=0
different=0

# Iterate commands
commands_to_test=()

if [[ -n "$SINGLE_COMMAND" ]]; then
    commands_to_test=("$SINGLE_COMMAND")
else
    for cmd in "${!TEST_CASES[@]}"; do
        commands_to_test+=("$cmd")
    done
    IFS=$'\n' commands_to_test=($(sort <<< "${commands_to_test[*]}")); unset IFS
fi

for cmd in "${commands_to_test[@]}"; do
    if [[ -z "${TEST_CASES[$cmd]+x}" ]]; then
        warn "No test cases defined for: $cmd"
        continue
    fi

    header "$cmd"

    IFS=';;' read -ra test_specs <<< "${TEST_CASES[$cmd]}"

    for test_spec in "${test_specs[@]}"; do
        result="$(compare_command "$cmd" "$test_spec")"
        if [[ -n "$result" ]]; then
            echo "$result" >> "$RESULTS_FILE"
            total=$((total + 1))
            IFS='|' read -r _ _ classification _ _ _ <<< "$result"
            if [[ "$classification" == "same" ]]; then
                same=$((same + 1))
            else
                different=$((different + 1))
            fi
        fi
    done
done

# ---------------------------------------------------------------------------
# Generate summary
# ---------------------------------------------------------------------------
header "Summary"
printf "  Total comparisons: %d\n" "$total"
printf "  ${GREEN}Matching:           %d${NC}\n" "$same"
printf "  ${RED}Differing:          %d${NC}\n" "$different"

if [[ $total -gt 0 ]]; then
    match_rate=$((same * 100 / total))
    echo ""
    if [[ $match_rate -ge 90 ]]; then
        ok "Match rate: ${match_rate}%"
    elif [[ $match_rate -ge 70 ]]; then
        warn "Match rate: ${match_rate}%"
    else
        fail "Match rate: ${match_rate}%"
    fi
fi

# ---------------------------------------------------------------------------
# Generate Markdown report
# ---------------------------------------------------------------------------
MD_FILE="$REPORT_DIR/COMPARISON_REPORT.md"

cat > "$MD_FILE" <<'MDEOF'
# WinuxCmd vs GNU Coreutils Output Comparison

**Generated**: TIMESTAMP_PLACEHOLDER
**WinuxCmd dir**: WINUX_DIR_PLACEHOLDER
**GNU dir**: GNU_DIR_PLACEHOLDER

## Summary

| Metric | Value |
|--------|------:|
| Total comparisons | TOTAL_PLACEHOLDER |
| Matching | SAME_PLACEHOLDER |
| Differing | DIFF_PLACEHOLDER |
| Match rate | RATE_PLACEHOLDER% |

## Results

| Command | Test | Result | Stdout | Stderr | Exit | Description |
|---------|------|--------|--------|--------|------|-------------|
MDEOF

# Post-process the markdown template
sed -i "s/TIMESTAMP_PLACEHOLDER/$(date -Iseconds)/" "$MD_FILE"
sed -i "s|WINUX_DIR_PLACEHOLDER|$WINUX_DIR|" "$MD_FILE"
sed -i "s|GNU_DIR_PLACEHOLDER|$GNU_DIR|" "$MD_FILE"
sed -i "s/TOTAL_PLACEHOLDER/$total/" "$MD_FILE"
sed -i "s/SAME_PLACEHOLDER/$same/" "$MD_FILE"
sed -i "s/DIFF_PLACEHOLDER/$different/" "$MD_FILE"
sed -i "s/RATE_PLACEHOLDER/$(( total > 0 ? same * 100 / total : 0 ))/" "$MD_FILE"

# Append results rows
tail -n +2 "$RESULTS_FILE" | while IFS='|' read -r cmd test cls ss se se_exit desc; do
    icon="PASS"
    if [[ "$cls" == "different" ]]; then icon="FAIL"; fi
    printf "| %s | %s | %s (%s) | %s | %s | %s | %s |\n" \
        "$cmd" "$test" "$icon" "$cls" "$ss" "$se" "$se_exit" "$desc" >> "$MD_FILE"
done

# Append diff details for failing tests
echo "" >> "$MD_FILE"
echo "## Diff Details (Differing Tests)" >> "$MD_FILE"
echo "" >> "$MD_FILE"

has_diffs=0
for cmd_dir in "$REPORT_DIR"/*/; do
    [[ -d "$cmd_dir" ]] || continue
    cmd_name="$(basename "$cmd_dir")"

    for diff_file in "$cmd_dir"/*.diff; do
        [[ -f "$diff_file" ]] || continue
        diff_name="$(basename "$diff_file" .diff)"

        # Check if there are actual differences
        if grep -q '^[-+]' "$diff_file" 2>/dev/null; then
            echo "### $cmd_name/$diff_name" >> "$MD_FILE"
            echo '```diff' >> "$MD_FILE"
            cat "$diff_file" >> "$MD_FILE"
            echo '```' >> "$MD_FILE"
            echo "" >> "$MD_FILE"
            has_diffs=1
        fi
    done
done

if [[ $has_diffs -eq 0 ]]; then
    echo '_No differences found._' >> "$MD_FILE"
fi

echo "" >> "$MD_FILE"
echo '---' >> "$MD_FILE"
echo '_Report generated by scripts/compare_outputs.sh_' >> "$MD_FILE"

info "Results CSV: $RESULTS_FILE"
info "Markdown report: $MD_FILE"
echo ""
ok "Comparison complete. Results in: $REPORT_DIR"