# Zsh associative array: typeset -A, direct key access, count.
# Order-independent queries only (iteration order is implementation
# defined and a documented divergence).
typeset -A cfg
cfg[host]=server1
cfg[port]=8080
echo "host: $cfg[host]"
echo "port: $cfg[port]"
echo "count: ${#cfg}"
cfg[port]=9090
echo "updated: $cfg[port]"
