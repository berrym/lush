# Corpus Punch List

Concrete, file-and-line-referenced lush defects surfaced by the
real-world corpus. Each entry is something a real upstream script
fails on (or that lush handles incorrectly while diff_oracle masks
the failure). This is the working punch list driving lush toward
v1.5.0-ready.

This file is updated as the corpus grows. Items get checked off as
fixes land. Items that turn out to be deliberate lush behavior (not
defects) get moved to `tests/fuzz/differential/known_divergences.txt`
with a written rationale.

---

## Status as of first real-corpus spin (2026-05-24)

Corpus: 33 scripts (22 curated + 11 corpus from bash-completion and
oh-my-zsh). Scorecard: 28 pass / 1 allowed-divergent / 4 divergent.
Pass rate **84.8%**. Five additional bash files trivially "pass" only
because diff_oracle ignores stderr (see [#1](#1-diff_oracle-stderr-blindness)).

---

## 1. diff_oracle stderr blindness (RESOLVED)

**Status:** fixed.

`results_agree` in tests/fuzz/diff_oracle.c now also checks **stderr
presence**: agreement requires that lush and the reference shell
either both emit empty stderr or both emit non-empty stderr. Exact
content is still not compared (error-message wording differs across
shells), but the asymmetric case where lush emits parse errors while
the reference shell runs clean is no longer masked.

**Scorecard delta:** 29/33 (90.9%) -> 22/33 (69.7%). Seven scripts
that were silently passing are now properly divergent:

- 5 bash-completion files (lush emits parse errors not in bash)
- spectrum.zsh (typeset -H unknown option, see [#11](#11-typeset--h-flag-unknown))
- key-bindings.zsh (zle builtin missing, see [#12](#12-zle-builtin-missing))

This is the honest pass rate. The drop is the point: the
scorecard is now a real measurement of fitness rather than an
exit-code-and-stdout shape check.

---

## 2. POSIX function definition misread as typed-fn call (RESOLVED)

**Status:** fixed. Two-part fix landed.

The typed-fn-call recognizer was matching empty-argument-list
`name()` and trying the call path. Bash-completion uses
`_comp_cmd_NAME()` POSIX-function form which was being misread.

Independent compounding bug: the input-buffering layer's
`needs_continuation` check didn't know about the
`saw_posix_func_parens` flag, so `name()\n{ body }` (brace on a new
line) was split into two separate parser invocations and the parser
never saw the body. POSIX function definitions with the brace on the
following line silently failed even when the recognizer was correct.

**Fixes:**

- `src/parser.c` `is_typed_fn_call_statement`: discriminate by
  argument presence inside the parens (zero non-trivial tokens =
  POSIX function definition; one or more = typed-fn call). The
  recognizer previously tried to peek past `)` for `{`, which can't
  work because the parser sees one statement at a time and `{` may
  be in a later statement.
- `src/input.c` `needs_continuation`: also return true when
  `saw_posix_func_parens` is set. This keeps the input buffer
  collecting lines until the `{` arrives, matching how the buffer
  already handled `function NAME` (the keyword form) and other
  compound constructs.

**Follow-up surfaced:** the bash-completion files reach further into
the file now and trip a new `E1001 "expected command name, got ''"`
parse error at the `fi` of an `if [[ ]]; then ...; fi` block (see
[#7](#7-fi-after-arithmetic-line-trips-e1001) below). Net progress:
the typed-fn collision is gone, exposing a deeper parser bug.

---

## 3. zsh `&|` background-and-disown operator unrecognized (RESOLVED)

**Status:** fixed.

Original diagnosis (function body with pipe) was wrong. The error
report's column-73 caret pointed at the `|` in `&|`, which is zsh's
"run in background and disown immediately" operator, not a pipe in a
function body.

```bash
function clipcopy() { cat "${1:-/dev/stdin}" | wl-copy &>/dev/null &|; }
```

Fix: new TOK_BACKGROUND_DISOWN token in src/tokenizer.c covering
both zsh spellings (`&|` and `&!`), accepted in parse_pipeline
wherever TOK_AND was. Lush treats the disown-bookkeeping difference
as an optimization the corpus doesn't observe; observable script
semantics match plain `&` (background + continue).

**Follow-up surfaced in clipboard.zsh:** line 113 uses zsh's multi-
name function-definition syntax `function NAME1 NAME2 { body }`
(define two functions sharing one body). Lush parser rejects with
"POSIX functions require '()' after the function name." Tracked as
[#8](#8-zsh-multi-name-function-definition).

---

## 4. Empty `(( ))` raises arithmetic syntax error (RESOLVED)

**Status:** fixed.

`execute_arithmetic_command` in src/executor.c now short-circuits an
all-whitespace expression to exit 1 (bash/zsh "false" placeholder
convention) without raising `SHELL_ERR_ARITHMETIC_SYNTAX`. The
existing evaluator is left untouched -- it is only invoked when
there is actual non-whitespace content to evaluate.

**Affected corpus entries:** key-bindings.zsh -- now reaches line 29
and trips the next defect: `bindkey` builtin missing (tracked under
[#5](#5-missing-zsh-specific-builtins)).

---

## 5. Missing zsh-specific builtins (RESOLVED)

**Status:** fixed. Three no-op stubs landed in
src/builtins/bin_zsh_stubs.c registered in src/builtins/builtins.c:

| Builtin | Stub behavior | Lush equivalent |
|---------|---------------|-----------------|
| `bindkey` | return 0 silently | `display lle bind` |
| `autoload` | return 0 silently | `source` (eager) |
| `zmodload` | return 0 silently | no equivalent needed (lush builds in what common zsh modules provide) |

The zsh bookkeeping these builtins perform (binding keys, registering
lazy functions, loading modules) is invisible from a non-interactive
script's stdout/stderr/exit signal -- the divergence the corpus is
measuring. Users who need the underlying behavior reach for the lush
surfaces named above.

**Affected corpus entries:** key-bindings.zsh now passes the
scorecard cleanly. completion.zsh and theme-and-appearance.zsh still
diverge but on the `setopt`/`unsetopt` unknown-option-name surface
([#6](#6-unrecognized-zsh-option-names)), not the missing-builtin
surface.

**Pass-rate delta:** 28/33 -> 29/33 (84.8% -> 90.9%).

---

## 8. zsh multi-name function definition

**Severity:** missing surface.

```bash
function clipcopy clippaste {
  unfunction clipcopy clippaste
  ...
}
```

zsh shape: `function NAME1 NAME2 ... { body }` defines all named
functions to share one body. Real-world idiom for declaring a
"trampoline" body that multiple aliases route through.

**Source location:** `src/parser.c` `parse_function_definition` --
needs to admit a list of names between `function` and `{`.

**Affected corpus entries:** clipboard.zsh.

## 7. `fi` after `((...))` line trips E1001

**Severity:** parser bug. Surfaced by the typed-fn collision fix
landing.

```bash
if [[ $cur == -* ]]; then
    _comp_compgen_usage -c help -s "$1"
    ((${#COMPREPLY[*]} != 1)) || compopt +o nospace
fi
```

lush reports at the `fi` line:

```
error[E1001]: expected command name, got ''
  --> alias.bash:34:5
```

The parser successfully consumes the arithmetic command on the
preceding line but enters a state where the `fi` keyword closing the
`if` is unrecognized as a keyword. Likely confusion in the
statement-list terminator between an `((expr))` command and the
expected `fi`.

**Affected corpus entries:** alias.bash and other bash-completion
files reach this surface.

**Source location:** `src/parser.c`, the if-statement body parser
where the next statement-list element is expected after a
short-circuit `||` chain.

## 11. `typeset -H` flag unknown (RESOLVED)

**Status:** fixed.

src/builtins/bin_declare.c now accepts two zsh-only typeset flags:

  -H   Hide from `typeset -p` listing. Pure display attribute; lush's
       typeset listing path doesn't enumerate variables the way zsh's
       does, so the no-op accept produces observably-identical
       behaviour.
  -U   Unique-element array attribute. Accepted but not enforced --
       per-array dedup needs an attribute on the array storage that
       lush doesn't have yet. Documented limitation: scripts that
       depend on automatic dedup will diverge if duplicates arrive
       via later operations. Tracked as a future enhancement.

Lush already accepted -g (global) and -A (assoc array); spectrum.zsh
uses `typeset -AHg`, all three flags are now valid.

**Pass-rate delta:** 23/33 -> 24/33 (72.7% -> 75.8%). spectrum.zsh
now passes cleanly.

## 12. `zle` builtin (RESOLVED)

**Status:** real implementation, not a stub.

New src/builtins/bin_zle.c with a side-table that records every
`zle -N WIDGET [FUNCTION]` registration. Supports:

  zle -N WIDGET [FUNCTION]   register widget
  zle -A OLD NEW             alias new to old
  zle -D WIDGET              delete widget
  zle -l                     list widget names
  zle -L                     list as re-runnable `zle -N ...` lines

The side-table is the introspection source: a script that registers
a widget and then calls `zle -l` sees the registration. Investigation
showed 56 `zle -N` sites in oh-my-zsh alone; silent no-op was masking
real divergences.

Interactive widget invocation (bare `zle WIDGET`) is a documented gap
-- non-interactive corpus runs never invoke widgets by name.

**Pass-rate delta:** key-bindings.zsh now passes cleanly
(22/33 -> 23/33, 69.7% -> 72.7%).

## 9. Top-level `return` and zsh `${+name}` is-set test

**Severity:** missing surface.

theme-and-appearance.zsh line 34:

```zsh
[[ "$DISABLE_LS_COLORS" != true ]] || return 0
```

- **bash:** prints `return: can only return from a function or
  sourced script` to stderr, **continues** execution.
- **zsh:** silently allows; treats top-level `return` as exit-from-
  script with the given status.
- **lush:** raises E1119 and aborts the script.

For the zsh-bucket oracle (zsh), lush should match zsh's silent-
allow behavior. For the bash bucket, the warn-and-continue behavior
is acceptable.

Line 35 also surfaces `((${+commands[dircolors]}))` -- zsh's
`${+name}` is "1 if name is set, 0 otherwise" expansion inside an
arithmetic context. Lush's arithmetic parser rejects it.

**Affected corpus entries:** theme-and-appearance.zsh.

## 10. `zstyle` builtin missing

**Severity:** missing surface.

```zsh
zstyle ':completion:*:*:*:*:*' menu select
```

zsh's completion-system configuration builtin. Used pervasively in
zsh user dotfiles (oh-my-zsh, prezto, p10k).

Like `bindkey` / `autoload` / `zmodload`, the right shape is
probably a no-op stub: lush has its own completion-configuration
surface (`display lle completion`) and the zstyle bookkeeping is
invisible from a non-interactive script.

**Affected corpus entries:** completion.zsh and correction.zsh.

## 13. `[[ -o name ]]` not actually implemented (RESOLVED)

**Status:** fixed. Pre-existing structural bug surfaced during the
zsh-compat-layer investigation.

`[[ -o NAME ]]` always returned false regardless of state -- not even
core POSIX options (errexit, xtrace, etc.) reported correctly after
`set -e`. The `-o` arm of the extended-test unary dispatch fell
through to the file-test handler, which always returns false for
arbitrary "paths" like `errexit`.

This is independent of the zsh-compat-layer work but had the same
ratio (~25-35% of setopt usage in real upstream code queries state
via `[[ -o name ]]`). Fixing the -o operator unblocks the entire
query class for every shell option, not just the noop-aliases.

**Fix:**

- src/executor.c `evaluate_simple_test`: add an explicit `-o` arm
  before the file-test fallback that calls the new
  `shell_is_option_set(name)` query.
- src/posix_opts.c new `shell_is_option_set(name)` function that
  walks four sources in order: `interactive` pseudo-option, the
  POSIX option_map (errexit, xtrace, etc.), the feature-matrix
  names + aliases (extglob, nullglob, etc.), and the noop-alias
  recorded-state table.
- src/shell_mode.c new `shell_feature_record_noop_alias_state` /
  `shell_feature_noop_alias_is_enabled` pair. setopt/unsetopt
  record the user's call into a side table; the query consults it
  for noop-alias names (default-true for any alias that hasn't
  been explicitly unset).

**Verified probes:**

- `[[ -o errexit ]]` correctly tracks `set -e` / `set +e`.
- `[[ -o prompt_subst ]]` tracks `setopt prompt_subst` /
  `unsetopt prompt_subst`.
- Unknown option names return false (no crash, no error).

**Test coverage:** three new cases in tests/unit/test_executor.c
locking in errexit, noop-alias-recording, and unknown-name
behavior.

## 6. Unrecognized zsh option names (RESOLVED)

**Status:** fixed.

New `shell_feature_is_noop_alias` mechanism in src/shell_mode.c +
shell_mode.h. setopt and unsetopt consult it before reporting
unknown-option. Zsh option names whose effect in lush is either
always-on, always-off, or supplanted by a different surface are
silently accepted as no-ops.

Initial set (with rationale per entry, see src/shell_mode.c):

- `prompt_subst` / `promptsubst` -- lush prompt always supports
  parameter / arith / command expansion
- `menu_complete` / `menucomplete` -- lush completion is always
  menu-shaped
- `always_to_end` / `alwaystoend` -- completion always lands at end
- `auto_menu` / `automenu` -- lush auto-menus on TAB
- `complete_in_word` / `completeinword` -- always mid-word completable
- `flowcontrol` -- lush LLE manages its own tty raw-mode
- `correct_all` / `correctall` -- no spelling-correction prompt to toggle

Companion fix: src/builtins/bin_zsh_stubs.c gained a `colors`
builtin stub. `autoload -U colors && colors` was tripping `colors:
command not found` once autoload became a silent no-op (the
autoloaded function never gets registered). The `colors` stub
returns 0 silently so theme init scripts run.

**Pass-rate delta:** the option-name fixes don't flip any specific
script on their own (theme-and-appearance.zsh and completion.zsh
have deeper compound issues: top-level `return` semantics, zsh
`${+name}` is-set syntax in arithmetic, `zstyle` builtin missing).
Tracked as [#9](#9-top-level-return-and-zsh-name-is-set-test) and
[#10](#10-zstyle-builtin-missing).

---

## What's NOT on this list

The following are **not** punch-list items, despite being
"divergences" in some informal sense:

- **Different `$0` / `$BASH_VERSION` / `$ZSH_VERSION` values.** Lush
  reports its own identity. Documented in the runtime introspection
  spec.
- **Different default `PS1` / `PS2`.** Lush has its own prompt system.
- **Different `time` command formatting.** Lush's `time` is its own
  implementation.
- **Different stderr wording for the same error category.** Different
  shells word errors differently; this is expected and not a defect.

---

## How to update this file

When adding a new finding:

1. Reproduce the divergence in isolation. Cite file:line.
2. Add a section with severity, behavior, lush vs reference output,
   affected corpus entries, source location guess.
3. Either fix and remove, or move to `known_divergences.txt` with a
   rationale.

When closing a finding:

1. Run the corpus scorecard before and after the fix. Pass count
   should go up.
2. Remove the section from this file.
3. Note the fix in the commit message (file:line).
