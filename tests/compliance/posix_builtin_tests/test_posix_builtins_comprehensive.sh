#!/bin/bash

# ===============================================================================
# LUSH POSIX BUILTIN COMPLIANCE TEST SUITE
# ===============================================================================
# Comprehensive test for all POSIX required builtins and their compliance
# Tests both presence and correct behavior according to POSIX standards
# ===============================================================================

SHELL_PATH="$(pwd)/build/lush"
TEMP_DIR="/tmp/lush_posix_builtin_tests"
LOGFILE="$TEMP_DIR/posix_builtin_test.log"

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
    echo "test content" > testfile.txt
    echo "#!/bin/bash" > testscript.sh
    echo "echo 'script executed'" >> testscript.sh
    chmod +x testscript.sh

    # Create directory structure
    mkdir -p testdir/subdir
    echo "nested file" > testdir/subdir/nested.txt

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
    local exit_code="$4"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [[ "$actual" == "$expected" && "$exit_code" == "0" ]]; then
        echo -e "  ${GREEN}OK${NC} $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: $test_name" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} $test_name"
        echo "    Expected: '$expected'"
        echo "    Actual: '$actual'"
        echo "    Exit Code: $exit_code"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: $test_name - Expected: '$expected', Actual: '$actual', Exit: $exit_code" >> "$LOGFILE"
    fi
}

# Test command existence
test_builtin_exists() {
    local builtin_name="$1"
    local result

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    result=$($SHELL_PATH -c "type $builtin_name" 2>/dev/null)
    if [[ $? -eq 0 && "$result" == "$builtin_name is a shell builtin" ]]; then
        echo -e "  ${GREEN}OK${NC} $builtin_name builtin exists"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: $builtin_name builtin exists" >> "$LOGFILE"
        return 0
    else
        echo -e "  ${RED}FAIL${NC} $builtin_name builtin missing"
        echo "    Output: $result"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: $builtin_name builtin missing - Output: $result" >> "$LOGFILE"
        return 1
    fi
}

# Run shell command and capture output
run_test() {
    local cmd="$1"
    local expected_output="$2"
    local test_name="$3"

    local output
    local exit_code

    output=$($SHELL_PATH -c "$cmd" 2>&1)
    exit_code=$?

    test_result "$test_name" "$expected_output" "$output" "0"
}

# Run shell command expecting specific exit code
run_test_exit_code() {
    local cmd="$1"
    local expected_exit="$2"
    local test_name="$3"

    local output
    local exit_code

    output=$($SHELL_PATH -c "$cmd" 2>/dev/null)
    exit_code=$?

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [[ "$exit_code" == "$expected_exit" ]]; then
        echo -e "  ${GREEN}OK${NC} $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: $test_name" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} $test_name"
        echo "    Expected exit code: $expected_exit"
        echo "    Actual exit code: $exit_code"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: $test_name - Expected exit: $expected_exit, Actual exit: $exit_code" >> "$LOGFILE"
    fi
}

print_section() {
    echo -e "\n${BLUE}### $1 ###${NC}"
}

print_subsection() {
    echo -e "\n${YELLOW}=== $1 ===${NC}"
}

# ===============================================================================
# POSIX BUILTIN EXISTENCE TESTS
# ===============================================================================

test_posix_builtin_existence() {
    print_section "POSIX REQUIRED BUILTIN EXISTENCE"

    print_subsection "Core Command Builtins"
    test_builtin_exists ":"
    test_builtin_exists "."
    test_builtin_exists "break"
    test_builtin_exists "continue"
    test_builtin_exists "eval"
    test_builtin_exists "exec"
    test_builtin_exists "exit"
    test_builtin_exists "return"

    print_subsection "File and Directory Builtins"
    test_builtin_exists "cd"
    test_builtin_exists "pwd"

    print_subsection "Variable and Environment Builtins"
    test_builtin_exists "export"
    test_builtin_exists "readonly"
    test_builtin_exists "set"
    test_builtin_exists "shift"
    test_builtin_exists "unset"

    print_subsection "I/O and Text Builtins"
    test_builtin_exists "echo"
    test_builtin_exists "printf"
    test_builtin_exists "read"

    print_subsection "Test and Logic Builtins"
    test_builtin_exists "test"
    test_builtin_exists "["
    test_builtin_exists "true"
    test_builtin_exists "false"

    print_subsection "System and Process Builtins"
    test_builtin_exists "times"
    test_builtin_exists "trap"
    test_builtin_exists "type"
    test_builtin_exists "umask"
    test_builtin_exists "ulimit"
    test_builtin_exists "wait"

    print_subsection "Option and Argument Builtins"
    test_builtin_exists "getopts"
    test_builtin_exists "hash"
}

