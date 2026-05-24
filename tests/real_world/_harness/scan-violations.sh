#!/bin/bash
# Hermeticity violation scanner.
#
# Walks a script for constructs that break the corpus's determinism /
# hermeticity / self-containment guarantees AND that the standard
# hermeticize.sh transforms cannot fix. Emits one line per violation
# to stdout. Each line: "<category>\t<line_number>\t<excerpt>".
#
# This scanner deliberately does NOT report auto-transformed constructs
# (`$RANDOM`, `$$`, `$(date)`, `$EPOCHSECONDS`, `/dev/[u]random`) --
# hermeticize.sh handles those and logs each transform; reporting them
# here would force a redundant waiver. The scanner's job is to catch
# constructs the harness CANNOT fix automatically:
#
#   network     -- curl, wget, ftp, nc, ssh, scp                  (hard reject)
#   sudo        -- sudo, su, doas, pkexec                         (hard reject)
#   filesys     -- write outside /tmp, read /proc, read /sys      (waivable)
#   external    -- live git/svn/hg remote ops                     (hard reject)
#   nondet      -- non-fixable non-determinism (e.g. $SECONDS)    (waivable)
#
# Exit code: number of violations (capped at 255). 0 = clean.

set -euo pipefail

SCRIPT="${1:?script path required}"

if [ ! -f "$SCRIPT" ]; then
    echo "scan-violations: not a file: $SCRIPT" >&2
    exit 255
fi

violations=0
emit() {
    local category="$1" lineno="$2" excerpt="$3"
    printf '%s\t%d\t%s\n' "$category" "$lineno" "$excerpt"
    violations=$((violations + 1))
}

# Walk line-by-line so we can emit accurate line numbers. The patterns
# below are intentionally over-broad: false positives are cheaper than
# missed violations, and the human-in-the-loop step decides per-site.
while IFS= read -r line; do
    lineno=$((${lineno:-0} + 1))

    # Skip lines that are entirely a comment after leading whitespace.
    case "$line" in
        *[![:space:]#]*) ;;
        *) continue ;;
    esac
    stripped="${line#"${line%%[![:space:]]*}"}"
    case "$stripped" in
        \#*) continue ;;
    esac

    # Non-fixable non-determinism (auto-transformed nondet constructs --
    # $RANDOM, $$, $(date), $EPOCHSECONDS, /dev/[u]random -- are handled by
    # hermeticize.sh and intentionally NOT reported here).
    case "$line" in
        *\$SECONDS*) emit nondet "$lineno" '$SECONDS' ;;
    esac
    case "$line" in
        *\$BASHPID*) emit nondet "$lineno" '$BASHPID' ;;
    esac
    case "$line" in
        *'$(uuidgen'*|*'`uuidgen'*) emit nondet "$lineno" 'uuidgen' ;;
    esac

    # network
    for cmd in curl wget ftp nc ssh scp rsync; do
        case "$line" in
            *"$cmd "*|*"$cmd"$'\t'*|*';'"$cmd"*|*'|'"$cmd"*)
                emit network "$lineno" "$cmd" ;;
        esac
    done

    # sudo / privilege escalation
    for cmd in sudo doas pkexec; do
        case "$line" in
            *"$cmd "*|*"$cmd"$'\t'*) emit sudo "$lineno" "$cmd" ;;
        esac
    done
    # `su -` and bare `su user` (avoid matching `sum`, `subst`, etc.)
    case "$line" in
        *' su '*|*' su -'*|*$'\t''su '*) emit sudo "$lineno" 'su' ;;
    esac

    # filesystem -- write outside /tmp
    case "$line" in
        *'> /var/'*|*'>> /var/'*) emit filesys "$lineno" 'write under /var' ;;
        *'> /etc/'*|*'>> /etc/'*) emit filesys "$lineno" 'write under /etc' ;;
        *'> /usr/'*|*'>> /usr/'*) emit filesys "$lineno" 'write under /usr' ;;
    esac
    case "$line" in
        *'/proc/'*) emit filesys "$lineno" '/proc read' ;;
        *'/sys/'*) emit filesys "$lineno" '/sys read' ;;
    esac

    # external -- live VCS
    case "$line" in
        *'git clone '*|*'git fetch '*|*'git pull '*|*'git push '*)
            emit external "$lineno" 'live git remote op' ;;
    esac

done < "$SCRIPT"

exit "$((violations > 255 ? 255 : violations))"
