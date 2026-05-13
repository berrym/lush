# Associative arrays via declare -A.
declare -A colors
colors[red]=FF0000
colors[green]=00FF00
colors[blue]=0000FF

echo "red: ${colors[red]}"
echo "blue: ${colors[blue]}"
echo "count: ${#colors[@]}"

# Membership test
if [[ -v colors[red] ]]; then
    echo "red is set"
fi
if [[ ! -v colors[purple] ]]; then
    echo "purple is unset"
fi

# Iteration -- order is implementation-defined; sort for determinism.
for k in $(printf '%s\n' "${!colors[@]}" | sort); do
    echo "$k=${colors[$k]}"
done
