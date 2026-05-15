# [[ ... ]] extended-test: string ops, glob matching, regex, && / ||.
s="hello"

if [[ "$s" == hello ]]; then echo "1: eq"; fi
if [[ "$s" != world ]]; then echo "2: neq"; fi
if [[ "$s" == h* ]]; then echo "3: glob-pre"; fi
if [[ "$s" == *llo ]]; then echo "4: glob-suf"; fi
if [[ "$s" == *ell* ]]; then echo "5: glob-mid"; fi

# Compound
x=5
if [[ "$s" == hello && "$x" -gt 0 ]]; then echo "6: both"; fi
if [[ "$s" == nope || "$x" -gt 0 ]]; then echo "7: either"; fi
if [[ ! "$s" == nope ]]; then echo "8: not"; fi

# String comparison (lexicographic)
if [[ "abc" < "abd" ]]; then echo "9: lex-lt"; fi

# Regex
if [[ "abc123" =~ ^[a-z]+[0-9]+$ ]]; then echo "10: regex-match"; fi
