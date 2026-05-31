#!/bin/zsh
## zsh-specific parameter-expansion flags: ${(j:sep:)arr} for join,
## sort, uniq, case, split. Lush implements the zsh flag syntax natively
## in its zsh mode.

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

## --- 7. (s:sep:) split scalar on separator -----------------------------
joined="alpha:beta:gamma:delta"
split_arr=("${(s/:/)joined}")
echo "split-len:  ${#split_arr[@]}"
echo "split-[1]:  ${split_arr[1]}"
echo "split-[4]:  ${split_arr[4]}"

## --- 8. (u) unique with [@] subscript ----------------------------------
uniq_arr=("${(u)arr[@]}")
echo "uniq-len:   ${#uniq_arr[@]}"
echo "uniq-vals:  ${uniq_arr[*]}"
