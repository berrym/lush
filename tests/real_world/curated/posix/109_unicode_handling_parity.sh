#!/bin/sh
## POSIX-mode Unicode handling parity (against dash/sh). The POSIX
## profile has the narrowest expansion surface, so this test focuses
## on the operations that are conformance-mandated by POSIX:
## echo / printf round-trip, parameter substitution, IFS splitting,
## and external pipelines that carry Unicode bytes unchanged.

## --- 1. Echo round-trip ---------------------------------------------------

echo "round-trip: café 中文 résumé"

## --- 2. printf with %s ----------------------------------------------------

printf 'p1: [%s]\n' "café"
printf 'p2: [%s][%s]\n' "中" "文"

## --- 3. Parameter substitution -- POSIX forms only -----------------------

p="café au lait"
echo "subst-é-X: ${p%%é*}X${p#*é}"

## --- 4. IFS-driven word splitting on Unicode-laden input -----------------

IFS=,
set -- $(printf 'alpha,café,中文')
echo "split-1: $1"
echo "split-2: $2"
echo "split-3: $3"
unset IFS

## --- 5. Concatenation -----------------------------------------------------

a="中文"
b="café"
echo "concat: $a $b"

## --- 6. Sort + uniq pipeline ---------------------------------------------

printf '%s\n' "café" "résumé" "café" | sort -u

## --- 7. Heredoc carrying Unicode -----------------------------------------

cat <<DOC
posix heredoc with café and 中文
DOC

## --- 8. case patterns ----------------------------------------------------

t="résumé"
case "$t" in
  *é*) echo "case-é: yes" ;;
  *)   echo "case-é: no"  ;;
esac
