#!/bin/sh
## Integration test for the interactive job-control auto-enable guard.
##
## An interactive shell defaults job control (shell.monitor) ON, but that
## is a DEFAULT, not an override: an explicit `set +m` in the rc script
## (or any source above the schema default) must win. The auto-enable runs
## AFTER rc processing, so without a guard it clobbers the user's choice.
##
## Verifies:
##   1. control  -- a plain interactive shell auto-enables monitor (true).
##   2. guard    -- an rc `set +m` survives the auto-enable (false).
##
## Approach mirrors test_nonlogin_init.sh: a controlled $HOME with a
## pre-written ~/.lushrc, run with `-i`. The probe command is fed on
## stdin so it runs AFTER full init (the auto-enable runs during init,
## before the interactive command loop).
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
TMPDIR_BASE="${TMPDIR:-/tmp}"

HOME_DIR=$(mktemp -d "$TMPDIR_BASE/lush_monitor_guard.XXXXXX")
trap 'rm -rf "$HOME_DIR"' EXIT

fail() {
    printf '%s: FAIL: %s\n' "$0" "$1" >&2
    exit 1
}

ok() {
    printf '%s: OK: %s\n' "$0" "$1"
}

## Run an interactive shell with the given rc body, probing shell.monitor
## from stdin (after init). Echoes the captured monitor value (true/false).
##
## The value is bracketed with unique sentinels and extracted from BETWEEN
## them, so a themed interactive prompt that happens to contain "true"/"false"
## (e.g. a git-state segment) cannot be mistaken for the queried value.
probe_monitor() {
    rc_body="$1"
    printf '%s\n' "$rc_body" > "$HOME_DIR/.lushrc"
    printf 'echo MON_BEGIN; config get shell.monitor; echo MON_END\n' |
        env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -i 2>/dev/null |
        awk '/MON_BEGIN/{f=1;next} /MON_END/{f=0} f' |
        grep -Eo 'true|false' | tail -1
}

## ---- 1. control: interactive shell auto-enables job control -----------

control=$(probe_monitor 'true')
case "$control" in
    true) ok "control: interactive shell auto-enables monitor" ;;
    *) fail "control: interactive shell did not auto-enable monitor
got: '$control'" ;;
esac

## ---- 2. guard: rc 'set +m' survives the auto-enable -------------------

guarded=$(probe_monitor 'set +m')
case "$guarded" in
    false) ok "guard: rc 'set +m' is respected (auto-enable skipped)" ;;
    *) fail "guard: rc 'set +m' was clobbered by the auto-enable
got: '$guarded'" ;;
esac

printf '%s: all assertions passed\n' "$0"
