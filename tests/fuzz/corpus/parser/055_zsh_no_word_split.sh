var="hello world"
for word in $var; do echo "[$word]"; done
list="a b c"
echo $list
arr=("one two" "three four")
for item in "${arr[@]}"; do echo "[$item]"; done
