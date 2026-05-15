declare -a arr=(one two three)
typeset -a vec=(10 20 30)
arr[5]=fiveth
arr+=(four five)
echo "${arr[@]}"
echo "${arr[2]}"
