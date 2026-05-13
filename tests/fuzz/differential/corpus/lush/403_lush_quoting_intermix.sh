# Lush quoting variants mixed: single, double, ANSI-C $'...', heredoc.
name=alice
echo 'single: $name literal'
echo "double: $name expanded"
echo $'ansi: \\t tabs \\n newlines'

# Heredoc with expansion
cat <<EOF
heredoc: $name expanded
literal-dollar: \$other
EOF

# Heredoc-literal (no expansion)
cat <<'EOF'
heredoc-q: $name literal
EOF

# Mixed within one command
echo 'pre-'"$name"'-post'$'\t'end
