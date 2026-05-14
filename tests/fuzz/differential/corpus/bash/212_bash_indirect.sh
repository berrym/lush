# Indirect parameter expansion ${!var}: use the value of var as a
# variable name and expand THAT variable.
target="hello"
ref="target"
echo "indirect: ${!ref}"

# Chain: through another reference
inner_ref="ref"
echo "chain: ${!inner_ref}"

# Indirect on positional params
set -- alpha beta gamma
which=2
echo "pos: ${!which}"

# ${!prefix*} / ${!prefix@} -- match variable names by prefix
prefix_a=1
prefix_b=2
prefix_c=3
# Note: order is implementation-defined; sort for determinism
matches=$(for n in ${!prefix_*}; do echo "$n"; done | sort)
echo "names:"
echo "$matches"
