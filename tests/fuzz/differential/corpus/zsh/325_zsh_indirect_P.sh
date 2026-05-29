# Zsh ${(P)name} indirect expansion: value of the named variable, and
# composing with another flag.
target=hello
ref=target
echo "indirect: ${(P)ref}"
echo "indirect-upper: ${(PU)ref}"
