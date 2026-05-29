# Zsh associative-array sorted keys/values via (ko)/(vo) ordering.
typeset -A m
m[banana]=3
m[apple]=1
m[cherry]=2
echo "keys: ${(ko)m}"
echo "count: ${#m}"
echo "apple: $m[apple]"
