# getopts: POSIX option parsing.
set -- -a -b foo bar baz
opts=""
opt_b=""
extra=""

while getopts ":ab:" opt; do
    case "$opt" in
        a) opts="${opts}a";;
        b) opt_b="$OPTARG";;
        \?) echo "bad: -$OPTARG";;
        :) echo "missing arg for -$OPTARG";;
    esac
done
shift $((OPTIND - 1))

echo "opts=$opts"
echo "opt_b=$opt_b"
echo "remaining: $*"
