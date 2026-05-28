# Bash indirect expansion ${!name} and prefix-name listing ${!prefix*}.
target=value123
ref=target
echo "indirect: ${!ref}"
VAR_A=1
VAR_B=2
for v in ${!VAR_*}; do
  echo "match: $v"
done
