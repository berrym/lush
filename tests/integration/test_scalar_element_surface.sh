#!/bin/sh
## The scalar element surface: what a subscript on a scalar means, on every
## surface that accepts one, in every mode.
##
## WHY THIS EXISTS
##
## A subscript on a scalar is not element addressing -- a scalar has no
## elements (SEMANTICS 3.1). It names a TR#29 grapheme-cluster slice: a
## read-only VIEW of the string. That single rule decides every surface, and
## the surfaces had drifted into four mutually contradictory answers:
##
##   ${s[0]}      a grapheme slice                       (correct)
##   ${#s[0]}     a flat 0, contradicting the slice       (#784)
##   s[0]=x       E1134, refusing to write through a view (correct)
##   unset s[0]   a silent no-op with status 0            (#634)
##
## Read and write already disagreed with length and unset, so this file pins
## all four together. The rule to hold: a view can be READ and MEASURED, and
## cannot be WRITTEN or REMOVED through.
##
## Mode gating follows SEMANTICS 3.9 -- strict in lush mode, reconciled to
## each oracle in the compatibility modes. The peers genuinely disagree here
## (bash reads a scalar as a degenerate array and DESTROYS the whole binding
## for `unset s[0]`; zsh and dash both reject the subscript), so "matches
## bash" is not a passing grade for any of these checks; each mode is asserted
## against its own reference.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
failures=0
checks=0

