# while + until loops with arithmetic counter.
i=0
while [ "$i" -lt 3 ]; do
    echo "while $i"
    i=$((i + 1))
done

j=0
until [ "$j" -ge 3 ]; do
    echo "until $j"
    j=$((j + 1))
done
