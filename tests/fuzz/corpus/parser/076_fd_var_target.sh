# fd_target = TOK_VARIABLE in fd_dup_op
exec 3>/tmp/three
fd=3
echo "via-var" >&$fd
echo "via-braces" >&${fd}
exec 3>&-
