# Arithmetic forms across modes: POSIX $((...)), bash (( cmd )),
# zsh-style array indexing. No oracle for lush corpus.

x=10
y=3

# POSIX $((...))
echo "div: $((x / y))"
echo "mod: $((x % y))"

# bash (( ... )) as command (no output)
((sum = x + y))
echo "sum: $sum"

# bash arithmetic in for-loop init
total=0
for ((i = 0; i < 5; i++)); do
    ((total += i))
done
echo "total: $total"

# Mixed with arrays
arr=(10 20 30 40 50)
sum_arr=0
for n in "${arr[@]}"; do
    ((sum_arr += n))
done
echo "sum-arr: $sum_arr"

# Arithmetic on array element
((arr[2] = arr[2] * 10))
echo "mutated: ${arr[2]}"
