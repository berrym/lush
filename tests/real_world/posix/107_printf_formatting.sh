# printf formatting -- string padding, number formatting, hex/oct,
# repeated format. Common in any script producing structured output.

# Field padding
printf '%-10s %s\n' "Name" "Value"
printf '%-10s %s\n' "----" "-----"
printf '%-10s %d\n' "alpha" 42
printf '%-10s %d\n' "beta" 100
printf '%-10s %d\n' "gamma" 7

# Number formats
printf 'decimal: %d\n' 255
printf 'hex:     %x\n' 255
printf 'octal:   %o\n' 255
printf 'padded:  %05d\n' 42

# Format reuses across arguments
printf '%s=%s\n' a 1 b 2 c 3

# Floating point
printf 'pi: %.4f\n' 3.14159265
printf 'pct: %.1f%%\n' 87.5

# Escape sequences (printf interprets, echo doesn't always)
printf 'tab\there\nline2\n'
