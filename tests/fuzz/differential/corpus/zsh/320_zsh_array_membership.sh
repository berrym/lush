# Zsh subscript flags (i)/(I): first and last matching index.
items=(one two three two one)
echo "first-two: ${items[(i)two]}"
echo "last-two: ${items[(I)two]}"
