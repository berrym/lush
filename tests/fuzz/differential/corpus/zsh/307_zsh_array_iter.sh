# Zsh-style array iteration and member tests.
fruits=(apple banana cherry date)

# foreach
for f in $fruits; do
    echo "fruit: $f"
done

# C-style index loop using ${#arr}
for i in {1..${#fruits}}; do
    echo "[$i] = $fruits[$i]"
done

# Reverse via (Oa) flag
for f in ${(Oa)fruits}; do
    echo "rev: $f"
done
