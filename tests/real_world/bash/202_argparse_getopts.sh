# Bash CLI argparse via getopts -- the standard pattern for any script
# that takes -f, -v, --foo, etc. Tests positional args, optargs, and
# missing-argument handling.

verbose=0
config_file=""
target=""

usage() {
    cat <<EOF
Usage: $0 [-v] [-f config] [-t target] [args...]
  -v          verbose mode
  -f FILE     config file path
  -t TARGET   build target
EOF
}

# Use set -- to install simulated argv so the script is hermetic
set -- -v -f /etc/example.conf -t release left over args

while getopts ":vf:t:h" opt; do
    case "$opt" in
        v) verbose=1 ;;
        f) config_file="$OPTARG" ;;
        t) target="$OPTARG" ;;
        h) usage; exit 0 ;;
        :) echo "missing arg for -$OPTARG" >&2; exit 2 ;;
        \?) echo "unknown option -$OPTARG" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

echo "verbose=$verbose"
echo "config=$config_file"
echo "target=$target"
echo "remaining: $*"
echo "remaining count: $#"

# Iterate positionals
i=1
for arg in "$@"; do
    echo "  arg $i: $arg"
    i=$((i + 1))
done
