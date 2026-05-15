# Subshell ( ) vs brace group { } scoping.
x=outer

# Brace group: same scope, modification persists
{ x=modified; echo "in-group: $x"; }
echo "after-group: $x"

# Subshell: separate scope, modification stays inside
x=outer
( x=child; echo "in-subshell: $x" )
echo "after-subshell: $x"

# Subshell exit code captured
( true; ); echo "sub-true: $?"
( false; ); echo "sub-false: $?"

# Brace group exit code = last command
{ true; }; echo "grp-true: $?"
{ true; false; }; echo "grp-trailing: $?"

# Subshell as part of pipeline
echo "data" | ( read -r line; echo "got-via-sub: $line" )
