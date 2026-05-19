# Bash cmd_prefix: POSIX core plus bash specifics. In non-POSIX mode
# (bash default) a prefix assignment on a function call is local to
# the call and does not persist; on external commands it is transient.

# Transient assignment seen by external command
FOO=transient env | grep '^FOO=transient$'
echo "after-transient: ${FOO:-unset}"

# Persistent assignment-only command
PERSIST=kept
echo "persist=$PERSIST"

# Empty value
EMPTY=
echo "empty-set: [${EMPTY-unset}]"

# Left-to-right evaluation
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

# Prefix on function call: bash default makes the assignment local
# to the call.
greet() {
    echo "inside: greeting=$greeting name=$name"
}
greeting=hello name=alice greet
echo "after-func: greeting=${greeting:-unset} name=${name:-unset}"

# Assignment + redirect, no command
X=5 2>/dev/null
echo "X=$X"

# Multiple prefixes interleaved with a redirect
unset M N
M=m_val 2>/dev/null N=n_val env | grep -E '^(M|N)=' | sort

# Prefix re-uses an earlier prefix on the same command
unset P
P=outer
P=inner env | grep '^P='
echo "after-prefix-shadow: P=$P"

# Two empty prefix assignments, adjacent
unset EA EB
EA= EB= env | grep -E '^(EA|EB)=' | sort

# Value containing '=' literally
unset VEQ
VEQ=a=b=c env | grep '^VEQ='

# Glob characters in assignment RHS are NOT expanded
unset VGLOB
VGLOB=*.sh env | grep '^VGLOB='

# Empty prefix + redirect, no command word
EMPTY_REDIR= 2>/dev/null
echo "empty-redir: [${EMPTY_REDIR-unset}]"

# Value containing '=' after a quoted segment (word concatenation
# across quote boundary). Exercises tokenizer end_position tracking.
unset QV
QV="hello"=world env | grep '^QV='

# Multiple consecutive '=' at start of value
unset MEQ
MEQ=== env | grep '^MEQ='

# Argument-context: X="a"=b must be a single argument "X=a=b"
echo X="a"=b
echo prefix="hello world"suffix

# Non-ASCII value (UTF-8)
unset UTF
UTF=café=naïve env | grep '^UTF='