# ===============================================================================
# POSIX BUILTIN FUNCTIONALITY TESTS
# ===============================================================================

test_colon_builtin() {
    print_subsection "Colon (:) Builtin Tests"

    run_test_exit_code ":" "0" "colon returns success"
    run_test_exit_code ": arg1 arg2" "0" "colon with arguments returns success"
    run_test ": && echo success" "success" "colon in conditional"
}

test_dot_builtin() {
    print_subsection "Dot (.) Builtin Tests"

    echo "TEST_VAR=sourced_value" > source_test.sh
    run_test ". ./source_test.sh && echo \$TEST_VAR" "sourced_value" "dot sources file"

    echo "exit 42" > exit_test.sh
    run_test_exit_code ". ./exit_test.sh" "42" "dot preserves exit status"
}

test_break_continue() {
    print_subsection "Break/Continue Builtin Tests"

    run_test "for i in 1 2 3; do if [ \$i -eq 2 ]; then break; fi; echo \$i; done" "1" "break exits loop"
    run_test "for i in 1 2 3; do if [ \$i -eq 2 ]; then continue; fi; echo \$i; done" $'1\n3' "continue skips iteration"

    run_test_exit_code "break" "1" "break outside loop fails"
    run_test_exit_code "continue" "1" "continue outside loop fails"
}

test_cd_builtin() {
    print_subsection "CD Builtin Tests"

    run_test "cd /tmp && pwd" "/tmp" "cd changes directory"
    run_test "cd && pwd | grep '/'" "" "cd with no args goes to HOME"
    run_test "cd - >/dev/null 2>&1; cd /tmp; cd - 2>/dev/null && pwd" "$HOME" "cd - returns to previous directory"

    run_test_exit_code "cd /nonexistent/directory" "1" "cd to nonexistent directory fails"
}

test_echo_builtin() {
    print_subsection "Echo Builtin Tests"

    run_test "echo hello world" "hello world" "echo basic text"
    run_test "echo -n hello" "hello" "echo -n suppresses newline"
    run_test "echo -e 'line1\\nline2'" $'line1\nline2' "echo -e interprets escapes"
    run_test "echo" "" "echo with no arguments"
}

test_eval_builtin() {
    print_subsection "Eval Builtin Tests"

    run_test "cmd='echo hello'; eval \$cmd" "hello" "eval executes command"
    run_test "var=world; eval 'echo hello \$var'" "hello world" "eval with variable expansion"

    run_test_exit_code "eval 'false'" "1" "eval preserves exit status"
}

test_exec_builtin() {
    print_subsection "Exec Builtin Tests"

    run_test "exec echo success" "success" "exec replaces shell with command"
    run_test_exit_code "exec" "0" "exec with no args succeeds"
}

test_exit_builtin() {
    print_subsection "Exit Builtin Tests"

    run_test_exit_code "exit" "0" "exit with no args"
    run_test_exit_code "exit 42" "42" "exit with specific code"
    run_test_exit_code "exit 256" "0" "exit code wraps around"
}

test_export_builtin() {
    print_subsection "Export Builtin Tests"

    run_test "export TEST_EXPORT=value; env | grep TEST_EXPORT" "TEST_EXPORT=value" "export makes variable available to environment"
    run_test "TEST_LOCAL=local; export TEST_LOCAL; env | grep TEST_LOCAL" "TEST_LOCAL=local" "export existing variable"
}

test_false_builtin() {
    print_subsection "False Builtin Tests"

    run_test_exit_code "false" "1" "false returns failure"
    run_test "false || echo success" "success" "false in conditional"
}

test_getopts_builtin() {
    print_subsection "Getopts Builtin Tests"

    # Test basic getopts functionality
    cat > getopts_test.sh << 'EOF'
#!/bin/bash
while getopts "abc:" opt; do
    case $opt in
        a) echo "option a" ;;
        b) echo "option b" ;;
        c) echo "option c: $OPTARG" ;;
        \?) echo "invalid option" ;;
    esac
