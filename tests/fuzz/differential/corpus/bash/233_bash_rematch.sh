# Bash [[ =~ ]] regex with BASH_REMATCH capture groups.
s="2026-05-29"
if [[ $s =~ ^([0-9]+)-([0-9]+)-([0-9]+)$ ]]; then
  echo "y=${BASH_REMATCH[1]} m=${BASH_REMATCH[2]} d=${BASH_REMATCH[3]}"
fi
echo "full: ${BASH_REMATCH[0]}"
