# Bash indirect expansion ${!ref}.
val=treasure
ref=val
echo "indirect: ${!ref}"
levels=ref
echo "single-level: ${!levels}"
