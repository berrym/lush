# Bash nameref (declare -n): a reference variable that resolves to
# another variable's value and writes through to it.
real=original
declare -n ref=real
echo "read-through: $ref"
ref=changed
echo "write-through: $real"
bump() {
  declare -n target="$1"
  target=$((target + 1))
}
n=41
bump n
echo "n: $n"
