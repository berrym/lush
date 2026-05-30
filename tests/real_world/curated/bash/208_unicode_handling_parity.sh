#!/bin/bash
## Comprehensive Unicode handling parity with bash. Exercises the
## subsystems most affected by the #157 width-policy unification:
## parameter expansion, length operators, string slicing, glob
## matching, printf formatting, and sort/uniq pipelines over Unicode
## input. The point is to confirm lush's output is byte-for-byte
## identical to bash on each of these surfaces -- if the width
## change desynced any internal codepoint accounting from the rest
## of the pipeline, one of these will diverge.

## --- 1. Echo round-trip (byte preservation) -------------------------------

echo "round-trip: café 中文 🌍 résumé"

## --- 2. Length operator on multi-byte content ----------------------------
## ${#var} in bash returns the CODEPOINT (not byte) count.

v_latin="café"
v_cjk="中文"
v_emoji="ab🌍c"
v_mixed="naïve"

echo "len-latin: ${#v_latin}"   ## 4
echo "len-cjk:   ${#v_cjk}"     ## 2
echo "len-emoji: ${#v_emoji}"   ## 4
echo "len-mixed: ${#v_mixed}"   ## 5

## --- 3. Substring slicing (codepoint-indexed) ----------------------------

echo "slice-latin-0-2:  ${v_latin:0:2}"   ## ca
echo "slice-latin-2-2:  ${v_latin:2:2}"   ## fé
echo "slice-cjk-0-1:    ${v_cjk:0:1}"     ## 中
echo "slice-emoji-2-1:  ${v_emoji:2:1}"   ## 🌍

## --- 4. Pattern matching with non-ASCII letters --------------------------

s="résumé"
if [[ $s == *é* ]]; then
    echo "match-é: yes"
else
    echo "match-é: no"
fi
if [[ $s == résumé ]]; then
    echo "match-eq: yes"
fi
if [[ $s != bogus ]]; then
    echo "neq-bogus: yes"
fi

## --- 5. Parameter expansion: substitution ${var/pat/repl} ----------------

p="café au lait"
echo "subst-é-X:  ${p/é/X}"      ## cafX au lait
echo "subst-all:  ${p//a/A}"     ## cAfé Au lAit

## --- 6. Case modification (already in 207, included briefly here) --------

up="${v_latin^^}"
lo="${v_latin,,}"
echo "case-up: $up"             ## CAFÉ
echo "case-lo: $lo"             ## café

## --- 7. printf with %s for unicode -- byte preservation ------------------

printf 'pf-1: [%s]\n' "café"
printf 'pf-2: [%s][%s]\n' "中" "文"

## --- 8. IFS-driven word splitting on a Unicode-laden string --------------

IFS=,
words="alpha,café,中文,emoji 🌍"
set -- $words
echo "split-1: $1"
echo "split-2: $2"
echo "split-3: $3"
echo "split-4: $4"
unset IFS

## --- 9. Sort + uniq over Unicode strings (external programs) -------------
## Verifies the pipeline carries bytes through unaltered.

printf '%s\n' "café" "résumé" "café" "naïve" | sort -u

## --- 10. Variable assignment with unicode RHS, then echo -----------------

x="中文 string"
y="naïve $x"
echo "concat: $y"

## --- 11. Heredoc carrying Unicode -----------------------------------------

cat <<DOC
heredoc carrying café and 中文 and 🌍 unchanged
DOC

## --- 12. Globbing against Unicode filenames ------------------------------
## bash glob ordering is locale-dependent; use a tmp dir + sort the
## output explicitly so the test is locale-stable.

dir=$(mktemp -d)
touch "$dir/café.txt" "$dir/résumé.txt" "$dir/中文.txt" "$dir/plain.txt"
( cd "$dir" && for f in *.txt; do echo "$f"; done ) | sort
rm -rf "$dir"
