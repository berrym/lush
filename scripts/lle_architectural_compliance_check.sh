#!/bin/bash
# LLE Architectural Compliance Check
# Automated verification that LLE code follows architectural specifications
#
# This script MUST pass before any LLE code is considered complete.
# Zero tolerance for architectural violations.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

VIOLATIONS=0
WARNINGS=0
CHECKS=0

echo "======================================================================="
echo "LLE Architectural Compliance Check"
echo "Version: 1.0.0"
echo "Date: $(date)"
echo "======================================================================="
echo ""

# Function to report violation
report_violation() {
    local check_name="$1"
    local details="$2"
    echo -e "${RED}FAIL VIOLATION${NC}: $check_name"
    echo "  Details: $details"
    echo ""
    ((VIOLATIONS++))
}

# Function to report warning
report_warning() {
    local check_name="$1"
    local details="$2"
    echo -e "${YELLOW}WARNING WARNING${NC}: $check_name"
    echo "  Details: $details"
    echo ""
    ((WARNINGS++))
}

# Function to report pass
report_pass() {
    local check_name="$1"
    echo -e "${GREEN}OK PASS${NC}: $check_name"
    ((CHECKS++))
}

echo "Checking LLE source directory: src/lle/"
echo ""

# Check if src/lle exists and has files
if [ ! -d "src/lle" ]; then
    echo "ERROR: src/lle/ directory not found"
    exit 1
fi

# Count source files
SOURCE_FILES=$(find src/lle -name "*.c" 2>/dev/null | wc -l)
echo "Found $SOURCE_FILES LLE source files"
echo ""

# ============================================================================
# CRITICAL CHECKS - MUST PASS
# ============================================================================

echo "--- CRITICAL ARCHITECTURAL COMPLIANCE CHECKS ---"
echo ""

# CHECK 1: No direct terminal writes in display code
echo "Check 1: No direct terminal writes in display code..."
if find src/lle/display -name "*.c" -exec grep "write\s*([^,]*fd" {} \; 2>/dev/null | grep -v "//.*write"; then
    report_violation "Direct terminal writes in display code" \
        "Display code MUST NOT write directly to terminal fd. Use Lush display system."
else
    report_pass "No direct terminal writes in display code"
fi

# CHECK 2: No ANSI escape sequences in display code
echo "Check 2: No ANSI escape sequences in display code..."
if find src/lle/display -name "*.c" -exec grep "\\\\x1b\|\\\\033\|\\\\e\[" {} \; 2>/dev/null | grep -v "//"; then
    report_violation "ANSI escape sequences in display code" \
        "Display code MUST NOT contain escape sequences. Use Lush display system."
else
    report_pass "No ANSI escape sequences in display code"
fi

