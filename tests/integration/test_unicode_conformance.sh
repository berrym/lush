#!/bin/sh
## Unicode conformance gate for the text-processing surfaces.
##
## WHY THIS EXISTS
##
## lush's Unicode work has repeatedly improved one surface and left a
## neighbouring one stepping by byte or by codepoint. `?` was made
## grapheme-aware in #682 and bracket classes in #702, yet the substitution
## SEARCH was still byte-wise when #768 found it emitting invalid UTF-8. Each
## fix was real; each left the next surface unexamined, and the unit tests could
## not tell because they used PRECOMPOSED literals, where one codepoint IS one
## grapheme and every implementation looks correct.
##
## So this gate sweeps EVERY text surface with input that discriminates:
##
##   NFD      a base plus a combining mark   (2 codepoints, 1 cluster)
##   ZWJ      a family emoji                 (3 codepoints, 1 cluster)
##   CJK      a wide 3-byte character        (1 codepoint, 1 cluster, 2 columns)
##
## A surface that steps by byte or by codepoint fails here. Adding a new text
## operation means adding it to this file; that is the point.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
failures=0
checks=0

## Compare a hex-encoded expansion against an expected hex string.
## $1 label, $2 shell fragment producing the value, $3 expected hex
check_hex() {
    checks=$((checks + 1))
    got=$("$LUSH" -c "$2" 2>&1 | od -An -tx1 | tr -d ' \n')
    if [ "$got" = "$3" ]; then
        printf '  OK   %s\n' "$1"
    else
        printf '  FAIL %s\n     expected %s\n     got      %s\n' "$1" "$3" "$got"
        failures=$((failures + 1))
    fi
}

## The three discriminating subjects, built with printf so the source of this
## file stays ASCII (see scripts/ascii-only.rules).
NFD='d=$(printf "cafe\\xcc\\x81z"); '
ZWJ='z=$(printf "a\\xf0\\x9f\\x91\\xa8\\xe2\\x80\\x8d\\xf0\\x9f\\x92\\xbbb"); '
CJK='c=$(printf "\\xe4\\xb8\\xad\\xe6\\x96\\x87y"); '

printf '== single-character wildcard steps one CLUSTER ==\n'
## `?` must consume the whole cluster, never a byte and never one codepoint.
check_hex 'prefix strip ${d#caf?}'   "${NFD}printf '%s' \"\${d#caf?}\""   '7a'
check_hex 'prefix strip ${c#?}'      "${CJK}printf '%s' \"\${c#?}\""      'e6968779'
check_hex 'prefix strip ${z#?}'      "${ZWJ}printf '%s' \"\${z#?}\""      'f09f91a8e2808df09f92bb62'
check_hex 'suffix strip ${d%?}'      "${NFD}printf '%s' \"\${d%?}\""      '63616665cc81'

printf '== substitution search does not split a character (#768) ==\n'
check_hex 'subst ${c/?/Q}'           "${CJK}printf '%s' \"\${c/?/Q}\""    '51e6968779'
check_hex 'subst global ${c//?/Q}'   "${CJK}printf '%s' \"\${c//?/Q}\""   '515151'
check_hex 'subst ${z/?/Q}'           "${ZWJ}printf '%s' \"\${z/?/Q}\""    '51f09f91a8e2808df09f92bb62'
## A literal is exact TEXT, so it does not match a cluster's base alone.
check_hex 'subst literal ${d/e/X}'   "${NFD}printf '%s' \"\${d/e/X}\""    '63616665cc817a'
## A class tests the cluster's BASE and consumes the cluster whole (SEMANTICS
## 3.12), so it matches where the literal does not. Both refuse to SPLIT.
check_hex 'subst class ${d/[e]/X}'   "${NFD}printf '%s' \"\${d/[e]/X}\""  '636166587a'
## Replacing a whole cluster still works -- the rule rejects matches that end
## mid-character, not matches of multi-byte text.
check_hex 'subst whole cluster'      "${NFD}printf '%s' \"\${d/\$(printf 'e\\xcc\\x81')/X}\"" '636166587a'

