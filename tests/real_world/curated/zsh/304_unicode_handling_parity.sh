#!/bin/zsh
## Unicode handling parity against zsh. zsh has its own parameter
## flag syntax for case modification and other transforms; this
## test verifies lush matches zsh's output across the surface area
## the #157 width-policy unification touched.

## --- 1. Echo round-trip ---------------------------------------------------

echo "round-trip: café 中文 🌍 résumé"

## --- 2. ${#var} length is codepoint count, not byte count ----------------

v_latin="café"
v_cjk="中文"
v_emoji="ab🌍c"
echo "len-latin: ${#v_latin}"   ## 4
echo "len-cjk:   ${#v_cjk}"     ## 2
echo "len-emoji: ${#v_emoji}"   ## 4

## --- 3. zsh-style parameter flags for case modification ------------------
## ${(U)var} = uppercase, ${(L)var} = lowercase, ${(C)var} = capitalize

v="café naïve"
echo "U: ${(U)v}"          ## CAFÉ NAÏVE
echo "L: ${(L)v}"          ## café naïve
echo "C: ${(C)v}"          ## Café Naïve

## --- 3. Substring slicing (zsh is 1-indexed, but [start,end] form works) -

s="café"
echo "s[1,2]: ${s[1,2]}"   ## ca
echo "s[3,4]: ${s[3,4]}"   ## fé

## --- 4. printf with %s ----------------------------------------------------

printf 'p1: [%s]\n' "café"
printf 'p2: [%s][%s]\n' "中" "文"

## --- 5. Pattern matching with non-ASCII ----------------------------------

s2="résumé"
if [[ $s2 == *é* ]]; then
    echo "match-é: yes"
fi

## --- 6. Heredoc -----------------------------------------------------------

cat <<DOC
zsh heredoc with café + 中文 + 🌍
DOC

## --- 7. Sort + uniq pipeline ---------------------------------------------

printf '%s\n' "café" "résumé" "café" "naïve" | sort -u

## --- 8. Concatenation ----------------------------------------------------

a="中文"
b="café"
echo "concat: ${a} ${b}"

## --- 9. Globbing on Unicode filenames ------------------------------------
## Locale-stable: explicit sort.

dir=$(mktemp -d)
touch "$dir/café.txt" "$dir/résumé.txt" "$dir/中文.txt"
( cd "$dir" && for f in *.txt; do echo "$f"; done ) | sort
rm -rf "$dir"
