# Mode-switch mid-script. lush's canonical 'mode <name>' selector.
# No oracle for lush corpus.

mode --show 2>/dev/null | head -1

# Start lush, run a zsh-only construct
arr=(a b c)
echo "lush ${(U)${(j/-/)arr}}"

# Switch to bash mode, use bash-only construct
mode bash
arr2=(x y z)
echo "bash ${arr2[@]^^}"

# Back to lush, polyglot superset
mode lush
echo "again-lush ${(C)arr} ${arr2[@]^^}"
