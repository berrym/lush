# Exit codes propagate correctly through pipelines, conditionals,
# command substitution. Avoids non-deterministic special params
# (PID, $0, $!).

# Direct exit code
true; echo "true: $?"
false; echo "false: $?"

# Custom exit code
(exit 42); echo "custom: $?"

# Pipeline -- POSIX returns last command's status by default
(true | false); echo "pipe-tail: $?"
(false | true); echo "pipe-success-tail: $?"

# Conditional chain
true && echo "after-true"
false || echo "after-false"

# Command sub doesn't propagate exit (unset $?)
out=$(false)
echo "cmdsub-then-true: $? out=[$out]"
