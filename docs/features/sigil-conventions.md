# Sigil Conventions

**Top-level `$`, `@`, `%` notation for scalar, vector, and pair
presentation contexts.**

**Status**: shipped (lush mode, curated default true).
**Spec lineage**: `docs/SEMANTICS.md` §3 (value kinds), §3.6 (quoting
irrelevant to presentation), §8 (kind sigils, formerly deferred).

---

## What it is

Lush exposes three top-level sigils that select how a resolved
variable binding tokenizes at the call site:

| Sigil | Presentation context | Output shape |
|-------|----------------------|--------------|
| `$x`  | Scalar               | Exactly one word (joined scalar text) |
| `@x`  | Vector               | N words: payload elements (list values, map values, or a single-element widen for a scalar) |
| `%x`  | Pair                 | 2N words: structural pairs (list → `index value index value …`, map → `key value key value …`, scalar → type mismatch) |

The sigil **selects how a binding tokenizes**; it does not select
*which* binding to resolve. `$x`, `@x`, and `%x` all walk the same
scope chain to find the binding named `x`. Whatever the binding's
kind is, the sigil tells the engine which presentation to emit.

This pulls a feature out of subscript syntax (`${arr[@]}`) and out of
quoting (`"$@"` vs `"$*"`) and puts it where it belongs: visible at
the call site, no quote-state required, no `[@]`/`[*]` ceremony.

## Why it exists -- the Bourne pitfall this kills

Classic Bourne-family shells conflate **kind**, **presentation**, and
**quoting**. The single worst trap: `"$@"` produces N quoted words
while `"$*"` produces one joined word, and the only thing that
distinguishes them visually is one character buried inside a quoted
expansion. The form `${arr[@]}` is the bash idiom; `$arr` silently
collapses to just the first element. Forty years of shell scripts
have shipped subtle bugs because of this.

Lush severs the chain by making **the sigil itself** carry the
presentation decision:

- The sigil is the first character of the token, so a reader sees the
  presentation context at a glance.
- The `@`/`%` sigils are bare-word-only: quotes suppress them, like
  `~` tilde expansion. `@x` fires the sigil; `"@x"` is the literal
  text `@x`. List *presentation* remains quote-irrelevant through the
  `$` forms -- `"${x[@]}"` is the N-word vector either way, per
  SEMANTICS.md §3.6.
- A user who wants element-stream semantics writes `@x`. A user who
  wants pair-stream iteration writes `%x`. There is no hidden
  joining behavior; there is no `IFS`-dependent surprise.

The ergonomics gain is real. Iterating a map's pairs in classic
shells requires either `for k in "${!m[@]}"; do v="${m[$k]}"; ...`
(four operators threaded together) or a manual array-of-keys dance.
In lush:

```bash
declare -A m
m[host]=fido m[role]=worker

for k v in %m; do
    echo "$k = $v"
done
```

The same pattern works on a list to recover index/value pairs:

```bash
arr=(alpha beta gamma)

for i v in %arr; do
    echo "[$i] $v"
done
# [0] alpha
# [1] beta
# [2] gamma
```

## Behavior matrix

| Variable kind | `$x` (scalar)   | `@x` (vector)              | `%x` (pair)                              |
|---------------|-----------------|----------------------------|------------------------------------------|
| scalar        | The value       | One-element widening       | **`SHELL_ERR_TYPE_MISMATCH`**            |
| list          | First element   | N words: each element      | 2N words: `i₀ v₀ i₁ v₁ …`                |
| map           | First value     | N words: values (insertion order) | 2N words: `k₀ v₀ k₁ v₁ …`                |
| unset         | empty           | empty                      | empty                                    |

A scalar widens to a one-element list under `@` because a scalar IS a
valid one-element vector -- no information is lost, the result is
unambiguous, and it matches bash/zsh behavior for `"${var[@]}"` on a
scalar.

A pair sigil on a scalar **does** raise a type error. There is no
defensible "pair from a singleton" reading: where does the second
half come from? The structured error reports the exact site:

```
error[E1133]: %x: pair sigil on scalar -- a singleton has no pair
component (use @x for vector context or declare x as a list/map)
```

## Disambiguation -- the strict identifier rule

`@` and `%` had decades of meaning as ordinary word characters
(`user@host`, `make %.o:%.c`, vim backup files, crontab `@reboot`,
git ref `@{-1}`, job specs `%1`, extended-glob `@(foo|bar)`,
parameter-suffix-strip `${var%pat}`). Lush cannot just steal those
characters.

The disambiguation rule:

> `@` or `%` at the **start of an unquoted bare word**, followed
> immediately by a valid variable identifier
> (`^[A-Za-z_][A-Za-z0-9_]*`), parses as a kind-sigil token.
> Anything else continues through the existing word, operator, or
> subscript paths.

Worked examples:

| Input          | Post-sigil span | Identifier? | Result |
|----------------|-----------------|-------------|--------|
| `@arr`         | `arr`           | yes         | sigil → expand `arr` in vector context |
| `%map`         | `map`           | yes         | sigil → expand `map` in pair context |
| `user@host`    | `@host` is mid-word | n/a     | bare word (sigil never fires) |
| `@(foo\|bar)`  | `(foo\|bar)`    | no          | bare word → extended-glob path |
| `@{name}`      | `{name}`        | no          | bare word (brace not identifier-start) |
| `@{-1}`        | `{-1}`          | no          | bare word → git ref preserved |
| `@123`         | `123`           | no (digit start) | bare word → Jira-style refs preserved |
| `%1`           | `1`             | no          | bare word → job spec preserved |
| `make %.o`     | `.o`            | no          | bare word → Makefile target preserved |

