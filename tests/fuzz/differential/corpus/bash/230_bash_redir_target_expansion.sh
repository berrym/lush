# Redirection and here-string (<<<) operands expand like command arguments:
# bare $(...) command substitution, $((...)) arithmetic, `...` backticks,
# and $'...' ANSI-C quoting. Regression guard for the redir-target
# word-expansion consolidation onto expand_arg_node.

# ANSI-C $'...' as a here-string operand
cat <<< $'a\tb\tc'
cat <<< $'line1\nline2'

# command substitution as a here-string operand
cat <<< $(echo from-cmdsub)

# arithmetic as a here-string operand
cat <<< $((6 * 7))

# backticks as a here-string operand
cat <<< `echo from-backtick`

# mapfile with an ANSI-C here-string (the original 211_bash_mapfile gap)
mapfile -t lines <<< $'one\ntwo\nthree'
echo "count=${#lines[@]} first=${lines[0]} last=${lines[2]}"

# a single-quoted here-string stays verbatim: no expansion, backslashes kept
cat <<< 'literal $HOME and \n stay'
