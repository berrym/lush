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

## 2. POSIX function definition misread as typed-fn call

**Severity:** parser bug.

```bash
_comp_cmd_alias()
{
    ...
}
```

lush reports:

```
error[E1007]: no typed function named '_comp_cmd_alias' is in scope
  --> alias.bash:13:17
```

The typed-fn-call recognizer sees `name()` and tries the call path,
treating an empty argument list as a typed-fn invocation. The parser
needs to fall through to POSIX-function-definition when (a) no `let`
prefix exists at this statement position and (b) the parens are
followed by a `{`-introduced body.

**Affected corpus entries:** every bash-completion file (the bash-
completion convention is `_comp_cmd_NAME()` POSIX-function form).

**Source location:** `src/parser.c`, the `is_typed_fn_call_statement`
recognizer or the surrounding statement-dispatch logic.

**Estimated impact:** small fix, large reach. Unblocks the entire
bash-completion bucket from emitting parse errors.

---

## 3. Function body with pipe doesn't parse

**Severity:** parser bug.

```bash
function clipcopy() { cat "${1:-/dev/stdin}" | doitclient wclip; }
```

lush reports:

```
error[E1001]: expected command name, got '|'
  --> clipboard.zsh:74:73
```

The `function name() { body }` form is being parsed with a body
grammar that doesn't admit `|`. The shape works fine in the same
parser when the body is a single command; introducing a pipe in the
body trips the inner-command parser.

**Affected corpus entries:** clipboard.zsh; will affect any zsh/bash
script using one-liner function bodies with pipes.

**Source location:** `src/parser.c`, likely the `parse_function_body`
or `parse_compound_command` paths.

---

## 4. Empty `(( ))` raises arithmetic syntax error

**Severity:** executor / arithmetic.

```bash
((  ))
```

lush reports:

```
error[E1304]: arithmetic syntax error in expression:
  = while: evaluating arithmetic command ((  ))
  = help: (( )) expects arithmetic expressions, not shell commands
```

Bash and zsh treat an empty `(( ))` as evaluating to 0, which sets
the command's exit status to 1 (the inverse-of-arithmetic-truth
convention). Real-world scripts use this as a "false" placeholder.

**Affected corpus entries:** key-bindings.zsh.

**Source location:** `src/arithmetic.c` evaluation entry point; needs
to accept an empty expression as 0.

---

## 5. Missing zsh-specific builtins

**Severity:** missing surface.

Builtins that exist in zsh and are referenced by real zsh scripts:

| Builtin | Used in | Decision needed |
|---------|---------|-----------------|
| `bindkey` | key-bindings.zsh | Implement as no-op for non-interactive contexts? Or stub with a structured error pointing at `display lle bind`? |
| `autoload` | theme-and-appearance.zsh | zsh's lazy-function mechanism; implementing the loader is real work. Stub for non-interactive? |
| `zmodload` | completion.zsh | zsh module loader. Almost certainly stub-as-no-op. |

**Recommendation:** stub each as no-op when invoked outside the
specific contexts that need them, with structured errors when invoked
in a context where the no-op would be wrong (interactive prompt setup,
etc.). The corpus then sees clean stderr and exit 0 even though the
builtin "didn't do anything." Document each stub in
`known_divergences.txt`.

---

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
