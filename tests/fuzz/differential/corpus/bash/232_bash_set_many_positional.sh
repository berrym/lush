# set -- with a large number of positional parameters must not crash, must
# keep parameters past $99, and must clear stale ones when the list shrinks.
# Regression guard for the uninitialized-argv-slot heap corruption.
set -- $(seq 1 200)
echo "count=$# p150=${150} p200=${200}"
set -- $(seq 1 2000)
echo "count2=$# last=${2000}"
set -- a b c
echo "shrunk=$# p150=[${150}] p1=$1"
