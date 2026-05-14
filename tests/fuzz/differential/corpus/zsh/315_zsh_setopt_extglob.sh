# zsh extended-glob patterns inside [[ == ]].
# Extended glob: ^pat (negation), pat~exclude, alternation (a|b).
setopt EXTENDED_GLOB 2>/dev/null

s="hello"

# Negation: ^h* means "not starting with h"
[[ "$s" != ^h* ]] && echo "neg-match"  # POSIX != negates, not zsh ^

# Glob alternation in case
case "$s" in
    hello|world) echo "alt-match" ;;
esac

# Glob with brackets and ranges
[[ "$s" == [a-z]* ]] && echo "lc-start"
[[ "ABC" == [A-Z]* ]] && echo "uc-start"

# Length-bounded glob
[[ "$s" == ???* ]] && echo "at-least-three"
