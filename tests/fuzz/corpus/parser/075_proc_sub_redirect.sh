# process_substitution as redirection target AND as command argument
# Argument position:
diff <(echo a; echo b) <(echo a; echo c)

# Redirection target position (rarer parse path):
cmd > >(tee /tmp/teed.out)
cmd2 < <(echo input-line)
