# Lush polyglot script: a user mixes bash assoc arrays with zsh param
# flags to build a small lookup tool.

# Bash-style declare for assoc array
declare -A users
users[alice]="alice@example.com"
users[bob]="bob@example.com"
users[carol]="carol@example.com"

# Bash array of names
names=(alice bob carol dave)

# Iterate names; for each, look up email and pretty-print using zsh
# parameter flags
for name in "${names[@]}"; do
    email="${users[$name]:-<unknown>}"
    # zsh (C) flag for capitalization (lush should accept either form)
    pretty="${(C)name}"
    echo "$pretty: $email"
done

# Bash count assertion
echo "total users registered: ${#users[@]}"
echo "total names checked: ${#names[@]}"

# Zsh sort using (o) flag, then bash iterate
sorted=("${(o)names}")
echo "sorted (zsh-flag): ${sorted[*]}"
