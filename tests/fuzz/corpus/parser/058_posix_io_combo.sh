cmd1 | cmd2 | cmd3 > out 2>&1
{ cmd_a; cmd_b; } > combined.log
cat <<-INDENTED
	leading tabs are stripped
	by the dash variant
	INDENTED
exec < input.txt
