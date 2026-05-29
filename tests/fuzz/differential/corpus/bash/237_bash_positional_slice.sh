# Bash positional-parameter slicing ${@:offset:length}, including a
# negative offset, in whole-word (for-list) positions.
set -- one two three four five
for w in "${@:2}"; do echo "from2: $w"; done
for w in "${@:2:2}"; do echo "slice: $w"; done
for w in "${@: -1}"; do echo "last: $w"; done
echo "count: $#"
