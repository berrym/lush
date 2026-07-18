# Command substitution must capture output larger than the OS pipe buffer
# (~64KB) without deadlocking. Regression guard for the drain-before-reap fix.
x=$(seq 20000)
echo "len=${#x}"
a=($(seq 1 5000))
echo "count=${#a[@]}"
y=$(exit 7)
echo "status=$?"
