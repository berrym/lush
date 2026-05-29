# Bash printf -v assigns formatted output to a variable (no stdout).
printf -v out "%05d-%s" 42 hello
echo "out: $out"
printf -v hex "%x" 255
echo "hex: $hex"
printf "direct %s\n" still-prints
