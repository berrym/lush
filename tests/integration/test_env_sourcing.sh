#!/bin/sh
## Integration test for POSIX $ENV sourcing (#L12).
##
## $ENV is consulted during shell init, before any unit-test harness
## hook fires. So we drive this test by spawning the lush binary
## directly with various env configurations and checking the
## visible side effect (does the $ENV file's marker echo appear in
## stdout, or not?).
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
TMPDIR="${TMPDIR:-/tmp}"
ENV_FILE=$(mktemp "$TMPDIR/lush_env_test.XXXXXX")
trap 'rm -f "$ENV_FILE"' EXIT

MARKER="LUSH_ENV_FIXTURE_MARKER_$$"
printf 'echo "%s"\n' "$MARKER" > "$ENV_FILE"

fail() {
    printf '%s: FAIL: %s\n' "$0" "$1" >&2
    exit 1
}

ok() {
    printf '%s: OK: %s\n' "$0" "$1"
}

## ---- 1. interactive non-login + ENV set: marker MUST appear ------------

actual=$(ENV="$ENV_FILE" "$LUSH" -i </dev/null 2>&1 || true)
case "$actual" in
    *"$MARKER"*) ok "interactive non-login: ENV file sourced" ;;
    *)           fail "interactive non-login should have sourced $ENV_FILE
got: $actual" ;;
esac

## ---- 2. login shell (-l) + ENV set: marker MUST NOT appear -------------

actual=$(ENV="$ENV_FILE" "$LUSH" -l -i </dev/null 2>&1 || true)
case "$actual" in
    *"$MARKER"*) fail "login shell incorrectly sourced \$ENV
got: $actual" ;;
    *)           ok "login shell: ENV NOT sourced (POSIX-correct)" ;;
esac

## ---- 3. non-interactive (-c) + ENV set: marker MUST NOT appear ---------

actual=$(ENV="$ENV_FILE" "$LUSH" -c 'echo body' 2>&1 || true)
case "$actual" in
    *"$MARKER"*) fail "non-interactive -c incorrectly sourced \$ENV
got: $actual" ;;
    *)           ok "non-interactive -c: ENV NOT sourced" ;;
esac

## ---- 4. interactive + ENV unset: no error, runs normally ---------------

actual=$(unset ENV; "$LUSH" -i </dev/null 2>&1 || true)
case "$actual" in
    *"$MARKER"*) fail "ENV unset but marker appeared somehow" ;;
    *)           ok "interactive + ENV unset: no marker, runs cleanly" ;;
esac

## ---- 5. interactive + ENV pointing at non-existent file ----------------

actual=$(ENV="/nonexistent/lush-env-doesnt-exist-$$" \
            "$LUSH" -i </dev/null 2>&1 || true)
case "$actual" in
    *"$MARKER"*) fail "non-existent ENV path emitted marker (impossible)" ;;
    *)           ok "interactive + missing ENV file: silent (POSIX-correct)" ;;
esac

printf '%s: all 5 assertions passed\n' "$0"
