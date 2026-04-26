LANG=C ls
TMPDIR=/tmp NAME=test gcc -o foo foo.c
PATH=/usr/bin:$PATH FOO=bar exec env
