# if/elif/else/fi with test operators.
x=3
if [ "$x" -eq 1 ]; then
    echo one
elif [ "$x" -eq 2 ]; then
    echo two
elif [ "$x" -eq 3 ]; then
    echo three
else
    echo other
fi

y=hello
if [ -z "$y" ]; then
    echo empty
elif [ "$y" = "world" ]; then
    echo world
else
    echo "$y"
fi
