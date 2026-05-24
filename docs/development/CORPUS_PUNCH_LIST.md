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

## 1. diff_oracle stderr blindness

**Severity:** structural / measurement.

`diff_oracle` compares exit code and stdout. It does **not** compare
stderr. The five bash-completion files in `corpus/bash/bash-completion/`
"pass" only because they exit 0 with empty stdout, even though lush
emits multiple parse errors and `complete: command not found` on
stderr that bash does not.

Until this is addressed, **the corpus pass rate is a weak signal.**
A script that bombs out with errors on stderr but happens to exit 0
looks "agreed."

**Options:**

- **Strict stderr match.** Compare verbatim. Produces noise from
  error-message wording differences between shells; lots of
  "divergences" that aren't real defects.
- **Loose stderr match.** Compare on "did one side produce stderr
  and the other not?" boolean. Catches the masking case here without
  matching on wording. Probably the right shape.
- **Per-script stderr expectation.** Each corpus entry could declare
  its expected stderr shape (empty / non-empty / specific lines).
  Most powerful, most curation cost.

**Resolution:** pending decision. Likely the loose boolean check as
a first cut.

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

## 6. Unrecognized zsh option names

**Severity:** missing surface.

| Option | Used in | Decision needed |
|--------|---------|-----------------|
| `prompt_subst` | theme-and-appearance.zsh | zsh prompt-substitution option. Lush has its own prompt system; option could be accepted as a no-op alias for the equivalent lush behavior. |
| `menu_complete` | completion.zsh | zsh completion mode. Lush has its own completion. Same story. |

**Recommendation:** add to the `setopt`/`unsetopt` recognized-name list
with a no-op effect and a documented `known_divergences.txt` entry.
Refusing the option name produces `unknown option` errors that mask
the actually-interesting behavior.

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
