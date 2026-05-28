# POSIX function definitions: arguments, return status, $? propagation.
greet() {
  echo "hello $1"
}
add() {
  echo $(($1 + $2))
}
status() {
  return "$1"
}
greet world
echo "sum: $(add 3 4)"
status 3
echo "rc: $?"
status 0
echo "rc: $?"
