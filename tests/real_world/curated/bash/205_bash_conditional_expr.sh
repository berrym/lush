# Bash [[ ]] conditional expression -- string/regex/file/numeric tests
# in one syntax. Replaces the POSIX `[ ]` for bash scripts.

s="hello world"

# String tests
[[ "$s" == "hello world" ]] && echo "eq-string ok"
[[ "$s" != "goodbye" ]] && echo "ne-string ok"

# Pattern match (no quotes around RHS for pattern)
[[ "$s" == hello* ]] && echo "prefix-match ok"
[[ "$s" == *world ]] && echo "suffix-match ok"
[[ "$s" == *o*o* ]] && echo "multi-match ok"

# Regex
if [[ "$s" =~ ^([a-z]+)\ ([a-z]+)$ ]]; then
    echo "regex matched"
    echo "  group 1: ${BASH_REMATCH[1]}"
    echo "  group 2: ${BASH_REMATCH[2]}"
fi

# Numeric
n=42
[[ $n -eq 42 ]] && echo "num-eq ok"
[[ $n -gt 10 ]] && echo "num-gt ok"
(( n == 42 )) && echo "(( )) num-eq ok"
(( n > 10 && n < 100 )) && echo "(( )) range ok"

# Logical combos
a=1; b=2; c=3
[[ $a -lt $b && $b -lt $c ]] && echo "chained AND ok"
[[ $a -gt $b || $b -lt $c ]] && echo "chained OR ok"

# File tests
[[ -d /tmp ]] && echo "/tmp dir ok"
[[ -e /etc/hosts ]] && echo "/etc/hosts exists"
[[ -r /etc/hosts ]] && echo "/etc/hosts readable"

# Empty/non-empty
[[ -z "" ]] && echo "empty ok"
[[ -n "$s" ]] && echo "non-empty ok"
