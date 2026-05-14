# Associative array advanced operations in zsh.
typeset -A cfg
cfg[host]=server1
cfg[port]=8080
cfg[user]=admin

# Count
echo "count: ${#cfg}"
echo "count[@]: ${#cfg[@]}"

# Get specific
echo "host: ${cfg[host]}"

# Membership via (i) flag returns N+1 on no-match
# (For assoc: returns the matching key or empty for no match)
# Sort for deterministic key listing
echo "keys (sorted):"
for k in ${(ko)cfg}; do echo "  $k"; done

# Update existing key
cfg[port]=9090
echo "updated-port: ${cfg[port]}"

# Add new key
cfg[debug]=true
echo "new-count: ${#cfg}"

# Remove
unset 'cfg[debug]'
echo "after-unset: ${#cfg}"
