# Heredoc idioms: variable expansion in body, quoted delimiter, <<-,
# heredocs feeding commands. Used in every install script.

name="world"
cat <<EOF
hello, $name
EOF

# Quoted delimiter -> no expansion
cat <<'EOF'
literal $name here
EOF

# <<- strips leading tabs
	cat <<-EOF
	indented heredoc
	$name still expands
	EOF

# Heredoc feeding a pipeline
cat <<EOF | tr '[:lower:]' '[:upper:]'
mixed case input
EOF

# Heredoc to a variable via command substitution
result=$(cat <<EOF
captured
multi-line
content
EOF
)
echo "captured-len: ${#result}"
echo "$result"
