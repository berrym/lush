# Bash per-element pattern replacement over an array.
arr=(foo.txt bar.txt baz.log)
echo "first:" "${arr[@]/.txt/.md}"
echo "allo:" "${arr[@]//o/0}"
