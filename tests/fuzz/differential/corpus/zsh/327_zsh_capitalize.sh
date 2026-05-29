# Zsh (C) capitalize-words parameter flag.
s="hello world foo"
echo "cap: ${(C)s}"
t="MIXED case HERE"
echo "cap2: ${(C)t}"
