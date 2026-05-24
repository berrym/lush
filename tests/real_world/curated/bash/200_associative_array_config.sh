# Bash associative-array as a config map. Pattern from many bash CLIs
# (kubectl helpers, dev scripts, etc.): declare -A, populate, iterate.

declare -A config
config[host]="localhost"
config[port]="8080"
config[user]="admin"
config[timeout]="30"

# Iterate by key
echo "=== keys ==="
for key in "${!config[@]}"; do
    echo "  $key"
done | sort

# Iterate by key+value
echo "=== entries ==="
for key in "${!config[@]}"; do
    echo "  $key=${config[$key]}"
done | sort

# Direct access
echo "host: ${config[host]}"
echo "port: ${config[port]}"

# Count
echo "entry count: ${#config[@]}"

# Conditional default via parameter expansion on assoc lookup
debug_level="${config[debug]:-info}"
echo "debug: $debug_level"

# Update
config[timeout]="60"
echo "updated timeout: ${config[timeout]}"

# Delete
unset 'config[user]'
echo "after-unset count: ${#config[@]}"
