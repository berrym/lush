# POSIX while-read loop consuming a here-document line by line. Leading
# and trailing IFS whitespace is stripped from each line by read.
while read -r line; do
  echo "got: $line"
done <<'EOF'
alpha
beta gamma
  leading-space
EOF
echo "done"
