# Pipeline composition: stdout of each stage feeds the next.
printf 'one\ntwo\nthree\n' | grep two
printf 'a\nb\nc\nb\na\n' | sort | uniq -c | wc -l
printf '%s\n' 1 2 3 | head -n 2
