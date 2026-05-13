# Pattern substitution: /pat/repl, //pat/repl (all), /#pat/repl (prefix-anchored),
# /%pat/repl (suffix-anchored).
s="banana bandana"
echo "1: ${s/ana/ENA}"
echo "2: ${s//ana/ENA}"
echo "3: ${s/#b/B}"
echo "4: ${s/%a/A}"

# With character classes
echo "5: ${s/[bd]/X}"
echo "6: ${s//[bd]/X}"

# Empty replacement = deletion
path="a/b/c/d"
echo "7: ${path//\//.}"
echo "8: ${path//\//}"
