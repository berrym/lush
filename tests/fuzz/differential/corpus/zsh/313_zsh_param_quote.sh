# zsh (q) family: quoting an expansion for safe re-eval.
#   (q)   -- backslash-escape special chars
#   (qq)  -- single-quote
#   (qqq) -- double-quote
#   (Q)   -- remove one level of quoting

raw="hello world; rm -rf /"
echo "q: ${(q)raw}"
echo "qq: ${(qq)raw}"
echo "qqq: ${(qqq)raw}"

# (Q) strips one level of quoting
quoted="'a b c'"
echo "Q: ${(Q)quoted}"

# Round-trip: quote then unquote
orig="he said \"hi\""
quoted=${(qq)orig}
back=${(Q)quoted}
echo "orig=[$orig]"
echo "quoted=[$quoted]"
echo "back=[$back]"