done
EOF
    chmod +x getopts_test.sh

    run_test "./getopts_test.sh -a -b" $'option a\noption b' "getopts parses multiple options"
    run_test "./getopts_test.sh -c value" "option c: value" "getopts handles option with argument"
}

test_pwd_builtin() {
    print_subsection "PWD Builtin Tests"

    local current_dir
    current_dir=$(pwd)
    run_test "pwd" "$current_dir" "pwd shows current directory"

    run_test "cd /tmp && pwd" "/tmp" "pwd reflects directory changes"
}

test_readonly_builtin() {
    print_subsection "Readonly Builtin Tests"

    if test_builtin_exists "readonly" >/dev/null 2>&1; then
        run_test "readonly TEST_READONLY=readonly_value; echo \$TEST_READONLY" "readonly_value" "readonly sets variable"
        run_test_exit_code "readonly TEST_READONLY=readonly_value; TEST_READONLY=changed" "1" "readonly prevents modification"
    else
        echo -e "  ${YELLOW}!${NC} readonly builtin not implemented - skipping tests"
        TOTAL_TESTS=$((TOTAL_TESTS + 2))
    fi
}

test_return_builtin() {
    print_subsection "Return Builtin Tests"

    cat > return_test.sh << 'EOF'
test_function() {
    return 42
}
test_function
echo $?
EOF

    run_test ". ./return_test.sh" "42" "return sets function exit status"

    run_test_exit_code "return" "1" "return outside function fails"
}

test_set_builtin() {
    print_subsection "Set Builtin Tests"

    run_test "set -e; echo 'errexit set'" "errexit set" "set -e enables errexit"
    run_test "set +e; echo 'errexit disabled'" "errexit disabled" "set +e disables errexit"
    run_test "set -- arg1 arg2; echo \$1 \$2" "arg1 arg2" "set -- sets positional parameters"
}

test_shift_builtin() {
    print_subsection "Shift Builtin Tests"

    run_test "set -- a b c; shift; echo \$1 \$2" "b c" "shift moves positional parameters"
    run_test "set -- a b c; shift 2; echo \$1" "c" "shift with count"

    run_test_exit_code "set --; shift" "1" "shift with no parameters fails"
}

test_test_builtin() {
    print_subsection "Test Builtin Tests"

    run_test_exit_code "test 1 -eq 1" "0" "test numeric equality"
    run_test_exit_code "test 1 -eq 2" "1" "test numeric inequality"
    run_test_exit_code "test 'abc' = 'abc'" "0" "test string equality"
    run_test_exit_code "test -f testfile.txt" "0" "test file existence"
    run_test_exit_code "test -d testdir" "0" "test directory existence"

    run_test_exit_code "[ 1 -eq 1 ]" "0" "bracket form of test"
}

test_times_builtin() {
    print_subsection "Times Builtin Tests"

    local times_output
    times_output=$($SHELL_PATH -c "times" 2>/dev/null)

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$times_output" =~ [0-9]+m[0-9]+\.[0-9]+s ]]; then
        echo -e "  ${GREEN}OK${NC} times shows process times"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: times shows process times" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} times format incorrect"
        echo "    Output: $times_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: times format incorrect - Output: $times_output" >> "$LOGFILE"
    fi
}

test_trap_builtin() {
    print_subsection "Trap Builtin Tests"

    cat > trap_test.sh << 'EOF'
#!/bin/bash
trap 'echo "trapped"' INT
kill -INT $$
EOF
    chmod +x trap_test.sh

    run_test "timeout 5 ./trap_test.sh" "trapped" "trap handles signals"

    run_test "trap 'echo exit_trap' EXIT; exit 0" "exit_trap" "trap handles EXIT"
}

test_true_builtin() {
    print_subsection "True Builtin Tests"

    run_test_exit_code "true" "0" "true returns success"
    run_test "true && echo success" "success" "true in conditional"
}

test_type_builtin() {
    print_subsection "Type Builtin Tests"

    run_test "type echo | grep builtin" "echo is a shell builtin" "type identifies builtins"
    run_test "type ls | grep -E '(command|file)'" "" "type identifies external commands"
}

test_umask_builtin() {
    print_subsection "Umask Builtin Tests"

    local original_umask
    original_umask=$(umask)

    run_test "umask 0022; umask" "0022" "umask sets and displays file creation mask"

    # Restore original umask
    umask "$original_umask"
}

