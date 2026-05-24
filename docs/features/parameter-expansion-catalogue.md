# Parameter-Expansion Error Catalogue

**Misapplied parameter-expansion operators raise structured type
mismatches at the exact site; well-typed per-element operations on
lists and maps fire on every element rather than coercing to a
joined scalar.**

**Status**: shipped.
**Spec lineage**: `docs/SEMANTICS.md` §3.5 (transformation always
fires), §3.9 (no implicit list-to-string), §8 (formerly deferred
line "Error catalogue for misapplied transformations").

---

## What it is

Two coupled engine changes:

1. **Loud structured errors at every silent-no-op site.** A
   case-modification, pattern-strip, replace, or transformation
   operator applied to a binding of the wrong kind now queues
   `SHELL_ERR_TYPE_MISMATCH` (code `E1133`) instead of silently
   returning the original value, the empty string, or the
   un-modified collection.

2. **Per-element semantics on collection vectors.** When the operand
   is a `[@]`-subscripted array (`${arr[@]op}`) or a flag form
   (`${(U)arr}`), the operator applies to **each element** rather
   than to the joined string. Previously these collapsed to the
   joined scalar first, applied the op once, and field-split the
   result -- which produced subtly wrong output and lost the
   element-count guarantee.

```bash
# Per-element case modification (both spellings work)
arr=(hello world)
echo "${arr[@]^^}"        # HELLO WORLD
echo "${(U)arr}"          # HELLO WORLD -- zsh-style flag, same semantics

# Per-element pattern strip
files=(file.txt other.log)
echo "${files[@]##*.}"    # txt log
echo "${files[@]%%.*}"    # file other

# Per-element replace
echo "${arr[@]//l/L}"     # heLLo worLd

# Per-element @Q transform
echo "${arr[@]@Q}"        # 'hello' 'world'

# Misapplication is loud, not silent
declare -A m; m[k]=v
echo "${(s:X:)m}"
# error[E1133]: type mismatch: split-string flag (s:X:) on map ...
```

## Why it exists -- the Bourne pitfall this kills

Two pitfalls, both well-loved in shell scripts that ship to production:

### Silent no-op on misapplied operators

The classical shells are forgiving in a way that hides bugs:
applying `${var^^}` (uppercase-all) to an array returns the array's
first element uppercased; applying `${(U)var}` -- a zsh flag --
inside bash silently does nothing. Neither behavior is what the
author meant, and neither produces a diagnostic.

The lush move: every misapplication that previously returned
"something reasonable but not what you asked for" now raises a
structured error pointing at the exact site, with a help line that
names the correct form. The diagnostic is the right output for
"this code doesn't mean what you typed."

### Collapse-then-operate on collections

`${arr[@]^^}` in bash works element-by-element. But many other
operators didn't: `${arr[@]##*.}`, `${arr[@]//l/L}`, `${(U)arr}`,
`${arr[@]@Q}` -- different shells handled these differently or
collapsed to the joined string first.

lush picks one rule: **`[@]`-subscripted vectors and `(flag)`-applied
collections operate per-element across the entire operator family**.
The element count is preserved; each element gets its own
independent transformation.

## Behavior matrix

For an operator `OP` applied to a binding `x`:

| `x` kind | `${x OP}` (scalar context) | `${x[@] OP}` (vector context) | `${(flag) x}` (flag form) |
|----------|-----------------------------|-------------------------------|---------------------------|
| scalar   | OP fires on the value       | type mismatch (no `[@]` on scalar) | OP fires; collection-only flags raise type mismatch |
| list     | type mismatch *if* OP requires scalar input (most do) | OP fires per element | OP fires per element |
| map      | type mismatch              | OP fires on each value (insertion order) | OP fires per value; collection-only flags work |

### Operators that gained per-element semantics

The per-element pipeline covers the following operator families when
applied to `${arr[@]op}`:

- **Case modification**: `^`, `^^`, `,`, `,,`
- **Pattern strip**: `#`, `##`, `%`, `%%`
- **Replace**: `/`, `//`, `/#`, `/%`
- **`@`-transforms**: `Q`, `E`, `P`, `A`, `a`

Equivalent flag forms (`${(U)arr}`, `${(L)arr}`, `${(C)arr}`,
`${(s:X:)arr}`, `${(o)arr}`, `${(O)arr}`, `${(u)arr}`) apply per
element with the same shape.

