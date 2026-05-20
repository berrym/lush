# case_statement with trailing_redirections
case "$1" in
    a) echo a-arm ;;
    b) echo b-arm ;;
    *) echo other ;;
esac > /tmp/case.out 2>&1
