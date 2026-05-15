# Lush mixing POSIX [, bash [[, and zsh-flavor expansions. No oracle.
x=5
if [ "$x" -eq 5 ]; then echo "posix-eq"; fi
if [[ "$x" -eq 5 ]]; then echo "bash-eq"; fi
if (( x == 5 )); then echo "arith-eq"; fi

items=(a b c d)
for i in "${items[@]}"; do
    [[ "$i" == [ab] ]] && echo "bash-pat: $i"
    case "$i" in a|b) echo "posix-case: $i" ;; esac
done

# zsh-style fall-through arithmetic
n=0
while (( n < 3 )); do
    echo "n=$n"
    (( n++ ))
done
