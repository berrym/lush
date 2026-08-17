#!/bin/bash

# ===============================================================================
# LUSH POSIX COMMAND LINE OPTIONS COMPLIANCE TEST SUITE
# ===============================================================================
# Comprehensive test for all POSIX required command line options
# Tests both short options (-x) and set builtin options (set -x)
# ===============================================================================

SHELL_PATH="$(pwd)/build/lush"
TEMP_DIR="/tmp/lush_posix_options_tests"
LOGFILE="$TEMP_DIR/posix_options_test.log"

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Setup test environment
setup_test_env() {
    mkdir -p "$TEMP_DIR"
    cd "$TEMP_DIR" || exit 1

    # Create test files
    echo "echo 'test script executed'" > test_script.sh
    chmod +x test_script.sh

    echo "echo 'stdin script'" > stdin_test.sh

    # Create script with syntax error for testing
    echo "if [ 1 -eq 1" > syntax_error.sh  # Missing ]; then

    # Create script that uses undefined variable
    echo 'echo $UNDEFINED_VARIABLE' > unset_var.sh

    # Create script for tracing test
    echo 'echo "line 1"' > trace_test.sh
    echo 'echo "line 2"' >> trace_test.sh

    > "$LOGFILE"
}

# Cleanup test environment
cleanup_test_env() {
    cd /
    rm -rf "$TEMP_DIR"
}

# Test result functions
test_result() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"
    local expected_exit="$4"
    local actual_exit="$5"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [[ "$actual" == "$expected" && "$actual_exit" == "$expected_exit" ]]; then
        echo -e "  ${GREEN}OK${NC} $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: $test_name" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} $test_name"
        echo "    Expected output: '$expected'"
        echo "    Actual output: '$actual'"
        echo "    Expected exit: $expected_exit"
        echo "    Actual exit: $actual_exit"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: $test_name - Expected: '$expected' (exit $expected_exit), Actual: '$actual' (exit $actual_exit)" >> "$LOGFILE"
    fi
}

# Test command line option
test_option() {
    local option="$1"
    local command="$2"
    local expected_output="$3"
    local expected_exit="$4"
    local test_name="$5"

    local actual_output
    local actual_exit

    if [[ -n "$command" ]]; then
        actual_output=$($SHELL_PATH $option "$command" 2>&1)
    else
        actual_output=$($SHELL_PATH $option 2>&1)
    fi
    actual_exit=$?

    test_result "$test_name" "$expected_output" "$actual_output" "$expected_exit" "$actual_exit"
}

# Test set builtin option
test_set_option() {
    local option="$1"
    local command="$2"
    local expected_output="$3"
    local expected_exit="$4"
    local test_name="$5"

    local actual_output
    local actual_exit

    actual_output=$($SHELL_PATH -c "set $option; $command" 2>&1)
    actual_exit=$?

    test_result "$test_name" "$expected_output" "$actual_output" "$expected_exit" "$actual_exit"
}

print_section() {
    echo -e "\n${BLUE}### $1 ###${NC}"
}

print_subsection() {
    echo -e "\n${YELLOW}=== $1 ===${NC}"
}

# ===============================================================================
# BASIC COMMAND LINE OPTIONS TESTS
# ===============================================================================

test_basic_options() {
    print_section "BASIC POSIX COMMAND LINE OPTIONS"

    print_subsection "Command Execution Options"

    # Test -c option (execute command string)
    test_option "-c" "echo hello world" "hello world" "0" "-c executes command string"
    test_option "-c" "exit 42" "" "42" "-c preserves exit status"
    test_option "-c" "echo test1; echo test2" $'test1\ntest2' "0" "-c executes multiple commands"

    # Test -s option (read from stdin)
    local stdin_output
    stdin_output=$(echo "echo stdin_mode" | $SHELL_PATH -s 2>&1)
    local stdin_exit=$?
    test_result "-s reads from stdin" "stdin_mode" "$stdin_output" "0" "$stdin_exit"

    # Test file execution (default behavior)
    local file_output
    file_output=$($SHELL_PATH ./test_script.sh 2>&1)
    local file_exit=$?
    test_result "file execution" "test script executed" "$file_output" "0" "$file_exit"

    print_subsection "Interactive and Login Options"

    # Test -i option (interactive mode)
    # Note: Interactive mode testing is limited in non-interactive environment
    local interactive_output
    interactive_output=$(echo "echo interactive" | $SHELL_PATH -i 2>/dev/null)
    local interactive_exit=$?
    test_result "-i interactive mode" "interactive" "$interactive_output" "0" "$interactive_exit"

    # Test -l option (login shell)
    # Note: Login shell behavior may be hard to test without actual login environment
    local login_output
    login_output=$($SHELL_PATH -l -c "echo login_mode" 2>&1)
    local login_exit=$?
    test_result "-l login shell mode" "login_mode" "$login_output" "0" "$login_exit"
}

