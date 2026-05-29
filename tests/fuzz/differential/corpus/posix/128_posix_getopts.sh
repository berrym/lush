# POSIX getopts: option flags, an option with an argument, and OPTIND.
set -- -a -b val -a rest1 rest2
while getopts "ab:" opt; do
  case "$opt" in
    a) echo "flag-a" ;;
    b) echo "opt-b=$OPTARG" ;;
  esac
done
shift $((OPTIND - 1))
echo "remaining: $*"
