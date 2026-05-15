# C-style for loop: for ((init; cond; update)).
sum=0
for ((i = 1; i <= 5; i++)); do
    sum=$((sum + i))
done
echo "sum=$sum"

# Reverse iteration
for ((j = 10; j > 0; j -= 2)); do
    echo "j=$j"
done

# Multiple init/update
for ((a = 0, b = 10; a < b; a++, b--)); do
    echo "a=$a b=$b"
done

# Empty parts = infinite, broken by break
i=0
for ((;;)); do
    i=$((i + 1))
    [ "$i" -ge 3 ] && break
done
echo "i=$i"
