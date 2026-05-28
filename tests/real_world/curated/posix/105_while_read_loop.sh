# while-read loop: the canonical pattern for processing line-oriented
# data. Appears in basically every log-processing script.

input="alpha 100
beta 200
gamma 300
delta 400"

total=0
count=0
while IFS=' ' read -r name value; do
    total=$((total + value))
    count=$((count + 1))
    echo "  $name=$value"
done <<EOF
$input
EOF

echo "count: $count"
echo "total: $total"

# IFS-driven CSV parsing
csv="alice,30,engineer
bob,45,manager
carol,28,designer"

while IFS=',' read -r name age role; do
    echo "$name is a $age-year-old $role"
done <<EOF
$csv
EOF
