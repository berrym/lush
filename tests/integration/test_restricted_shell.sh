#!/bin/sh
## Integration test for the `-r` / `--restricted` / rlush restricted
## shell mode (#L15). Mirrors bash's rbash restriction set and POSIX
## 2024 `set -r` semantics.
##
## Restricted mode is a usability boundary, NOT a security boundary
## (matching bash's own manual). The test verifies each restriction
## fires; it does NOT attempt to verify lush is sandboxed against a
## determined adversary because that's not what restricted shells
## are for.
##
## meson passes the lush binary path as $1.

set -e

LUSH="${1:?lush binary path required as first argument}"
TMPDIR_BASE="${TMPDIR:-/tmp}"

WORK=$(mktemp -d "$TMPDIR_BASE/lush_restricted.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

fail() {
    printf '%s: FAIL: %s\n' "$0" "$1" >&2
    exit 1
}

ok() {
    printf '%s: OK: %s\n' "$0" "$1"
}

run_r() {
    env -i HOME="$WORK" PATH="$PATH" TERM=dumb \
        "$LUSH" -r -c "$1" 2>&1
}

run_long() {
    env -i HOME="$WORK" PATH="$PATH" TERM=dumb \
        "$LUSH" --restricted -c "$1" 2>&1
}

run_unrestricted() {
    env -i HOME="$WORK" PATH="$PATH" TERM=dumb \
        "$LUSH" -c "$1" 2>&1
}

## ---- 1. cd is disabled in restricted mode -----------------------------

out=$(run_r 'cd /tmp; pwd' || true)
case "$out" in
    *"cd: restricted"*)
        ok "-r: cd is disabled" ;;
    *)
        fail "-r: cd was NOT rejected
got: $out" ;;
esac

## ---- 2. Command names containing '/' are rejected ---------------------

out=$(run_r '/bin/echo hi' || true)
case "$out" in
    *"restricted"*"'/'"*)
        ok "-r: /-containing command names rejected" ;;
    *)
        fail "-r: /-containing command name was NOT rejected
got: $out" ;;
esac

## Sanity: PATH-lookup command names still work
out=$(run_r 'echo from_restricted' || true)
case "$out" in
    *"from_restricted"*)
        ok "-r: PATH-resolved command names still work" ;;
    *)
        fail "-r: PATH-resolved 'echo' broke
got: $out" ;;
esac

## ---- 3. Output redirection is forbidden -------------------------------

target="$WORK/output_target"
out=$(run_r "echo hi > $target" || true)
case "$out" in
    *"restricted"*"output"*)
        ok "-r: output redirection (>) rejected" ;;
    *)
        fail "-r: > redirection was NOT rejected
got: $out" ;;
esac
if [ -f "$target" ]; then
    fail "-r: output file was created despite the rejection"
fi

## Append redirection too
out=$(run_r "echo hi >> $target" || true)
case "$out" in
    *"restricted"*"output"*)
        ok "-r: append redirection (>>) rejected" ;;
    *)
        fail "-r: >> was NOT rejected
got: $out" ;;
esac

## Stderr redirection too
out=$(run_r "echo hi 2> $target" || true)
case "$out" in
    *"restricted"*"output"*)
        ok "-r: stderr redirection (2>) rejected" ;;
    *)
        fail "-r: 2> was NOT rejected
got: $out" ;;
esac

## Input redirection should still work (it doesn't write anywhere).
infile="$WORK/infile"
printf "from_stdin_input\n" > "$infile"
out=$(run_r "cat < $infile" || true)
case "$out" in
    *"from_stdin_input"*)
        ok "-r: input redirection (<) still works" ;;
    *)
        fail "-r: < was incorrectly rejected
got: $out" ;;
esac

## ---- 4. exec to replace the shell is forbidden ------------------------

out=$(run_r 'exec echo replaced' || true)
case "$out" in
    *"exec"*"restricted"*)
        ok "-r: exec with args rejected" ;;
    *)
        fail "-r: exec was NOT rejected
