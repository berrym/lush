#!/usr/bin/env lush
# Lush kind-aware semantics (docs/SEMANTICS.md §3 -- the value model).
# The engine distinguishes scalar / list / map at the value level;
# `[@]` / `[*]` are presentation operators ON list values, not type
# switches. The whole-word constraint (§3.9) makes presentation
# depend only on the subscript and never on quote context.
#
# This fixture exercises the kinds in positive shape -- showing that
# operations agree with how they're documented. The boundary-error
# behavior (list-in-scalar-position fault) is documented in the
# allowlist entries for 200_bash_arrays.sh / 300_zsh_param_pipeline.sh
# (E1133 -- the runtime type-mismatch diagnostic).

# --- Scalar ---
greeting="hello world"
echo "scalar-value:  $greeting"
echo "scalar-length: ${#greeting}"

# --- List ---
nums=(1 2 3 4 5)
echo "list-length:   ${#nums[@]}"
echo "list-first:    ${nums[0]}"
echo "list-last:     ${nums[-1]}"
echo "list-all:"
for n in "${nums[@]}"; do
    echo "  [$n]"
done

# --- List presentation: [@] vs [*] ---
# [@] yields N words; [*] yields one IFS-joined string. The
# difference shows up in argv/iteration context.
echo "joined-by-IFS: ${nums[*]}"

# Slicing: lush supports both bash and zsh array-slicing forms.
echo "slice-2-to-4:  ${nums[@]:1:3}"

# --- Map (associative array) ---
declare -A config
config[shell]="lush"
config[mode]="polyglot"
config[version]="1.5.0"

echo "map-size:      ${#config[@]}"
echo "map-get-shell: ${config[shell]}"
echo "map-keys:"
# Iteration order is insertion order per project policy
# (libhashtable's ht_enum_t; documented in SEMANTICS).
for k in "${!config[@]}"; do
    echo "  $k = ${config[$k]}"
done

# --- Round-trip: append, then re-read ---
# Use [*] (join via IFS) in scalar string context per SEMANTICS §3.9
# whole-word constraint; [@] is for vector-accepting positions.
nums+=(6 7)
echo "after-append:  ${nums[*]}"
echo "new-length:    ${#nums[@]}"

# --- Empty list distinguished from unset ---
empty=()
echo "empty-len:     ${#empty[@]}"
echo "empty-via-star:[${empty[*]}]"

# --- Demonstrate the engine's value-kind discipline:
# scalar and list-as-scalar (via [*]) are interchangeable in
# scalar contexts; bare list reference in scalar context is
# the error case (covered by allowlist'd 200_bash_arrays.sh).
joined="${nums[*]}"
echo "scalar-via-star: $joined"
