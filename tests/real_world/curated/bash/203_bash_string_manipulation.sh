# Bash string manipulation: case mods, replacement, substring. Used
# everywhere from prompt builders to log processors.

text="Hello, World!"

# Case modification
echo "upper:    ${text^^}"
echo "lower:    ${text,,}"
echo "first-up: ${text^}"
echo "first-lo: ${text,}"

# Substring extraction
echo "first-5:  ${text:0:5}"
echo "from-7:   ${text:7}"
echo "last-6:   ${text: -6}"

# Replacement
echo "replace-first: ${text/o/0}"
echo "replace-all:   ${text//o/0}"
echo "replace-front: ${text/#Hello/Hi}"
echo "replace-back:  ${text/%!/?}"

# Length
echo "length:   ${#text}"

# Building a path
basedir="/var/log"
project="my-app"
suffix="latest.log"
logfile="${basedir}/${project}/${suffix}"
echo "path: $logfile"

# Variable indirection
ref="text"
echo "indirect: ${!ref}"
