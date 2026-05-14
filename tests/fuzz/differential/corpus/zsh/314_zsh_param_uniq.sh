# zsh (u) unique flag: dedupe consecutive (in array order) elements.
dups=(a b a c b a c c)
echo "raw: $dups"
echo "u:   ${(u)dups}"
echo "uo:  ${(uo)dups}"
echo "uO:  ${(uO)dups}"

# Length of unique set
echo "len-unique: ${#${(u)dups}[@]}"

# With strings of mixed case
mixed=(apple Apple APPLE banana Banana)
echo "u-mixed: ${(u)mixed}"
echo "u-i-mixed: ${(ui)mixed}"
