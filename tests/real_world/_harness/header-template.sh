#!/bin/bash
# Generate the standard corpus-script header block.
#
# The header is emitted to stdout. The caller prepends it to the
# adapted script body. Every field is required except WAIVED, which is
# emitted only when the caller passes --waive.
#
# Usage:
#   header-template.sh \
#       --source-url URL \
#       --upstream-commit SHA \
#       --license SPDX_ID \
#       --bucket posix|bash|zsh|lush \
#       --upstream-set SET_NAME \
#       [--waive "category:reason" ...] \
#       [--transform "rule:lineno" ...]
#
# The transforms argument is repeatable; each one becomes one
# "TRANSFORMS:" line in the header. Same for --waive.

set -euo pipefail

source_url=""
upstream_commit=""
license=""
bucket=""
upstream_set=""
waivers=()
transforms=()

while [ $# -gt 0 ]; do
    case "$1" in
        --source-url) source_url="$2"; shift 2 ;;
        --upstream-commit) upstream_commit="$2"; shift 2 ;;
        --license) license="$2"; shift 2 ;;
        --bucket) bucket="$2"; shift 2 ;;
        --upstream-set) upstream_set="$2"; shift 2 ;;
        --waive) waivers+=("$2"); shift 2 ;;
        --transform) transforms+=("$2"); shift 2 ;;
        *) echo "header-template: unknown arg: $1" >&2; exit 2 ;;
    esac
done

for required in source_url upstream_commit license bucket upstream_set; do
    if [ -z "${!required}" ]; then
        echo "header-template: --${required//_/-} is required" >&2
        exit 2
    fi
done

case "$bucket" in
    posix|bash|zsh|lush) ;;
    *) echo "header-template: --bucket must be posix|bash|zsh|lush" >&2
       exit 2 ;;
esac

today="$(date -u +%Y-%m-%d)"

cat <<EOF
# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           $source_url
# UPSTREAM-COMMIT:  $upstream_commit
# UPSTREAM-SET:     $upstream_set
# LICENSE:          $license
# BUCKET:           $bucket
# ADAPTED:          $today
EOF

if [ "${#transforms[@]}" -gt 0 ]; then
    echo "# TRANSFORMS:"
    for t in "${transforms[@]}"; do
        echo "#   - $t"
    done
fi

if [ "${#waivers[@]}" -gt 0 ]; then
    echo "# WAIVED:"
    for w in "${waivers[@]}"; do
        echo "#   - $w"
    done
fi

echo "# ============================================================================"
