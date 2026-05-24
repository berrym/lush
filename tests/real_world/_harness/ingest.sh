#!/bin/bash
# Ingest a candidate script into the upstream-sourced corpus.
#
# Steps:
#   1. Scan the input for hermeticity violations (scan-violations.sh).
#      Hard-reject violations stop the pipeline unless waived; only
#      categories in WAIVABLE_CATEGORIES below accept a --waive.
#   2. Apply standard hermeticity transforms (hermeticize.sh) and
#      record each transform applied.
#   3. Generate the header block (header-template.sh) and prepend it
#      to the transformed body.
#   4. Place the result under tests/real_world/corpus/<bucket>/<set>/
#      using the source filename (or --rename-to NAME if specified).
#   5. Append a row to corpus/<bucket>/<set>/SOURCES.md.
#   6. Append a row to corpus/WAIVERS.md for every --waive used.
#   7. Run the script's reference shell against the adapted file twice
#      and refuse to commit if the output isn't byte-identical between
#      runs (oracle dry-run determinism check).
#
# Usage:
#   ingest.sh \
#       --source-url URL \
#       --upstream-commit SHA \
#       --license SPDX_ID \
#       --bucket posix|bash|zsh|lush \
#       --upstream-set SET_NAME \
#       [--rename-to NAME] \
#       [--waive "category:reason" ...] \
#       <input-script>

set -euo pipefail

HARNESS_DIR="$(cd "$(dirname "$0")" && pwd)"
CORPUS_ROOT="$(cd "$HARNESS_DIR/.." && pwd)/corpus"
WAIVERS_LEDGER="$CORPUS_ROOT/WAIVERS.md"

# Categories that may be waived (a real human reason must accompany).
# `network` is waivable because the scanner's command-position heuristic
# is good but not perfect; false positives are real (e.g., command names
# appearing as data inside quoted arguments). Genuine invocations should
# never be waived -- waivers are reviewed in the WAIVERS.md ledger.
# `sudo` and `external` (live VCS remote ops) remain hard-reject.
WAIVABLE_CATEGORIES=("nondet" "filesys" "network")

source_url=""
upstream_commit=""
license=""
bucket=""
upstream_set=""
rename_to=""
waivers=()
input=""

while [ $# -gt 0 ]; do
    case "$1" in
        --source-url) source_url="$2"; shift 2 ;;
        --upstream-commit) upstream_commit="$2"; shift 2 ;;
        --license) license="$2"; shift 2 ;;
        --bucket) bucket="$2"; shift 2 ;;
        --upstream-set) upstream_set="$2"; shift 2 ;;
        --rename-to) rename_to="$2"; shift 2 ;;
        --waive) waivers+=("$2"); shift 2 ;;
        -*)
            echo "ingest: unknown option: $1" >&2; exit 2 ;;
        *)
            if [ -n "$input" ]; then
                echo "ingest: only one input script expected" >&2; exit 2
            fi
            input="$1"; shift ;;
    esac
done

if [ -z "$input" ] || [ ! -f "$input" ]; then
    echo "ingest: input script required and must exist" >&2
    exit 2
fi
for required in source_url upstream_commit license bucket upstream_set; do
    if [ -z "${!required}" ]; then
        echo "ingest: --${required//_/-} is required" >&2
        exit 2
    fi
done
case "$bucket" in
    posix|bash|zsh|lush) ;;
    *) echo "ingest: --bucket must be posix|bash|zsh|lush" >&2; exit 2 ;;
esac

# ---------------------------------------------------------------------
# Step 1: hermeticity scan
# ---------------------------------------------------------------------
scan_output="$(mktemp -t ingest_scan.XXXXXX)"
trap 'rm -f "$scan_output"' EXIT

# Run the scanner. It exits with the count of violations; we want to
# inspect those rather than fail outright.
set +e
"$HARNESS_DIR/scan-violations.sh" "$input" > "$scan_output"
set -e

# Build waived-categories list (avoid `declare -A` -- not available on
# macOS bash 3.2). Each waiver is "category:reason"; we keep the
# categories as a space-separated string and check for membership.
# Note: `set -u` plus `"${arr[@]}"` on an empty bash array trips the
# unbound check in some bash versions, so guard the loop on length.
waived_categories=""
if [ "${#waivers[@]}" -gt 0 ]; then
    for w in "${waivers[@]}"; do
        cat="${w%%:*}"
        waived_categories="$waived_categories $cat "
    done
fi

is_waived() {
    case "$waived_categories" in
        *" $1 "*) return 0 ;;
        *) return 1 ;;
    esac
}

unwaived=0
while IFS=$'\t' read -r category lineno excerpt; do
    [ -z "$category" ] && continue
    # Hard-reject if the category is not waivable.
    hard_reject=1
    for ok_cat in "${WAIVABLE_CATEGORIES[@]}"; do
        if [ "$category" = "$ok_cat" ]; then
            hard_reject=0
            break
        fi
    done
    if [ "$hard_reject" = 1 ]; then
        echo "ingest: REJECT [$category] line $lineno: $excerpt (not waivable)" >&2
        unwaived=$((unwaived + 1))
        continue
    fi
    # Soft-reject only if not waived for this category.
    if ! is_waived "$category"; then
        echo "ingest: violation [$category] line $lineno: $excerpt (needs --waive '$category:<reason>')" >&2
        unwaived=$((unwaived + 1))
    fi
