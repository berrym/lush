# POSIX parameter expansion patterns: ${var:-default}, ${var:=...},
# ${var:?...}, ${var:+...}, ${var#prefix}, ${var##prefix}, ${var%suffix},
# ${var%%suffix}. These appear in basically every nontrivial POSIX shell
# script -- if any are broken, lots of real scripts break.

# Defaults
unset GREETING
greeting="${GREETING:-hello}"
echo "default: $greeting"

# Assign-default
unset CONFIG
: "${CONFIG:=/etc/example.conf}"
echo "assigned: $CONFIG"

# Conditional substitution
NAME="world"
salutation="${NAME:+Hello, $NAME!}"
echo "conditional: $salutation"

# Empty case
unset MAYBE
absent="${MAYBE:+seen}"
echo "absent: [$absent]"

# Prefix strip (shortest)
path="/usr/local/bin/lush"
tail="${path#*/}"
echo "shortest-prefix: $tail"

# Prefix strip (longest)
basename="${path##*/}"
echo "longest-prefix: $basename"

# Suffix strip (shortest)
file="archive.tar.gz"
without_ext="${file%.*}"
echo "shortest-suffix: $without_ext"

# Suffix strip (longest)
without_all_ext="${file%%.*}"
echo "longest-suffix: $without_all_ext"

# String length
text="lush shell"
echo "length: ${#text}"

# Combined: extract directory, then basename, then strip extension
fullpath="/var/log/messages.1.gz"
dir="${fullpath%/*}"
base="${fullpath##*/}"
stem="${base%%.*}"
echo "dir=$dir base=$base stem=$stem"
