# Zsh history-style parameter modifiers: head/tail/root/ext, case,
# quote, substitute, and chained/nested composition.
p=/usr/local/bin/script.tar.gz
echo "h: ${p:h}"
echo "t: ${p:t}"
echo "r: ${p:r}"
echo "e: ${p:e}"
echo "chain: ${p:t:r}"
echo "nest: ${${p:t}:r}"
s="Hello World"
echo "l: ${s:l}"
echo "u: ${s:u}"
echo "subst: ${p:s/local/LOCAL/}"
echo "gsubst: ${p:gs/o/0/}"
