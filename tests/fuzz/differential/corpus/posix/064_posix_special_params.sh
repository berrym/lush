# POSIX special parameters that are runtime-deterministic.
# $$ (PID) and $0 (shell name) are excluded because they differ
# between processes by construction; diff_oracle compares stdout
# byte-for-byte and would always flag them. shift-on-empty error
# semantics are covered separately (POSIX requires abort; bash/zsh
# warn-and-continue) so this test sticks to the parameter
# expansions and exits without invoking shift.
echo "args: $#"
echo "all: $*"
echo "all-quoted: $@"
echo "exit: $?"
echo "first: $1"
