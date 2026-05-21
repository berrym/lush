# Zsh glob qualifiers (*(.)/(.D)/etc.) -- one of zsh's killer features
# for shell-pipeline file selection. Used heavily in oh-my-zsh.

# Set up a tmp area
tmpdir=$(mktemp -d -t zsh_glob.XXXXXX)
trap "rm -rf $tmpdir" EXIT

cd $tmpdir
touch file1.txt file2.txt file3.log
mkdir subdir
touch subdir/inner.txt
touch .hidden

# Glob qualifier: only regular files
files=(*(.N))
echo "regular files: ${#files[@]}"
for f in $files; do echo "  $f"; done | sort

# Only directories
dirs=(*(/N))
echo "directories: ${#dirs[@]}"
for d in $dirs; do echo "  $d"; done

# Include dotfiles
all=(*(DN))
echo "with-dotfiles: ${#all[@]}"

# By extension
txts=(*.txt(N))
echo "txt count: ${#txts[@]}"

# Specific glob features (just print pattern, don't necessarily expect matches)
echo "done"
