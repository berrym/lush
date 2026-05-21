# Command substitution + arithmetic + pipelines in idiomatic combinations.
# Classic patterns: counting lines, building a sum from a stream, capturing
# multi-line output.

# Counting via wc -l + capturing
items="one
two
three
four
five"
count=$(printf '%s\n' "$items" | wc -l | tr -d ' ')
echo "count: $count"

# Sum a column of numbers using arithmetic + pipeline
numbers="10 20 30 40 50"
total=0
for n in $numbers; do
    total=$((total + n))
done
echo "total: $total"

# Backtick form (legacy but still in many scripts)
backtick_result=`echo hello`
echo "backtick: $backtick_result"

# Nested command substitution
inner_then_outer=$(echo $(echo "deep"))
echo "nested: $inner_then_outer"

# Pipelines feeding command substitution
words=$(printf 'apple\nbanana\ncherry\n' | sort | head -2 | tr '\n' ' ')
echo "pipeline: $words"

# Arithmetic with conditional
n=15
parity=$(( n % 2 == 0 ? 0 : 1 ))
[ $parity -eq 0 ] && echo "$n is even" || echo "$n is odd"

# Multi-stage transformation
csv="alpha,beta,gamma,delta"
first=$(echo "$csv" | cut -d, -f1)
last=$(echo "$csv" | awk -F, '{print $NF}')
echo "first=$first last=$last"
