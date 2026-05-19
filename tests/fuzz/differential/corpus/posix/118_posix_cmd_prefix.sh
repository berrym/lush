# POSIX cmd_prefix: per IEEE 1003.1 2.9.1 Simple Commands.
#   cmd_prefix : io_redirect
#              | cmd_prefix io_redirect
#              | ASSIGNMENT_WORD
#              | cmd_prefix ASSIGNMENT_WORD
# Assignments preceding a simple command apply to the command's
# environment (for regular utilities); assignment-only commands
# install the assignment in the current shell.

# Transient: assignment visible to external utility, not after
FOO=transient env | grep '^FOO=transient$'
echo "after-transient: ${FOO:-unset}"

# Empty value: '=' with nothing after sets the variable to empty,
# distinct from unset. ${EMPTY-x} expands to '' not 'x'.
EMPTY=
echo "empty-set: [${EMPTY-unset}]"

# Persistent: assignment-only command word
PERSIST=kept
echo "persist=$PERSIST"

# Multiple prefix assignments visible to external command
A=1 B=2 env | grep -E '^(A|B)=' | sort
echo "after-multi: ${A:-unset} ${B:-unset}"

# Left-to-right evaluation: B sees the new A
unset A B
A=10 B=$((A + 5)) env | grep -E '^(A|B)=' | sort

# Re-assignment ordering: rightmost wins
unset X
X=first X=second env | grep '^X='

# Quoted RHS with spaces
unset Q
Q="alpha beta" env | grep '^Q='

# Command-substitution RHS
unset S
S=$(printf hello) env | grep '^S='

# Arithmetic-expansion RHS
unset N
N=$((6 * 7)) env | grep '^N='

# Interleaved redirect within cmd_prefix
unset I
I=interleaved 2>/dev/null env | grep '^I='

# Assignment + redirect with no command word: assignment persists,
# redirect performed (and discarded). The #105 fix path.
X=after_redir 2>/dev/null
echo "X=$X"

# Multiple assignments, no command: all persist
unset P Q
P=p1 Q=q1
echo "P=$P Q=$Q"

# Prefix on regular builtin (read): IFS assignment is transient for
# regular utilities per POSIX.
unset IFS
IFS=, read -r a b c <<EOF
alpha,beta,gamma
EOF
echo "read: $a|$b|$c"
echo "ifs-after: ${IFS:-default}"

# Empty RHS in prefix position: child env contains EMPTY_PREFIX=
EMPTY_PREFIX= env | grep '^EMPTY_PREFIX='

# Two empty prefix assignments, adjacent
unset EA EB
EA= EB= env | grep -E '^(EA|EB)=' | sort

# Value containing '=' literally: only the first '=' delimits, the
# rest is value.
unset VEQ
VEQ=a=b=c env | grep '^VEQ='

# POSIX: glob characters in assignment RHS are NOT expanded (assignment
# value is treated as a single word with no pathname expansion).
unset VGLOB
VGLOB=*.sh env | grep '^VGLOB='

# Empty prefix + redirect, no command word (the #105 path with empty val)
EMPTY_REDIR= 2>/dev/null
echo "empty-redir: [${EMPTY_REDIR-unset}]"

# Value containing '=' after a quoted segment: POSIX 2.10.2 word
# concatenation -- "a"=b is one shell word, value of the assignment is
# the concatenation "a=b". Exercises tokenizer end_position tracking
# for quoted strings (otherwise the closing quote char is dropped from
# the adjacency calculation).
unset QV
QV="hello"=world env | grep '^QV='

# Multiple consecutive '=' tokens at start of value
unset MEQ
MEQ=== env | grep '^MEQ='

# Argument-context concatenation across quotes (must produce one
# argument "X=a=b" not three).
echo X="a"=b
echo prefix="hello world"suffix

# Non-ASCII value (UTF-8): adjacency on byte offsets must still work.
unset UTF
UTF=café=naïve env | grep '^UTF='
