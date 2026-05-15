# zsh repeat keyword: repeat N { body } executes body N times.
i=0
repeat 4; do
    i=$((i + 1))
    echo "iter $i"
done

# Brace form
echo "---"
repeat 3 { echo "brace-iter" }

# With variable count
n=5
echo "---"
repeat $n; do
    print -n "*"
done
echo
