# for-in loops over literal lists and parameter expansions.
for fruit in apple banana cherry; do
    echo "fruit: $fruit"
done

list="one two three"
for w in $list; do
    echo "word: $w"
done

set -- a b c
for arg in "$@"; do
    echo "arg: $arg"
done
