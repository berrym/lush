# case statement with multiple pattern forms.
test_case() {
    case "$1" in
        a|A) echo "letter a" ;;
        [bcd]) echo "letter b/c/d" ;;
        [0-9]) echo "digit" ;;
        *.txt) echo "text file" ;;
        '') echo "empty" ;;
        *) echo "other: $1" ;;
    esac
}

test_case a
test_case B 2>/dev/null || test_case c
test_case 5
test_case file.txt
test_case ''
test_case other
