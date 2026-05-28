# Value Inspector

**Live variable inspection from the command line: the
`inspect-variable-at-cursor` widget publishes the resolved
identifier, kind, and value of the `$NAME` / `${NAME}` reference
under the cursor to three shell variables the user composes
surfaces around.**

**Status**: shipped.
**Spec lineage**: `docs/SEMANTICS.md` §8 (formerly deferred line
"LLE real-time variable inspection -- inspection hooks on the
command line").

---

## What it is

A built-in LLE widget that, when invoked, scans the buffer near the
cursor for a `$NAME` or `${NAME}` reference, resolves it through the
live symbol table, and publishes three shell variables:

| Variable             | Contents |
|----------------------|----------|
| `LUSH_INSPECT_NAME`  | The resolved identifier. Empty if no reference is under the cursor. |
| `LUSH_INSPECT_KIND`  | One of `none`, `unset`, `scalar`, `list`, `map`. |
| `LUSH_INSPECT_VALUE` | The formatted value: scalar text verbatim, list/map entries `@Q`-quoted. Empty for `none`/`unset`. |

The widget is the primitive. Surfaces are composed from the
variables: a prompt segment that displays `$LUSH_INSPECT_VALUE`, a
post-widget hook that writes it to a log, a status line, a transient
tooltip. The widget is unbound by default — pick a key.

```bash
# In ~/.config/lush/lushrc or interactively:
display lle bind 'C-x v' inspect-variable-at-cursor

display lle segment add inspect LUSH_INSPECT_VALUE
# Then add ${inspect} to the prompt template via `display theme`.
```

After binding, with the cursor on `$HOME` somewhere on the command
line, pressing `Ctrl-X V` immediately:

- Sets `LUSH_INSPECT_NAME=HOME`
- Sets `LUSH_INSPECT_KIND=scalar`
- Sets `LUSH_INSPECT_VALUE=/Users/mberry`
- Triggers a prompt redraw, which shows the new segment value.

For a list:

```bash
arr=(alpha "two words" beta)
# Cursor on `$arr`:
# LUSH_INSPECT_NAME=arr
# LUSH_INSPECT_KIND=list
# LUSH_INSPECT_VALUE='alpha' 'two words' 'beta'
```

For a map:

```bash
declare -A cfg
cfg[host]=fido
cfg[role]=worker
# Cursor on `$cfg`:
# LUSH_INSPECT_NAME=cfg
# LUSH_INSPECT_KIND=map
# LUSH_INSPECT_VALUE='host'='fido', 'role'='worker'
```

## Why it exists -- the Bourne pitfall this kills

Shell debugging has historically required leaving the line you were
typing: dump to stderr, run a separate `echo`, fire `set -x` and
re-execute, or hit the integrated debugger and step. None of those
work *during* command-line editing.

The classical trap: you type a long pipeline, suspect `$ARCHIVE_DIR`
is not what you think it is, and to verify you either run an
exploratory `echo` (interrupting your edit) or commit to running the
whole pipeline blind. Either path loses the editing context.

The inspector closes that gap:

- The widget runs in the LLE event loop, against the live symbol
  table, without leaving the prompt.
- The data is published to three shell variables, so any composable
  surface (segment, status line, hook-driven log) can render it.
- The output uses `@Q`-quoting for list and map elements so a value
  containing spaces, `=`, or `,` stays unambiguous and round-trips
  through `eval`.

## Token rule (where the cursor matters)

Strict half-open match: the cursor must sit in
`[token_start, token_end)`.

| Cursor position relative to `${BAR}`             | Result |
|--------------------------------------------------|--------|
| On `$`                                           | resolves |
| On `{`                                           | resolves |
| Mid-identifier (`BAR`)                           | resolves |
| On the closing `}`                               | resolves |
| One past the closing `}` (just typed it)         | reports `none` |

The rule mirrors editor convention (IDE hover, vim `*`): cursor inside
the token is "on the token"; cursor past the end is outside. To
inspect a `${BAR}` you just finished typing, press `Left` once.

## The scriptable surface

`display lle widget invoke NAME` runs any registered widget against
the current editor state. This lets the inspector (and any future
widget) be triggered from a script, a hook, or a key, not only from a
keypress. It also gives the test suite a way to exercise widgets
without driving the LLE input stream.

```bash
# From within a key-bound widget body, or a `display lle hook`:
display lle widget invoke inspect-variable-at-cursor
```

## Value formatting and round-trippability

List and map values pass through `transform_quote`, the same `@Q`
formatter the parameter-expansion `@Q` operator uses. Output forms:

| Kind   | Format                                                 | Round-trip |
|--------|--------------------------------------------------------|------------|
| scalar | value verbatim                                         | `var="$LUSH_INSPECT_VALUE"` |
| list   | `'e0' 'e1' 'e2' ...`                                   | `eval "arr=($LUSH_INSPECT_VALUE)"` |
| map    | `'k0'='v0', 'k1'='v1', ...`                            | (manual parse) |

Embedded single quotes in any element use the standard bash escape
`'\''` (close, literal, reopen). Control characters use ANSI-C
`$'...'` quoting.

The map form does not round-trip directly into `declare -A` because
no shell has a standard "associative-array literal from quoted
key=value list" syntax. The format is unambiguous-to-the-eye, which
is what a live inspector needs.

## Curated defaults

The widget is unbound by default. Why: keybinding conventions vary by
user (vi vs emacs, terminal vs IDE-style), and the inspector is most
useful when bound to a key the user picks for themselves.

The `display lle widget invoke` surface is always available.

## Architectural note

The inspector lives in `src/builtins/display/lle_inspect_widget.c`,
**outside `liblle`**. Pulling `symtable` into `liblle` would tie the
line editor to a shell that owns variables. The shell-side widget
registration runs once from `src/init.c` after
`lle_shell_integration_init()` reports success.

This pattern -- widgets that need shell state live shell-side and
register at startup, while LLE primitives stay in `liblle` -- is the
intended model for any future stateful widget (variable assignment
inspectors, builtin diagnostic widgets, runtime metric viewers).

## Gotchas

- **The inspector reads the live symbol table, not the buffer's
  syntax tree.** A `$p` inside a typed-function body that hasn't been
  invoked yet reports `unset` -- the parameter `p` only binds when
  the function is called. Static inspection of declared parameter
  kinds is a separate feature.

- **Maps are not directly round-trippable** as shell syntax. Use the
  inspector for human consumption of map state; reach for `declare
  -p name` if a programmatic round-trip is needed.

- **The widget is unbound by default.** First-time users may not
  discover it without reading this guide. The convention this
  documentation suggests is `Ctrl-X V`; users are free to pick
  anything.

- **`LUSH_INSPECT_*` variables persist between inspections.**
  Re-invoke the widget on a non-variable region to clear them
  (`LUSH_INSPECT_KIND=none`), or `unset` them in a hook.

## See also

- `docs/SEMANTICS.md` §3 -- the kind-tagged value model the inspector
  reports.
- `docs/features/sigil-conventions.md` -- the `$` / `@` / `%`
  presentation rules; the inspector currently reads `$`-form
  references and reports the resolved binding regardless of sigil
  context.
- `docs/HOOKS_AND_PLUGINS.md` -- the `display lle hook` surface for
  binding hooks that consume `LUSH_INSPECT_*`.
- `docs/CONFIGURATION.md` -- the `display lle segment` surface for
  prompt segments backed by shell variables.
- `docs/DEBUGGER_GUIDE.md` -- the integrated `(lush-debug)` prompt
  with its own `inspect` / `type` / `watch` commands for use during
  breakpoints. The two inspectors are complementary: the LLE widget
  works at the prompt, the debugger inspector works at breakpoints.
