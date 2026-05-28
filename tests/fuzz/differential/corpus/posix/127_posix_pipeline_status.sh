# POSIX pipeline exit status: $? is the last command's status; a
# subshell isolates variable assignments.
echo hello | cat | cat
echo "rc: $?"
false | true
echo "true-last: $?"
true | false
echo "false-last: $?"
x=outer
(x=inner; echo "in-sub: $x")
echo "after-sub: $x"
