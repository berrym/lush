# POSIX positional parameters: count, access, shift by 1 and by N.
set -- alpha beta gamma delta
echo "count: $#"
echo "first: $1"
shift
echo "after1: $1 count=$#"
shift 2
echo "after2: $1 count=$#"
echo "all: $*"
