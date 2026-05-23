#!/bin/bash
# check_comment_style.sh -- enforce lush's mixed C11/Doxygen comment style.
#
# Rules (per the project's documented standard):
#   - `/** ... */` Javadoc-style blocks for documenting functions, structures,
#     enums, file headers. Detailed prose lives here.
#   - `/**< ... */` (or `///<`) for trailing comments on the same line as a
#     struct/union/enum member or variable.
#   - `//` C++ single-line for brief internal implementation notes that are
#     NOT meant for generated documentation.
#   - `/* ... */` traditional block reserved for multi-line internal notes
#     and ASCII section dividers. Single-line `/* X */` blocks for short
#     notes are NOT permitted -- use `//` instead.
#
# This script checks codebase-wide invariants:
#   1. No standalone single-line `/* X */` block comments on their own line
#      (must be `//`).
#   2. No trailing `; /* X */` or `, /* X */` after a declarator (must be
#      `/**< X */` inside structs/enums, or `// X` elsewhere).
#   3. `clang-format -i` produces no diff (entire tree is format-clean).
#
# Exits 0 on success, 1 on any violation.
#
# Usage:
#   scripts/check_comment_style.sh             # check entire tree
#   scripts/check_comment_style.sh --staged    # only check files staged for commit

set -u

cd "$(git rev-parse --show-toplevel)" || exit 2

MODE="${1:-all}"

if [ "$MODE" = "--staged" ]; then
    FILES=$(git diff --cached --name-only --diff-filter=ACMR | grep -E '\.(c|h)$' || true)
    if [ -z "$FILES" ]; then
        echo "check_comment_style: no .c/.h files staged"
        exit 0
    fi
else
    FILES=$(find src include tests -type f \( -name '*.c' -o -name '*.h' \))
fi

EXIT=0

# Check 1: standalone single-line /* X */ block comments on their own line.
# Excludes lines starting with ` *` (inside an open /** doc-block).
STANDALONE=$(echo "$FILES" | xargs -I{} perl -ne 'next if /^\s*\*/; print "$ARGV:$.:$_" if m{^\s*/\*[ \t]+[^*=].*?[^*=][ \t]+\*/[ \t]*$}' {} 2>/dev/null)
if [ -n "$STANDALONE" ]; then
    echo "FAIL: standalone single-line /* */ block comments found (must be //):"
    echo "$STANDALONE" | head -10
    [ "$(echo "$STANDALONE" | wc -l)" -gt 10 ] && echo "    ... and $(($(echo "$STANDALONE" | wc -l) - 10)) more"
    EXIT=1
fi

# Check 2: trailing /* X */ on declarators.
# After-member context should be /**< */ or ///<. Anything else should be //.
TRAILING=$(echo "$FILES" | xargs -I{} perl -ne 'next if /^\s*\*/; print "$ARGV:$.:$_" if m{[;,][ \t]+/\*[ \t]+[^*=].*?[^*=][ \t]+\*/[ \t]*$}' {} 2>/dev/null)
if [ -n "$TRAILING" ]; then
    echo "FAIL: trailing ; /* */ or , /* */ block comments found (must be /**< */ or //):"
    echo "$TRAILING" | head -10
    [ "$(echo "$TRAILING" | wc -l)" -gt 10 ] && echo "    ... and $(($(echo "$TRAILING" | wc -l) - 10)) more"
    EXIT=1
fi

# Check 3: clang-format conformance.
if command -v clang-format >/dev/null 2>&1; then
    NEEDS_FORMAT=""
    for f in $FILES; do
        if ! diff -q "$f" <(clang-format "$f") >/dev/null 2>&1; then
            NEEDS_FORMAT="$NEEDS_FORMAT$f\n"
        fi
    done
    if [ -n "$NEEDS_FORMAT" ]; then
        echo "FAIL: files not clang-format-conformant (run clang-format -i):"
        printf "%b" "$NEEDS_FORMAT" | head -10
        EXIT=1
    fi
else
    echo "WARN: clang-format not installed -- skipping format check"
fi

if [ $EXIT -eq 0 ]; then
    echo "check_comment_style: PASS (no violations)"
fi

exit $EXIT
