# Zsh print advanced: -P prompt expansion, -f format, combined flags.

# Basic
print hello world

# Format (-f routes to printf)
print -f "%-8s = %s\n" name alice
print -f "%s\n" one two three

# Prompt-style escape: %n is username, %m hostname.
# Both vary by host -- use literal-tolerant escapes instead.
# %% literal percent
print -P "literal %% percent"
# %{...%} -- escaped no-op for terminal control; ignored in plain output
print -P "before%{%}after"

# -n + -l combined: -l overrides -n? In zsh -l wins.
# Actually combined behavior: -l overrides separator (newline), -n
# suppresses trailing.
print -ln a b c
print -nl a b c
