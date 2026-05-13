# Lush should accept both bash and zsh parameter-expansion forms in
# the same script. No oracle -- runs lush only, checked for crash/UB.
s="hello world"

# bash forms
echo "bash up: ${s^^}"
echo "bash lo: ${s,,}"
echo "bash subst: ${s/world/lush}"
echo "bash sub: ${s:6:5}"

# zsh forms
echo "zsh up:  ${(U)s}"
echo "zsh lo:  ${(L)s}"
echo "zsh cap: ${(C)s}"

# Mixed in one expression -- legal because lush is the polyglot superset.
echo "${(U)s/world/lush}"
