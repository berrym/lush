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

    # Network / sudo / VCS: only flag when the command appears in command
    # position -- at the start of a logical command (after newline, `;`,
    # `|`, `&&`, `||`, `$(`, `` ` ``, or only-whitespace prefix). This
    # avoids false positives when the same word appears as data inside a
    # quoted argument (e.g., `zstyle ':completion:*' users-ignore '... ftp
    # ...'`).
    #
    # Heuristic: extract the first command-position token on the line and
    # any token immediately following `; | && ||`. The pattern is good
    # enough to catch genuine invocations; pathological constructions
    # (e.g., `eval "curl ..."`) get a human eyeball at review time.
    leading_command=$(printf '%s\n' "$line" |
        awk '{
            # Drop leading whitespace.
            sub(/^[ \t]+/, "", $0);
            # First chunk before whitespace or a shell separator.
            match($0, /^[^ \t;|&<>()`#]+/);
            print substr($0, RSTART, RLENGTH);
        }')
    after_separator=$(printf '%s\n' "$line" |
        awk '{
            # Pull each command-position token introduced by a separator.
            n = split($0, parts, /(;|\|\||&&|\||\$\(|`)/);
            for (i = 2; i <= n; i++) {
                sub(/^[ \t]+/, "", parts[i]);
                match(parts[i], /^[^ \t;|&<>()`#]+/);
                if (RLENGTH > 0) print substr(parts[i], RSTART, RLENGTH);
            }
        }')

    for tok in $leading_command $after_separator; do
        case "$tok" in
            curl|wget|ftp|nc|ssh|scp|rsync|telnet)
                emit network "$lineno" "$tok" ;;
            sudo|doas|pkexec|su)
                emit sudo "$lineno" "$tok" ;;
        esac
    done

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

    # external -- live VCS remote ops (only when in command position).
    for tok in $leading_command $after_separator; do
        if [ "$tok" = "git" ]; then
            case "$line" in
                *'git clone '*|*'git fetch '*|*'git pull '*|*'git push '*)
                    emit external "$lineno" 'live git remote op' ;;
            esac
        fi
    done

done < "$SCRIPT"

exit "$((violations > 255 ? 255 : violations))"
