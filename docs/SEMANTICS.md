# Lush Semantics

**The engine-layer contract: what a lush value *is*, and how a name resolves.**

**Status**: Foundational engine specification. The decisions recorded
here are settled; revising them requires an explicit owner decision,
the same bar as `VISION.md`.

**Scope**: this document specifies the value model and the scoping
discipline -- the core of how lush evaluates. It is deliberately not
yet exhaustive (S8 records what is still open); it grows toward a
complete semantic specification.

This document is the concrete form of `PHILOSOPHY.md` S2 ("Spelling is
polyglot; behavior is canonical lush"). PHILOSOPHY states the principle
abstractly -- one unified engine behind many dialect spellings. This
document specifies that engine: the kinds of values it holds, the rules
by which they are transformed and presented, and the discipline by
which names resolve to them.

It is a working contract, not marketing. Every expansion-, array-, and
scope-related implementation choice should be measurable against it.

---

## 1. The two layers: engine and preset

Lush has exactly two layers, and they are never the same layer.

**The preset layer** is the four configuration surfaces of
`PHILOSOPHY.md` S3 -- `mode`, `set`, `setopt`/`shopt`, `config`. It
governs *which syntax is accepted* and *which behaviors default on*.
It is curation over a substrate.

**The engine layer** is the substrate itself: what a value fundamentally
is, how a transformation acts on it, how it is presented at a boundary,
and how a name resolves to it. This document specifies the engine.

The dividing rule is absolute:

> The preset layer configures the engine. It never redefines the
> engine. No `mode`, no `setopt`, no `config` key changes what a value
> *is*, the presentation rule, or the scoping discipline. What a preset
> *may* select is a **boundary policy** -- what happens when a value of
> one kind reaches a slot expecting another -- exactly as it selects
> word splitting.

This is not a stylistic preference; it is what makes lush one shell.
A preset that could fork the type system or the name-resolution rule
would not produce a polyglot shell -- it would produce N shells
sharing a parser. "Polyglot" means many syntactic front doors onto
*one* engine (PHILOSOPHY S2: "not because it runs three engines under
the hood"). The **value model** -- the kinds (scalar/list/map), the
`[@]`-vs-`[*]` presentation, name resolution, scoping -- is uniform
across every mode, by construction. Whether a kind mismatch at a
boundary is **diagnosed** (a type error) or **reconciled to the oracle**
(a silent flatten) is a preset (S3.9): a list flattened by `mode bash`
was still a list at the point of reference -- the flag changed only the
crossing policy, not the value's nature.

When a behavior feels like it could belong to either layer, apply the
test: *does it change what a value is, or only how a value is spelled
or defaulted?* The former is engine; the latter is preset. Word
splitting (S3.8) is a preset because it gates an expansion behavior. The
list/scalar *distinction* (a list is a list) is engine; the *boundary
policy* for a list meeting a scalar slot -- strict type error vs oracle
flatten -- is a preset (`FEATURE_STRICT_VALUE_TYPING`), strict by default
in lush mode (its flagship safety feature) and relaxed to the oracle in
the compatibility modes. Revised per explicit owner decision (2026-07):
the value-nature is engine, the crossing policy is preset.

---

## 2. Pure-local reasoning

Every expansion, every function, every construct must be understandable
*from itself* -- without consulting the statement that encloses it or
the mode that is active.

A reader (human, static analyzer, or the interactive debugger) looking
at `${arr[@]}` knows it is a vector from those four characters alone.
Looking at a function's declaration line, they know its scoping
discipline. They never have to scan outward to a wrapping quote, an
enclosing assignment, or a `mode` setting to know what a fragment
means.

Pure-local reasoning is the property the rest of this document is
built to preserve. Where a design choice would make meaning depend on
surrounding context, the choice is wrong. This is why presentation is
bound to the subscript and not to quoting (S3.5), and why scoping is
bound to the declaration form and not to the mode (S5).

---

## 3. The value model

### 3.1 Three value kinds

A lush value is exactly one of:

- **scalar** -- a single string.
- **list** -- an ordered sequence of scalars (the indexed array).
- **map** -- an ordered set of key/scalar pairs (the associative
  array).

Lists and maps are **first-class values**, not text that happens to be
interpreted as structure. A variable holds a value of one kind; the
kind is part of the value.

### 3.2 Bounded and flat

The value model is **bounded**: there are three kinds and no more.
It is **flat**: a list element is a scalar, a map value is a scalar.
A list of lists, a map of maps, a list of maps -- none of these are
native values.

This is a deliberate fork away from the fully-nested structured-data
model (Nushell, Elvish). The reasoning:

- **The serialization trap.** External commands understand only
  byte streams. A fully-nested value crossing a pipe into `grep`,
  `awk`, or `sed` must be flattened to text, and the shell would have
  to *guess* how. A guess is implicit coercion -- the precise thing
  S3.4 forbids. A bounded model makes every text-boundary crossing
  total and obvious: a flat list maps to an argument vector or to
  lines; a flat map maps to key/value lines or to environment
  variables. Nothing is guessed.
- **Debugger sanity.** Lush ships an interactive debugger. A bounded
  model renders runtime state as flat rows -- low, constant cognitive
  load while stepping. A nested model forces tree rendering and makes
  mutation- and scope-tracking across multidimensional state vastly
  harder to keep correct.
- **Shell-first ergonomics.** A shell's job is executing programs and
  stitching them together. A fully-nested type system pulls the
  language toward expression-heavy, Python-shaped syntax. Bounded
  keeps lush close to the Unix shore: it delivers exactly what bash
  and zsh reached for -- first-class, space-safe arrays and native
  maps -- without the structural pitfalls.

### 3.3 Strict flatness; depth via explicit serialization

Nesting a structured value inside another is never implicit. An
attempt to do so is either a diagnosed error or an explicitly
operator-driven flatten -- never a silent restructuring.

When a script genuinely needs depth, it reaches for it explicitly,
through the **utility layer**, not the type system: serialized strings
(JSON, etc.) handled by named helpers (e.g. `json.get(var,
"path.to.key")`). The native type system stays small and total; reach
lives in functions. This is the same move as preferring an explicit
`split()` over an implicit coercion -- power through named, visible
operations rather than through type-system complexity.

### 3.4 No implicit coercion: lists are never silently flattened (lush mode)

In lush mode, a list is never converted to a scalar string implicitly
-- not by a double quote, not by an assignment, not by reaching a
string-shaped slot. Joining a list into a string is always an operation
the script *asks for* (the `[*]` subscript of S3.5, an explicit `join`,
a `(j:)` flag).

Implicit list-to-string coercion is one of the largest sources of
silent bugs and quoting gymnastics in legacy shells. It violates least
surprise (a list the author built silently stops being a list), and it
hides type errors (passing a list where a scalar was meant should be a
clear, immediate diagnostic, not a quiet flatten).

This is lush mode's central safety property -- its flagship type
guarantee. Everything in S3.5 and S3.6 exists to uphold it. The
compatibility modes (`bash`, `zsh`, `posix`) *relax* it back to the
oracle's silent flatten so that legacy scripts run unchanged; this is a
**boundary policy** selected by the preset (S3.9,
`FEATURE_STRICT_VALUE_TYPING`), not a change to what a value is -- a
list is a list in every mode, and the diagnostic-vs-flatten choice is
the only thing the mode moves.

### 3.5 Transformation and presentation are orthogonal

Two independent things happen to a value reference, and they never
interfere:

**Transformation** -- what a parameter flag *does* to the data: `(o)`
sort, `(O)` reverse-sort, `(u)` unique, `(s:x:)` split, `(U)`
uppercase, and so on. A transformation flag **always fires**,
regardless of how the reference is written or quoted. `(o)` always
sorts.

**Presentation** -- whether the reference yields a **vector** (N
distinct words) or a **scalar** (one string). Presentation is
determined **solely by the subscript** on the reference:

- `${arr[@]}` -- vector. The elements, distinct, one-to-one.
- `${arr[*]}` -- scalar. The elements joined into a single string
  (joined by the first character of `IFS`, as POSIX `"$*"` already
  does).

The two compose cleanly:

| Reference        | Transformation | Presentation        |
|------------------|----------------|---------------------|
| `${arr[@]}`      | none           | vector              |
| `${arr[*]}`      | none           | joined scalar       |
| `${(o)arr[@]}`   | sorted         | vector              |
| `${(o)arr[*]}`   | sorted         | joined scalar       |

The transformation is uniform; the subscript alone selects the shape.
This ratifies the existing documented behavior (`known_divergences.txt`
entries 301, 307, 314): lush applies flags uniformly, and the `[@]`
subscript -- not quoting -- marks a vector.

### 3.6 Double quotes are a whitespace anchor, not a type operator

In legacy shells the double quote is overloaded: it means both "treat
this whitespace-containing string as one field" and "flatten this
array" (or, with `[*]` vs `[@]`, "join or keep distinct"). Lush
removes the second meaning entirely.

A double quote does exactly one thing: it guarantees a **scalar** is
carried as a single field, immune to word splitting. It has **no
effect on a list's structure**. `${arr[@]}` is a vector whether
quoted or not; `${arr[*]}` is a joined scalar whether quoted or not.
Quoting and presentation are independent axes.

The consequence is zero quoting footguns of the legacy kind: omitting
a quote can never silently turn a list into a merged scalar, and the
structural intent of a reference is baked into the reference itself
(`[@]` vs `[*]`), not into the syntax that happens to wrap it. This is
S2 (pure-local reasoning) applied to expansion.

This rule governs *list structure* on the `$` / `[@]` / `[*]` forms.
It does **not** apply to the bare `@name` / `%name` kind sigils, which
are a separate surface (S3 value kinds). Those sigils are
bare-word-only and quote-suppressed, like `~` tilde expansion: `echo
@arr` expands but `echo "@arr"` is the literal `@arr`, just as `~`
expands but `"~"` does not. This keeps double-quoted strings safe for
literal `@`/`%` text -- `printf "%s\n" "$x"`, `"user@host"`,
`"100% off"` -- and is what keeps bare and quoted readings consistent
with the word-start-only bare rule. See
`docs/features/sigil-conventions.md`.

### 3.7 The `(@)` flag is redundant

zsh's `(@)` parameter flag forces array context. In lush the `[@]`
subscript already *is* the vector marker, unconditionally and locally.
`(@)` is therefore not load-bearing and not part of the engine. If it
is accepted at all, it is accepted only as a polyglot **spelling**
courtesy (PHILOSOPHY S2) routing to the same presentation as `[@]`. It
carries no semantics that `[@]` does not already carry.

### 3.8 Word splitting is retained as a preset

S3.4 forbids implicit list-to-**string** coercion. It does *not*
forbid string-to-**list** word splitting (`for f in $files`). These
are different operations, and only one is a silent engine coercion:

- list-to-string is silent, destroys structure irreversibly, and is
  triggered by nothing visible. The engine forbids it.
- word splitting is triggered by a *visible* syntactic choice (leaving
  an expansion unquoted) and is already a curated preset --
  `FEATURE_WORD_SPLIT_DEFAULT`, on in POSIX/bash modes, off in zsh mode
  **and in the lush default profile**.

The asymmetry is principled: one is the engine silently lying; the
other is a configurable, syntactically-requested expansion. Word
splitting stays exactly as it is, governed by the feature matrix.

**Curation.** bash and dash split an unquoted non-empty expansion on
`IFS`; zsh does not. lush curates the zsh behavior as its default, so
`x="a b c"; cmd $x` passes **one** argument (`a b c`), not three. This
is a documented divergence from bash/dash, reachable in the other
direction via `set -o posix` / `mode bash` (or `setopt sh_word_split`),
which turn `FEATURE_WORD_SPLIT_DEFAULT` on.

**Command substitution splits under its own preset.** An unquoted
`$(cmd)` / `` `cmd` `` splits on `IFS` when `FEATURE_CMDSUB_WORD_SPLIT`
is on -- a *separate* flag from `FEATURE_WORD_SPLIT_DEFAULT`, because the
two diverge by mode: bash, zsh, and dash all split command output, but
zsh alone does **not** split a bare `$var`. lush's default profile turns
`FEATURE_CMDSUB_WORD_SPLIT` off as well, so `set -- $(echo a b c)` passes
**one** argument, exactly like `set -- $x` -- one consistent mental model
(no implicit `IFS`-driven splitting of command output, S4.1). The compat
modes restore each reference's behavior: `mode bash` / `mode posix` split
both a bare `$var` and a command sub, `mode zsh` splits only the command
sub. Explicit splitting stays available via an array literal
`arr=( $(cmd) )` or a native splitter (S4.1).

**Null-word removal is a separate operation and is always on.** Word
splitting turns a *non-empty* value into several words; null-word
removal turns an *empty* one into *none*. An unquoted expansion that
produces the empty string contributes **zero** words: `$x` with `x=""`
is a null command (exit 0, no "command not found"), and `cmd $x` passes
no argument. A **quoted** empty (`"$x"`, `''`) stays **one** empty word.
Unlike word splitting this is bash/zsh/dash consensus (POSIX
S2.6.5 field splitting removes a wholly-empty unquoted field), so it is
the lush default in every profile, independent of
`FEATURE_WORD_SPLIT_DEFAULT`. The removal keys on the empty string
alone, never on whitespace -- so a whitespace-only value under the
no-split default stays one word (matching zsh), rather than collapsing
to zero (bash/dash). This rule applies uniformly in every
word-collecting position: the command name, command arguments, `for`
and `select` word lists, and array literals.

### 3.9 List and map values meeting a position

S3.4 forbids implicit list->string coercion; S3.5 binds presentation to
the subscript. This section completes the model: what a list or map
value does when it reaches a position, and what is forbidden outright.

**Mode gating (the boundary policy is a preset).** The strict behavior
described below -- the type error a list or map raises when it reaches a
scalar-requiring slot or violates the whole-word constraint -- is lush
mode's default and its flagship safety feature. It is gated by
`FEATURE_STRICT_VALUE_TYPING`: **on** in lush mode, **off** in the
`bash`, `zsh`, and `posix` compatibility modes, where the same crossing
is *reconciled to the oracle* instead of diagnosed. This is the S1
engine-vs-preset split in action: the value model is uniform (a list is
a list in every mode; `[@]`/`[*]` presentation is unchanged); only the
**boundary policy** -- diagnose vs silently flatten -- moves with the
mode, exactly as word splitting (S3.8) does. Under the relaxed policy
the flatten follows the oracle precisely: a `${arr[@]}` in a scalar slot
joins on a literal space (bash/posix, IFS-independent) or on IFS[0]
(zsh); a bare `${arr}` yields element 0 (bash/posix) or the whole array
joined on IFS[0] (zsh); a map yields its values in insertion order.
`${arr[*]}` / `$*` are scalar in every mode and always join on IFS[0] --
never gated. A **named list or map in the command-name position** is the
same crossing: strict in lush mode (a type error -- positionals are argv,
a named list is not), relaxed in the compat modes to the oracle spread
(bash/zsh expand `${cmd[@]}` into command words; bash reads a bare
`${cmd}` as element 0, zsh spreads it). **`export name=(...)` and
`readonly name=(...)`** are the same crossing on the write side: a type
error in lush mode, relaxed in the compat modes to the oracle, which binds
an indexed array with the export/readonly attribute (an array is not an
environment string, so the export attribute is a no-op for the process
environment, exactly as in bash). The **vector-producing parameter-flag
forms** -- `${(v)a}` (values), `${(k)a}` (keys), `${(kv)m}` (pairs),
`${(@)a}` (the `[@]` alias), and `${(s:x:)a}` splitting a collection -- are
likewise the crossing: they spread in a vector slot (`try_expand_vector_arg`)
but in a scalar slot they raise the type error in lush mode and flatten to
the oracle (space / IFS[0]) in the compat modes, matching `${a[@]}`. The
explicit join `${(j:x:)a}` is the sanctioned list-to-scalar collapse and is
never the error; `${(s:x:)str}` splitting a *scalar* is a legitimate
list-from-scalar and is not gated. The rest of this section describes the
strict (lush-mode) model; each strict clause below reads "in lush mode"
implicitly.

**Scalar operators on a bare collection.** A scalar parameter operator
applied to a bare collection name -- `${arr:-default}`, `${arr#pat}`,
`${arr^^}`, `${arr:o:l}`, `${arr@Q}`, and the rest -- is a scalar-slot
crossing and is gated by the same flag. In lush mode it is the type error
(use `${arr[0]:-default}` for an element operation or `${arr[@]op...}` to
vectorize). In the compat modes it is relaxed by the uniform rule
*flatten the collection to its mode scalar (bash/posix element 0, zsh
whole-join), then apply the operator to that scalar* -- which is exact
bash element-0 parity for every operator, and exact zsh parity for every
operator in a scalar (quoted) slot. Two zsh behaviors are deliberately
**not** cloned, and are curated to the flatten-then-apply reading (see the
divergence registry, S6):

- zsh reads bare `${arr:o:l}` as *element* slicing (`${arr:1:2}` = two
  elements); lush keeps it a scalar substring of the joined value. The
  explicit `${arr[@]:o:l}` is the element-slice spelling, so lush does not
  add a second implicit one.
- zsh's bare-name pattern operators are element-wise in an unquoted
  context and its `${arr:=word}` collapses the array to one element; lush
  applies the operator once to the joined scalar and, for `:=`/`=`, writes
  element 0 while preserving the array shape (the less-destructive bash
  rule), in both compat modes.

`${#arr}` is not one of these operators -- it is length/count and keeps
its own curation (bash/posix: codepoint length of element 0; zsh/lush:
element count).

**The whole-word constraint.** A vector-yielding expansion -- a bare
`${arr}` that resolves to a list/map, `${arr[@]}`, or a
vector-producing map operator (`(k)`, `(v)`, `(kv)`) -- must occupy
the *entire* whitespace-delimited word it appears in. It may not be
glued to literal text or to another expansion within a single word.

- Permitted: `${arr[@]}`, `"${arr[@]}"` -- the expansion is the whole
  word's content (the quotes are a whitespace anchor, S3.6, and do not
  change this).
- Forbidden: `x${arr}`, `"prefix_${arr[@]}"` -- a list glued to text.
  Gluing a list to a string has no coherent meaning; allowing it would
  force an implicit flatten, which S3.4 forbids.

String concatenation is therefore *not* a slot category -- it is a
within-word phenomenon governed entirely by this constraint. To build
a string from a list, join it explicitly (`${arr[*]}`, an explicit
join) and concatenate the resulting *scalar*.

The constraint is enforced at **runtime**: whether an expansion is
vector-yielding depends on the variable's value, which is runtime
state -- `x${arr}` is ordinary concatenation when `arr` holds a scalar
and a violation when it holds a list. (An explicit `[@]` glued to text
is statically detectable, and an implementation may diagnose it early,
but the guarantee is defined at runtime, uniform with the slot check
below.)

**Slot context.** Every position an expansion can occupy is either
vector-accepting or scalar-requiring:

| Slot category    | Positions |
|------------------|-----------|
| Vector-accepting | argv (command arguments); the command-name position (positional-parameter vectors only -- see below); array initializer `( ... )`; for-loop word list |
| Scalar-requiring | variable assignment RHS; `case` word (`case $x in`); redirect target (`>`, `>>`); here-string (`<<<`); arithmetic operand `$(( ... ))`; conditional-expression operand `[[ ... ]]` |

This table states the model's intent; the engine must codify the
*complete* enumeration of positions. It is a living classification,
not yet exhaustive here.

A list or map value -- bare `${arr}`, or a vector-producing operator
-- is valid in a vector-accepting slot: it contributes its elements,
exactly as `${arr[@]}` does. In a scalar-requiring slot it is a type
error. `${arr[*]}` and explicit joins produce a scalar and are valid
in scalar-requiring slots.

**The command-name position is a curated exception.** It is
vector-accepting for the **positional-parameter** vectors -- `$@` /
`"$@"` / `$*` / `${@}` -- which are argv, not a lush list-kind value:
`set -- ls -la; "$@"` runs `ls -la`, and an empty positional set is a
null command (exit 0). But a **named list or map** value in the
command-name slot -- `"${arr[@]}"`, bare `${arr}`, a vector-producing
flag form `"${(v)arr}"` / `"${(k)m}"` -- is not a command vector, so it
is never silently spread into a command word: it joins to one word (and
is looked up whole) or, for the `[@]` forms, raises the type error. This
keeps the value model strict. bash and zsh
expand a named array in command position; lush curates the type error,
so an array is executed as a command only through an explicit form
(`"${arr[@]}"` as *arguments*, or a joined scalar name). The `*`
subscript is scalar in either case: `"$*"` / `"${arr[*]}"` join to one
word, so `set -- echo hi; "$*"` looks up the single command name
`echo hi`.

For a **map**, "its elements" are its **values** in insertion order --
`${m}`, `${m[@]}`, and the vector sigil `@m` all contribute the values,
matching `${arr[@]}` for an associative array in bash and zsh (lush fixes
the insertion order rather than a hash order). The other projections are
explicit: `${(k)m}` / `${(v)m}` / `${(kv)m}` yield the keys, the values,
and interleaved `key value key value ...` pairs; the pair sigil `%m` is
the same key-value interleave. `${m[*]}` joins the values to a scalar.

**Quotes do not flatten.** Slot category is set by the surrounding
syntactic position, not by the quotes around the expansion. A
vector-yielding expansion inside double quotes is still a vector
when it sits in a vector-accepting slot:

```
arr=(alpha beta gamma)
copy=("${arr[@]}")           # array initializer slot, splices flat
                             # copy is 3 elements, not "alpha beta gamma"

parts=("${(s/:/)str}")       # split-yielded list, splices flat
uniq=("${(u)other[@]}")      # uniq-yielded list, splices flat
```

Quotes are a whitespace anchor (S3.6); they preserve the grouping of
each individual element, but they do not coerce a list into a scalar.
The principle is data topology (list vs. scalar) versus evaluation
context (preserve word boundaries inside each element): these are
orthogonal axes, and the engine treats them as such.

**Type-mismatch diagnostic.** A list or map value that reaches a
scalar-requiring slot, or that violates the whole-word constraint, is
a runtime type error. The engine halts the offending evaluation and
emits a diagnostic through the structured-error system, the source
span on the offending expansion:

```
error[E_TYPE]: type mismatch -- expected scalar string, got list
  --> script.lush:12:13
   |
12 | msg="prefix ${my_list}"
   |             ^^^^^^^^^^ list value in a scalar within-word position
   |
   = help: join the list explicitly to place it in a string position --
           ${my_list[*]} for space-joining, or an explicit join.
```

`E_TYPE` is a placeholder; the real code is assigned from lush's
structured-error registry, not invented here.

**Enforcement.** The check is runtime (a variable's kind is runtime
state), and it is active only when `FEATURE_STRICT_VALUE_TYPING` is on
(lush mode). In a script, a type mismatch aborts with a non-zero exit
before the bad value can reach a downstream command. Interactively,
the diagnostic prints to stderr, the current command line is
abandoned, and control returns cleanly to the prompt and the
debugger -- the session is not killed. In the compatibility modes the
check is off and the value flattens to the oracle's result instead
(see *Mode gating* above).

This makes the bare `${arr}` fully defined: it is the first-class
list (or map) value itself; `[@]` / `[*]` are operators *on* that
value, not type switches; the value flows into vector-accepting
positions and is a diagnosed type error in scalar-requiring ones.
There is no implicit join, ever.

**The write-side mirror: a list operation on a scalar-kind variable.**
S3.9 above governs a list or map *value* reaching a scalar *slot*. The
mirror is a list *operation* -- an element write `s[i]=v`, an append
`s+=(...)`, or the arithmetic writer `(( s[i]=v ))` -- applied to a
variable that currently holds a *scalar* value. This is a kind transition
(scalar -> list), gated by the same `FEATURE_STRICT_VALUE_TYPING` flag. In
lush mode it is the type error (`E1134`): a scalar is not a list, and the
implicit re-kind is refused with the guidance to `declare -a name` (or
`unset name`) first, exactly as the value-side crossing is refused. In the
compatibility modes the flag is off and the operation *preserve-promotes*:
the variable becomes a one-element list whose base element is the former
scalar value (a scalar is, in the value model, the single element of a
one-element list), and the write then proceeds -- non-lossy, so the
scalar's data is never silently dropped. An explicit write to the base
index overwrites the seeded value; a whole-array assignment (`s=(...)`,
`s=()`) is an explicit re-declaration, not an implicit re-kind, and
replaces in every mode; an *unbound* name is a fresh array with no
existing kind to violate. (POSIX mode has indexed arrays disabled at the
parser, so `s[i]=v` there is not an array operation at all.) The relaxed
preserve-promote is the bash/zsh consensus for append and lush's curated
choice for the bracket write; zsh's alternative -- in-place character
substitution on the scalar string -- is incompatible with lush's
first-class value kinds and is deliberately not adopted. The strict error
is fatal for the assignment-word (`s[i]=v`) and append (`s+=(...)`) forms
-- the command aborts, like the value-side crossing -- while the
arithmetic-writer form `(( s[i]=v ))` reports it through the
arithmetic-command exit status (non-fatal, consistent with other
arithmetic errors such as division by zero); in both the write is refused
and the scalar is unchanged.

### 3.10 Substitution replacements are literal

In `${var/pattern/replacement}` the two halves follow different rules, and the
difference is deliberate.

The **pattern** is a pattern: `\X` means "a literal X", so `${v//\b/-}`
replaces the character `b` and `${v//\*/-}` replaces a literal asterisk. This
is the same escape rule `${v#\a}` and `case` patterns use.

The **replacement** is literal text. A backslash in it is a backslash:

```sh
v=aXb
echo "${v//X/\a}"      # a\ab  -- the backslash survives
```

lush follows zsh here; bash unescapes the replacement. The reason to prefer
the literal rule is that it has no escape rules to remember, so a value
containing backslashes round-trips without the doubling an unescaping rule
forces.

No capability is lost by it. The delimiter is the **first unescaped `/`**, so
every later `/` in the replacement is already literal and needs no escape:

```sh
v=aXb
echo "${v//X//}"          # a/b        -- a literal slash
echo "${v//X//usr/lib}"   # a/usr/libb -- slashes throughout
```

### 3.11 Lists are sparse, in every mode

An index that was never assigned holds no value. Assigning past the end leaves
a gap rather than filling it:

```sh
a=(x); a[9]=y
echo "${#a[@]}"        # 2  -- two elements, not ten
echo "${!a[@]}"        # 0 9
```

This follows from 3.1 and 3.4 rather than from any reference shell. A list is a
sequence of the values it holds; materializing eight empty strings would invent
eight values the script never asked for, and would erase the difference between
"assigned empty" and "never assigned". Sparse is also the more expressive of
the two: a dense array is recoverable from a sparse one by filling explicitly,
and the reverse is not.

**No mode changes this.** Storage is an engine property, and section 1's
dividing rule applies: a preset configures the engine, it never redefines what
a value IS. zsh has no sparse arrays and fills the gap; `mode zsh` still does
not, because a preset that could fork the element count would fork the value
model, which is the thing section 1 rules out. A mode may move the *indexing
base*, because a base is a spelling; it does not move the storage model.

Recorded as a deliberate divergence, not a gap in `mode zsh`.

### 3.12 Pattern matching segments by cluster and tests by codepoint

Two Unicode axes meet in a pattern, and lush keeps them apart.

**Segmentation** is by grapheme cluster: a `?` or a bracket class consumes one
user-perceived character, combining marks included. **Membership** is by
codepoint, tested against the cluster's base:

```sh
v=$(printf 'cafe\xcc\x81')   # NFD: base e + U+0301
echo "${v%[a-z]}"              # caf  -- base e is in a-z; the cluster goes whole

v=$(printf 'caf\xc3\xa9')    # NFC: U+00E9
echo "${v%[a-z]}"              # cafe-acute -- U+00E9 is not in a-z
```

The matcher does **not** normalize. That is not an oversight: lush's
normalization policy is a single canonical form, NFC, established at ingest,
with no NFC/NFD branching downstream. Decomposing inside the matcher would add
exactly that branch, and would make pattern matching normalize where `=` and
`==` do not, so the two surfaces would stop agreeing. It would also break a
range like `[U+00C0-U+00FF]` against NFD text whose base is ASCII.

To ask "is this a letter" without asking "which codepoint is it", use a
category class -- `[[:alpha:]]` is spelling-independent by construction:

```sh
v=$(printf 'caf\xc3\xa9')
echo "${v%[[:alpha:]]}"        # caf
```

Segmenting by cluster is already better than the codepoint matchers in bash and
zsh, which consume only the base and can leave a combining mark orphaned.
Normalizing for membership as well is deliberately declined **in the matcher**.
Being spelling-independent is a worthwhile goal; the place to reach it is the
INGEST layer, where text is brought to one canonical form on the way in (as the
completion path already does), not a decomposition branch inside
`pattern_match.c`. A matcher that decomposed would be the downstream NFC/NFD
branch the single-form policy exists to prevent, and it would leave `=` and `==`
disagreeing with patterns. Improve normalization at the entry points; leave the
matcher single-pass.

### 3.13 A subscript's quoting says how to read it (lush mode)

**Status: approved, not yet implemented.** The enabling work is the quote-context
fix (#695 cluster 1); see the note at the end of this section.

In lush mode the quoting of a subscript states what the subscript IS:

```sh
${a[i]}       # i is an ARITHMETIC EXPRESSION -- evaluate it, use it as an index
${a["i"]}     # i is a LITERAL KEY -- do not evaluate anything
${a['i']}     # likewise
```

#### Why the quoting has to carry this

Today the question "is this subscript an index or a key?" is answered by the
array's KIND -- was the name declared as a list or a map. That is ambient state
the reader cannot see at the point of use, so a reference cannot be understood
from itself. That is precisely what section 2 forbids: a reader looking at
`${a[i]}` should know what it means from those characters, without scanning
outward for a declaration.

The cost is not hypothetical. In issue #780 a map subscript was handed to the
arithmetic evaluator because the code path had no kind branch; the key `ab`
evaluated to 0, the element at index 0 was read, and the length of a map element
came back as 0 for every key. A spaced key raised a parse error out of a
read-only length query. Both are the same root: the meaning of the subscript was
inferred from state rather than stated at the site.

Quoting is the natural carrier because the reader is already using it to say
"these bytes are literal". Extending that to subscripts adds no new syntax and
no new rule to remember -- it makes an existing rule apply where it previously
did nothing.

#### What this is NOT

It is not a fork of the value model. A list is still indexed by integers and a
map is still keyed by strings, in every mode; section 1's dividing rule holds.
What changes is how a REFERENCE states which it wants -- a spelling, in the sense
of PHILOSOPHY section 2. The engine underneath is unchanged.

It is also not a new dequoting rule. The quotes are removed exactly as they are
anywhere else; what is added is that their PRESENCE is information.

#### Modes

The distinction is enforced in lush mode. The compatibility modes reproduce
their own baselines, where quotes inside a subscript are removed and carry no
meaning, so `${a["i"]}` and `${a[i]}` are the same reference there. A script
written for another dialect keeps working under its own mode; a script written
for lush gets a reference that can be read locally.

#### Blocked on quote context

This cannot be implemented until quote context survives the `${` scan. Today it
does not: in a double-quoted word, an inner `"` terminates the enclosing span, so

```sh
declare -A m; m["a b"]=9
set -- "${m["a b"]}"; echo $#      # 2 -- the word SPLIT, and the text is literal
```

the subscript's quotes never reach the expander to mean anything. That is
issue #695 cluster 1. This section is recorded now so the quote-context work is
built knowing what the quoting will be asked to carry, rather than having this
grafted on afterwards.

---

## 4. The three boundary rules

The engine holds typed values; the world outside holds byte streams.
These three rules govern every crossing.

### 4.1 Text into the engine: capture as scalar, split explicitly

The output of an external command is captured as a **scalar string**.
Command substitution strips trailing newlines (POSIX behavior,
retained). It is *not* implicitly split into a list -- that would be
implicit coercion.

Converting captured text to a list is always explicit. The one native,
blessed splitter is **newline** (the `(f)` flag / a `lines` helper),
because lines-of-text is the actual Unix interchange convention and it
is whitespace-safe. A trailing newline is a *terminator*, not a
separator: `"a\nb\n"` splits to two elements, not three. Other shapes
(custom delimiter, NUL-delimited, CSV, JSON) are explicit helpers or
flags. In the lush profile there is no implicit `IFS`-driven splitting of
command output -- gated by `FEATURE_CMDSUB_WORD_SPLIT` (off in lush, on
in the bash/zsh/posix compat modes; see S3.8), so `set -- $(echo a b c)`
yields one argument, the same as `set -- $x`.

### 4.2 Maps preserve insertion order

A map enumerates in the order its keys were first inserted. Order is
deterministic, never hash-bucket order. This is implemented by building
`array_value_t.assoc_map` as an insertion-ordered libhashtable table
(Issue #69) and is documented as a deliberate divergence
(`known_divergences.txt` entry 052); this model ratifies it.

Insertion order is the modern consensus (Python 3.7+, Ruby, JS) and
the only choice compatible with S2 -- a map that enumerated
nondeterministically would render differently in the debugger on every
step. Sorted *views* are available on top via `${(ko)map}` /
`${(kO)map}`: a deterministic default, plus explicit tools to reorder.

### 4.3 The engine into argv: every slot preserved

When a list is expanded into an external command's argument vector,
the invariant is:

> A list's element count equals the number of argv slots it
> contributes.

A three-element list contributes three arguments, in order. An empty
element is preserved as a zero-length argument -- it is not dropped,
and it never shifts the position of later arguments. An *empty list*
contributes zero arguments; a one-element list whose element is the
empty string contributes one (empty) argument. `${#list[@]}` always
predicts the slot count exactly.

Dropping an empty element would be the engine silently mutating the
data. A script that wants empties removed does so explicitly, with a
filter transformation -- never as a side effect of expansion.

---

## 5. The scope model

### 5.1 Scoping is a property of the declaration form, not the mode

How a name resolves to a value is engine-layer (S1) -- it cannot be
forked by a preset. But lush supports two function-declaration forms,
and **each form carries its own scoping discipline**, uniformly in
every mode.

### 5.2 POSIX-form functions: dynamic scoping

A function declared in POSIX form -- `name() { ... }` or `function
name { ... }` -- is **dynamically scoped**, in every mode including
lush mode. Its locals are visible to the functions it calls. This is
the behavior the inherited shell ecosystem expects; scripts in that
form keep it.

### 5.3 Lush typed-function form: lexical scoping

A function declared in lush's typed-function form (see S7 -- this form
is not yet built) is **lexically (block) scoped**, in every mode
including POSIX mode. Name resolution is determined by the program's
block structure, not by the dynamic call chain. There is no
cross-function variable pollution; the debugger and analyzer can
resolve a name's binding from the program text alone.

### 5.4 One engine, both disciplines, self-describing code

The engine supports both disciplines as first-class. A function's
declaration form states which one applies; a reader determines a
function's scoping from its own header line, with no reference to mode
or caller -- S2 again. A single script may contain a dynamically
scoped POSIX-form helper and a lexically scoped typed function side by
side, each coherent, because each is self-describing.

Lexical scoping is therefore not a mode and not a bolt-on -- it is a
property the typed-function form *carries*. A consistency dividend
from S3.8: dynamic scoping's one genuinely-used feature in legacy
shells -- a caller setting `local IFS` to steer a callee's splitting --
is already largely moot, because implicit `IFS` splitting is no longer
the default path.

### 5.5 Environment variables are a separate axis

Exported environment variables are process-level: inherited by child
processes, always, by POSIX rule. They are *not* governed by this
scope model. The lexical/dynamic choice governs only **shell-local
(non-exported) variable** resolution. "Functions share the process
environment" and "functions isolate their local variables" are two
different statements about two different axes; do not conflate them.

---

## 6. Relationship to the preset layer and the divergence registry

The four preset surfaces (`mode`, `set`, `setopt`/`shopt`, `config`)
govern dialect spelling and curated defaults. They never govern the
value kinds (S3.1), the transformation/presentation split (S3.5), or
the scoping discipline (S5). Those are engine. What they *may* govern is
a **boundary policy** -- what happens when a value crosses into a slot
of another kind -- which is a behavior, not a kind.

Two legitimate presets of this form exist:

- `FEATURE_WORD_SPLIT_DEFAULT` gates an expansion *behavior*, not a
  value *kind* (S3.8).
- `FEATURE_STRICT_VALUE_TYPING` gates the boundary policy for a
  list/map reaching a scalar slot (S3.4, S3.9): a diagnosed type error
  (lush mode) versus an oracle flatten (compat modes). The value is a
  list in both cases; only the crossing policy differs. This was an
  explicit owner decision (2026-07) recorded here so the S3.4/S3.9
  strictness is understood as lush mode's flagship default, not an
  engine invariant.

Deliberate divergences from bash/zsh are recorded in
`tests/fuzz/differential/known_divergences.txt` with rationale, and
honored by `diff_oracle` (an allow-listed divergence is reported but
does not count as a failure). **This document is the principle behind
those entries.** A deliberate, developer-reasoned divergence is a
feature of lush, not a debt -- it is recorded, justified, and honored,
never silently "corrected" toward a reference shell. Entries 052, 301,
307, and 314 are ratified by this model.

A guard for tooling built on the corpus: differential testing against
bash/zsh is excellent at catching cases where lush is *unambiguously
wrong*, but it cannot, by mechanism, distinguish "wrong" from
"deliberately different." A divergence from a reference shell is only a
defect if it contradicts *this document*. If it is consistent with
this document, it belongs in `known_divergences.txt`, not in a bug fix.

---

## 7. Current implementation vs. this model

This model is the **vision**. The following is the **reality** as of
this writing, so the two are aligned and the gap is never lost. (Gap
sizes are rough engineering estimates.)

| Area | Current state | Target | Gap |
|------|---------------|--------|-----|
| Value kinds | scalar/list/map all live in per-scope `vars_ht` as kind-tagged `symvar` entries; the previous global `array_storage` side-table is removed; `symtable_lookup` returns a kind-tagged view (`lush_value_view_t`) | scalar/list/map are first-class throughout the expansion engine | **match** |
| Flatness / nesting | none (a list element is always `char *`) | none (bounded, S3.2) | **match** |
| Map insertion order | implemented -- insertion-ordered `array_value_t.assoc_map` (Issue #69) | insertion order (S4.2) | **match** |
| Transformation always fires | yes (`known_divergences.txt` 301/314) | yes (S3.5) | **match** |
| Presentation by subscript | yes -- `${arr[@]}` vector, `${(flags)arr}` scalar (`known_divergences.txt` 307) | yes (S3.5) | **match** |
| Quoting irrelevant to presentation | `parse_parameter_expansion` does not receive quote context; `expand_quoted_string` tracks `in_double_quotes` but does not thread it down | presentation must NOT consult quote context (S3.6) -- so the un-threaded state is *correct*, not a gap | **match** |
| Implicit list-to-string | `${arr[@]}` in a scalar slot raises `SHELL_ERR_TYPE_MISMATCH` and aborts the script (`executor.c` general parameter-expansion fallthrough). Bare `${arr}` enforced the same way: vector slot yields N elements, scalar slot raises type mismatch, glued-to-text raises type mismatch. `export` and `readonly` raise on list values rather than silently joining. | no implicit coercion (S3.4, S3.9) | **match** |
| `(@)` flag | accepted as a no-op spelling alias for `[@]` presentation in `try_expand_vector_arg` (`${(@)arr}` yields the same as `${arr[@]}`) | redundant; at most a spelling alias (S3.7) | **match** |
| Array-literal discrimination | parser-internal `\x1F` sentinel prefix on unquoted `name=(...)` argv elements lets `local`/`declare`/`typeset` route to array-literal handling while `local data="(scoped)"` (which strips quotes to the same shape) correctly stays a scalar | a parsed `local arr=(a b c)` must build a real array, distinct from `local data="(...)"` | **match** |
| Word splitting | `FEATURE_WORD_SPLIT_DEFAULT`, per-mode | retained as a preset (S3.8) | **match** |
| Typed-function form | declaration grammar implemented -- `fn name(p: kind, ...) [-> kind] { body }` parses to NODE_FN_DECL; call expression, typed `return`, and `let name = call(args)` capture in flight. The legacy `return_value` builtin and its `__LUSH_RETURN__` marker scanner were removed; the typed form is the only path to a structured return value | a typed form carrying lexical scope (S5.3) | parser surface landed; runtime + lexical resolution + debugger surface remain |
| Scoping | dynamic scope-chain for all functions (`symtable` walks `scope->parent`; issue #47 assignment semantics) | dynamic for POSIX form, lexical for the typed form (S5) | large -- lexical resolution and the typed form both unbuilt |

The two "large" gaps -- the typed-function form and lexical scoping --
are coupled and are recorded in S8 as the next pieces of work. The
six value-model rows that previously sat at medium / small / "one
targeted fix" have all landed: `tests/real_world/` runs at 100% (20
passes + 2 principled `known_divergences.txt` entries), and the
storage layer is unified (the array side-table is gone; local
arrays die with their function scope; `[[ -v arr ]]` returns true
on arrays; `${!ref}` no longer leaks pointer bytes).

---

## 8. Deliberately deferred

Recorded here so they are not lost. These are *not* decided by this
document; they are to be decided against it, as their own work.

- **The typed-function form.** Its syntax and parameter declaration
  landed; the call expression, the typed `return EXPR` statement, the
  `let name = call(args)` capture form, and the lexical-scope
  resolution pass remain to land. The legacy `return_value` builtin
  was retired at the start of this work; the typed form is the only
  path to a structured return value.
- **Error catalog for misapplied transformations** -- e.g. an
  array-only flag applied to a scalar. The principle is settled (an
  immediate, clear diagnostic); the exhaustive catalog is not.

---

## See also

- [VISION.md](VISION.md) -- what lush is.
- [PHILOSOPHY.md](PHILOSOPHY.md) -- the founding principles; S2 and
  S3 are the abstract form of S1 here.
- [CONFIGURATION.md](CONFIGURATION.md) -- the four preset surfaces.
- `tests/fuzz/differential/known_divergences.txt` -- the deliberate-
  divergence registry this document is the rationale for.
