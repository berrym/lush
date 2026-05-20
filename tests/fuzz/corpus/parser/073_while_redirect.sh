# while_statement and until_statement with trailing_redirections
i=0
while [ $i -lt 3 ]; do
    echo "w$i"
    i=$((i+1))
done > /tmp/while.out

n=3
until [ $n -le 0 ]; do
    echo "u$n"
    n=$((n-1))
done 2>&1 | head -10
