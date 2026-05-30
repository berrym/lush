#!/bin/bash
## Per-shell invariants that bash sets up on startup and lush must
## match (project-defaults-bash-zsh-consensus).
##
## diff_oracle runs this script through both lush and bash with the
## same parent environment, so any variable lush sets up on startup
## (SHLVL, $0, ...) must end up equal to bash's. Pre-fix, lush
## inherited SHLVL unchanged while bash incremented; that divergence
## would have failed this test.

## --- SHLVL is set and incremented from parent ---------------------------

echo "shlvl: $SHLVL"
## sanity: numeric, >= 1
case "$SHLVL" in
    ''|*[!0-9]*) echo "shlvl-numeric: no" ;;
    *)
        if [ "$SHLVL" -ge 1 ]; then
            echo "shlvl-numeric: yes (>=1)"
        else
            echo "shlvl-numeric: no (< 1)"
        fi
        ;;
esac

## --- Exit status round-trip ---------------------------------------------

(exit 0); echo "after-true:  $?"
(exit 1); echo "after-false: $?"
(exit 7); echo "after-seven: $?"

## --- $? carries through a pipeline (PIPESTATUS not tested here) ----------

true | false; echo "pipe-last: $?"   ## 1 (last in pipe)
false | true; echo "pipe-last: $?"   ## 0 (last in pipe)

## --- Basic arithmetic still works ----------------------------------------

echo $((2 + 3 * 4))      ## 14
echo $((10 % 3))         ## 1
