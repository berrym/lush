# A failing command in a function or loop body does not abort the body by
# default (POSIX/bash/zsh consensus). Regression guard: the executor used to
# break a function body unconditionally on the first non-zero command, and
# lush mode defaulted errexit_in_loops on (#512).
f() { echo a; false; echo b; }
f
echo "---"
for x in 1 2 3; do printf '%s' "$x"; false; done
echo
echo "---"
g() { grep zzz /dev/null; echo g-continued; }
f2() { g; echo f2-continued; }
f2
