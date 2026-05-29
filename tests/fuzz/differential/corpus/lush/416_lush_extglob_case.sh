# Extended glob in case patterns through lush's canonical matcher
# (lush_pattern_match): @(...) exact-one, +(...) one-or-more, and the
# zsh bare-alternation (a|b) form. extglob is on by default in lush mode.
for w in cat dog bird 42; do
  case "$w" in
    @(cat|dog)) echo "pet: $w" ;;
    +([a-z])) echo "word: $w" ;;
    *) echo "other: $w" ;;
  esac
done
