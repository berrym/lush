# Bash 4.4+ ${var@op} parameter transformations:
#   @Q  -- quote so input is safe to re-eval
#   @E  -- expand ANSI escapes (like printf %b)
#   @U  -- uppercase
#   @L  -- lowercase
#   @a  -- attribute string

# Q: quoting
s="hello 'world'"
echo "Q: ${s@Q}"

# E: escape interpretation
e='line1\nline2'
echo "E:"
echo "${e@E}"

# Case
mix="HelLo WoRlD"
echo "U: ${mix@U}"
echo "L: ${mix@L}"

# a: attributes (declare -i n -- expect i in attribute string)
declare -i n=5
echo "a-int: ${n@a}"
declare -r ro="x"
echo "a-readonly: ${ro@a}"
declare -A arr
echo "a-assoc: ${arr@a}"
