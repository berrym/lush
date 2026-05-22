# Lush Semantics

**The engine-layer contract: what a lush value *is*, and how a name resolves.**

**Status**: Foundational engine specification. The decisions recorded
here are settled; revising them requires an explicit owner decision,
the same bar as `VISION.md`.

**Scope**: this document specifies the value model and the scoping
discipline -- the core of how lush evaluates. It is deliberately not
yet exhaustive (§8 records what is still open); it grows toward a
complete semantic specification.

This document is the concrete form of `PHILOSOPHY.md` §2 ("Spelling is
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
`PHILOSOPHY.md` §3 -- `mode`, `set`, `setopt`/`shopt`, `config`. It
governs *which syntax is accepted* and *which behaviors default on*.
It is curation over a substrate.

**The engine layer** is the substrate itself: what a value fundamentally
is, how a transformation acts on it, how it is presented at a boundary,
and how a name resolves to it. This document specifies the engine.

The dividing rule is absolute:

> The preset layer configures the engine. It never redefines the
> engine. No `mode`, no `setopt`, no `config` key changes what a value
> *is*, the coercion rules, the presentation rule, or the scoping
> discipline.

This is not a stylistic preference; it is what makes lush one shell.
A preset that could fork the type system or the name-resolution rule
would not produce a polyglot shell -- it would produce N shells
sharing a parser. "Polyglot" means many syntactic front doors onto
*one* engine (PHILOSOPHY §2: "not because it runs three engines under
the hood"). The engine is uniform across every mode, by construction.

When a behavior feels like it could belong to either layer, apply the
test: *does it change what a value is, or only how a value is spelled
or defaulted?* The former is engine; the latter is preset. Word
splitting (§3.8) is a preset because it gates an expansion behavior;
the list/scalar distinction is engine because it is a value's nature.

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
bound to the subscript and not to quoting (§3.5), and why scoping is
bound to the declaration form and not to the mode (§5).

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
  §3.4 forbids. A bounded model makes every text-boundary crossing
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

### 3.4 No implicit coercion: lists are never silently flattened

A list is never converted to a scalar string implicitly -- not by a
double quote, not by an assignment, not by reaching a string-shaped
slot. Joining a list into a string is always an operation the script
*asks for* (the `[*]` subscript of §3.5, an explicit `join`, a `(j:)`
flag).

Implicit list-to-string coercion is one of the largest sources of
silent bugs and quoting gymnastics in legacy shells. It violates least
surprise (a list the author built silently stops being a list), and it
hides type errors (passing a list where a scalar was meant should be a
clear, immediate diagnostic, not a quiet flatten).

This is the engine's central safety property. Everything in §3.5 and
§3.6 exists to uphold it.

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
§2 (pure-local reasoning) applied to expansion.

### 3.7 The `(@)` flag is redundant

zsh's `(@)` parameter flag forces array context. In lush the `[@]`
subscript already *is* the vector marker, unconditionally and locally.
`(@)` is therefore not load-bearing and not part of the engine. If it
is accepted at all, it is accepted only as a polyglot **spelling**
courtesy (PHILOSOPHY §2) routing to the same presentation as `[@]`. It
carries no semantics that `[@]` does not already carry.

### 3.8 Word splitting is retained as a preset

§3.4 forbids implicit list-to-**string** coercion. It does *not*
forbid string-to-**list** word splitting (`for f in $files`). These
are different operations, and only one is a silent engine coercion:

- list-to-string is silent, destroys structure irreversibly, and is
  triggered by nothing visible. The engine forbids it.
- word splitting is triggered by a *visible* syntactic choice (leaving
  an expansion unquoted) and is already a curated preset --
  `FEATURE_WORD_SPLIT_DEFAULT`, on in POSIX/bash modes, off in zsh
  mode.

The asymmetry is principled: one is the engine silently lying; the
other is a configurable, syntactically-requested expansion. Word
splitting stays exactly as it is, governed by the feature matrix.

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
flags. There is no implicit `IFS`-driven splitting of command output.

### 4.2 Maps preserve insertion order

A map enumerates in the order its keys were first inserted. Order is
deterministic, never hash-bucket order. This is already implemented
(`array_value_t.assoc_insertion_order`, Issue #69) and already
documented as a deliberate divergence (`known_divergences.txt` entry
052); this model ratifies it.

Insertion order is the modern consensus (Python 3.7+, Ruby, JS) and
the only choice compatible with §2 -- a map that enumerated
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

How a name resolves to a value is engine-layer (§1) -- it cannot be
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

A function declared in lush's typed-function form (see §7 -- this form
is not yet built) is **lexically (block) scoped**, in every mode
including POSIX mode. Name resolution is determined by the program's
block structure, not by the dynamic call chain. There is no
cross-function variable pollution; the debugger and analyzer can
resolve a name's binding from the program text alone.

### 5.4 One engine, both disciplines, self-describing code

The engine supports both disciplines as first-class. A function's
declaration form states which one applies; a reader determines a
function's scoping from its own header line, with no reference to mode
or caller -- §2 again. A single script may contain a dynamically
scoped POSIX-form helper and a lexically scoped typed function side by
side, each coherent, because each is self-describing.

Lexical scoping is therefore not a mode and not a bolt-on -- it is a
property the typed-function form *carries*. A consistency dividend
from §3.8: dynamic scoping's one genuinely-used feature in legacy
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
value kinds (§3.1), the no-coercion rule (§3.4), the
transformation/presentation split (§3.5), or the scoping discipline
(§5). Those are engine.

`FEATURE_WORD_SPLIT_DEFAULT` is a legitimate preset: it gates an
expansion *behavior*, not a value *kind* (§3.8).

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
| Value kinds | `symvar.value` is `char *`; arrays in a side-table `array_value_t`; a `symvar_type_t` tag exists | scalar/list/map are first-class throughout the expansion engine | medium |
| Flatness / nesting | none (a list element is always `char *`) | none (bounded, §3.2) | **match** |
| Map insertion order | implemented -- `array_value_t.assoc_insertion_order` (Issue #69) | insertion order (§4.2) | **match** |
| Transformation always fires | yes (`known_divergences.txt` 301/314) | yes (§3.5) | **match** |
| Presentation by subscript | yes -- `${arr[@]}` vector, `${(flags)arr}` scalar (`known_divergences.txt` 307) | yes (§3.5) | **match** |
| Quoting irrelevant to presentation | `parse_parameter_expansion` does not receive quote context; `expand_quoted_string` tracks `in_double_quotes` but does not thread it down | presentation must NOT consult quote context (§3.6) -- so the un-threaded state is *correct*, not a gap | **match** |
| Implicit list-to-string | the expansion engine still joins lists to strings on some paths | no implicit coercion (§3.4) | real -- needs a full audit of expansion sites |
| `(@)` flag | recognized among parameter flags | redundant; at most a spelling alias (§3.7) | small |
| Word splitting | `FEATURE_WORD_SPLIT_DEFAULT`, per-mode | retained as a preset (§3.8) | **match** |
| Typed-function form | not implemented -- no `fn` keyword, no typed parameters; `return_value` is a builtin emitting a `__LUSH_RETURN__` marker, not a language construct | a typed form carrying lexical scope (§5.3) | large -- form not yet designed |
| Scoping | dynamic scope-chain for all functions (`symtable` walks `scope->parent`; issue #47 assignment semantics) | dynamic for POSIX form, lexical for the typed form (§5) | large -- lexical resolution and the typed form both unbuilt |

The two "large" gaps -- the typed-function form and lexical scoping --
are coupled and are deliberately out of scope for this document; see
§8.

---

## 8. Deliberately deferred

Recorded here so they are not lost. These are *not* decided by this
document; they are to be decided against it, as their own work.

- **The bare, un-subscripted reference `${arr}`** on a list- or
  map-valued variable. `[@]` and `[*]` are defined (§3.5); the
  no-subscript form is not. Candidates: it is the first-class list
  value itself; it is a diagnosed error ("a structured value
  referenced without a presentation subscript"); it defaults to
  `[*]`-style scalar. Current behavior is joined-scalar
  (`known_divergences.txt` 307). To be decided.
- **The typed-function form.** Its syntax, typed parameters, a proper
  `return_value` construct (replacing the marker-hack builtin), and
  the lexical-scope resolution pass. A full design of its own.
- **Pipeline status reporting** -- a modern alternative to `pipefail`
  feeding clean per-stage exit states into the structured-error
  system.
- **LLE real-time variable inspection** -- inspection hooks on the
  command line.
- **Sigil conventions** (`$`, `@`, `%`) once values are first-class.
- **Error catalogue for misapplied transformations** -- e.g. an
  array-only flag applied to a scalar. The principle is settled (an
  immediate, clear diagnostic); the exhaustive catalogue is not.

---

## See also

- [VISION.md](VISION.md) -- what lush is.
- [PHILOSOPHY.md](PHILOSOPHY.md) -- the founding principles; §2 and
  §3 are the abstract form of §1 here.
- [CONFIGURATION.md](CONFIGURATION.md) -- the four preset surfaces.
- `tests/fuzz/differential/known_divergences.txt` -- the deliberate-
  divergence registry this document is the rationale for.
