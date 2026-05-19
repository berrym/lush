# Zsh cmd_prefix: POSIX core. Zsh applies transient assignments to
# external commands and (default) to function calls only for the
# call's duration.

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

# Assignment + redirect, no command
X=5 2>/dev/null
echo "X=$X"

# Interleaved redirect + assignment within cmd_prefix
unset M N
M=m_val 2>/dev/null N=n_val env | grep -E '^(M|N)=' | sort

# Prefix on function call -- zsh scopes the assignment to the call
# by default.
greet() {
    echo "inside: greeting=$greeting"
}
greeting=hi greet
echo "after-func: greeting=${greeting:-unset}"

# Multiple assignment-only commands, all persist
unset P Q
P=p1 Q=q1
echo "P=$P Q=$Q"

# Two empty prefix assignments, adjacent
unset EA EB
EA= EB= env | grep -E '^(EA|EB)=' | sort

# Value containing '=' literally
unset VEQ
VEQ=a=b=c env | grep '^VEQ='

# Glob characters in assignment RHS are NOT expanded (zsh default;
# this matches POSIX/bash consensus)
unset VGLOB
VGLOB=*.sh env | grep '^VGLOB='

# Empty prefix + redirect, no command word
EMPTY_REDIR= 2>/dev/null
echo "empty-redir: [${EMPTY_REDIR-unset}]"

# Value containing '=' after a quoted segment (word concatenation
# across quote boundary).
unset QV
QV="hello"=world env | grep '^QV='

# (zsh interprets a bare leading `=` as `=cmd` path expansion, so
# `MEQ===` would error before any assignment. That case lives in the
# POSIX and bash seeds where it is a standard ASSIGNMENT_WORD.)

# Argument-context: X="a"=b is a single argument
echo X="a"=b
echo prefix="hello world"suffix

# Non-ASCII value (UTF-8)
unset UTF
UTF=café=naïve env | grep '^UTF='
