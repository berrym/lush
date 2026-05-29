# Bash C-style arithmetic for loop.
sum=0
for ((i = 1; i <= 5; i++)); do
  sum=$((sum + i))
done
echo "sum: $sum"
for ((j = 3; j > 0; j--)); do
  printf '%s ' "$j"
done
echo ""
