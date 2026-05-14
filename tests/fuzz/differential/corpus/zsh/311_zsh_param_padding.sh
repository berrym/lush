# zsh padding flags: (l:N::fill::) left-pad, (r:N::fill::) right-pad.
# Without fill: pads with spaces.
s="42"

# Left-pad to width 5 (spaces)
echo "[${(l:5:)s}]"

# Left-pad to width 5 with '0'
echo "[${(l:5::0:)s}]"

# Right-pad to width 7 (spaces)
echo "[${(r:7:)s}]"

# Right-pad to width 7 with '.'
echo "[${(r:7::.:)s}]"

# Truncate if value longer than width
long="abcdefgh"
echo "[${(l:3:)long}]"
