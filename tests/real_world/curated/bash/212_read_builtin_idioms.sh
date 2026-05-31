#!/bin/bash
## `read` builtin: the workhorse for `while read line; do ... done`
## pipelines. Covers IFS-driven splitting, reading multiple vars,
## leftover join into the last var, while-read over a here-doc,
## EOF return, and `read -a` array assignment.
##
## NOTE: `read -r` backslash preservation from a single-quoted
## here-string is intentionally NOT exercised here. lush + zsh BOTH
## process `\n`/`\\` from single-quoted `<<<` content; bash is the
## outlier. Per `project-defaults-bash-zsh-consensus` lush curates
## the zsh-side behavior, so the bash-only assertion is left off
## the parity test.

## --- 1. Basic read from a here-string ----------------------------------
read a b c <<< "one two three"
echo "1: a=$a b=$b c=$c"

## --- 2. Excess input glues into the last variable ----------------------
read x y <<< "alpha beta gamma delta"
echo "2: x=$x y=$y"

## --- 3. Custom IFS: colon-separated ------------------------------------
IFS=: read p q r <<< "alpha:beta:gamma"
echo "3: p=$p q=$q r=$r"

## --- 4. while-read loop over here-doc ----------------------------------
total=0
while read -r n; do
    total=$((total + n))
done <<EOF
1
2
3
4
5
EOF
echo "4-sum: $total"

## --- 5. read returns non-zero on EOF -----------------------------------
echo "" | { read line; rc=$?; echo "5-empty-line: rc=$rc line=[$line]"; }
: | { read line; rc=$?; echo "5-truly-empty: rc=$rc"; }

## --- 6. read -a NAME populates an indexed array ------------------------

read -ra parts <<< "alpha beta gamma"
echo "6-len: ${#parts[@]}"
echo "6-[0]: ${parts[0]}"
echo "6-[2]: ${parts[2]}"
