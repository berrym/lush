# Here-string <<<: feed a string as stdin of a command.
# bash and zsh support directly; POSIX strictly does not have <<<
# but dash since 0.5.10 added it as an extension. Test under dash;
# if the local dash lacks it, this seed will fail uniformly on the
# oracle side and become an allowlist candidate.

cat <<< "hello via here-string"
read -r line <<< "captured value"
echo "got: $line"

# Multi-word here-string
read -r first rest <<< "alpha beta gamma"
echo "first=$first rest=$rest"
