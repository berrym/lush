# Bash here-string <<< feeding read and a pipeline.
read -r a b c <<< "one two three"
echo "a=$a b=$b c=$c"
cat <<< "literal here-string"
wc_in=$(tr a-z A-Z <<< "shout")
echo "upper: $wc_in"
