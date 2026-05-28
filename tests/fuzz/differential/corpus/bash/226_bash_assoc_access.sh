# Bash associative array: declare, direct key access, count, membership.
# Iteration order is left out deliberately (it is implementation-defined
# and a documented lush/bash divergence); only order-independent queries.
declare -A color
color[red]=FF0000
color[green]=00FF00
color[blue]=0000FF
echo "red: ${color[red]}"
echo "blue: ${color[blue]}"
echo "count: ${#color[@]}"
echo "has-green: ${color[green]:-missing}"
echo "has-purple: ${color[purple]:-missing}"
color[red]=AA0000
echo "updated-red: ${color[red]}"
unset 'color[blue]'
echo "after-unset: ${#color[@]}"
