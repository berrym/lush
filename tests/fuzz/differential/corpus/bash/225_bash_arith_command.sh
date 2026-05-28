# Bash (( )) arithmetic command and C-style operators.
(( x = 5 ))
echo "x: $x"
(( x += 10 ))
echo "x-plus: $x"
(( x++ ))
echo "x-post: $x"
(( ++x ))
echo "x-pre: $x"
if (( x > 15 )); then echo "gt15"; fi
(( y = x * 2 ))
echo "y: $y"
