# Pattern lifted from autoconf-generated `configure` scripts: detect a
# command, fall through alternatives, build up a result variable. This
# shape appears in tens of thousands of OSS configure scripts.

# Simulate "find a usable awk"
awk_candidates="gawk mawk nawk awk"
found_awk=""
for cand in $awk_candidates; do
    # Stub the check: just pretend we're looking for the command in PATH
    # without actually calling command -v (so the script is hermetic).
    case "$cand" in
        nawk) found_awk="$cand"; break ;;  # pretend nawk is the first hit
    esac
done

if [ -z "$found_awk" ]; then
    echo "no awk found" >&2
    exit 1
fi

echo "selected: $found_awk"

# Version probe pattern: parse a "vX.Y.Z" string into components
version="v1.23.456"
major=$(echo "$version" | sed -n 's/^v\([0-9]\{1,\}\)\..*/\1/p')
minor=$(echo "$version" | sed -n 's/^v[0-9]\{1,\}\.\([0-9]\{1,\}\)\..*/\1/p')
patch=$(echo "$version" | sed -n 's/^v[0-9]\{1,\}\.[0-9]\{1,\}\.\([0-9]\{1,\}\).*/\1/p')

echo "major=$major minor=$minor patch=$patch"

# Conditional based on version: "if major >= 1, enable feature"
if [ "$major" -ge 1 ] 2>/dev/null; then
    feature="enabled"
else
    feature="disabled"
fi
echo "feature: $feature"
