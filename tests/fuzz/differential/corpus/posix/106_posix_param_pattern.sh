# Pattern trimming: ${var#pat} (short prefix), ${var##pat} (long prefix),
# ${var%pat} (short suffix), ${var%%pat} (long suffix).
path="/usr/local/bin/cmd.sh"
echo "1: ${path#*/}"
echo "2: ${path##*/}"
echo "3: ${path%/*}"
echo "4: ${path%%/*}"

file="archive.tar.gz"
echo "5: ${file%.*}"
echo "6: ${file%%.*}"
echo "7: ${file#*.}"
echo "8: ${file##*.}"
