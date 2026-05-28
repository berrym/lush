# POSIX test command [ ]: numeric, string, and emptiness operators.
n=7
[ "$n" -eq 7 ] && echo "eq7"
[ "$n" -gt 3 ] && echo "gt3"
[ "$n" -lt 10 ] && echo "lt10"
s=hello
[ "$s" = hello ] && echo "streq"
[ "$s" != world ] && echo "strne"
[ -z "" ] && echo "empty-z"
[ -n "$s" ] && echo "nonempty-n"
[ a = a ] && [ b = b ] && echo "and-chain"
