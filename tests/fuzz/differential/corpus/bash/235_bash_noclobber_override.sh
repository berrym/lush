# >| (noclobber override) must truncate the target like >. Regression guard:
# setup_redirections skipped NODE_REDIR_CLOBBER, so >| never took effect.
f=$(mktemp)
echo one >| "$f"
echo two >| "$f"
cat "$f"
set -o noclobber
echo three >| "$f"
cat "$f"
rm -f "$f"
