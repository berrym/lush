# Zsh parameter case flags: (U) upper, (L) lower, (C) capitalize.
s="hello world"
echo "upper: ${(U)s}"
echo "lower: ${(L)s}"
echo "cap: ${(C)s}"
