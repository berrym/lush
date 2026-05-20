# Multiple heredocs attached to a single simple command
cmd <<A <<B
first heredoc
A
second heredoc
B

# Heredoc with quoted delimiter (body taken literally; no expansion)
cat <<'NOEXP'
$HOME stays literal
`uname` not run
NOEXP

# Heredoc with double-quoted delimiter (also literal body in POSIX)
cat <<"ALSO"
$HOME stays literal here too
ALSO
