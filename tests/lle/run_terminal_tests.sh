#!/bin/bash
# Run LLE terminal capability tests
# This script works in both TTY and non-TTY environments

set -e

echo "==================================================================="
echo "Building LLE Terminal Tests"
echo "==================================================================="
echo ""

# Build directory
BUILD_DIR="../../build"
if [ ! -d "$BUILD_DIR" ]; then
    BUILD_DIR="../../build"
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found"
    echo "Please run 'meson setup build' first"
    exit 1
fi

# Compile the test
echo "Compiling test_terminal_capability..."
gcc -o test_terminal_capability \
    unit/test_terminal_capability.c \
    ../../src/lle/foundation/terminal/terminal.c \
    -I../../src \
    -I../../include \
    -std=c11 \
    -Wall -Wextra \
    -g

echo "OK Compilation successful"
echo ""

# Run the test
echo "==================================================================="
echo "Running Terminal Capability Tests"
echo "==================================================================="
echo ""

if [ -t 0 ]; then
    echo "Running in TTY environment"
    echo "(All tests will execute)"
else
    echo "Running in NON-TTY environment (pipe, CI/CD, AI interface)"
    echo "(TTY-specific tests will be skipped)"
fi
echo ""

./test_terminal_capability
TEST_RESULT=$?

echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo "OK ALL TESTS PASSED"
else
    echo "FAIL SOME TESTS FAILED"
fi

# Cleanup
rm -f test_terminal_capability

exit $TEST_RESULT
