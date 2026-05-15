# Loop variants across modes in lush.
# No oracle for lush corpus.

# POSIX while
i=0
while [ $i -lt 3 ]; do
    echo "w$i"
    i=$((i + 1))
done

# bash C-style for
for ((j = 0; j < 3; j++)); do
    echo "f$j"
done

# POSIX for-in
for n in alpha beta gamma; do
    echo "n=$n"
done

# bash until
k=3
until [ $k -le 0 ]; do
    echo "u$k"
    k=$((k - 1))
done

# Nested
for outer in a b; do
    for inner in 1 2; do
        echo "${outer}${inner}"
    done
done
