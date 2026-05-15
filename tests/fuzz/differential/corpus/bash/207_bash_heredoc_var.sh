# Heredoc forms: <<TAG (var-expanded), <<'TAG' (literal), <<-TAG (tab-stripped).
name=alice
cat <<EOF
hello $name
literal: \$dollar
EOF

cat <<'EOF'
no expansion: $name
literal: \$dollar
EOF

# Tab-strip with <<- (literal tabs at line starts are removed).
cat <<-EOF
	indented with tab
	another tabbed line
	value: $name
	EOF
