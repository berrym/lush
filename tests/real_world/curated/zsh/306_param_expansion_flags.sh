#!/bin/zsh
## zsh-specific parameter-expansion flags: ${(j:sep:)arr} for join,
## sort, uniq, case. Lush implements the zsh flag syntax natively
## in its zsh mode; this script pins the curated behaviors that
## already work cleanly.
##
## NOTE: ${(s:sep:)str} split and the ${(u)arr} array-uniq subscript
## semantics are intentionally NOT exercised here because lush
## diverges from zsh on both. Tracked separately so this script
## stays in the green column.

arr=(banana apple cherry banana apple date)

## --- 1. (j:sep:) join with separator -----------------------------------
echo "join-comma: ${(j:,:)arr}"
echo "join-pipe:  ${(j:|:)arr}"

## --- 2. (o) ascending sort ---------------------------------------------
sorted_asc=("${(o)arr[@]}")
echo "sort-asc:   ${sorted_asc[*]}"

## --- 3. (O) descending sort --------------------------------------------
sorted_desc=("${(O)arr[@]}")
echo "sort-desc:  ${sorted_desc[*]}"

## --- 4. (U) uppercase --------------------------------------------------
text="hello world"
echo "upper:      ${(U)text}"

## --- 5. (L) lowercase --------------------------------------------------
text2="HELLO World"
echo "lower:      ${(L)text2}"

## --- 6. (C) capitalize each word ---------------------------------------
text3="hello world from zsh"
echo "cap:        ${(C)text3}"
