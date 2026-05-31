#!/bin/sh
## Integration test for explicit-exit logout-script firing (#L11 explicit).
##
## Verifies that ~/.lush_logout (or XDG ~/.config/lush/lush_logout) is
## sourced when a login shell terminates via `exit` or the `logout`
## builtin. Mirrors bash's `tests/invocation.tests` style:
##
##     HOME=$TDIR ${THIS_SH} --login -c 'logout'
##
## Non-login shells must NOT fire the logout script.
##
## The SIGHUP-induced-exit case (where the controlling terminal hangs
## up and lush runs logout in response to the signal) is a separate
## test that requires a PTY harness and is not covered here.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
TMPDIR_BASE="${TMPDIR:-/tmp}"

HOME_DIR=$(mktemp -d "$TMPDIR_BASE/lush_logout_explicit.XXXXXX")
trap 'rm -rf "$HOME_DIR"' EXIT

fail() {
    printf '%s: FAIL: %s\n' "$0" "$1" >&2
    exit 1
}

ok() {
    printf '%s: OK: %s\n' "$0" "$1"
}

## ---- 1. login + explicit `exit` runs ~/.lush_logout -------------------

cat > "$HOME_DIR/.lush_logout" <<'EOF'
echo "MARK_LOGOUT"
EOF

actual=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -l -i <<'INPUT' 2>/dev/null
exit
INPUT
)
case "$actual" in
    *MARK_LOGOUT*)
        ok "login + exit: ~/.lush_logout fires" ;;
    *)
        fail "login + exit did not fire ~/.lush_logout
got: $actual" ;;
esac

## ---- 2. login + `logout` builtin runs ~/.lush_logout ------------------

actual=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -l -i <<'INPUT' 2>/dev/null
logout
INPUT
)
case "$actual" in
    *MARK_LOGOUT*)
        ok "login + logout builtin: ~/.lush_logout fires" ;;
    *)
        fail "login + logout did not fire ~/.lush_logout
got: $actual" ;;
esac

## ---- 3. XDG ~/.config/lush/lush_logout wins over ~/ -------------------

mkdir -p "$HOME_DIR/.config/lush"
cat > "$HOME_DIR/.config/lush/lush_logout" <<'EOF'
echo "MARK_LOGOUT_XDG"
EOF

actual=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -l -i <<'INPUT' 2>/dev/null
exit
INPUT
)
case "$actual" in
    *MARK_LOGOUT_XDG*)
        case "$actual" in
            *MARK_LOGOUT*)
                ## MARK_LOGOUT is a substring of MARK_LOGOUT_XDG, so the
                ## case branch above matched both. Distinguish by checking
                ## the BASE marker without the XDG suffix in isolation.
                ## More explicit: count occurrences -- the XDG file
                ## printed once; if ~/ also fired we'd see two markers
                ## of which one is the bare MARK_LOGOUT only.
                count_bare=$(printf '%s' "$actual" | grep -c '^MARK_LOGOUT$' || true)
                if [ "$count_bare" -gt 0 ]; then
                    fail "both XDG and ~/ lush_logout fired
got: $actual" ;
                fi
                ok "xdg-precedence: XDG lush_logout wins" ;;
        esac
        ;;
    *)
        fail "XDG lush_logout did not fire
got: $actual" ;;
esac

## ---- 4. non-login shell does NOT fire ~/.lush_logout ------------------

## Use a marker that will only appear if lush_logout incorrectly runs.
## Both XDG and ~/ logout scripts are present from earlier steps.
actual=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -i <<'INPUT' 2>/dev/null
exit
INPUT
)
case "$actual" in
    *MARK_LOGOUT*)
        fail "non-login shell incorrectly fired logout script
got: $actual" ;;
    *)
        ok "non-login: logout scripts skipped" ;;
esac

## ---- 5. -c mode does NOT fire logout scripts --------------------------

actual=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
            "$LUSH" -l -c 'echo BODY' 2>/dev/null)

case "$actual" in
    *MARK_LOGOUT*)
        ## bash actually DOES fire ~/.bash_logout on -l -c too. lush's
        ## current behavior on this edge is what the test pins, but
        ## verify it matches bash:
        bash_check=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
                        bash -l -c 'echo BODY' 2>/dev/null || true)
        case "$bash_check" in
            *MARK_LOGOUT*)
                ok "-l -c: logout fires (matches bash)" ;;
            *)
                fail "-l -c fires logout but bash does NOT -- divergence
lush: $actual
bash: $bash_check" ;;
        esac
        ;;
    *)
        bash_check=$(env -i HOME="$HOME_DIR" PATH="$PATH" TERM=dumb \
                        bash -l -c 'echo BODY' 2>/dev/null || true)
        case "$bash_check" in
            *MARK_LOGOUT*)
                fail "lush -l -c skipped logout but bash fires it -- divergence
lush: $actual
bash: $bash_check" ;;
            *)
                ok "-l -c: logout skipped (matches bash)" ;;
        esac
        ;;
esac

printf '%s: all 5 assertions passed\n' "$0"