done < "$scan_output"

if [ "$unwaived" -gt 0 ]; then
    echo "ingest: $unwaived unresolved violation(s); fix or waive before ingesting" >&2
    exit 3
fi

# ---------------------------------------------------------------------
# Step 2: hermeticity transforms
# ---------------------------------------------------------------------
transformed="$(mktemp -t ingest_xform.XXXXXX)"
transform_log="$(mktemp -t ingest_xformlog.XXXXXX)"
trap 'rm -f "$scan_output" "$transformed" "$transform_log"' EXIT

"$HARNESS_DIR/hermeticize.sh" "$input" "$transformed" 2> "$transform_log"

# Collect transform descriptions for the header.
transform_args=()
while IFS=$'\t' read -r rule lineno excerpt; do
    [ -z "$rule" ] && continue
    transform_args+=(--transform "$rule:$lineno")
done < "$transform_log"

# ---------------------------------------------------------------------
# Step 3: header
# ---------------------------------------------------------------------
header_args=(
    --source-url "$source_url"
    --upstream-commit "$upstream_commit"
    --license "$license"
    --bucket "$bucket"
    --upstream-set "$upstream_set"
)
if [ "${#waivers[@]}" -gt 0 ]; then
    for w in "${waivers[@]}"; do
        header_args+=(--waive "$w")
    done
fi
if [ "${#transform_args[@]}" -gt 0 ]; then
    for t in "${transform_args[@]}"; do
        header_args+=("$t")
    done
fi

header_out="$(mktemp -t ingest_header.XXXXXX)"
"$HARNESS_DIR/header-template.sh" "${header_args[@]}" > "$header_out"

# ---------------------------------------------------------------------
# Step 4: place output
# ---------------------------------------------------------------------
basename_in="$(basename "$input")"
final_name="${rename_to:-$basename_in}"
set_dir="$CORPUS_ROOT/$bucket/$upstream_set"
mkdir -p "$set_dir"
final_path="$set_dir/$final_name"

if [ -e "$final_path" ]; then
    echo "ingest: refuse to overwrite existing $final_path" >&2
    exit 4
fi

cat "$header_out" "$transformed" > "$final_path"
chmod +x "$final_path"

# ---------------------------------------------------------------------
# Step 5: SOURCES.md row
# ---------------------------------------------------------------------
sources_md="$set_dir/SOURCES.md"
if [ ! -f "$sources_md" ]; then
    cat > "$sources_md" <<EOF
# Upstream sources for $upstream_set

Generated and maintained by tests/real_world/_harness/ingest.sh.

| Script | Upstream URL | Commit |
|--------|--------------|--------|
EOF
fi
printf '| %s | %s | %s |\n' "$final_name" "$source_url" "$upstream_commit" >> "$sources_md"

# ---------------------------------------------------------------------
# Step 6: WAIVERS.md (one row per waiver)
# ---------------------------------------------------------------------
if [ "${#waivers[@]}" -gt 0 ]; then
    mkdir -p "$CORPUS_ROOT"
    if [ ! -f "$WAIVERS_LEDGER" ]; then
        cat > "$WAIVERS_LEDGER" <<'EOF'
# Corpus hermeticity waivers

Every waiver applied via `ingest.sh --waive` lands here as one row.
Audit periodically; tighten policy by closing waivers that are no
longer justified.

| Date | Bucket | Set | Script | Category | Reason |
|------|--------|-----|--------|----------|--------|
EOF
    fi
    today="$(date -u +%Y-%m-%d)"
    for w in "${waivers[@]}"; do
        category="${w%%:*}"
        reason="${w#*:}"
        printf '| %s | %s | %s | %s | %s | %s |\n' \
            "$today" "$bucket" "$upstream_set" "$final_name" \
            "$category" "$reason" >> "$WAIVERS_LEDGER"
    done
fi

# ---------------------------------------------------------------------
# Step 7: oracle dry-run (determinism check, LC_ALL=C)
# ---------------------------------------------------------------------
oracle_bin=""
case "$bucket" in
    posix) oracle_bin="$(command -v dash || true)" ;;
    bash) oracle_bin="$(command -v bash || true)" ;;
    zsh) oracle_bin="$(command -v zsh || true)" ;;
    lush) oracle_bin="" ;; # No oracle for lush polyglot scripts
esac

if [ -n "$oracle_bin" ]; then
    out_a="$(mktemp -t ingest_oracle_a.XXXXXX)"
    out_b="$(mktemp -t ingest_oracle_b.XXXXXX)"
    trap 'rm -f "$scan_output" "$transformed" "$transform_log" "$header_out" "$out_a" "$out_b"' EXIT

    LC_ALL=C "$oracle_bin" "$final_path" > "$out_a" 2>&1 || true
    LC_ALL=C "$oracle_bin" "$final_path" > "$out_b" 2>&1 || true

    if ! diff -q "$out_a" "$out_b" > /dev/null 2>&1; then
        echo "ingest: oracle dry-run NOT deterministic across two runs" >&2
        echo "ingest: candidate placed at $final_path but flagged for review" >&2
        echo "ingest: diff:" >&2
        diff "$out_a" "$out_b" >&2 || true
        # Don't unlink -- a human needs to investigate.
        exit 5
    fi
    echo "ingest: oracle dry-run deterministic ($oracle_bin)"
fi

echo "ingest: $final_path"
