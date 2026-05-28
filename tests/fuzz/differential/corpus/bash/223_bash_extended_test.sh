# Bash [[ ]] extended test: glob match, regex, inequality, arithmetic,
# and the string emptiness operators.
s="hello"
[[ $s == h* ]] && echo "glob-match"
[[ $s =~ ^h.*o$ ]] && echo "regex-match"
[[ $s != world ]] && echo "ne-match"
n=5
[[ $n -gt 3 ]] && echo "arith-gt"
[[ -z "" ]] && echo "empty-z"
[[ -n "x" ]] && echo "nonempty-n"