The strict-identifier check is the entire disambiguation logic. No
context-sensitive parsing, no ambiguity scoring, no escape hatches.

## Sigils are bare-word-only; quotes suppress them

A kind sigil fires only as an unquoted bare word (the disambiguation
rule above). Inside single or double quotes, `@` and `%` are ordinary
literal characters -- exactly like `~` tilde expansion, which also
applies only to an unquoted leading `~`.

```bash
arr=(one two three)
echo @arr      # one two three  (3 words -- sigil fires)
echo "@arr"    # @arr           (literal -- quotes suppress the sigil)
echo $arr      # one            (scalar context = first element)
echo "$arr"    # one            (same)
```

This keeps double-quoted strings safe wherever `@` and `%` are
everyday literal text: `printf "%s\n" "$x"`, `"user@host"`,
`"100% off"`, and prompt/theme escapes like `"%m"` all pass through
unchanged. List interpolation inside quotes uses the `$` form,
`"${arr[@]}"`.

**Quote `printf` format strings.** A `printf` conversion is a leading
`%` followed by an identifier-like letter (`%s`, `%d`, `%q`), which is
exactly the pair-sigil shape. Written bare, `printf %s "$x"` parses `%s`
as the pair sigil on `s`, not as a format string -- an undeclared `s`
makes it empty, and a declared scalar `s` makes it an `E1134` type
error. Always quote the format: `printf "%s\n" "$x"` or `printf '%q'
"$x"`. This is the same rule as everywhere else -- an unquoted leading
`%`/`@` is a sigil; quotes make it literal -- applied to the one place
it most often surprises people coming from bash, where `%` has no
special meaning.

**On SEMANTICS.md §3.6.** §3.6 ("quoting is irrelevant to
presentation") governs *list structure*: `${arr[@]}` is a vector and
`${arr[*]}` a joined scalar whether quoted or not. It does **not**
make the `@`/`%` *sigil surface* fire inside quotes. The sigil is a
bare-word reference form, and bare must agree with quoted -- `echo
user@host` and `echo "user@host"` both yield `user@host`, so a sigil
that expanded inside quotes (but not mid-word when bare) would itself
break §3.6. Expanding sigils inside quotes was removed for exactly
this reason.

## Curated defaults by mode

| Mode  | `FEATURE_KIND_SIGILS` | Rationale |
|-------|-----------------------|-----------|
| POSIX | off                   | `@` and `%` remain ordinary word characters; full Bourne-shell compatibility. |
| Bash  | off                   | Existing bash scripts that use `@reboot`, `user@host`, `make %.o:%.c`, etc., keep their bare-word reading. |
| Zsh   | off                   | Same as Bash; zsh has its own preserved meanings for `@`/`%` in subscript/parameter contexts. |
| Lush  | **on**                | The local-reasoning payoff of first-class value kinds. Curated default for lush mode per the principled-deviation rule. |

Toggle in any mode with `setopt kind_sigils` / `unsetopt kind_sigils`.

## Behavior with the rest of the shell

**Lexical scope**: the sigil does not change resolution. `@x` inside
a typed-function body still resolves `x` via the function's captured
declaration-site scope; `%x` as an argument to a function call
evaluates `x` in the **caller's** scope (arguments evaluate before
the call). This is the same rule that already applies to `$x`.

**LLE tandem updates** shipped alongside:

- **Syntax highlighting** — `@name` and `%name` highlight identically
  to `$name`.
- **Completion** — `@arr<TAB>` and `%arr<TAB>` offer variable-name
  completion the same way `$arr<TAB>` does.
- **Debugger** — `inspect`, `watch`, and `type` accept any of `$NAME`,
  `@NAME`, `%NAME`, or bare `NAME`. The sigil is stripped before
  symbol-table lookup; the binding it names is the same.

## Gotchas

- **`@reboot` resolves to empty if `reboot` isn't bound.** This
  matches default `$reboot` behavior in lush, bash, and zsh — unset
  variables expand to empty. Use `set -u` (`nounset`) to promote that
  to a runtime error. Static catching of "this looks like a typo" or
  "you wrote `@reboot` but `reboot` is never declared" is the job of
  `debug analyze`.
- **Mode matters.** If a script needs to run in both bash and lush
  modes, write `${arr[@]}` rather than `@arr` — the long form works
  in every mode, the sigil only in lush mode. The polyglot rule is
  not "lush works everywhere"; it is "the shell can speak the dialect
  you need." Use the dialect that fits the audience.
- **Pair-sigil on a scalar is a hard error**, not a silent empty.
  This is intentional: there is no defensible reading, so the
  diagnostic is the right output. If your code might hit this path
  legitimately, branch on kind first (see SEMANTICS.md §3 for the
  kind-tagged value model).

## See also

- `docs/SEMANTICS.md` — full value-model and presentation spec.
- `docs/CONFIGURATION.md` — the four configuration surfaces (`mode`,
  `set`, `setopt`/`shopt`, `config`).
- `docs/PHILOSOPHY.md` §2 — identity vs. polyglot, why curated
  defaults in lush mode are principled and not arbitrary.
