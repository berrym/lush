ls /nonexistent 2>&1
echo "to stderr" 1>&2
exec 3>&1
echo "via fd 3" >&3
exec 3>&-
