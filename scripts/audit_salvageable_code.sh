#!/bin/bash
# LLE Salvageable Code Audit
# Identifies clean components worth preserving before nuclear option
# Version: 1.0.0
# Date: 2025-10-18

set -e

echo "======================================================================"
echo "LLE Code Salvage Audit"
echo "Version: 1.0.0"
echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
echo "======================================================================"
echo ""

# Create audit results directory
AUDIT_DIR="audit_results"
mkdir -p "$AUDIT_DIR"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
SALVAGEABLE_COUNT=0
DISCARD_COUNT=0

# Audit criteria weights
VIOLATION_CRITICAL=1000   # Any violation is critical
TODO_MAJOR=100           # Many TODOs indicate incomplete
TODO_MINOR=10            # Few TODOs might be acceptable

#------------------------------------------------------------------------------
# Audit function - comprehensive component evaluation
#------------------------------------------------------------------------------
audit_component() {
    local path=$1
    local name=$2
    local output_file="$AUDIT_DIR/$(echo $name | tr ' ' '_' | tr '[:upper:]' '[:lower:]').txt"

    echo "======================================================================"
    echo "AUDITING: $name"
    echo "======================================================================"
    echo "Location: $path"
    echo ""

    # Initialize scores
    local violations=0
    local todos=0
    local score=0
    local verdict="UNKNOWN"

    # Check if path exists
    if [ ! -e "$path" ]; then
        echo -e "${YELLOW}WARNING WARNING: Path not found${NC}"
        echo "VERDICT: SKIP (not found)" > "$output_file"
        return 1
    fi

    # Determine if directory or file
    if [ -d "$path" ]; then
        echo "Type: Directory"
        SEARCH_PATH="$path"
    else
        echo "Type: File"
        SEARCH_PATH="$path"
    fi
    echo ""

    #--------------------------------------------------------------------------
    # CHECK 1: Architectural Violations (CRITICAL)
    #--------------------------------------------------------------------------
    echo "--- Check 1: Architectural Violations ---"

    # Direct terminal writes
    local write_violations=$(grep -r "write(\s*fd\s*," "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  Direct write() calls to fd: $write_violations"

    # Escape sequences
    local escape_violations=$(grep -r "\\\\x1b\|\\\\033\|\\\\e\[" "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  Escape sequences: $escape_violations"

    # fprintf/printf to terminal
    local printf_violations=$(grep -r "fprintf.*stdout\|printf.*\\\\x1b" "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  fprintf/printf to terminal: $printf_violations"

    violations=$((write_violations + escape_violations + printf_violations))

    if [ $violations -gt 0 ]; then
        echo -e "  ${RED}FAIL VIOLATIONS FOUND: $violations${NC}"
        score=$((score + violations * VIOLATION_CRITICAL))
    else
        echo -e "  ${GREEN}OK No architectural violations${NC}"
    fi
    echo ""

    #--------------------------------------------------------------------------
    # CHECK 2: TODO/Placeholder Markers
    #--------------------------------------------------------------------------
    echo "--- Check 2: Placeholder Code ---"

    # TODO markers
    local todo_count=$(grep -r "TODO" "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  TODO markers: $todo_count"

    # FIXME markers
    local fixme_count=$(grep -r "FIXME" "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  FIXME markers: $fixme_count"

    # STUB markers
    local stub_count=$(grep -r "STUB\|TODO_SPEC" "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  STUB/TODO_SPEC markers: $stub_count"

    # "Phase 2" promises
    local phase2_count=$(grep -r "Phase 2\|For now" "$SEARCH_PATH" 2>/dev/null | grep -v test | wc -l)
    echo "  'Phase 2'/'For now' promises: $phase2_count"

    todos=$((todo_count + fixme_count + stub_count + phase2_count))

    if [ $todos -gt 5 ]; then
        echo -e "  ${RED}FAIL EXCESSIVE PLACEHOLDERS: $todos${NC}"
        score=$((score + todos * TODO_MAJOR))
    elif [ $todos -gt 0 ]; then
        echo -e "  ${YELLOW}WARNING Some placeholders: $todos (review required)${NC}"
        score=$((score + todos * TODO_MINOR))
    else
        echo -e "  ${GREEN}OK No placeholder markers${NC}"
    fi
    echo ""

    #--------------------------------------------------------------------------
    # CHECK 3: Code Metrics
    #--------------------------------------------------------------------------
    echo "--- Check 3: Code Metrics ---"

    # Line count
    if [ -d "$path" ]; then
        local lines=$(find "$SEARCH_PATH" -name "*.c" -o -name "*.h" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
        local files=$(find "$SEARCH_PATH" -name "*.c" -o -name "*.h" | wc -l)
    else
        local lines=$(wc -l "$SEARCH_PATH" 2>/dev/null | awk '{print $1}')
        local files=1
    fi

    echo "  Files: $files"
    echo "  Total lines: $lines"
    echo ""

    #--------------------------------------------------------------------------
    # CHECK 4: Specific Code Patterns
    #--------------------------------------------------------------------------
    echo "--- Check 4: Code Quality Patterns ---"

    # Check for display_controller usage (good sign for integration)
    local display_integration=$(grep -r "display_controller_t\|composition_engine" "$SEARCH_PATH" 2>/dev/null | grep -v "NULL" | grep -v "//" | wc -l)
    echo "  Display controller integration: $display_integration references"

    # Check for memory pool usage (good sign)
    local memory_pool=$(grep -r "memory_pool" "$SEARCH_PATH" 2>/dev/null | grep -v "//" | wc -l)
    echo "  Memory pool usage: $memory_pool references"

    # Check for error handling
    local error_handling=$(grep -r "if.*!.*{\|if.*==.*NULL\|return.*ERR" "$SEARCH_PATH" 2>/dev/null | wc -l)
    echo "  Error handling patterns: $error_handling"
    echo ""

    #--------------------------------------------------------------------------
    # VERDICT
    #--------------------------------------------------------------------------
    echo "--- VERDICT ---"
    echo "  Violations: $violations"
    echo "  Placeholders: $todos"
    echo "  Score: $score (lower is better)"
    echo ""

    # Determine verdict
    if [ $violations -gt 0 ]; then
        verdict="DISCARD"
        verdict_color=$RED
        verdict_symbol="FAIL"
        verdict_reason="Architectural violations present"
        DISCARD_COUNT=$((DISCARD_COUNT + 1))
    elif [ $todos -gt 5 ]; then
        verdict="DISCARD"
        verdict_color=$RED
        verdict_symbol="FAIL"
        verdict_reason="Excessive placeholder markers ($todos)"
        DISCARD_COUNT=$((DISCARD_COUNT + 1))
    elif [ $todos -gt 0 ]; then
        verdict="REVIEW_REQUIRED"
        verdict_color=$YELLOW
        verdict_symbol="WARNING"
        verdict_reason="Some placeholders present ($todos) - manual review needed"
    else
        verdict="SALVAGEABLE"
        verdict_color=$GREEN
        verdict_symbol="OK"
        verdict_reason="Clean code - no violations, no placeholders"
        SALVAGEABLE_COUNT=$((SALVAGEABLE_COUNT + 1))
    fi

    echo -e "${verdict_color}${verdict_symbol} VERDICT: $verdict${NC}"
    echo "  Reason: $verdict_reason"
    echo ""

    #--------------------------------------------------------------------------
    # Write detailed report
    #--------------------------------------------------------------------------
    cat > "$output_file" << EOF
# Audit Report: $name
Date: $(date '+%Y-%m-%d %H:%M:%S')
Location: $path

## VERDICT: $verdict
Reason: $verdict_reason

## Audit Scores
- Architectural violations: $violations
- Placeholder markers: $todos
- Total score: $score (lower is better)

## Detailed Results

### Architectural Violations
- Direct write() to fd: $write_violations
- Escape sequences: $escape_violations
- fprintf/printf to terminal: $printf_violations
- **Total violations: $violations**

### Placeholder Code
- TODO markers: $todo_count
- FIXME markers: $fixme_count
- STUB/TODO_SPEC markers: $stub_count
- Phase 2 promises: $phase2_count
- **Total placeholders: $todos**

### Code Metrics
- Files: $files
- Total lines: $lines

### Quality Indicators
- Display controller integration: $display_integration references
- Memory pool usage: $memory_pool references
- Error handling patterns: $error_handling

## Recommendation

EOF

    if [ "$verdict" = "SALVAGEABLE" ]; then
        cat >> "$output_file" << EOF
OK SALVAGE THIS COMPONENT

This code passed all audit criteria:
- Zero architectural violations
- Zero placeholder markers
- Clean, complete implementation

Salvage procedure:
1. Copy to reference/salvaged_lle/
2. Create SALVAGE_MANIFEST.md
3. Mark for potential reuse in fresh implementation

Reuse considerations:
- Re-audit before integration
- Verify against updated specifications
- Ensure compliance with new safeguards
- Update tests for behavior validation
EOF
    elif [ "$verdict" = "REVIEW_REQUIRED" ]; then
        cat >> "$output_file" << EOF
WARNING MANUAL REVIEW REQUIRED

This code has minor issues that need review:
- Violations: $violations
- Placeholders: $todos

Review procedure:
1. Examine each TODO/placeholder marker
2. Determine if they're critical or minor
3. Decide: salvage with cleanup, or discard

If salvaged:
- Remove all placeholder markers
- Complete any unfinished implementation
- Re-audit after cleanup
EOF
    else
        cat >> "$output_file" << EOF
FAIL DISCARD THIS COMPONENT

This code failed audit criteria:
$verdict_reason

Do NOT salvage. Re-implement fresh following specifications.

Reasons to discard:
EOF
        if [ $violations -gt 0 ]; then
            echo "- Contains architectural violations (direct terminal control)" >> "$output_file"
        fi
        if [ $todos -gt 5 ]; then
            echo "- Excessive placeholder markers indicate incomplete implementation" >> "$output_file"
        fi
        cat >> "$output_file" << EOF

Fresh implementation approach:
1. Follow corresponding specification exactly
2. No placeholder code
3. Compliance checks from commit 1
4. Behavior validation tests
EOF
    fi

    echo "Detailed report: $output_file"
    echo ""

    return 0
}

#------------------------------------------------------------------------------
# Main Audit Execution
#------------------------------------------------------------------------------

echo "Starting audit of LLE codebase components..."
echo ""
echo "Audit criteria:"
echo "  - Zero architectural violations (direct terminal writes, escape sequences)"
echo "  - Minimal placeholder markers (TODO, FIXME, STUB, 'Phase 2')"
echo "  - Complete implementation (not prototype code)"
echo ""
echo "======================================================================"
echo ""

# Audit components in priority order

# 1. Buffer Management (highest priority salvage candidate)
audit_component "src/lle/foundation/buffer" "Buffer Management"

# 2. Terminal Capability Detection
audit_component "src/lle/foundation/terminal/capability.c" "Terminal Capability Detection"

# 3. Terminal Abstraction Core
audit_component "src/lle/foundation/terminal/terminal.c" "Terminal Abstraction"

# 4. Display Code (expected to fail - just confirming)
audit_component "src/lle/foundation/display/display.c" "Display Rendering"

# 5. Display Integration System (expected to fail - compliance theater)
audit_component "src/lle/foundation/display/display_integration_system.c" "Display Integration System"

# 6. History System
audit_component "src/lle/foundation/history" "History System"

#------------------------------------------------------------------------------
# Summary Report
#------------------------------------------------------------------------------

echo "======================================================================"
echo "AUDIT SUMMARY"
echo "======================================================================"
echo ""
echo "Components audited: $((SALVAGEABLE_COUNT + DISCARD_COUNT))"
echo -e "${GREEN}Salvageable: $SALVAGEABLE_COUNT${NC}"
echo -e "${RED}Discard: $DISCARD_COUNT${NC}"
echo ""
echo "Detailed reports: $AUDIT_DIR/"
echo ""

# Create summary file
cat > "$AUDIT_DIR/AUDIT_SUMMARY.md" << EOF
# LLE Code Audit - Summary Report

**Date**: $(date '+%Y-%m-%d %H:%M:%S')
**Total Components Audited**: $((SALVAGEABLE_COUNT + DISCARD_COUNT))

## Results

- **Salvageable**: $SALVAGEABLE_COUNT components
- **Discard**: $DISCARD_COUNT components

## Salvageable Components

EOF

# List salvageable components
for report in "$AUDIT_DIR"/*.txt; do
    if grep -q "VERDICT: SALVAGEABLE" "$report"; then
        component=$(basename "$report" .txt | tr '_' ' ')
        echo "- OK $component" >> "$AUDIT_DIR/AUDIT_SUMMARY.md"
    fi
done

cat >> "$AUDIT_DIR/AUDIT_SUMMARY.md" << EOF

## Components Requiring Manual Review

EOF

for report in "$AUDIT_DIR"/*.txt; do
    if grep -q "VERDICT: REVIEW_REQUIRED" "$report"; then
        component=$(basename "$report" .txt | tr '_' ' ')
        echo "- WARNING$component (see detailed report)" >> "$AUDIT_DIR/AUDIT_SUMMARY.md"
    fi
done

cat >> "$AUDIT_DIR/AUDIT_SUMMARY.md" << EOF

## Components to Discard

EOF

for report in "$AUDIT_DIR"/*.txt; do
    if grep -q "VERDICT: DISCARD" "$report"; then
        component=$(basename "$report" .txt | tr '_' ' ')
        reason=$(grep "Reason:" "$report" | cut -d: -f2-)
        echo "- FAIL $component -$reason" >> "$AUDIT_DIR/AUDIT_SUMMARY.md"
    fi
done

cat >> "$AUDIT_DIR/AUDIT_SUMMARY.md" << EOF

## Recommendations

### For Salvageable Components
1. Copy to reference/salvaged_lle/
2. Create SALVAGE_MANIFEST.md documenting audit results
3. Mark for potential reuse in fresh implementation
4. Re-audit before actual integration

### For Components Requiring Review
1. Manual examination of placeholder markers
2. Determine if cleanup is worthwhile
3. If salvaged, remove all placeholders and re-audit

### For Discarded Components
1. Do NOT salvage - re-implement fresh
2. Follow specifications exactly
3. Use compliance checks from commit 1
4. Implement behavior validation tests

## Next Steps

1. Review detailed reports in audit_results/
2. Make salvage decisions for REVIEW_REQUIRED components
3. Execute salvage operation for approved components
4. Proceed with nuclear option

---

**Audit Script**: scripts/audit_salvageable_code.sh
**Detailed Reports**: audit_results/*.txt
EOF

echo "Summary report: $AUDIT_DIR/AUDIT_SUMMARY.md"
echo ""

if [ $SALVAGEABLE_COUNT -gt 0 ]; then
    echo -e "${GREEN}Good news: Found $SALVAGEABLE_COUNT salvageable component(s)${NC}"
    echo "These can be preserved and potentially reused in fresh implementation."
else
    echo -e "${YELLOW}No components passed all salvage criteria.${NC}"
    echo "Fresh implementation will start completely from scratch."
fi

echo ""
echo "======================================================================"
echo "Audit complete. Review detailed reports before proceeding."
echo "======================================================================"
