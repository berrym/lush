name=world
cat <<< "hello $name"
read -r line <<< "${name}"
