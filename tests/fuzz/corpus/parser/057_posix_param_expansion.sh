name=${USER:-anonymous}
ext=${file##*.}
base=${file%.*}
empty=${unset_var:?must be set}
fallback=${maybe_set:+yes}
length=${#name}
