# POSIX ${#var} length and prefix/suffix pattern removal.
s=hello.world.txt
echo "len: ${#s}"
echo "rm-short-suffix: ${s%.*}"
echo "rm-long-suffix: ${s%%.*}"
echo "rm-short-prefix: ${s#*.}"
echo "rm-long-prefix: ${s##*.}"