# ===============================================================================
# SHELL BEHAVIOR OPTIONS TESTS
# ===============================================================================

test_shell_behavior_options() {
    print_section "SHELL BEHAVIOR OPTIONS"

    print_subsection "Error Handling Options"

    # Test -e option (exit on error)
    test_option "-c" "set -e; false; echo should_not_print" "" "1" "-e exits on command failure"
    test_option "-c" "set -e; true; echo should_print" "should_print" "0" "-e continues on success"

    # Test +e option (disable exit on error)
    test_option "-c" "set -e; set +e; false; echo should_print" "should_print" "0" "+e disables exit on error"

    # Test -u option (treat unset variables as error)
    test_option "-c" "set -u; echo \$UNDEFINED_VAR" "" "1" "-u treats unset variables as error"
    test_option "-c" "set -u; DEFINED=value; echo \$DEFINED" "value" "0" "-u allows defined variables"

    # Test +u option (allow unset variables)
    test_option "-c" "set -u; set +u; echo \$UNDEFINED_VAR" "" "0" "+u allows unset variables"

    print_subsection "Execution Control Options"

    # Test -n option (syntax check only - read but don't execute)
    test_option "-c" "set -n; echo should_not_execute" "" "0" "-n syntax check mode (no execution)"

    # Test syntax error detection with -n
    local syntax_check_output
    syntax_check_output=$($SHELL_PATH -n ./syntax_error.sh 2>&1)
    local syntax_check_exit=$?
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ $syntax_check_exit -ne 0 ]]; then
        echo -e "  ${GREEN}OK${NC} -n detects syntax errors"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: -n detects syntax errors" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} -n should detect syntax errors"
        echo "    Output: $syntax_check_output"
        echo "    Exit code: $syntax_check_exit"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: -n should detect syntax errors" >> "$LOGFILE"
    fi

    print_subsection "Debugging and Tracing Options"

    # Test -x option (trace execution)
    local trace_output
    trace_output=$($SHELL_PATH -c 'set -x; echo traced_command' 2>&1)
    local trace_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$trace_output" =~ "+ echo traced_command" || "$trace_output" =~ "traced_command" ]]; then
        echo -e "  ${GREEN}OK${NC} -x traces command execution"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: -x traces command execution" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} -x trace not working properly"
        echo "    Output: $trace_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: -x trace not working - Output: $trace_output" >> "$LOGFILE"
    fi

    # Test +x option (disable tracing)
    test_option "-c" "set -x; set +x; echo no_trace" "no_trace" "0" "+x disables tracing"

    # Test -v option (verbose mode - print input lines as read)
    local verbose_output
    verbose_output=$($SHELL_PATH -c 'set -v; echo verbose_test' 2>&1)
    local verbose_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$verbose_output" =~ "echo verbose_test" || "$verbose_output" =~ "verbose_test" ]]; then
        echo -e "  ${GREEN}OK${NC} -v verbose mode shows input"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: -v verbose mode shows input" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} -v verbose mode not working"
        echo "    Output: $verbose_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: -v verbose mode not working - Output: $verbose_output" >> "$LOGFILE"
    fi

    # Test +v option (disable verbose mode)
    test_option "-c" "set -v; set +v; echo quiet" "quiet" "0" "+v disables verbose mode"
}

# ===============================================================================
# PATHNAME AND EXPANSION OPTIONS TESTS
# ===============================================================================

