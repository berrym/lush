# Zsh case-modification flags: (U) upper, (L) lower, (C) capitalize.
# Pure zsh syntax only -- bash's ${var^^}/${var,,} is tested in the
# bash corpus and isn't valid input to zsh in non-bash-compat mode.
s="hello world"
echo ${(U)s}
echo ${(L)s}
echo ${(C)s}

# Combined with split + join: cap each word.
sentence="the quick brown fox"
echo ${(j/ /)${(C)${(s/ /)sentence}}}
