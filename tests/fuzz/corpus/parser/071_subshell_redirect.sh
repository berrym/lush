# subshell with trailing_redirections
( echo a; echo b; ) > /tmp/sub.out
( cat /etc/hostname 2>/dev/null ) > /tmp/sub2.out 2>&1
( false ) 2> /tmp/sub.err
