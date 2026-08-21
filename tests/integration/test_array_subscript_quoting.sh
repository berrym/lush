#!/bin/sh
## Quoting inside an array subscript, on the surfaces that accept one.
##
## WHY THIS EXISTS
##
## A subscript that collides with shell syntax -- `@`, `*`, a space -- needs a
## spelling, and quoting is that spelling. It did not work on the indexed
## assignment target: the quotes reached the arithmetic evaluator still
## attached and died on `unexpected character '"'`, so `a["0"]=x` had no way
## to be written at all (#639). The associative branch beside it had
## canonicalized its interior since #631; the indexed branch never did.
##
## The rule being pinned here: one level of quoting comes off the subscript,
## and only the QUOTING -- expansion stays with the arithmetic evaluator,
## which does its own. Dequoting twice or expanding twice are both visible in
## these checks (`a["0+1"]` must still evaluate; `a["$k"]` must expand once).
##
## SEMANTICS 3.13 adds a further, lush-mode-only meaning on top of this: a
## QUOTED subscript is a literal key and an unquoted one an arithmetic
## expression. That is approved but not yet implemented (it is blocked on the
## quote-context work, #695), so this file asserts today's behavior and will
## grow when 3.13 lands. Nothing here should be read as fixing 3.13's
## distinction in place.
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

printf '== indexed assignment target accepts a quoted subscript (#639) ==\n'
## The reported case: this raised E1304 "unexpected character" and wrote
## nothing.
check 'a["1"]=X'      '' 'a=(p q r); a["1"]=X; printf "%s" "${a[*]}"' 'p X r'
check "a['1']=X"      '' "a=(p q r); a['1']=X; printf '%s' \"\${a[*]}\"" 'p X r'
## The subscript is still an arithmetic expression after the quotes come off
## -- dequoting must not disable evaluation.
check 'a["0+1"]=X evaluates' '' \
    'a=(p q r); a["0+1"]=X; printf "%s" "${a[*]}"' 'p X r'
## Expansion belongs to the arithmetic evaluator, and must happen exactly
## once. A second expansion pass would resolve the VALUE of a again.
check 'a["$k"]=X expands once' '' \
    'a=(p q r); k=1; a["$k"]=X; printf "%s" "${a[*]}"' 'p X r'
check 'a[$k]=X unquoted, unchanged' '' \
    'a=(p q r); k=1; a[$k]=X; printf "%s" "${a[*]}"' 'p X r'
check 'a[1]=X bare, unchanged' '' \
    'a=(p q r); a[1]=X; printf "%s" "${a[*]}"' 'p X r'
## Append through a quoted subscript lands on the same element.
check 'a["1"]+=Y appends' '' \
    'a=(p q r); a["1"]+=Y; printf "%s" "${a[*]}"' 'p qY r'

printf '== the origin stays mode-gated, as it was ==\n'
## zsh mode is 1-based, so index 1 is the FIRST element and 0 addresses
## nothing. Removing the quotes must not disturb that.
check 'zsh mode a["1"]=X is first' '--zsh' \
    'a=(p q r); a["1"]=X; printf "%s" "${a[*]}"' 'X q r'
## An assignment cannot carry its own redirection here (#655), so stderr is
## suppressed by the harness rather than inline -- writing `a[0]=X 2>/dev/null`
## turns this into a parse-error test by accident.
check 'zsh mode a["0"]=X refused'  '--zsh' \
    'a=(p q r); a["0"]=X; printf "%s" "${a[*]}"' 'p q r'
check 'bash mode a["0"]=X is first' '--bash' \
    'a=(p q r); a["0"]=X; printf "%s" "${a[*]}"' 'X q r'

printf '== associative keys round-trip through quoting (unchanged) ==\n'
## These already worked; they are controls proving the indexed change did not
## disturb the map path, and that a key colliding with syntax survives.
check 'm["@"] stores the literal key' '' \
    'declare -A m; m["@"]=x; printf "%s" "${!m[*]}"' '@'
check 'm["a b"] keeps the space'      '' \
    'declare -A m; m["a b"]=x; printf "%s" "${!m[*]}"' 'a b'
check "m['@'] single-quoted key"      '' \
    "declare -A m; m['@']=x; printf '%s' \"\${!m[*]}\"" '@'
check 'm["*"] stores the literal key' '' \
    'declare -A m; m["*"]=x; printf "%s" "${!m[*]}"' '*'
## A key written with quotes reads back through a variable subscript, which is
## the spelling that works on the read path today -- the quoted READ form
## (${m["a b"]}) is #654, blocked on the quote-context work.
check 'quoted key reads back'         '' \
    'declare -A m; m["a b"]=v; k="a b"; printf "%s" "${m[$k]}"' 'v'

printf '== a malformed subscript is still diagnosed, not silently indexed ==\n'
## Removing the quotes must not swallow a genuine arithmetic error. The
## quoted form must fail exactly as the bare form does: status 1, the binding
## untouched, execution continuing.
check 'a["1+"]=X leaves the array alone' '' \
    'a=(p q r); a["1+"]=X; printf "%s" "${a[*]}"' 'p q r'
check 'a["1+"]=X reports status 1' '' \
    'a=(p q r); a["1+"]=X; printf "%s" "$?"' '1'
check 'bare a[1+]=X fails identically' '' \
    'a=(p q r); a[1+]=X; printf "%s|%s" "$?" "${a[*]}"' '1|p q r'

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
