# fd_alloc_redirection: {name}> / {name}< / {name}>>
exec {logfd}> /tmp/log
echo "hello" >&${logfd}
exec {logfd}>&-
exec {infd}< /tmp/log
read -u ${infd} line
exec {infd}<&-
exec {bothfd}>> /tmp/append.log
