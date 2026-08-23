#!/bin/sh
## A negative subscript counts from the end -- on every surface, in every mode.
##
## WHY THIS EXISTS
##
## The index BASE and the SIGN are orthogonal. The base says where counting
## starts (0 in lush/bash/posix modes, 1 in zsh mode); the sign says which END
## to count from, and that is the same question in either base.
##
## The 1-based guards conflated the two by refusing every index <= 0, so zsh
## mode gave three different answers to one question (#629):
##
##   ${a[-1]}          read from the end       -- correct
##   a[-1]=v           refused as "must be positive"
##   $(( a[-1] ))      answered 0, silently -- a wrong NUMBER, not an error
##   (( a[-1] = v ))   refused as "out of range"
##
## Only index 0 addresses nothing in a 1-based mode. That boundary is asserted
## here too, because separating the sign from the base widens what is accepted
## and index 0 must not be swept in with it.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
failures=0
checks=0

## $1 label, $2 mode flag (empty for lush mode), $3 script, $4 expected stdout
check() {
    checks=$((checks + 1))
    ## Mode flag stays unquoted so an empty flag contributes no argument.
    # shellcheck disable=SC2086
    got=$("$LUSH" $2 -c "$3" 2>/dev/null) || true
    if [ "$got" = "$4" ]; then
        printf '  OK   %s\n' "$1"
    else
        printf '  FAIL %s\n     expected [%s]\n     got      [%s]\n' \
            "$1" "$4" "$got"
        failures=$((failures + 1))
    fi
}

## Every surface, asserted in each mode by the same loop, so a mode cannot be
## quietly left out of a row.
for spec in "lush:" "bash:--bash" "zsh:--zsh"; do
    name=${spec%%:*}
    flag=${spec#*:}

    printf '== %s mode ==\n' "$name"

    check "$name read \${a[-1]}"        "$flag" \
        'a=(10 20 30); printf "%s" "${a[-1]}"' '30'
    check "$name read \${a[-3]}"        "$flag" \
        'a=(10 20 30); printf "%s" "${a[-3]}"' '10'
    check "$name length \${#a[-1]}"     "$flag" \
        'a=(10 20 30); printf "%s" "${#a[-1]}"' '2'
    check "$name write a[-1]=99"        "$flag" \
        'a=(10 20 30); a[-1]=99; printf "%s" "${a[*]}"' '10 20 99'
    check "$name write a[-2]=X"         "$flag" \
        'a=(10 20 30); a[-2]=X; printf "%s" "${a[*]}"' '10 X 30'
    check "$name unset a[-1]"           "$flag" \
        'a=(10 20 30); unset "a[-1]"; printf "%s" "${a[*]}"' '10 20'
    ## The arithmetic surface answered 0 here in zsh mode: a plausible number
    ## rather than a diagnostic, which is the worst failure mode of the four.
    check "$name arith \$(( a[-1] ))"   "$flag" \
        'a=(10 20 30); printf "%s" "$(( a[-1] ))"' '30'
    check "$name arith write"           "$flag" \
        'a=(10 20 30); (( a[-1] = 99 )); printf "%s" "${a[*]}"' '10 20 99'
    ## The two surfaces must agree with each other, not merely each with a
    ## reference: a plain write is read back by the arithmetic surface.
    check "$name write/arith agree"     "$flag" \
        'a=(10 20 30); a[-1]=77; printf "%s" "$(( a[-1] ))"' '77'
done

printf '== index 0: the boundary the change must not cross ==\n'
## 0 is a valid index where counting starts at 0, and addresses nothing where
## it starts at 1.
check 'lush mode a[0] is the first element' '' \
    'a=(10 20 30); printf "%s" "${a[0]}"' '10'
check 'zsh mode ${a[0]} is empty'  '--zsh' \
    'a=(10 20 30); printf "%s" "${a[0]}"' ''
check 'zsh mode a[0]=X refused'    '--zsh' \
    'a=(10 20 30); a[0]=X; printf "%s" "${a[*]}"' '10 20 30'
check 'zsh mode (( a[0]=X )) refused' '--zsh' \
    'a=(10 20 30); (( a[0] = 1 )); printf "%s" "${a[*]}"' '10 20 30'

printf '== out of range, and neighbouring kinds ==\n'
## A negative reaching past the start selects nothing; it must not wrap round
## to the end or clamp to the first element.
check 'lush ${a[-9]} out of range' '' \
    'a=(10 20 30); printf "[%s]" "${a[-9]}"' '[]'
check 'zsh  ${a[-9]} out of range' '--zsh' \
    'a=(10 20 30); printf "[%s]" "${a[-9]}"' '[]'
## A sparse list counts from its highest index, not its element count
## (SEMANTICS 3.11).
check 'sparse list counts from the end' '' \
    'a=([0]=p [5]=q); printf "%s" "${a[-1]}"' 'q'
## A map is keyed by strings, so "-1" is a KEY and no arithmetic happens.
check 'map key -1 stays a literal key' '' \
    'declare -A m; m[-1]=v; printf "%s|%s" "${!m[*]}" "${m[-1]}"' '-1|v'
## The scalar grapheme slice already counted from the end (#68); it must stay
## that way in both bases.
check 'scalar slice ${s[-1]}'      '' \
    's=abcd; printf "%s" "${s[-1]}"' 'd'
check 'zsh scalar slice ${s[-1]}'  '--zsh' \
    's=abcd; printf "%s" "${s[-1]}"' 'd'

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
