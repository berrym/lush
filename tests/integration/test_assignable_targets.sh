#!/bin/sh
## An array element address is an assignable target on EVERY surface that
## takes one.
##
## WHY THIS EXISTS
##
## `a[2]=v` and `(( a[2]=v ))` write the element. Three surfaces that also take
## an assignable target did not: `printf -v 'a[2]'` wrote nothing and reported
## success (#643), and `declare a[2]=v` and `read 'a[2]'` refused the address as
## "not a valid identifier" (#798).
##
## They now share one helper, so the subscript is handled exactly as the
## assignment surfaces handle it: an indexed subscript is an arithmetic
## expression, a negative counts from the end, the index base is the mode's,
## and an associative key is canonicalized. Asserting those through EACH
## surface is the point -- a per-surface copy of that logic is how #629, #640,
## #793 and #795 drifted apart.
##
## NOTE ON null_glob. `a[2]=NEW` contains a bracket glob, and null_glob is
## deliberately ON in lush and zsh modes, so an unquoted target vanishes before
## any builtin sees it. That is a curated feature, not part of this defect --
## it merely MASKED it, which is why the unquoted form is exercised in bash
## mode and the lush-mode cases quote the target.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
failures=0
checks=0

## $1 label, $2 mode flag (empty for lush mode), $3 script, $4 expected stdout
check() {
    checks=$((checks + 1))
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

printf '== the anchor: surfaces that already accepted an element address ==\n'
check 'a[2]=v'         '' 'a=(x y z); a[2]=NEW; printf "%s" "${a[*]}"' 'x y NEW'
check '(( a[2]=v ))'   '' 'a=(x y z); (( a[2] = 7 )); printf "%s" "${a[*]}"' 'x y 7'

printf '== printf -v writes the element (#643) ==\n'
## This left the array untouched and returned 0.
check 'printf -v a[2]'        '' \
    'a=(x y z); printf -v "a[2]" NEW; printf "%s" "${a[*]}"' 'x y NEW'
check 'printf -v keeps format' '' \
    'a=(x y z); printf -v "a[0]" "%05.2f" 3.14159; printf "%s" "${a[*]}"' '03.14 y z'
check 'printf -v scalar unchanged' '' \
    'printf -v s NEW; printf "%s" "$s"' 'NEW'

printf '== declare writes the element (#798) ==\n'
## Refused as "not a valid identifier"; in lush mode null_glob hid that by
## eating the word first, so it looked like a silent no-op.
check 'declare quoted target'   '' \
    'a=(x y z); declare "a[2]=NEW"; printf "%s" "${a[*]}"' 'x y NEW'
check 'declare unquoted, bash mode' '--bash' \
    'a=(x y z); declare a[2]=NEW; printf "%s" "${a[*]}"' 'x y NEW'
check 'declare scalar unchanged'  '' \
    'declare s=hi; printf "%s" "$s"' 'hi'
check 'declare attribute unchanged' '' \
    'declare -u u=abc; printf "%s" "$u"' 'ABC'

printf '== read writes the element (#798) ==\n'
## A here-string, not a pipeline: `read` in a pipeline runs in a subshell in
## some shells, which would hide the write and make a working surface look
## broken.
check 'read into a[2]'       '' \
    'a=(x y z); read "a[2]" <<< NEW; printf "%s" "${a[*]}"' 'x y NEW'
check 'read scalar unchanged' '' \
    'read s <<< hi; printf "%s" "$s"' 'hi'
check 'read two names'        '' \
    'read p q <<< "one two"; printf "%s|%s" "$p" "$q"' 'one|two'
check 'read -a unchanged'     '' \
    'read -a arr <<< "1 2 3"; printf "%s" "${arr[*]}"' '1 2 3'
## A mixed target list must place each field on its own kind of target.
check 'read scalar and element' '' \
    'a=(x y z); read s "a[1]" <<< "one two"; printf "%s|%s" "$s" "${a[*]}"' \
    'one|x two z'

printf '== the subscript behaves as it does on the assignment surfaces ==\n'
## Arithmetic, not a literal digit string.
check 'printf -v a[i]'  '' \
    'a=(x y z); i=1; printf -v "a[i]" NEW; printf "%s" "${a[*]}"' 'x NEW z'
check 'read a[i+1]'     '' \
    'a=(x y z); i=0; read "a[i+1]" <<< NEW; printf "%s" "${a[*]}"' 'x NEW z'
## A negative counts from the end (#629, #795).
check 'printf -v a[-1]' '' \
    'a=(x y z); printf -v "a[-1]" NEW; printf "%s" "${a[*]}"' 'x y NEW'
check 'read a[-2]'      '' \
    'a=(x y z); read "a[-2]" <<< NEW; printf "%s" "${a[*]}"' 'x NEW z'
## The index base is the mode's: zsh mode is 1-based here too (#793).
check 'zsh mode printf -v a[1]' '--zsh' \
    'a=(x y z); printf -v "a[1]" NEW; printf "%s" "${a[*]}"' 'NEW y z'
## An associative key is canonicalized, so a spaced key survives.
## Read back through a VARIABLE subscript: the quoted read form
## ${m["a b"]} and $( ) inside a subscript are both #654/#695, blocked on the
## quote-context work, and fail the same way after a plain m["a b"]=V too.
check 'printf -v into a map key' '' \
    'declare -A m; printf -v "m[a b]" V; k="a b"; printf "%s|%s" "${!m[*]}" "${m[$k]}"' \
    'a b|V'
check 'read into a map key'      '' \
    'declare -A m; read "m[k]" <<< V; printf "%s|%s" "${!m[*]}" "${m[k]}"' 'k|V'

printf '== targets that are still not identifiers stay refused ==\n'
## Widening the target must not make a genuinely bad name acceptable.
check 'declare 1bad refused' '' \
    'declare "1bad=x" 2>/dev/null; printf "%s" "$?"' '1'
check 'read 1bad refused'    '' \
    'read 1bad <<< x 2>/dev/null; printf "%s" "$?"' '1'

printf '== a subscript span must TERMINATE the target (#801) ==\n'
## `unset` found the subscript with strrchr -- the last `]` anywhere in the
## string -- and never checked it ended the target, so everything after it was
## ignored and a malformed address silently destroyed a real element with
## status 0. The span now comes from scan_subscript_bounds() and must end the
## target, so these fall through to the plain-name path and remove nothing.
check 'unset a[0]junk removes nothing' '' \
    'a=(p q r); unset "a[0]junk" 2>/dev/null; printf "%s" "${a[*]}"' 'p q r'
check 'unset a[0]=x removes nothing'   '' \
    'a=(p q r); unset "a[0]=x" 2>/dev/null; printf "%s" "${a[*]}"' 'p q r'
check 'unset "a[0] " removes nothing'  '' \
    'a=(p q r); unset "a[0] " 2>/dev/null; printf "%s" "${a[*]}"' 'p q r'
## Not removing it is only half the answer. A malformed address is a
## structural mistake, and falling through to the plain-name path would report
## success for a name that cannot exist -- a silent wrong answer in place of a
## destructive one. lush says what is wrong and fails.
check_err 'malformed target diagnosed'  '' \
    'a=(p q r); unset "a[0]junk"' 'invalid array element target'
check_err 'the diagnostic names the target' '' \
    'a=(p q r); unset "a[0]junk"' 'a[0]junk'
check 'malformed target fails'          '' \
    'a=(p q r); unset "a[0]junk" 2>/dev/null; printf "%s" "$?"' '1'
check 'zsh mode diagnoses too'    '--zsh' \
    'a=(p q r); unset "a[0]junk" 2>/dev/null; printf "%s" "$?"' '1'
## bash mode keeps bash's silent success: a script written against that
## behavior is who the mode is for, and its own oracle decides there.
check 'bash mode stays silent'   '--bash' \
    'a=(p q r); unset "a[0]junk" 2>/dev/null; printf "%s|%s" "$?" "${a[*]}"' '0|p q r'
## The legitimate forms must be untouched by that tightening -- including the
## ones the naive scan happened to get right.
check 'unset a[0] still works'    '' \
    'a=(p q r); unset "a[0]"; printf "%s" "${a[*]}"' 'q r'
check 'unset a[-1] from the end'  '' \
    'a=(p q r); unset "a[-1]"; printf "%s" "${a[*]}"' 'p q'
check 'unset a[1+1] arithmetic'   '' \
    'a=(p q r); unset "a[1+1]"; printf "%s" "${a[*]}"' 'p q'
## A nested subscript closes at the OUTER bracket: a depth-counted scan, not
## the first `]`.
check 'unset a[b[0]] nested'      '' \
    'a=(p q r); b=(2); unset "a[b[0]]"; printf "%s" "${a[*]}"' 'p q'
## An associative key keeps its spaces, and a plain name still unsets.
check 'unset m[a b] spaced key'   '' \
    'declare -A m; m["a b"]=1; m[j]=2; unset "m[a b]"; printf "%s" "${!m[*]}"' 'j'
check 'unset a plain name'        '' \
    's=7; unset s; printf "[%s]" "$s"' '[]'

printf '== every surface measures a subscript the same way (#799) ==\n'
## The length path found its closing bracket with strchr -- the FIRST `]` --
## so a nested subscript was cut short and the fragment reached the arithmetic
## evaluator. The read path resolved the same reference correctly, so lush
## disagreed with itself about what a reference MEANS depending on the surface.
check 'read resolves a nested subscript'   '' \
    'a=(xx yyy zzzz); b=(2); printf "%s" "${a[b[0]]}"' 'zzzz'
check 'length resolves it identically'     '' \
    'a=(xx yyy zzzz); b=(2); printf "%s" "${#a[b[0]]}"' '4'
check 'unset resolves it identically'      '' \
    'a=(xx yyy zzzz); b=(2); unset "a[b[0]]"; printf "%s" "${a[*]}"' 'xx yyy'
## The ordinary length forms must survive that change.
check 'length of an element'    '' 'a=(xx yyy); printf "%s" "${#a[1]}"' '3'
check 'length of the array'     '' 'a=(xx yyy); printf "%s" "${#a[@]}"' '2'
check 'length from the end'     '' 'a=(xx yyy); printf "%s" "${#a[-1]}"' '3'
check 'length of a map value'   '' \
    'declare -A m; m[k]=hello; printf "%s" "${#m[k]}"' '5'
check 'length of a spaced key'  '' \
    'declare -A m; m["a b"]=hello; printf "%s" "${#m[a b]}"' '5'

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
