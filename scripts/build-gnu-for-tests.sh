#!/usr/bin/env bash
# WinuxCmd GNU Coreutils Test Integration
# Based on uutils/coreutils approach
# Usage: bash scripts/build-gnu-for-tests.sh

set -e

GNU_DIR="/mnt/j/gnu-coreutils-9.11"
UU_DIR="/mnt/d/repo/unixwin-winuxcmd"

echo "=== Building GNU Coreutils for test infrastructure ==="

cd "${GNU_DIR}"

# Configure if not already done
if [ ! -f Makefile ]; then
    echo "Configuring GNU coreutils..."
    FORCE_UNSAFE_CONFIGURE=1 ./configure --quiet --disable-nls --disable-dependency-tracking
fi

# Build GNU binaries (needed as test harness)
echo "Building GNU binaries..."
make -j$(nproc) 2>/dev/null

# Build test infrastructure
echo "Building test infrastructure..."
make -j$(nproc) gnulib-tests 2>/dev/null

echo "=== Patching GNU tests for WinuxCmd ==="

# Point PATH to WinuxCmd build directory
UU_BUILD="${UU_DIR}/build-vs/usr/bin"
echo "WinuxCmd binaries: ${UU_BUILD}"

# Patch tests/local.mk to use WinuxCmd PATH
if [ -f tests/local.mk ]; then
    sed -i "s|^[[:blank:]]*PATH=.*|  PATH='${UU_BUILD//\\/\\/}$(PATH_SEPARATOR)'"\$\$PATH" \\/" tests/local.mk
fi

# Patch Makefile to use WinuxCmd PATH  
if [ -f Makefile ]; then
    sed -i "s|^[[:blank:]]*PATH=.*|  PATH='${UU_BUILD//\\/\\/}$(PATH_SEPARATOR)'"\$\$PATH" \\/" Makefile
fi

# Create symlinks for all WinuxCmd binaries
for binary in $(ls "${UU_BUILD}"/*.exe 2>/dev/null | xargs -I{} basename {} .exe); do
    [ -e "${UU_BUILD}/${binary}" ] || ln -vf "${UU_BUILD}/${binary}.exe" "${UU_BUILD}/${binary}" 2>/dev/null || true
done

# Skip tests that don't work on Windows
SKIP_TESTS="tests/chcon tests/runcon tests/selinux tests/misc/no-mtab"

echo "=== Running GNU Tests ==="
echo "GNU dir: ${GNU_DIR}"
echo "WinuxCmd: ${UU_BUILD}"
echo ""

# Run tests
make -j4 check SUBDIRS=. RUN_EXPENSIVE_TESTS=yes RUN_VERY_EXPENSIVE_TESTS=yes \
    VERBOSE=no gl_public_submodule_commit="" \
    2>&1 | tee /tmp/winuxcmd-gnu-test-results.log

echo ""
echo "=== Results saved to /tmp/winuxcmd-gnu-test-results.log ==="
