#!/bin/sh
## Integration test for the login-shell startup-file chain (#L8).
##
## Verifies that for a login shell (`lush -l`) the four startup files
## are sourced in the canonical order:
##
##   1. /etc/profile + /etc/profile.d/*.sh + /etc/lushrc   (system-wide)
##   2. ~/.profile                                          (POSIX, in bash mode)
##   3. ~/.lush_login    or ~/.config/lush/lush_login       (lush-specific)
##   4. ~/.lushrc        or ~/.config/lush/lushrc           (per-shell startup)
##
## The system-wide step (1) is skipped here because writing to /etc/*
## requires root and is platform-variable; lush's existing manual
## smoke covers that path. The user-scoped chain (steps 2-4) is what
## this fixture pins.
##
## Approach mirrors bash's `tests/invocation.tests` pattern: spawn the
## shell with a controlled $HOME, pre-populate each startup file with
## an `echo MARKER` line, run with `-l -i` and `</dev/null` so the
## reader sees immediate EOF, and verify the captured stdout shows
## the markers in the right order. No PTY is needed -- the source
## ordering is determined entirely inside the shell process.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
TMPDIR_BASE="${TMPDIR:-/tmp}"

HOME_DIR=$(mktemp -d "$TMPDIR_BASE/lush_login_chain.XXXXXX")
trap 'rm -rf "$HOME_DIR"' EXIT

fail() {
    printf '%s: FAIL: %s\n' "$0" "$1" >&2
    exit 1
}

ok() {
    printf '%s: OK: %s\n' "$0" "$1"
}

## Capture the stdout of a controlled lush -l -i run. ENV is cleared
## so the existing $ENV-sourcing test is not entangled. PS1/PROMPT
## variables are also cleared to keep prompt rendering out of the
## captured stream; lush still writes a styled prompt to stderr in
## interactive mode, hence the 2>/dev/null on the capture.
run_login() {
    env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
        "$LUSH" -l -i </dev/null 2>/dev/null
}

## ---- 1. all four files present, ~/-based paths ------------------------

cat > "$HOME_DIR/.profile" <<'EOF'
echo "MARK_PROFILE"
EOF
cat > "$HOME_DIR/.lush_login" <<'EOF'
echo "MARK_LUSH_LOGIN"
EOF
cat > "$HOME_DIR/.lushrc" <<'EOF'
echo "MARK_LUSHRC"
EOF

actual=$(run_login)
case "$actual" in
    *MARK_PROFILE*MARK_LUSH_LOGIN*MARK_LUSHRC*)
        ok "all-four-present: profile -> lush_login -> lushrc"
        ;;
    *)
        fail "ordering wrong or markers missing
got: $actual" ;;
esac

## ---- 2. XDG-canonical lush_login wins over ~/.lush_login --------------

mkdir -p "$HOME_DIR/.config/lush"
cat > "$HOME_DIR/.config/lush/lush_login" <<'EOF'
echo "MARK_XDG_LUSH_LOGIN"
EOF
## Also write the ~/ fallback with a different marker so we can detect
## if both fired (they shouldn't).
cat > "$HOME_DIR/.lush_login" <<'EOF'
echo "MARK_HOME_LUSH_LOGIN"
EOF

actual=$(run_login)
case "$actual" in
    *MARK_XDG_LUSH_LOGIN*)
        case "$actual" in
            *MARK_HOME_LUSH_LOGIN*)
                fail "both XDG and ~/ lush_login fired; XDG should win and ~/ should be skipped
got: $actual" ;;
            *)
                ok "xdg-precedence: XDG lush_login wins, ~/ skipped" ;;
        esac
        ;;
    *)
        fail "XDG lush_login did not fire
got: $actual" ;;
esac

## ---- 3. .lushrc fires AFTER login scripts -----------------------------

## With both ~/.lush_login (overwritten above) and ~/.lushrc set, the
## lush_login marker must appear before the lushrc marker. The XDG
## precedence test above already proved XDG-vs-fallback selection;
## reuse the XDG file here.
actual=$(run_login)
case "$actual" in
    *MARK_XDG_LUSH_LOGIN*MARK_LUSHRC*)
        ok "ordering: lush_login fires before lushrc" ;;
    *MARK_LUSHRC*MARK_XDG_LUSH_LOGIN*)
        fail "wrong ordering: lushrc fired before lush_login
got: $actual" ;;
    *)
        fail "missing markers
got: $actual" ;;
esac

## ---- 4. .profile is sourced in bash mode ------------------------------

## .profile is meant to be portable across POSIX shells; lush sources
## it with shell_mode_set(BASH) temporarily. We can confirm by writing
## a bash-only construct into .profile and checking it didn't blow up.
## The simplest tell: `[[ ... ]]` is a bash/zsh extension; if .profile
## is run in pure POSIX mode lush would error. (lush's POSIX mode
## accepts [[ ]] anyway as a curated extension, so use `shopt` -- which
## is bash-only and POSIX-mode rejects.)
cat > "$HOME_DIR/.profile" <<'EOF'
shopt -s nullglob 2>/dev/null && echo "MARK_PROFILE_BASH_MODE_OK"
EOF
## Keep XDG lush_login and lushrc from step 3 in place.

actual=$(run_login)
case "$actual" in
    *MARK_PROFILE_BASH_MODE_OK*)
        ok ".profile sources in bash mode (shopt accepted)" ;;
    *)
        fail ".profile did not run shopt cleanly in bash mode
got: $actual" ;;
esac

## ---- 5. missing files are silent --------------------------------------

rm -f "$HOME_DIR/.profile" "$HOME_DIR/.lushrc"
rm -rf "$HOME_DIR/.config"
rm -f "$HOME_DIR/.lush_login"

actual=$(run_login)
case "$actual" in
    *FAIL*|*error*|*Error*)
        fail "missing startup files should be silent
got: $actual" ;;
    *)
        ok "no startup files present: clean silent startup" ;;
esac

printf '%s: all 5 assertions passed\n' "$0"
