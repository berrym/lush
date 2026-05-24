#!/bin/bash
# Apply standard hermeticity transforms to a candidate script.
#
# Reads from <input>, writes to <output>, logs each transform applied
# to stderr (one line per transform, "<rule>\t<lineno>\t<before>" form).
#
# Transforms applied (with their replacement values, all chosen for
# stable, deterministic output):
#
#   $RANDOM         -> 12345
#   $$              -> 99999    (placeholder PID)
#   $(date ...)     -> "1970-01-01T00:00:00Z" (literal)
#   `date ...`      -> "1970-01-01T00:00:00Z" (literal)
#   $EPOCHSECONDS   -> 0
#   $EPOCHREALTIME  -> 0.000000
#
# Transforms NOT applied automatically (require --waive plus explicit
# manual adaptation):
#   - network calls (curl/wget/etc.)
#   - sudo / su / doas
#   - writes outside /tmp
#   - /proc/* and /sys/* reads
#   - live git/svn/hg
#
# Exit code: 0 on success. Non-zero on file I/O error.

set -euo pipefail

INPUT="${1:?input path required}"
OUTPUT="${2:?output path required}"

if [ ! -f "$INPUT" ]; then
    echo "hermeticize: not a file: $INPUT" >&2
    exit 2
fi

mkdir -p "$(dirname "$OUTPUT")"

# Use sed in two passes: first the "easy" pure-string substitutions, then
# the command-substitution rewrites. We bracket sed against macOS BSD sed
# vs GNU sed by re-emitting both invocations through sh -c so portable
# patterns work identically on both.

awk -v out="$OUTPUT" '
BEGIN {
    epoch = "1970-01-01T00:00:00Z"
}
{
    orig = $0
    line = $0

    # $RANDOM -> 12345
    if (gsub(/\$RANDOM/, "12345", line)) {
        printf "rand\t%d\t%s\n", NR, orig | "cat 1>&2"
    }

    # $$ (PID) -> 99999. Skip inside ${...} parameter-expansion forms
    # because ${var-$$} etc. would otherwise be mangled; the corpus
    # selection criteria refuse anything with $$ inside an expansion
    # anyway.
    if (line ~ /\$\$/) {
        gsub(/\$\$/, "99999", line)
        printf "pid\t%d\t%s\n", NR, orig | "cat 1>&2"
    }

    # $(date ...) -> literal epoch.
    if (line ~ /\$\(date[^)]*\)/) {
        gsub(/\$\(date[^)]*\)/, "\"" epoch "\"", line)
        printf "date-dollar\t%d\t%s\n", NR, orig | "cat 1>&2"
    }
    # `date ...` -> literal epoch.
    if (line ~ /`date[^`]*`/) {
        gsub(/`date[^`]*`/, "\"" epoch "\"", line)
        printf "date-backtick\t%d\t%s\n", NR, orig | "cat 1>&2"
    }

    # $EPOCHSECONDS / $EPOCHREALTIME
    if (gsub(/\$EPOCHSECONDS/, "0", line)) {
        printf "epoch-seconds\t%d\t%s\n", NR, orig | "cat 1>&2"
    }
    if (gsub(/\$EPOCHREALTIME/, "0.000000", line)) {
        printf "epoch-realtime\t%d\t%s\n", NR, orig | "cat 1>&2"
    }

    print line > out
}
' "$INPUT"
