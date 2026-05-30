#!/usr/bin/env lush
## `logout` outside a login shell prints a diagnostic and returns
## non-zero. This is lush's own behavior; bash and zsh both behave
## the same way (the builtin name and semantics come from the wider
## Unix tradition, not from any single reference shell). Running
## under diff_oracle's lush-mode lane means we don't assert against
## an oracle here -- the assertions are baked into the script via
## explicit echoes and the exit code lush itself reports.

logout 2>/dev/null
echo "logout-rc: $?"  ## non-zero

logout 7 2>/dev/null
echo "logout-7-rc: $?"  ## still non-zero (we're not a login shell)

## Sanity: `type logout` recognises the builtin even outside login.
type logout | head -1
