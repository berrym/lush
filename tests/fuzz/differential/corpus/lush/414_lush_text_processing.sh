# Common text-processing patterns mixing all three syntactic flavors.
# No oracle for lush corpus.

path="/usr/local/bin/script.sh"

# POSIX basename/dirname via parameter expansion
echo "base: ${path##*/}"
echo "dir: ${path%/*}"

# bash substring
echo "sub: ${path:0:11}"

# bash substitution
echo "ext-replaced: ${path/%.sh/.py}"

# zsh case-mod
echo "upper: ${(U)path}"

# Combined: bash subst + zsh case
file="${path##*/}"
echo "file-upper: ${(U)file}"

# Path manipulation chain
cleaned="${path#/}"
parts=(${(s:/:)cleaned})
echo "parts: ${(j: > :)parts}"
echo "count: ${#parts}"