got: $out" ;;
esac

## ---- 5. . / source with /-containing filename rejected ---------------

script="$WORK/sourced_script"
printf "echo from_sourced\n" > "$script"
out=$(run_r ". $script" || true)
case "$out" in
    *"restricted"*)
        ok "-r: source with /-path rejected" ;;
    *)
        fail "-r: . with /-path was NOT rejected
got: $out" ;;
esac

## ---- 6. PATH / SHELL / ENV / BASH_ENV / HISTFILE are readonly --------

for var in PATH SHELL ENV BASH_ENV HISTFILE; do
    out=$(run_r "$var=hacked; echo \$$var" || true)
    case "$out" in
        *"readonly"*"$var"*|*"$var"*"readonly"*)
            ok "-r: $var is readonly" ;;
        *"hacked"*)
            fail "-r: $var was reassigned despite restriction
got: $out" ;;
        *)
            fail "-r: $var assignment did not produce a readonly error
got: $out" ;;
    esac
done

## ---- 7. set +r / set +o restricted cannot clear the mode ------------

out=$(run_r 'set +r' || true)
case "$out" in
    *"cannot clear restricted"*)
        ok "-r: set +r is rejected" ;;
    *)
        fail "-r: set +r was not rejected
got: $out" ;;
esac

out=$(run_r 'set +o restricted' || true)
case "$out" in
    *"cannot clear restricted"*)
        ok "-r: set +o restricted is rejected" ;;
    *)
        fail "-r: set +o restricted was not rejected
got: $out" ;;
esac

## ---- 8. --restricted long flag is equivalent --------------------------

out=$(run_long 'cd /tmp' || true)
case "$out" in
    *"cd: restricted"*)
        ok "--restricted: equivalent to -r" ;;
    *)
        fail "--restricted: did not engage restrictions
got: $out" ;;
esac

## ---- 9. Unrestricted shell does NOT apply any of these ---------------

target="$WORK/unrestricted_target"
out=$(run_unrestricted "echo hi > $target") || fail "unrestricted shell errored on plain redirect"
if [ ! -f "$target" ]; then
    fail "unrestricted shell did not create the redirect target"
fi
case "$(cat "$target")" in
    hi*) ok "unrestricted: redirection works normally" ;;
    *) fail "unrestricted: target content unexpected" ;;
esac

out=$(run_unrestricted 'cd /tmp; echo "pwd=$PWD"')
case "$out" in
    *"pwd=/tmp"*|*"pwd=/private/tmp"*)
        ok "unrestricted: cd works normally" ;;
    *)
        fail "unrestricted: cd did not work
got: $out" ;;
esac

## ---- 10. rc files run BEFORE restrictions engage ---------------------

## bash and zsh engage restrictions AFTER rc-file processing so admin
## startup scripts can configure the environment. Verify lush matches:
## a startup script that does `cd /tmp` and a redirection should run
## even though the post-init invocation is restricted.
mkdir -p "$WORK/.config/lush"
cat > "$WORK/.config/lush/lushrc" <<'RC'
cd /tmp
echo "RC_DID_CD: $PWD"
RC

## -r alone without -i doesn't trigger the interactive rc path; we need
## an interactive invocation to exercise rc-then-restrict. Pipe a
## command in via stdin since `-c` doesn't source rc.
out=$(env -i HOME="$WORK" PATH="$PATH" TERM=dumb \
        "$LUSH" -r -i <<'INPUT' 2>&1 || true
cd /usr
INPUT
)
case "$out" in
    *"RC_DID_CD:"*"tmp"*)
        case "$out" in
            *"cd: restricted"*)
                ok "rc runs before -r engages; post-rc cd is blocked" ;;
            *)
                fail "rc ran but post-rc cd was NOT blocked
got: $out" ;;
        esac
        ;;
    *)
        fail "rc did not run pre-restrict (its cd should have shown)
got: $out" ;;
esac

printf '%s: all assertions passed\n' "$0"
