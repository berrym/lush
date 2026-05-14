# POSIX test (and [) operators: string comparison, length tests,
# numeric comparison. File tests excluded (filesystem-dependent).
empty=""
nonempty="hello"

# String tests
[ -z "$empty" ] && echo "z-empty"
[ -n "$nonempty" ] && echo "n-nonempty"
[ -z "$nonempty" ] || echo "not-z-nonempty"

# Equality
[ "$nonempty" = "hello" ] && echo "eq-hello"
[ "$nonempty" != "world" ] && echo "neq-world"

# Numeric
a=5
b=10
[ "$a" -eq 5 ] && echo "a-eq-5"
[ "$a" -lt "$b" ] && echo "a-lt-b"
[ "$b" -gt "$a" ] && echo "b-gt-a"
[ "$a" -ne "$b" ] && echo "a-ne-b"
[ "$a" -le 5 ] && echo "a-le-5"
[ "$b" -ge "$b" ] && echo "b-ge-b"

# Combined via && / ||
[ "$a" -lt "$b" ] && [ "$nonempty" = "hello" ] && echo "both"
