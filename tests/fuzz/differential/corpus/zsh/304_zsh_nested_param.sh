# Nested parameter expansion: apply flags or operations to the result of
# an inner expansion. Common idiom in zsh -- bash cannot do this directly.
words=(hello world foo bar)
echo ${(C)${(j/ /)words}}

# Upper-case the joined string.
echo ${(U)${(j/-/)words}}

# Sort the array then join.
echo ${(j/|/)${(o)words}}

# Split a string, then capitalize each piece.
csv="alpha,beta,gamma"
echo ${(j/-/)${(C)${(s/,/)csv}}}

# Length of nested.
echo ${#${(U)csv}}
