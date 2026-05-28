# Bash case modification and substring/replacement expansion.
s="Hello World"
echo "upper: ${s^^}"
echo "lower: ${s,,}"
echo "upper-first: ${s^}"
echo "substr-from: ${s:6}"
echo "substr-range: ${s:0:5}"
echo "replace-first: ${s/o/0}"
echo "replace-all: ${s//o/0}"
