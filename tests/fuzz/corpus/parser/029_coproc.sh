coproc reader { read line; echo "got: $line"; }
echo "input" >&${reader[1]}