### Operators that stay scalar-only

Some operators have no meaningful per-element reading:

- **Substring slice** `${arr[@]:N:M}` -- already a vector operation
  on the *list*, not on each element. Unchanged.
- **Length** `${#arr[@]}` -- a count of the array, not of each
  element. Unchanged.
- **Conditional default** `${var:-DEFAULT}` -- defined for whole-value
  presence; applied to whole binding.

## Examples

**Strip extensions from a file list:**

```bash
files=(report.pdf data.csv photo.png)
basenames=("${files[@]%%.*}")
# basenames is now (report data photo)
```

**Uppercase every value of a map:**

```bash
declare -A env
env[host]=fido env[role]=worker
echo "${env[@]^^}"
# FIDO WORKER
```

**Catch a typo at expansion time:**

```bash
config_string=quiet
echo "${config_string[@]##c}"
# error[E1133]: type mismatch: subscript [@] on scalar
#   --> script.sh:2:6
#    = help: scalar values have no [@] presentation; remove the
#            subscript, or declare config_string as a list.
```

**Mixed lush-style flag forms** -- the `(U)` flag is zsh syntax,
`^^` is bash syntax, both reach the same per-element pipeline:

```bash
arr=(one two three)
echo "${(U)arr}"      # ONE TWO THREE
echo "${arr[@]^^}"    # ONE TWO THREE -- identical result
```

This is the syntax-bridging principle in action: two surface forms,
one engine.

## Curated defaults

The catalogue is **engine-level**, not mode-gated. Every shell mode
gets the type-mismatch diagnostics and per-element semantics. This
is deliberate: the previous silent-no-op behavior was a bug
masquerading as a feature, not a compatibility surface. Scripts that
relied on the silent collapse were probably broken in subtle ways;
the loud diagnostic surfaces the breakage.

There is no `unsetopt` for "go back to silent no-ops." If a script
genuinely needs the joined-scalar collapse, use `${arr[*]}` (with
asterisk, not at-sign), which is the documented joined-form.

## Diagnostics

All catalogue errors share error code `E1133`
(`SHELL_ERR_TYPE_MISMATCH`). The message format is:

```
error[E1133]: type mismatch: <what was attempted>
  --> <file>:<line>:<col>
   |
 N | <source line>
   |     ^~~~~~~~ <span pointer>
   |
   = help: <concrete fix>
```

The help line names the correct form for the operator. Examples:

- `${(s:X:)m}` (split on map): help suggests "split is defined on
  scalars; iterate the map with `for k v in %m` or use a specific
  key."
- `${var^^}` (case-mod on list with scalar slot): help suggests
  "use `${var[@]^^}` for per-element, or pick a specific index
  `${var[0]^^}`."

## Gotchas

- **`${arr[*]op}` vs `${arr[@]op}`**: `[*]` is the joined-scalar
  vector context. `[*]` plus an operator applies to the joined
  scalar -- a single transformation, single result. `[@]` plus an
  operator applies per element. The asterisk-vs-at convention is
  preserved from bash/zsh.

- **Per-element does not mean per-byte.** A pattern strip like
  `${arr[@]##c}` removes a leading `c` from *each element*, not
  every `c` from each element. For per-byte, the operator stays the
  same; for "every occurrence," the replace form `${arr[@]//c}` is
  what you want.

- **The `(@)` flag is a presentation no-op.** `${(@)arr}` and
  `${arr[@]}` mean the same thing. The flag is accepted as a
  spelling alias, not a redundant separate feature -- it doesn't
  alter the operator's behavior. (Per SEMANTICS §3.7.)

- **Map iteration order is insertion order.** A `${m[@]^^}` over a
  map fires on values in the order keys were assigned to the map.
  Use `${m[@]@Q}` to see both keys and values quoted if order
  matters for downstream processing.

## See also

- `docs/SEMANTICS.md` §3.5 (transformation always fires), §3.9 (no
  implicit list-to-string coercion).
- `docs/features/sigil-conventions.md` -- the `@x` lush-mode
  shorthand for the `${x[@]}` form covered here.
- `docs/EXTENDED_SYNTAX.md` -- the full parameter-expansion
  operator reference. The catalogue described here applies on top
  of the operator set documented there.
