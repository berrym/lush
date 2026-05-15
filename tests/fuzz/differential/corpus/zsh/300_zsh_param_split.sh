# Zsh parameter flags: (s/X/) split, (j/X/) join, (f) split on newline.
csv="a,b,c,d"
echo ${(s/,/)csv}

items=(one two three)
echo ${(j/-/)items}
echo ${(j/, /)items}

# Split on newline
lines=$'one\ntwo\nthree'
echo ${(f)lines}

# Round-trip: split then join
echo ${(j/|/)${(s/,/)csv}}
