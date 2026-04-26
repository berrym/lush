VAR=value
export VAR
unset VAR
test -f /etc/hosts && echo present
[ -d /tmp ] && echo dir
if test "$1" = "yes"; then echo accepted; fi