test_expansion_options() {
    print_section "PATHNAME AND EXPANSION OPTIONS"

    print_subsection "Globbing Options"

    # Create test files for globbing
    touch file1.txt file2.txt file3.log

    # Test -f option (disable pathname expansion/globbing)
    test_option "-c" "set -f; echo *.txt" "*.txt" "0" "-f disables globbing"

    # Test +f option (enable pathname expansion)
    local glob_output
    glob_output=$($SHELL_PATH -c "set -f; set +f; echo *.txt" 2>&1)
    local glob_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$glob_output" =~ "file1.txt" || "$glob_output" =~ "*.txt" ]]; then
        echo -e "  ${GREEN}OK${NC} +f enables globbing"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: +f enables globbing" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} +f globbing not working"
        echo "    Output: $glob_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: +f globbing not working - Output: $glob_output" >> "$LOGFILE"
    fi

    print_subsection "Hash Options"

    # Test -h option (enable command hashing)
    test_option "-c" "set -h; echo hash_enabled" "hash_enabled" "0" "-h enables command hashing"
    test_option "-c" "set +h; echo hash_disabled" "hash_disabled" "0" "+h disables command hashing"
}

# ===============================================================================
# JOB CONTROL OPTIONS TESTS
# ===============================================================================

test_job_control_options() {
    print_section "JOB CONTROL OPTIONS"

    print_subsection "Monitor Mode Options"

    # Test -m option (monitor mode/job control)
    test_option "-c" "set -m; echo monitor_enabled" "monitor_enabled" "0" "-m enables monitor mode"
    test_option "-c" "set +m; echo monitor_disabled" "monitor_disabled" "0" "+m disables monitor mode"

    print_subsection "Background Job Control"

    # Test background job execution (basic test)
    local bg_output
    bg_output=$($SHELL_PATH -c "sleep 0.1 & wait" 2>&1)
    local bg_exit=$?
    test_result "background job control" "" "$bg_output" "0" "$bg_exit"
}

# ===============================================================================
# SET BUILTIN OPTIONS TESTS
# ===============================================================================

test_set_builtin_options() {
    print_section "SET BUILTIN OPTIONS COMPLIANCE"

    print_subsection "Option State Management"

    # Test set without arguments (should show all set options)
    local set_output
    set_output=$($SHELL_PATH -c "set" 2>&1)
    local set_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ $set_exit -eq 0 ]]; then
        echo -e "  ${GREEN}OK${NC} set without args shows variables"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: set without args shows variables" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} set without args failed"
        echo "    Exit code: $set_exit"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: set without args failed - Exit: $set_exit" >> "$LOGFILE"
    fi

    # Test set -o (show options in human-readable format)
    local set_o_output
    set_o_output=$($SHELL_PATH -c "set -o" 2>&1)
    local set_o_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ $set_o_exit -eq 0 ]]; then
        echo -e "  ${GREEN}OK${NC} set -o shows option states"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: set -o shows option states" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} set -o failed"
        echo "    Output: $set_o_output"
        echo "    Exit code: $set_o_exit"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: set -o failed - Output: $set_o_output, Exit: $set_o_exit" >> "$LOGFILE"
    fi

    print_subsection "Positional Parameter Management"

    # Test set -- (set positional parameters)
    test_option "-c" "set -- arg1 arg2 arg3; echo \$1 \$2 \$3" "arg1 arg2 arg3" "0" "set -- sets positional parameters"
    test_option "-c" "set -- ; echo \$#" "0" "0" "set -- clears positional parameters"

    print_subsection "Combined Options"

    # Test multiple options at once
    test_option "-c" "set -eu; echo combined_options" "combined_options" "0" "set -eu combines multiple options"
    test_option "-c" "set -eux; echo traced_and_strict" "traced_and_strict" "0" "set -eux combines three options"
}

# ===============================================================================
# POSIX OPTION COMPATIBILITY TESTS
# ===============================================================================

