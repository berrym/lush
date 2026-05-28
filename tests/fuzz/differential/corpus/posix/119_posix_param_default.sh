# POSIX parameter expansion: use-default, assign-default, alternative,
# and the colon-null variants.
unset foo
echo "default: ${foo:-fallback}"
echo "minus-unset: ${foo-still}"
bar=set
echo "alt: ${bar:+present}"
echo "assigned: ${baz:=assigned}"
echo "baz-now: $baz"