printf '== extglob groups inherit cluster stepping (#703) ==\n'
check_hex 'group ${d%@(?)}'          "${NFD}printf '%s' \"\${d%@(?)}\""   '63616665cc81'
check_hex 'group ${z%@(?)}'          "${ZWJ}printf '%s' \"\${z%@(?)}\""   '61f09f91a8e2808df09f92bb'
## `%` takes the SHORTEST suffix, so `+(?)` here removes only the trailing `b`;
## `%%` takes the longest and removes every cluster. Both must step by cluster.
check_hex 'group ${z%+(?)} shortest' "${ZWJ}printf '%s' \"\${z%+(?)}\""  '61f09f91a8e2808df09f92bb'
check_hex 'group ${z%%+(?)} longest' "${ZWJ}printf '%s' \"\${z%%+(?)}\"" ''
## An alternative equal to a cluster's base must not consume part of it.
check_hex 'group ${d%@(e)} refuses'  "${NFD}printf '%s' \"\${d%@(e)}\""   '63616665cc817a'

printf '== slicing indexes by CLUSTER ==\n'
check_hex 'slice ${d:3:1} is the cluster' "${NFD}printf '%s' \"\${d:3:1}\"" '65cc81'
check_hex 'slice ${z:1:1} is the emoji'   "${ZWJ}printf '%s' \"\${z:1:1}\"" 'f09f91a8e2808df09f92bb'
check_hex 'slice ${c:0:1} is one CJK'     "${CJK}printf '%s' \"\${c:0:1}\"" 'e4b8ad'

printf '== case conversion preserves the mark ==\n'
check_hex 'upper ${d^^}'             "${NFD}printf '%s' \"\${d^^}\""      '43414645cc815a'

printf '== length counts CLUSTERS, and agrees with slicing (#770) ==\n'
## The unit of ${#var} and the unit of ${var:o:l} must be the same one, or the
## ordinary idioms built from both silently break. Before #770 the count was in
## codepoints while the index was in clusters, so the last-character idiom read
## past the end and produced nothing.
check_hex 'len ${#d} counts clusters' "${NFD}printf '%s' \"\${#d}\""      '35'
check_hex 'len ${#z} counts clusters' "${ZWJ}printf '%s' \"\${#z}\""      '33'
check_hex 'len ${#c} counts clusters' "${CJK}printf '%s' \"\${#c}\""      '33'
## The idioms that only work when the two units agree.
check_hex 'last char ${d:len-1}'   "${NFD}printf '%s' \"\${d:\$((\${#d}-1))}\"" '7a'
check_hex 'last char ${z:len-1}'   "${ZWJ}printf '%s' \"\${z:\$((\${#z}-1))}\"" '62'
check_hex 'identity ${d:0:len}'    "${NFD}printf '%s' \"\${d:0:\${#d}}\"" '63616665cc817a'
check_hex 'identity ${z:0:len}'    "${ZWJ}printf '%s' \"\${z:0:\${#z}}\"" '61f09f91a8e2808df09f92bb62'

printf '== a scalar subscript slices by CLUSTER, and its length agrees (#784) ==\n'
## On a scalar, ${s[N]} / ${s[N,M]} is a grapheme-cluster slice, not an
## element -- a scalar has no elements (SEMANTICS 3.1). The slice was already
## cluster-correct; the LENGTH operator had no scalar case and answered a flat
## 0 for every slice, so ${s[1,2]} expanded to two characters while ${#s[1,2]}
## said 0. Both now derive from one shared slice, so they cannot drift apart
## again. zsh slices the same references by CODEPOINT and returns a bare base
## character for the NFD subject, which is what these expectations exclude.
check_hex 'slice ${d[3]} whole cluster' "${NFD}printf '%s' \"\${d[3]}\""    '65cc81'
check_hex 'len   ${#d[3]} is 1'         "${NFD}printf '%s' \"\${#d[3]}\""   '31'
check_hex 'slice ${z[1]} whole ZWJ'     "${ZWJ}printf '%s' \"\${z[1]}\""    'f09f91a8e2808df09f92bb'
check_hex 'len   ${#z[1]} is 1'         "${ZWJ}printf '%s' \"\${#z[1]}\""   '31'
check_hex 'range ${c[0,1]} two wide'    "${CJK}printf '%s' \"\${c[0,1]}\""  'e4b8ade69687'
check_hex 'len   ${#c[0,1]} is 2'       "${CJK}printf '%s' \"\${#c[0,1]}\"" '32'
## Negative indices count clusters from the end (#68) and the length follows.
check_hex 'slice ${d[-1]} from end'     "${NFD}printf '%s' \"\${d[-1]}\""   '7a'
check_hex 'len   ${#d[-1]} is 1'        "${NFD}printf '%s' \"\${#d[-1]}\""  '31'

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