test_ulimit_builtin() {
    print_subsection "Ulimit Builtin Tests"

    run_test_exit_code "ulimit" "0" "ulimit shows default limit"
    run_test_exit_code "ulimit -a" "0" "ulimit -a shows all limits"
}

test_unset_builtin() {
    print_subsection "Unset Builtin Tests"

    run_test "TEST_UNSET=value; unset TEST_UNSET; echo \${TEST_UNSET:-unset}" "unset" "unset removes variable"
}

test_wait_builtin() {
    print_subsection "Wait Builtin Tests"

    run_test "sleep 0.1 & wait" "" "wait for background job"
    run_test_exit_code "wait 99999" "127" "wait for nonexistent job fails"
}

test_hash_builtin() {
    print_subsection "Hash Builtin Tests"

    if test_builtin_exists "hash" >/dev/null 2>&1; then
        run_test_exit_code "hash" "0" "hash command exists"
        run_test_exit_code "hash ls" "0" "hash can hash command"
    else
        echo -e "  ${YELLOW}!${NC} hash builtin not implemented - skipping tests"
        TOTAL_TESTS=$((TOTAL_TESTS + 2))
    fi
}

# ===============================================================================
# POSIX COMMAND LINE OPTION TESTS
# ===============================================================================

test_command_line_options() {
    print_section "POSIX COMMAND LINE OPTION COMPLIANCE"

    print_subsection "Basic Command Line Options"

    # Test -c option
    run_test "$SHELL_PATH -c 'echo command_mode'" "command_mode" "-c executes command"

    # Test -s option (read from stdin)
    run_test "echo 'echo stdin_mode' | $SHELL_PATH -s" "stdin_mode" "-s reads from stdin"

    # Test exit codes with -c
    run_test_exit_code "$SHELL_PATH -c 'exit 42'" "42" "-c preserves exit status"

    print_subsection "Shell Option Tests"

    # Test -e option (exit on error)
    run_test_exit_code "$SHELL_PATH -c 'set -e; false; echo should_not_print'" "1" "-e exits on command failure"

    # Test -u option (treat unset variables as error)
    run_test_exit_code "$SHELL_PATH -c 'set -u; echo \$UNDEFINED_VAR'" "1" "-u treats unset variables as error"

    # Test -x option (trace execution)
    local trace_output
    trace_output=$($SHELL_PATH -c 'set -x; echo traced' 2>&1)

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$trace_output" =~ "+ echo traced" ]]; then
        echo -e "  ${GREEN}OK${NC} -x traces command execution"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: -x traces command execution" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} -x trace not working"
        echo "    Output: $trace_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: -x trace not working - Output: $trace_output" >> "$LOGFILE"
    fi

    # Test -n option (syntax check only)
    run_test_exit_code "$SHELL_PATH -c 'set -n; echo should_not_execute'" "0" "-n syntax check mode"

    # Test -v option (verbose mode)
    local verbose_output
    verbose_output=$($SHELL_PATH -c 'set -v; echo verbose' 2>&1)

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$verbose_output" =~ "echo verbose" ]]; then
        echo -e "  ${GREEN}OK${NC} -v verbose mode shows input"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "PASS: -v verbose mode shows input" >> "$LOGFILE"
    else
        echo -e "  ${RED}FAIL${NC} -v verbose mode not working"
        echo "    Output: $verbose_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "FAIL: -v verbose mode not working - Output: $verbose_output" >> "$LOGFILE"
    fi
}

# ===============================================================================
# ADVANCED POSIX COMPLIANCE TESTS
# ===============================================================================

