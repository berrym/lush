# Bash library-of-functions pattern: many real CLI tools structure their
# logic as 30-100 small functions in a single script, dispatched by a
# main case. This exercises function defs, local vars, return codes,
# and argument passing.

log_info() {
    echo "[INFO] $*"
}

log_warn() {
    echo "[WARN] $*" >&2
}

log_error() {
    echo "[ERROR] $*" >&2
    return 1
}

trim() {
    local s="$1"
    # Strip leading whitespace
    s="${s#"${s%%[![:space:]]*}"}"
    # Strip trailing whitespace
    s="${s%"${s##*[![:space:]]}"}"
    echo "$s"
}

join_by() {
    local separator="$1"
    shift
    local first="$1"
    shift
    printf '%s' "$first"
    for item in "$@"; do
        printf '%s%s' "$separator" "$item"
    done
    printf '\n'
}

is_in() {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        [[ "$item" == "$needle" ]] && return 0
    done
    return 1
}

# Use them
log_info "starting"
log_warn "this is a warning"

trimmed=$(trim "   padded   text   ")
echo "trimmed: [$trimmed]"

joined=$(join_by ", " alpha beta gamma delta)
echo "joined: $joined"

if is_in "beta" alpha beta gamma; then
    echo "found beta"
else
    echo "no beta"
fi

if is_in "epsilon" alpha beta gamma; then
    echo "found epsilon"
else
    echo "no epsilon"
fi

log_info "done"
