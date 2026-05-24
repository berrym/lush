#!/bin/bash
# Real-world script scorecard. Runs diff_oracle on every script under the
# given root and prints a summary suitable for release-readiness review.
#
# Usage: real_world_scorecard.sh <diff_oracle path> <corpus root>
#
# Exit code: 0 (always). The test target is informational -- divergences
# are concrete 1.5.0 punch-list items rather than CI failures, so the
# scorecard does not fail the test suite. If you want this to gate CI,
# wrap it in a stricter front-end.

set -e
DIFF_ORACLE="${1:?diff_oracle binary path required}"
ROOT="${2:?corpus root required}"
LUSH_BIN="${3:-./build/lush}"

if [ ! -x "$DIFF_ORACLE" ]; then
    echo "diff_oracle not built at $DIFF_ORACLE" >&2
    exit 0
fi

if [ ! -d "$ROOT" ]; then
    echo "corpus root not found: $ROOT" >&2
    exit 0
fi

SCRIPTS=$(find "$ROOT" \
    -path '*/_harness/*' -prune -o \
    -name '*.sh' -type f -print | sort)
if [ -z "$SCRIPTS" ]; then
    echo "no scripts under $ROOT (yet)"
    exit 0
fi

# Capture diff_oracle output and parse with python3 for the summary
TMP=$(mktemp -t real_world.XXXXXX)
trap 'rm -f "$TMP"' EXIT

# diff_oracle's default --allowlist path is relative to its CWD
# (tests/fuzz/differential/known_divergences.txt). meson runs the
# scorecard from the build directory so that relative path doesn't
# resolve. Compute the absolute allowlist path from the corpus root:
# tests/real_world/ lives at the repo top, so the grandparent of the
# corpus root is the repo root.
ALLOWLIST="$(dirname "$(dirname "$ROOT")")/tests/fuzz/differential/known_divergences.txt"

# shellcheck disable=SC2086  # SCRIPTS is intentionally word-split
"$DIFF_ORACLE" --allowlist "$ALLOWLIST" --lush "$LUSH_BIN" $SCRIPTS \
    > "$TMP" 2>&1 || true

python3 - "$TMP" "$ROOT" <<'PYEOF'
import json, sys, os
log_path, root = sys.argv[1], sys.argv[2]
agree=diverge=allowed=oracle_missing=0
diverged=[]
with open(log_path) as f:
    for line in f:
        line=line.strip()
        if not line.startswith('{'): continue
        try: d=json.loads(line)
        except Exception: continue
        if not d.get('oracle', True):
            oracle_missing += 1
            continue
        if d['agree']:
            agree += 1
        elif d.get('allowed'):
            allowed += 1
        else:
            diverge += 1
            path=d['path'].replace(root.rstrip('/')+'/', '')
            le=d['lush'].get('exit')
            oe=d['oracle_result'].get('exit')
            tag=[]
            if le != oe: tag.append(f'exit {le}!={oe}')
            if d['lush'].get('stdout','') != d['oracle_result'].get('stdout',''):
                tag.append('stdout')
            if d['lush'].get('stderr','') != d['oracle_result'].get('stderr',''):
                tag.append('stderr')
            diverged.append((path, ', '.join(tag) or 'misc'))

total = agree+diverge+allowed
print('=== real-world scorecard ===')
print(f'Total scripts:     {total}')
print(f'Pass:              {agree}')
print(f'Allowed-divergent: {allowed}')
print(f'DIVERGE:           {diverge}')
print(f'Oracle missing:    {oracle_missing}')
print()
if total:
    pct = 100.0 * (agree + allowed) / total
    print(f'Pass rate: {pct:.1f}%')
print()
if diverged:
    print('=== divergences (1.5.0 punch list) ===')
    for path, tag in diverged:
        print(f'  {path}  ({tag})')
PYEOF
