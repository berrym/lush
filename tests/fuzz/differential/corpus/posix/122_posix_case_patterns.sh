# POSIX case with alternation, glob patterns, and a quoted empty word
# in the for list (the empty word must be iterated, not dropped).
for w in apple banana cherry 42 ""; do
  case "$w" in
    apple|cherry) echo "fruit-ac: $w" ;;
    b*) echo "b-word: $w" ;;
    [0-9]*) echo "number: $w" ;;
    "") echo "empty" ;;
    *) echo "other: $w" ;;
  esac
done
