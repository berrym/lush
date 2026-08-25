#!/bin/sh
## The `[index]=value` element form inside a list literal.
##
## WHY THIS EXISTS
##
## `a=( ... )` and `a+=( ... )` are two spellings of one syntax, and they had
## drifted apart in three separate ways -- each one a silent wrong answer
## rather than a visible failure:
##
##   a=(z [1]=y w)     dropped y: the positional counter was left AT the
##                     explicit index instead of past it, so the next bare
##                     element overwrote the slot just filled (#640)
##   a+=([1]=Z)        grew the list by one and stored the literal five-byte
##                     string "[1]=Z" as an element, because the append loop
##                     never parsed the form at all (#640)
##   zsh mode [n]=     ignored the 1-based origin that the element write path
##                     `a[1]=v` right beside it honors, and silently accepted
##                     an index 0 that its sibling rejects (#793)
##
## Both spellings now go through one shared helper, so they cannot diverge
## again. These checks pin the rules that helper implements:
##
##   1. An explicit [n]= writes n, and the counter resumes at n+1.
##   2. `+=` means AFTER what is already there -- a bare element in an append
##      takes the next free slot, never the origin.
##   3. The origin is the mode's, whichever spelling states the index.
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

## Assert the diagnostic reaches stderr, not merely that nothing was written:
## a silent no-op and a diagnosed refusal both leave the array untouched.
## $1 label, $2 mode flag, $3 script, $4 substring required in stderr
check_err() {
    checks=$((checks + 1))
    # shellcheck disable=SC2086
    got=$("$LUSH" $2 -c "$3" 2>&1 >/dev/null) || true
    case "$got" in
    *"$4"*) printf '  OK   %s\n' "$1" ;;
    *)
        printf '  FAIL %s\n     stderr lacked [%s]\n     got      [%s]\n' \
            "$1" "$4" "$got"
        failures=$((failures + 1))
        ;;
    esac
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

printf '== the origin is the mode owners, whichever spelling states it (#793) ==\n'
## zsh mode is 1-based, and `a[1]=v` has always honored that. The literal form
## stored the raw arithmetic result, so lush disagreed with itself.
check 'zsh mode [1] is the first slot' '--zsh' \
    'a=(z y w); a[1]=Q; printf "%s" "${a[*]}"' 'Q y w'
check 'zsh mode literal [1] agrees'    '--zsh' \
    'a=(z [1]=y w); printf "%s" "${a[*]}"' 'y w'
check 'zsh mode append [1] agrees'     '--zsh' \
    'a=(p q); a+=([1]=Z); printf "%s" "${a[*]}"' 'Z q'
## Index 0 addresses nothing in a 1-based mode; the element path already
## refused it and the literal form silently accepted it.
check 'zsh mode literal [0] refused'   '--zsh' \
    'a=(z [0]=y w); printf "%s" "${a[*]}"' ''
## The 0-based modes are untouched.
check 'lush mode stays 0-based'  '' \
    'a=(z [1]=y w); printf "%s" "${a[*]}"' 'z y w'
check 'bash mode stays 0-based'  '--bash' \
    'a=(z [1]=y w); printf "%s" "${a[*]}"' 'z y w'
## posix mode is deliberately absent from this group: it provides no array
## literal at all, matching dash, so there is no origin for it to state.

printf '== a negative element counts from the END, like every other surface (#795) ==\n'
## It used to fall through to the positional counter, so [-1] wrote wherever
## the counter happened to point. The append path made that visible: its
## counter starts unseeded at -1, which the primitive then resolved from the
## end -- so the SAME spelling gave two answers depending on whether a bare
## element came first.
check 'a+=([-1]=v) writes the last'   '' \
    'a=(10 20 30); a+=([-1]=v); printf "%s" "${a[*]}"' '10 20 v'
check 'a+=(x [-1]=v) writes the last' '' \
    'a=(10 20 30); a+=(x [-1]=v); printf "%s" "${a[*]}"' '10 20 30 v'
## In a literal the end is wherever the literal has reached so far.
check 'a=(p q [-1]=v) from the end'   '' \
    'a=(p q [-1]=v); printf "%s" "${a[*]}"' 'p v'
check 'a=(p q r [-2]=v)'              '' \
    'a=(p q r [-2]=v); printf "%s" "${a[*]}"' 'p v r'
## The counter resumes past the RESOLVED index, not past the negative.
check 'a=(p q [-1]=v w) resumes'      '' \
    'a=(p q [-1]=v w); printf "%s|%s" "${a[*]}" "${!a[*]}"' 'p v w|0 1 2'
## A negative with nothing yet to count back from is out of range, and is
## diagnosed rather than silently written somewhere. The element write surface
## already answered this condition the same way.
check_err 'a=([-1]=v) out of range'   '' 'a=([-1]=v)' 'out of range'
check_err 'a=([-1]=v) names the array' '' 'a=([-1]=v)' "for 'a'"
check 'a=([-1]=v) binds nothing'      '' \
    'a=([-1]=v); printf "[%s]" "${a[*]}"' '[]'
check_err 'a=(p [-9]=v) past the start' '' 'a=(p [-9]=v)' 'out of range'
## bash mode shares the engine rule.
check 'bash mode negative from the end' '--bash' \
    'a=(p q [-1]=v); printf "%s" "${a[*]}"' 'p v'
check 'bash mode append from the end'   '--bash' \
    'a=(10 20 30); a+=(x [-1]=v); printf "%s" "${a[*]}"' '10 20 30 v'

printf '== zsh mode refuses a negative HERE, and only here (oracle fidelity) ==\n'
## This is deliberately NOT the engine rule. zsh accepts `a[-1]=v` on its
## element surface and refuses `a=([-1]=v)` in a literal, so a script written
## for zsh sees that split. A compatibility mode reproduces its target rather
## than correcting it; engine cohesion is lush mode's job.
check 'zsh mode refuses a literal negative' '--zsh' \
    'a=(10 20 30); a=(p q [-1]=v); printf "%s" "${a[*]}"' '10 20 30'
check_err 'zsh mode diagnoses it'          '--zsh' \
    'a=(p q [-1]=v)' 'must be positive'
check 'zsh mode refuses it on append too'  '--zsh' \
    'a=(10 20 30); a+=([-1]=v); printf "%s" "${a[*]}"' '10 20 30'
## The other half of the split: the ELEMENT surface still takes a negative in
## zsh mode, exactly as zsh does (#629). If this ever starts failing, the gate
## above has leaked out of the literal surface.
check 'zsh mode element negative still works' '--zsh' \
    'a=(p q r); a[-1]=v; printf "%s" "${a[*]}"' 'p q v'
check 'zsh mode arith negative still works'   '--zsh' \
    'a=(10 20 30); printf "%s" "$(( a[-1] ))"' '30'
## ...and zsh-mode POSITIVES in a literal stay 1-based (#793).
check 'zsh mode positive still 1-based' '--zsh' \
    'a=(z [1]=y w); printf "%s" "${a[*]}"' 'y w'

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
