# Brace expansion: numeric range, letter range, step, comma list.
echo {1..5}
echo {1..10..2}
echo {5..1}
echo {a..e}
echo {a..f..2}

# Comma list
echo {red,green,blue}

# Nested
echo {a,b}{1,2}

# With prefix/suffix
echo file{1..3}.txt
echo {pre,post}fix