test_advanced_posix_features() {
    print_section "ADVANCED POSIX COMPLIANCE FEATURES"

    print_subsection "Parameter Expansion"
    run_test "var=hello; echo \${var}" "hello" "basic parameter expansion"
    run_test "echo \${var:-default}" "default" "parameter expansion with default"
    run_test "var=hello; echo \${var:+set}" "set" "parameter expansion when set"
    run_test "var=hello; echo \${#var}" "5" "parameter length expansion"

    print_subsection "Command Substitution"
    run_test "echo \$(echo nested)" "nested" "command substitution with \$()"
    run_test "echo \`echo backtick\`" "backtick" "command substitution with backticks"

    print_subsection "Arithmetic Expansion"
    run_test "echo \$((2 + 3))" "5" "arithmetic expansion"
    run_test "echo \$((10 / 3))" "3" "arithmetic division"

    print_subsection "Pathname Expansion"
    run_test "echo *.txt" "testfile.txt" "glob expansion for files"
    run_test "echo test*/sub*" "testdir/subdir" "glob expansion for directories"

    print_subsection "Quote Removal"
    run_test "echo 'single quotes'" "single quotes" "single quote handling"
    run_test "echo \"double quotes\"" "double quotes" "double quote handling"
    run_test "echo mixed'quote\"test" "mixed'quote\"test" "mixed quote handling"

    print_subsection "Field Splitting"
    run_test "IFS=:; set -- a:b:c; echo \$2" "b" "IFS field splitting"
}

# ===============================================================================
# MISSING BUILTIN IDENTIFICATION
# ===============================================================================

test_missing_builtins() {
    print_section "MISSING POSIX BUILTIN IDENTIFICATION"

    local missing_builtins=()

    # Check for missing POSIX required builtins
    if ! test_builtin_exists "readonly" >/dev/null 2>&1; then
        missing_builtins+=("readonly")
    fi

    if ! test_builtin_exists "hash" >/dev/null 2>&1; then
        missing_builtins+=("hash")
    fi

    if [ ${#missing_builtins[@]} -eq 0 ]; then
        echo -e "  ${GREEN}OK${NC} All POSIX required builtins are implemented"
    else
        echo -e "  ${YELLOW}!${NC} Missing POSIX required builtins:"
        for builtin in "${missing_builtins[@]}"; do
            echo -e "    ${RED}FAIL${NC} $builtin"
        done
        echo ""
        echo "POSIX Shell Standards require these builtins to be implemented:"
        echo "- readonly: Create read-only variables"
        echo "- hash: Command path hashing for performance"
    fi
}

# ===============================================================================
# MAIN TEST EXECUTION
# ===============================================================================

main() {
    echo "==============================================================================="
    echo "LUSH POSIX BUILTIN COMPLIANCE TEST SUITE"
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
    test_posix_builtin_existence

    test_colon_builtin
    test_dot_builtin
    test_break_continue
    test_cd_builtin
    test_echo_builtin
    test_eval_builtin
    test_exec_builtin
    test_exit_builtin
    test_export_builtin
    test_false_builtin
    test_getopts_builtin
    test_pwd_builtin
    test_readonly_builtin
    test_return_builtin
    test_set_builtin
    test_shift_builtin
    test_test_builtin
    test_times_builtin
    test_trap_builtin
    test_true_builtin
    test_type_builtin
    test_umask_builtin
    test_ulimit_builtin
    test_unset_builtin
    test_wait_builtin
    test_hash_builtin

    test_command_line_options
    test_advanced_posix_features
    test_missing_builtins

    # Print final results
    echo ""
    echo "==============================================================================="
    echo "POSIX BUILTIN COMPLIANCE TEST RESULTS"
    echo "==============================================================================="
    echo -e "Total Tests: $TOTAL_TESTS"
    echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"

    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "\n${GREEN}OK ALL POSIX BUILTIN TESTS PASSED${NC}"
        echo "Lush demonstrates excellent POSIX builtin compliance!"
    else
        echo -e "\n${YELLOW}WARN SOME POSIX BUILTIN TESTS FAILED${NC}"
        echo "Check $LOGFILE for detailed failure information."
        echo ""
        echo "Summary of areas needing attention:"
        grep "FAIL:" "$LOGFILE" | cut -d'-' -f1 | sort | uniq -c | sort -nr
    fi

    local compliance_percentage
    compliance_percentage=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    echo ""
    echo "POSIX Builtin Compliance Rate: ${compliance_percentage}%"

    if [[ $compliance_percentage -ge 95 ]]; then
        echo -e "${GREEN}EXCELLENT${NC} - Production ready POSIX compliance"
    elif [[ $compliance_percentage -ge 90 ]]; then
        echo -e "${YELLOW}GOOD${NC} - High POSIX compliance with minor gaps"
    elif [[ $compliance_percentage -ge 80 ]]; then
        echo -e "${YELLOW}MODERATE${NC} - Reasonable POSIX compliance"
    else
        echo -e "${RED}NEEDS IMPROVEMENT${NC} - Significant POSIX compliance gaps"
    fi

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
