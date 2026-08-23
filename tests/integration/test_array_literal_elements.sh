#!/bin/sh
## The `[index]=value` element form inside a list literal.
##
## WHY THIS EXISTS
##
## `a=( ... )` and `a+=( ... )` are two spellings of one syntax, and they had
## drifted apart -- each one a silent wrong answer rather than a visible
## failure:
##
##   a=(z [1]=y w)     dropped y: the positional counter was left AT the
##                     explicit index instead of past it, so the next bare
##                     element overwrote the slot just filled (#640)
##   a+=([1]=Z)        grew the list by one and stored the literal five-byte
##                     string "[1]=Z" as an element, because the append loop
##                     never parsed the form at all (#640)
##
## Both spellings now go through one shared helper, so they cannot diverge
## again. These checks pin the rules that helper implements:
##
##   1. An explicit [n]= writes n, and the counter resumes at n+1.
##   2. `+=` means AFTER what is already there -- a bare element in an append
##      takes the next free slot, never the origin.
##
## Rule 2 is lush's own derivation rather than a copy: the peers disagree here,
## one continuing after the existing content and the other restarting at the
## origin, and restarting would make an append overwrite what it appends to.
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

printf '== the positional counter resumes PAST an explicit index (#640) ==\n'
## Values alone are not enough here: the keys are asserted too, because a
## dropped element and a shifted one can print the same joined string.
check 'a=(z [1]=y w) keeps y' '' \
    'a=(z [1]=y w); printf "%s" "${a[*]}"' 'z y w'
check 'a=(z [1]=y w) indices' '' \
    'a=(z [1]=y w); printf "%s" "${!a[*]}"' '0 1 2'
## A gap is left where the explicit index skipped ahead; the list is sparse
## (SEMANTICS 3.11), and the counter continues from the stated index.
check 'a=(z [5]=y w) indices' '' \
    'a=(z [5]=y w); printf "%s" "${!a[*]}"' '0 5 6'
check 'two explicit indices' '' \
    'a=([2]=c [0]=a b); printf "%s" "${!a[*]}"' '0 1 2'

printf '== a bare append interprets the element form (#640) ==\n'
## This stored the literal string "[1]=Z" as a third element.
check 'a+=([1]=Z) writes index 1' '' \
    'a=(p q); a+=([1]=Z); printf "%s|%s" "${a[*]}" "${#a[@]}"' 'p Z|2'
## `+=` means after what exists: the bare x takes the next free slot, then the
## explicit [5] moves the counter, then z follows it.
check 'a+=(x [5]=y z) positions' '' \
    'a=(p q); a+=(x [5]=y z); printf "%s" "${!a[*]}"' '0 1 2 5 6'
check 'a+=(x [5]=y z) values' '' \
    'a=(p q); a+=(x [5]=y z); printf "%s" "${a[*]}"' 'p q x y z'
## A leading explicit index in an append must not be treated as position 0.
check 'a+=([5]=y z) after existing' '' \
    'a=(p q); a+=([5]=y z); printf "%s" "${!a[*]}"' '0 1 5 6'

printf '== controls: the surrounding behavior is unchanged ==\n'
check 'plain literal'        '' 'a=(p q r); printf "%s" "${a[*]}"' 'p q r'
check 'plain append'         '' 'a=(p q); a+=(x y); printf "%s" "${a[*]}"' 'p q x y'
check 'assoc literal'        '' \
    'declare -A m; m=([k]=v [j]=w); printf "%s" "${#m[@]}"' '2'
check 'assoc append'         '' \
    'declare -A m; m=([a]=1); m+=([k]=v); printf "%s|%s" "${#m[@]}" "${!m[*]}"' '2|a k'
## The aggregate selector is not a writable element target, in either
## spelling (#627).
check 'a=([@]=x) refused'    '' 'a=([@]=x); printf "%s" "${a[*]}"' ''
check 'a+=([@]=x) refused'   '' 'a=(p); a+=([@]=x); printf "%s" "${a[*]}"' 'p'
## An expression is still evaluated in the subscript.
check 'a=([1+1]=y) evaluates' '' 'a=([1+1]=y); printf "%s" "${!a[*]}"' '2'

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
