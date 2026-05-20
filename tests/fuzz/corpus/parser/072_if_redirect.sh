# if_statement with trailing_redirections
if true; then
    echo body-output
fi > /tmp/if.out

if [ -f /etc/hostname ]; then
    cat /etc/hostname
elif [ -f /etc/release ]; then
    cat /etc/release
else
    echo unknown
fi 2> /tmp/if.err
