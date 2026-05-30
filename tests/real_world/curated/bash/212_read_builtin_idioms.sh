#!/bin/bash
## `read` builtin: the workhorse for `while read line; do ... done`
## pipelines. Covers IFS-driven splitting, reading multiple vars,
## leftover join into the last var, while-read over a here-doc,
## and read returning non-zero on EOF.
##
## NOTE: `read -r` backslash preservation and `read -a array` are
## both intentionally NOT exercised here because lush currently
## diverges from the bash/zsh/dash consensus on both. Tracked
## separately so this script stays in the green column.

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
