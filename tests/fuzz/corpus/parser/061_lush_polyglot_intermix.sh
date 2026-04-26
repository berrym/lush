var="hello"
echo ${var^^}
echo ${(U)var}
arr=(one two three)
echo "${arr[@]}"
echo $arr[1]
[[ $var == "hello" ]] && echo bash-style
[ "$var" = "hello" ] && echo posix-style
