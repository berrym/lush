#!/bin/sh
## Integration test for the non-login interactive startup chain (#L9).
##
## Verifies that a non-login interactive shell (`lush -i`, no `-l`)
## sources ONLY the per-shell startup file (`~/.lushrc` or XDG
## `~/.config/lush/lushrc`) -- not `.profile`, not `.lush_login`,
## not the system profile chain. POSIX semantics: profile and
## login_script are login-shell-only.
##
## Approach mirrors the bash `tests/invocation.tests` pattern --
## controlled $HOME with pre-written startup files, run with `-i`,
## assert markers from non-login files do NOT appear in stdout.
##
## (The POSIX `$ENV` sourcing path -- which IS specific to the
## non-login interactive case -- is covered by test_env_sourcing.sh
## and not duplicated here.)
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
TMPDIR_BASE="${TMPDIR:-/tmp}"

HOME_DIR=$(mktemp -d "$TMPDIR_BASE/lush_nonlogin_init.XXXXXX")
trap 'rm -rf "$HOME_DIR"' EXIT

fail() {
    printf '%s: FAIL: %s\n' "$0" "$1" >&2
    exit 1
}

ok() {
    printf '%s: OK: %s\n' "$0" "$1"
}

run_nonlogin() {
    env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
        "$LUSH" -i </dev/null 2>/dev/null
}

## ---- 1. only ~/.lushrc fires; .profile and .lush_login do NOT ---------

cat > "$HOME_DIR/.profile"    <<'EOF'
echo "MARK_PROFILE"
EOF
cat > "$HOME_DIR/.lush_login" <<'EOF'
echo "MARK_LUSH_LOGIN"
EOF
cat > "$HOME_DIR/.lushrc"     <<'EOF'
echo "MARK_LUSHRC"
EOF

actual=$(run_nonlogin)

case "$actual" in
    *MARK_PROFILE*)
        fail "non-login shell incorrectly sourced .profile
got: $actual" ;;
esac
case "$actual" in
    *MARK_LUSH_LOGIN*)
        fail "non-login shell incorrectly sourced .lush_login
got: $actual" ;;
esac
case "$actual" in
    *MARK_LUSHRC*)
        ok "non-login: only .lushrc fired (profile/lush_login skipped)" ;;
    *)
        fail ".lushrc did not fire
got: $actual" ;;
esac

## ---- 2. XDG-canonical lushrc wins over ~/.lushrc ----------------------

mkdir -p "$HOME_DIR/.config/lush"
cat > "$HOME_DIR/.config/lush/lushrc" <<'EOF'
echo "MARK_XDG_LUSHRC"
EOF
## Replace ~/.lushrc with a distinct marker so we can detect dual-fire.
cat > "$HOME_DIR/.lushrc" <<'EOF'
echo "MARK_HOME_LUSHRC"
EOF

actual=$(run_nonlogin)

case "$actual" in
    *MARK_XDG_LUSHRC*)
        case "$actual" in
            *MARK_HOME_LUSHRC*)
                fail "both XDG and ~/ lushrc fired
got: $actual" ;;
            *)
                ok "xdg-precedence: XDG lushrc wins, ~/ skipped" ;;
        esac
        ;;
    *)
        fail "XDG lushrc did not fire
got: $actual" ;;
esac

## ---- 3. -c mode skips all startup files -------------------------------

## POSIX: a -c shell is non-interactive and runs nothing but its
## argument. None of the markers above should appear.
actual=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -c 'echo MARK_BODY' 2>/dev/null)

case "$actual" in
    *MARK_BODY*)
        case "$actual" in
            *MARK_*LUSHRC*|*MARK_PROFILE*|*MARK_LUSH_LOGIN*)
                fail "-c mode sourced a startup file
got: $actual" ;;
            *)
                ok "-c mode: body runs, no startup files sourced" ;;
        esac
        ;;
    *)
        fail "-c mode: body did not run
got: $actual" ;;
esac

printf '%s: all 3 assertions passed\n' "$0"