## $1 label, $2 mode flag (empty for lush mode), $3 script, $4 expected stdout
check() {
    checks=$((checks + 1))
    ## Mode flag must stay unquoted so an empty flag contributes no argument.
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

## Assert the diagnostic actually reaches stderr, not merely that the status
## is non-zero -- a silent failure and a diagnosed one both exit non-zero.
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

printf '== a subscript on a scalar READS a grapheme slice ==\n'
## Base is the same preset the array surfaces use: 0-based in lush, bash and
## posix modes, 1-based in zsh mode. The subject is four distinct characters
## so an off-by-one cannot pass, unlike a single-character subject.
check 'lush  ${s[0]}'  ''        's=abcd; printf "%s" "${s[0]}"'   'a'
check 'bash  ${s[0]}'  '--bash'  's=abcd; printf "%s" "${s[0]}"'   'a'
check 'posix ${s[0]}'  '--posix' 's=abcd; printf "%s" "${s[0]}"'   'a'
check 'zsh   ${s[1]}'  '--zsh'   's=abcd; printf "%s" "${s[1]}"'   'a'
check 'zsh   ${s[0]} addresses nothing' '--zsh' \
    's=abcd; printf "[%s]" "${s[0]}"' '[]'
check 'lush  ${s[1,2]} range' '' 's=abcd; printf "%s" "${s[1,2]}"' 'bc'
check 'lush  ${s[-1]} from end' '' 's=abcd; printf "%s" "${s[-1]}"' 'd'

printf '== ${#s[N]} measures that same slice, in every mode (#784) ==\n'
## The length of a reference is the length of what the reference expands to.
## This answered a flat 0 for every slice, in every mode, while the slice
## beside it expanded non-empty.
check 'lush  ${#s[0]}'    ''        's=abcd; printf "%s" "${#s[0]}"'   '1'
check 'bash  ${#s[0]}'    '--bash'  's=abcd; printf "%s" "${#s[0]}"'   '1'
check 'posix ${#s[0]}'    '--posix' 's=abcd; printf "%s" "${#s[0]}"'   '1'
check 'zsh   ${#s[1]}'    '--zsh'   's=abcd; printf "%s" "${#s[1]}"'   '1'
check 'zsh   ${#s[0]} is 0' '--zsh' 's=abcd; printf "%s" "${#s[0]}"'   '0'
check 'lush  ${#s[1,2]}'  ''        's=abcd; printf "%s" "${#s[1,2]}"' '2'
check 'lush  ${#s[-1]}'   ''        's=abcd; printf "%s" "${#s[-1]}"'  '1'
## The length must track the slice rather than being computed beside it: an
## out-of-range slice is empty, so its length is 0 and not the string length.
check 'lush  ${#s[9]} out of range' '' 's=abcd; printf "%s" "${#s[9]}"' '0'

printf '== a view cannot be WRITTEN through (S3.9, strict in lush mode) ==\n'
check_err 'lush  s[0]=x diagnoses' '' 's=abcd; s[0]=x' 'E1134'
## The write error requests a POSIX exit, so the refused assignment is run in
## a subshell to leave a surviving context that can observe the binding. The
## point is that the value is unchanged, not merely that the status is 1.
check     'lush  s[0]=x leaves the value intact' '' \
    's=abcd; (s[0]=x); printf "%s" "$s"' 'abcd'
## `unset`'s refusal sets status 1 and continues rather than aborting, which
## is the convention of the readonly refusal directly beside it in the same
## builtin; the assignment surface has its own abort discipline.

printf '== a view cannot be REMOVED through either (#634) ==\n'
## `unset` removes a binding or a list/map element; a slice is neither, so the
## request had nothing to act on and disappeared with status 0. Diagnosed as
## the same category error the write surface refuses, so the two spellings of
## one mistake stop disagreeing.
check_err 'lush  unset s[0] diagnoses' '' 's=7; unset "s[0]"' 'E1134'
check     'lush  unset s[0] status 1'  '' \
    's=7; unset "s[0]" 2>/dev/null; printf "%s" "$?"' '1'
check     'lush  unset s[0] value survives' '' \
    's=7; unset "s[0]" 2>/dev/null; printf "%s" "$s"' '7'
## zsh and dash both reject a subscript on a scalar, so those modes diagnose.
check_err 'zsh   unset s[0] diagnoses'   '--zsh'   's=7; unset "s[0]"' 'E1134'
check_err 'posix unset s[0] diagnoses'   '--posix' 's=7; unset "s[0]"' 'E1134'
## bash reads a scalar as a degenerate array whose element 0 IS the variable,
## so bash mode destroys the binding and succeeds -- reconciled to the oracle,
## not to lush's rule.
check 'bash  unset s[0] removes the binding' '--bash' \
    's=7; unset "s[0]"; printf "[%s]%s" "$s" "$?"' '[]0'

printf '== the readonly guard still applies on every path ==\n'
## bash mode retargets the unset at the bare name and falls through to the
## shared nameref + readonly tail, so it must NOT gain a way around the guard
## that every other mutation surface enforces. bash refuses this too.
check 'lush  readonly survives'  '' \
    'readonly s=7; unset "s[0]" 2>/dev/null; printf "%s" "$s"' '7'
check 'bash  readonly survives'  '--bash' \
    'readonly s=7; unset "s[0]" 2>/dev/null; printf "%s" "$s"' '7'
check 'zsh   readonly survives'  '--zsh' \
    'readonly s=7; unset "s[0]" 2>/dev/null; printf "%s" "$s"' '7'
check 'bash  readonly reports failure' '--bash' \
    'readonly s=7; unset "s[0]" 2>/dev/null; printf "%s" "$?"' '1'

printf '== real lists and maps are untouched by all of the above ==\n'
## The scalar branch must not intercept a genuine element unset.
check 'indexed unset a[0]' '' \
    'a=(x y z); unset "a[0]"; printf "%s|%s" "${#a[@]}" "${a[*]}"' '2|y z'
check 'zsh-mode indexed unset a[1]' '--zsh' \
    'a=(x y z); unset "a[1]"; printf "%s" "${a[*]}"' 'y z'
check 'assoc unset m[k]' '' \
    'declare -A m; m[k]=1; m[j]=2; unset "m[k]"; printf "%s" "${#m[@]}"' '1'
check 'plain unset still removes the binding' '' \
    's=7; unset s; printf "[%s]" "$s"' '[]'
## An unset name with a subscript binds nothing; removing nothing succeeds,
## as it does in bash.
check 'unset nonexistent[0] succeeds' '' \
    'unset "nope[0]"; printf "%s" "$?"' '0'

printf '== the subscript is an arithmetic expression (#785) ==\n'
## SEMANTICS 3.13: ${a[i]} is an expression, ${a["i"]} a literal key. This
## path used atoi, which parses a leading integer and stops -- so `i` read as
## 0 and `1+1` as 1, silently, while lush's own array path evaluated both.
## The subject is four distinct characters so reading 0 cannot pass as a
## correct answer.
check 'variable subscript ${s[i]}'   '' 's=abcd; i=2; printf "%s" "${s[i]}"'   'c'
check 'expression ${s[1+1]}'         '' 's=abcd; printf "%s" "${s[1+1]}"'      'c'
check 'nested ${s[$((1+1))]}'        '' 's=abcd; printf "%s" "${s[$((1+1))]}"' 'c'
check 'both range bounds ${s[i,j]}'  '' \
    's=abcd; i=1; j=2; printf "%s" "${s[i,j]}"' 'bc'
check 'expression range ${s[0+1,1+2]}' '' \
    's=abcd; printf "%s" "${s[0+1,1+2]}"' 'bcd'
## An unset name is 0 in shell arithmetic, so this addresses the first
## cluster -- it is not an error, and must not become one.
check 'unset name in subscript is 0' '' \
    's=abcd; printf "%s" "${s[nope]}"' 'a'
## The length operator shares the slice, so it inherits the arithmetic.
check 'length follows: ${#s[i]}'     '' 's=abcd; i=2; printf "%s" "${#s[i]}"'  '1'
## Negative arithmetic still counts clusters from the end (#68).
check 'negative expression ${s[-1+0]}' '' 's=abcd; printf "%s" "${s[-1+0]}"'   'd'
## zsh mode evaluates the same expression against its own 1-based origin.
check 'zsh mode ${s[i]} i=2'  '--zsh' 's=abcd; i=2; printf "%s" "${s[i]}"'     'b'

printf '== a malformed subscript is diagnosed, not read as 0 ==\n'
## It used to yield index 1 for `1+` with status 0. Both bounds of a range
## must report, and an empty slice cannot double as the failure token -- the
## discipline #647 and #710 set for the element surfaces.
check_err 'bad subscript diagnoses'      '' 's=abcd; printf "%s" "${s[1+]}"'   'E1304'
check_err 'bad subscript names the site' '' 's=abcd; printf "%s" "${s[1+]}"' \
    'evaluating a scalar slice subscript'
check_err 'bad SECOND range bound'       '' 's=abcd; printf "%s" "${s[0,1+]}"' 'E1304'
check_err 'bad subscript in ${#s[N]}'    '' 's=abcd; printf "%s" "${#s[1+]}"'  'E1304'
check 'bad subscript sets status 1'      '' \
    's=abcd; printf "%s" "${s[1+]}" 2>/dev/null; printf "%s" "$?"' '1'

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