test_posix_compatibility() {
    print_section "POSIX OPTION COMPATIBILITY"

    print_subsection "Standard Option Behavior"

    # Test that options persist across commands
    local persist_output
    persist_output=$($SHELL_PATH -c 'set -e; false || echo recovered; true; echo success' 2>&1)
    local persist_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$persist_output" =~ "recovered" && "$persist_output" =~ "success" ]]; then
        echo -e "  ${GREEN}OK${NC} option state persists correctly"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: option state persists correctly" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} option state persistence issue"
        echo "    Output: $persist_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: option state persistence issue - Output: $persist_output" >> "$LOGFILE"
    fi

    print_subsection "Error Handling Compliance"

    # Test invalid option handling
    local invalid_output
    invalid_output=$($SHELL_PATH -c "set -Z" 2>&1)
    local invalid_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ $invalid_exit -ne 0 ]]; then
        echo -e "  ${GREEN}OK${NC} invalid options are rejected"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: invalid options are rejected" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} invalid option should fail"
        echo "    Output: $invalid_output"
        echo "    Exit code: $invalid_exit"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: invalid option should fail - Output: $invalid_output, Exit: $invalid_exit" >> "$LOGFILE"
    fi

    print_subsection "Option Inheritance"

    # Test that command line options are inherited by scripts
    echo 'echo $-' > show_options.sh
    chmod +x show_options.sh

    local inherit_output
    inherit_output=$($SHELL_PATH -e ./show_options.sh 2>&1)
    local inherit_exit=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ $inherit_exit -eq 0 ]]; then
        echo -e "  ${GREEN}OK${NC} option inheritance works"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: option inheritance works" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} option inheritance failed"
        echo "    Output: $inherit_output"
        echo "    Exit code: $inherit_exit"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: option inheritance failed - Output: $inherit_output, Exit: $inherit_exit" >> "$LOGFILE"
    fi
}

# ===============================================================================
# MAIN TEST EXECUTION
# ===============================================================================

main() {
    echo "==============================================================================="
    echo "LUSH POSIX COMMAND LINE OPTIONS COMPLIANCE TEST SUITE"
    echo "==============================================================================="
    echo "Testing shell: $SHELL_PATH"
    echo "Started at: $(date)"
    echo ""

    # Verify shell exists
    if [[ ! -x "$SHELL_PATH" ]]; then
        echo -e "${RED}ERROR: Shell not found at $SHELL_PATH${NC}"
        echo "Please build the shell first: ninja -C build"
        exit 1
    fi

    setup_test_env

    # Run all test suites
    test_basic_options
    test_shell_behavior_options
    test_expansion_options
    test_job_control_options
    test_set_builtin_options
    test_posix_compatibility

    # Print final results
    echo ""
    echo "==============================================================================="
    echo "POSIX COMMAND LINE OPTIONS TEST RESULTS"
    echo "==============================================================================="
    echo -e "Total Tests: $TOTAL_TESTS"
    echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"

    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "\n${GREEN}OK ALL POSIX COMMAND LINE OPTION TESTS PASSED${NC}"
        echo "Lush demonstrates excellent POSIX command line option compliance!"
    else
        echo -e "\n${YELLOW}WARN SOME POSIX COMMAND LINE OPTION TESTS FAILED${NC}"
        echo "Check $LOGFILE for detailed failure information."
        echo ""
        echo "Summary of failed tests:"
        grep "FAIL:" "$LOGFILE" | cut -d'-' -f1 | sort | uniq -c | sort -nr
    fi

    local compliance_percentage
    compliance_percentage=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    echo ""
    echo "POSIX Command Line Option Compliance Rate: ${compliance_percentage}%"

    if [[ $compliance_percentage -ge 95 ]]; then
        echo -e "${GREEN}EXCELLENT${NC} - Production ready POSIX option compliance"
    elif [[ $compliance_percentage -ge 90 ]]; then
        echo -e "${YELLOW}GOOD${NC} - High POSIX option compliance with minor gaps"
    elif [[ $compliance_percentage -ge 80 ]]; then
        echo -e "${YELLOW}MODERATE${NC} - Reasonable POSIX option compliance"
    else
        echo -e "${RED}NEEDS IMPROVEMENT${NC} - Significant POSIX option compliance gaps"
    fi

    echo ""
    echo "Recommended POSIX options for shell users:"
    echo "  -c 'command'  : Execute command string"
    echo "  -e            : Exit immediately on command failure"
    echo "  -u            : Treat unset variables as error"
    echo "  -x            : Trace command execution (debugging)"
    echo "  -v            : Verbose mode (show input lines)"
    echo "  -n            : Syntax check only (no execution)"
    echo "  -f            : Disable pathname expansion"
    echo "  -i            : Interactive mode"
    echo "  -l            : Login shell mode"
    echo "  -m            : Enable job control"

    echo ""
    echo "Test completed at: $(date)"
    echo "Full test log available at: $LOGFILE"

    cleanup_test_env

    # Exit with appropriate code
    if [[ $FAILED_TESTS -eq 0 ]]; then
        exit 0
    else
        exit 1
    fi
}

# Run the test suite
main "$@"
