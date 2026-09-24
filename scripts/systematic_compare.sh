#!/bin/bash
# WinuxCmd vs GNU Coreutils comprehensive comparison
# Usage: bash scripts/systematic_compare.sh

GNU_DIR="/opt/coreutils-9.11/bin"
WINUX_DIR="/mnt/d/repo/unixwin-winuxcmd/build-vs/usr/bin"
RESULTS_DIR="/tmp/winuxcmd-comparison"
mkdir -p "$RESULTS_DIR"

PASS=0
FAIL=0
SKIP=0
TOTAL=0

compare() {
    local cmd="$1"
    local args="$2"
    local input="$3"
    local description="$4"
    
    TOTAL=$((TOTAL + 1))
    
    # Create test input
    if [ -n "$input" ]; then
        echo -e "$input" > /tmp/winuxcmd_test_input.txt
    fi
    
    # Run GNU version
    local gnu_out
    if [ -n "$input" ]; then
        gnu_out=$(echo -e "$input" | $GNU_DIR/$cmd $args 2>&1)
    else
        gnu_out=$($GNU_DIR/$cmd $args 2>&1)
    fi
    local gnu_exit=$?
    
    # Run WinuxCmd version
    local winux_out
    if [ -n "$input" ]; then
        winux_out=$(echo -e "$input" | $WINUX_DIR/$cmd.exe $args 2>&1)
    else
        winux_out=$($WINUX_DIR/$cmd.exe $args 2>&1)
    fi
    local winux_exit=$?
    
    # Normalize
    gnu_out=$(echo "$gnu_out" | tr -d '\r' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
    winux_out=$(echo "$winux_out" | tr -d '\r' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
    
    # Compare
    if [ "$gnu_out" = "$winux_out" ] && [ "$gnu_exit" = "$winux_exit" ]; then
        echo "✓ $cmd $args" >> "$RESULTS_DIR/pass.txt"
        PASS=$((PASS + 1))
    else
        echo "✗ $cmd $args" >> "$RESULTS_DIR/fail.txt"
        echo "  Description: $description" >> "$RESULTS_DIR/fail.txt"
        echo "  GNU exit=$gnu_exit Winux=$winux_exit" >> "$RESULTS_DIR/fail.txt"
        echo "  GNU: $(echo "$gnu_out" | head -1)" >> "$RESULTS_DIR/fail.txt"
        echo "  Winux: $(echo "$winux_out" | head -1)" >> "$RESULTS_DIR/fail.txt"
        echo "" >> "$RESULTS_DIR/fail.txt"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== WinuxCmd vs GNU Coreutils Systematic Comparison ==="
echo "Started: $(date)"
echo ""

# === FILE OPERATIONS ===
compare "cat" "" "" "cat with no input"
compare "echo" "" "Hello World" "echo basic"
compare "echo" "-n" "Hello World" "echo -n no newline"
compare "echo" "-e" "Hello\tWorld" "echo -e escape"

# === TEXT PROCESSING ===
compare "sort" "" "banana\napple\ncherry" "sort basic"
compare "sort" "-r" "banana\napple\ncherry" "sort reverse"
compare "sort" "-n" "3\n1\n2" "sort numeric"
compare "uniq" "" "a\na\nb\nb\nb\nc" "uniq basic"
compare "uniq" "-c" "a\na\nb\nb\nb\nc" "uniq count"
compare "tr" "'a-z' 'A-Z'" "hello world" "tr lowercase to uppercase"
compare "cut" "-d: -f1" "root:x:0:0" "cut field 1"
compare "paste" "" "a\nb\nc" "paste basic"

# === LINE/WORD/COUNT ===
compare "wc" "" "Line one\nLine two\nLine three" "wc basic"
compare "wc" "-l" "Line one\nLine two\nLine three" "wc -l lines"
compare "wc" "-w" "Line one\nLine two" "wc -w words"
compare "head" "-n 3" "L1\nL2\nL3\nL4\nL5" "head -n 3"
compare "tail" "-n 2" "L1\nL2\nL3\nL4\nL5" "tail -n 2"

# === CHECKSUMS ===
compare "md5sum" "" "Hello World" "md5sum basic"
compare "sha256sum" "" "Hello World" "sha256sum basic"
compare "cksum" "" "Hello World" "cksum basic"

# === ENCODING ===
compare "base64" "" "Hello World" "base64 encode"
compare "base32" "" "Hello World" "base32 encode"

# === FILE INFO ===
compare "stat" "" "/tmp" "stat directory"
compare "file" "" "/tmp" "file on directory"
compare "du" "-s" "/tmp" "du summary"

# === DIRECTORY ===
compare "dirname" "" "/path/to/file" "dirname basic"
compare "basename" "" "/path/to/file" "basename basic"
compare "ls" "" "/tmp" "ls basic"
compare "ls" "-la" "/tmp" "ls -la"

# === MATH ===
compare "seq" "" "5" "seq 1-5"
compare "expr" "" "1 + 2" "expr addition"
compare "yes" "" "" "yes (1 line)"

# === SYSTEM ===
compare "uname" "" "" "uname basic"
compare "hostname" "" "" "hostname basic"
compare "id" "" "" "id basic"
compare "whoami" "" "" "whoami basic"
compare "uptime" "" "" "uptime basic"
compare "nproc" "" "" "nproc basic"
compare "free" "" "" "free basic"
compare "df" "" "" "df basic"

echo ""
echo "=== RESULTS ==="
echo "Total: $TOTAL"
echo "Pass: $PASS"
echo "Fail: $FAIL"
echo "Pass rate: $(( PASS * 100 / TOTAL ))%"
echo ""
echo "Detailed results in: $RESULTS_DIR/"
echo "Completed: $(date)"
