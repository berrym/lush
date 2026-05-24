# Bash brace expansion: list and range forms, nesting, prefix/suffix.

# List form
echo {a,b,c,d}
echo prefix-{1,2,3}.txt
echo {one,two,three}-{1,2,3}

# Range form
echo {1..5}
echo {a..e}
echo {01..10}
echo {0..20..5}

# Negative ranges
echo {5..1}
echo {-3..3}

# Nested
echo {{1..3},{a..c}}
echo file{1..3}.{log,txt}

# Inside command
for i in {1..5}; do
    echo "iter $i"
done

# Path constructs
mkdir_paths=(/tmp/test/{src,build,docs}/sub)
for p in "${mkdir_paths[@]}"; do
    echo "  $p"
done