# CHECK 3: Display controller must be used (not NULL)
echo "Check 3: Display controller integration present..."
if grep -r "display_controller.*=.*NULL" src/lle/foundation/display/*.c 2>/dev/null | grep -v "//"; then
    report_violation "Display controller not integrated" \
        "display_controller must be connected to Lush display system, not NULL"
else
    report_pass "Display controller integration appears present"
fi

# CHECK 4: No direct terminal control in editor code
echo "Check 4: No direct terminal control in editor code..."
if grep -r "write\s*([^,]*fd" src/lle/foundation/editor/*.c 2>/dev/null | grep -v "//.*write"; then
    report_violation "Direct terminal writes in editor code" \
        "Editor code MUST NOT write directly to terminal. Use display layer."
else
    report_pass "No direct terminal control in editor code"
fi

# CHECK 5: Verify display system API usage
echo "Check 5: Display system API usage verification..."
DISPLAY_API_COUNT=$(grep -r "display_controller\|composition_engine\|command_layer" src/lle/foundation/display/*.c 2>/dev/null | grep -v "//" | wc -l)
if [ "$DISPLAY_API_COUNT" -lt 3 ]; then
    report_warning "Insufficient display system API usage" \
        "Found only $DISPLAY_API_COUNT references. Verify proper integration."
else
    report_pass "Display system API references found ($DISPLAY_API_COUNT)"
fi

# CHECK 6: No sprintf/snprintf of escape sequences
echo "Check 6: No formatting of escape sequences..."
if grep -r "sprintf.*\\\\x1b\|snprintf.*\\\\x1b" src/lle/foundation/ 2>/dev/null | grep -v "//"; then
    report_violation "Formatting escape sequences" \
        "Must not format escape sequences. Use display system color APIs."
else
    report_pass "No formatting of escape sequences"
fi

# CHECK 7: Terminal abstraction not bypassed
echo "Check 7: Terminal abstraction layer integrity..."
if grep -r "term->.*->output_fd" src/lle/foundation/display/*.c src/lle/foundation/editor/*.c 2>/dev/null | grep "write" | grep -v "//"; then
    report_violation "Terminal abstraction bypassed" \
        "Must not access terminal fd directly through term struct"
else
    report_pass "Terminal abstraction layer respected"
fi

# CHECK 8: Verify Lush display integration points
echo "Check 8: Lush display integration points..."
INTEGRATION_POINTS=0
if grep -rq "lush_display" src/lle/foundation/display/ 2>/dev/null; then
    ((INTEGRATION_POINTS++))
fi
if grep -rq "display_bridge" src/lle/foundation/display/ 2>/dev/null; then
    ((INTEGRATION_POINTS++))
fi
if grep -rq "render_controller" src/lle/foundation/display/ 2>/dev/null; then
    ((INTEGRATION_POINTS++))
fi

if [ "$INTEGRATION_POINTS" -eq 0 ]; then
    report_violation "No Lush display integration" \
        "No evidence of integration with Lush display system components"
else
    report_pass "Lush display integration points present ($INTEGRATION_POINTS/3)"
fi

echo ""
echo "--- SPECIFICATION PATTERN COMPLIANCE ---"
echo ""

# CHECK 9: Spec 08 required structures present
echo "Check 9: Spec 08 required structures..."
REQUIRED_STRUCTS=(
    "lle_display_integration_t"
    "lle_display_bridge_t"
    "lle_render_controller_t"
)

MISSING_STRUCTS=0
for struct in "${REQUIRED_STRUCTS[@]}"; do
    if ! grep -rq "typedef struct.*$struct\|struct $struct" src/lle/foundation/display/ include/lle/ 2>/dev/null; then
        report_warning "Missing required structure: $struct" \
            "Spec 08 requires this structure. May be stubbed but must exist."
        ((MISSING_STRUCTS++))
    fi
done

if [ "$MISSING_STRUCTS" -eq 0 ]; then
    report_pass "All Spec 08 required structures present"
fi

# CHECK 10: Prohibited function patterns
echo "Check 10: Prohibited function patterns..."
PROHIBITED_FOUND=0

# Check for printf to stdout in LLE code
if grep -r "printf\|fprintf.*stdout" src/lle/foundation/ 2>/dev/null | grep -v "//.*printf" | grep -v "test"; then
    report_warning "Direct stdout writes found" \
        "LLE should not write directly to stdout. Use display system."
    ((PROHIBITED_FOUND++))
fi

if [ "$PROHIBITED_FOUND" -eq 0 ]; then
    report_pass "No prohibited function patterns"
fi

echo ""
echo "--- INTEGRATION VERIFICATION ---"
echo ""

# CHECK 11: Display flush must use display system
echo "Check 11: Display flush implementation..."
if [ -f "src/lle/foundation/display/display.c" ]; then
    if grep -A 50 "lle_display_flush" src/lle/foundation/display/display.c | grep -q "write.*fd"; then
        report_violation "Display flush writes directly to terminal" \
            "lle_display_flush MUST route through Lush display system, not write to fd"
    elif grep -A 50 "lle_display_flush" src/lle/foundation/display/display.c | grep -q "lush_display\|display_controller\|composition_engine"; then
        report_pass "Display flush appears to use display system"
    else
        report_warning "Display flush integration unclear" \
            "Cannot determine if flush uses display system. Manual review needed."
    fi
fi

# CHECK 12: No hardcoded terminal sequences
echo "Check 12: No hardcoded terminal control sequences..."
HARDCODED_SEQS=(
    "\\x1b\[H"         # Home position
    "\\x1b\[2J"        # Clear screen
    "\\x1b\[K"         # Clear line
    "\\x1b\[A"         # Cursor up
    "\\x1b\[B"         # Cursor down
    "\\x1b\[C"         # Cursor right
    "\\x1b\[D"         # Cursor left
)

HARDCODED_FOUND=0
for seq in "${HARDCODED_SEQS[@]}"; do
    if grep -r "$seq" src/lle/foundation/display/ src/lle/foundation/editor/ 2>/dev/null | grep -v "//"; then
        report_violation "Hardcoded terminal sequence: $seq" \
            "Terminal sequences MUST be generated by display system, not hardcoded"
        ((HARDCODED_FOUND++))
    fi
done

if [ "$HARDCODED_FOUND" -eq 0 ]; then
    report_pass "No hardcoded terminal sequences"
fi

# CHECK 13: Proper error handling for display operations
echo "Check 13: Display error handling patterns..."
if grep -r "lle_display_.*(" src/lle/foundation/display/*.c 2>/dev/null | grep -v "if.*result\|if.*!=" | head -5 | grep -q .; then
    report_warning "Potential missing error checks" \
        "Display operations should check return values"
fi
report_pass "Error handling patterns check complete"

# CHECK 14: Memory pool usage (not raw malloc in display)
echo "Check 14: Memory pool usage in display code..."
if grep -r "malloc\|calloc\|realloc" src/lle/foundation/display/*.c 2>/dev/null | grep -v "//.*alloc" | grep -v "memory_pool"; then
    report_warning "Raw allocation in display code" \
        "Should use Lush memory pool for consistency"
fi
report_pass "Memory allocation patterns checked"

echo ""
echo "======================================================================="
echo "COMPLIANCE CHECK SUMMARY"
echo "======================================================================="
echo ""
echo "Checks Passed:  $CHECKS"
echo -e "Violations:     ${RED}$VIOLATIONS${NC}"
echo -e "Warnings:       ${YELLOW}$WARNINGS${NC}"
echo ""

if [ "$VIOLATIONS" -gt 0 ]; then
    echo -e "${RED}COMPLIANCE CHECK FAILED${NC}"
    echo ""
    echo "CRITICAL VIOLATIONS FOUND: $VIOLATIONS"
    echo ""
    echo "LLE code contains architectural violations that MUST be fixed."
    echo "These violations prevent proper integration with Lush display system."
    echo ""
    echo "Required Actions:"
    echo "1. Review DESIGN_VIOLATION_ANALYSIS.md"
    echo "2. Fix all violations listed above"
    echo "3. Re-run this compliance check"
    echo "4. Do NOT mark code as complete until this check passes"
    echo ""
    echo "See docs/lle_specification/08_display_integration_complete.md"
    echo "for correct implementation patterns."
    echo ""
    exit 1
fi

if [ "$WARNINGS" -gt 0 ]; then
    echo -e "${YELLOW}COMPLIANCE CHECK PASSED WITH WARNINGS${NC}"
    echo ""
    echo "No critical violations, but $WARNINGS warnings found."
    echo "Review warnings and address if applicable."
    echo ""
    exit 0
fi

echo -e "${GREEN}COMPLIANCE CHECK PASSED${NC}"
echo ""
echo "LLE code appears to follow architectural specifications."
echo "All critical compliance checks passed."
echo ""
exit 0
