# Expansion Engine Audit: list-to-string conversion

**Purpose**: ground SEMANTICS.md §3.4 ("no implicit list→string
coercion") against the actual expansion engine. This document maps
every site where a list (indexed array) or map (associative array)
value is converted to a flat string, classifies each as legitimate or
a conformance gap, and produces the work list for §3.4 conformance.

**Point-in-time**: master at `a0442af8` (PR #113 merge), 2026-05-22.
Re-verify file:line references against current code before acting.

**Method**: traced the expansion call graph in `src/executor.c` and
the array join helper in `src/symtable.c`; enumerated every call site
of the join helper and classified by what triggered the join. Manual
verification of the two load-bearing sites (12437, 4789). Coverage is
high for the core expansion paths; §6 lists what was not exhaustively
traced.

---

## 1. The expansion call graph

```
expand_if_needed()            executor.c:5226   unquoted top-level
  -> expand_variable()        executor.c:13610  $VAR / ${...} dispatch
       -> parse_parameter_expansion()  executor.c:11187  ${...} + flags
expand_quoted_string()        executor.c:14750  double-quoted
build_argv_from_ast()         executor.c:4805   command argv assembly
  -> try_expand_vector_arg()  executor.c:4232   [@]/[*]/$@ vector detect
```

A list becomes a string at exactly one helper.

## 2. The join chokepoint

`symtable_array_expand(array_value_t *array, const char *sep)`
(`src/symtable.c:2623`) joins every element of an array into one
`char *`. It is the single point at which a list becomes a string.
Its call sites are therefore the entire audit surface:

| Site | What triggers the join | Classification |
|------|------------------------|----------------|
| `executor.c:4801` (`expand_array_unsubscripted`) | bare `$arr` / `${arr}`, no subscript, zsh/lush mode | **DEFERRED** (§8 of SEMANTICS.md) |
| `executor.c:12437` | `${arr[@]}` **or** `${arr[*]}` -- same branch | **GAP** (the `[@]` half) / legitimate (the `[*]` half) |
| `executor.c:11245`, `11309` | `${(kv)arr}`, `${(k)arr}` zsh flags | legitimate -- explicit flag |

Separately, the positional `$*` join (`executor.c:13804` area) and the
`${(kv)assoc}` key/value interleave (`executor.c:11246`) are
hand-rolled joins, both explicitly requested -- legitimate.

## 3. Finding 1 -- the one real conformance gap

`src/executor.c:12434-12437`:

```c
} else if (strcmp(subscript, "@") == 0 ||
           strcmp(subscript, "*") == 0) {
    // ${arr[@]} or ${arr[*]} - all elements
    result = symtable_array_expand(array, " ");
}
```

`[@]` and `[*]` share one branch and are **both joined into a scalar
string**. Per SEMANTICS.md §3.5:

- `${arr[*]}` → joined scalar. Joining here is **correct**.
- `${arr[@]}` → vector (N distinct elements). Joining here is the
  **conformance gap**: `[@]` must present as a vector, not a string.

Important nuance: `${arr[@]}` *is* already handled correctly as a
vector in command-argument and for-loop positions, via
`try_expand_vector_arg()` (`executor.c:4232`). Line 12437 is the
**general parameter-expansion fallthrough** -- it is reached when
`${arr[@]}` appears outside those vector-aware positions (e.g. inside
a larger expansion, a command substitution, a scalar assignment).
There, `[@]` is wrongly collapsed to a joined scalar.

The corrective shape -- splitting the `@` and `*` cases so `[*]` joins
and `[@]` yields a vector -- is partially coupled to the §8-deferred
question of what a vector does when it genuinely reaches a
scalar-only slot. The split itself is clear; its interaction with
scalar-context fallback should be settled alongside §8.

## 4. Finding 2 -- bare unsubscripted reference (deferred)

`src/executor.c:4789-4803` (`expand_array_unsubscripted`): a bare
`$arr` / `${arr}` on an array variable resolves mode-dependently --
first element in bash/posix mode, space-joined in zsh/lush mode (an
implicit join).

SEMANTICS.md §8 **explicitly defers** the bare-unsubscripted-reference
decision. This is therefore recorded as current behavior, not a
violation to fix now. It is blocked on the §8 resolution; once §8 is
decided, this site is updated to match.

## 5. Conformant -- no action

- `${arr[*]}` -- explicit scalar join (the `*` half of finding 1).
- `${(k)arr}` / `${(v)arr}` / `${(kv)arr}` -- explicit parameter
  flags; the join is the requested operation.
- `$*` -- explicit positional join.
- `try_expand_vector_arg()` -- produces element vectors for `${arr[@]}`,
  `$@`, and bare `$arr` (zsh/lush) in argv/iteration positions; never
  joins. Correct.

## 6. Audit coverage gaps

Not exhaustively traced; close before §3.4 conformance is declared
complete:

- **Arithmetic expansion** (`src/arithmetic.c`) -- variable resolution
  inside `$((...))`; an array reaching arithmetic context was not
  traced.
- **Process substitution** -- not examined; low risk (unlikely to
  expand an array to a string) but unverified.

Command substitution captures output as a scalar by design
(SEMANTICS.md §4.1) -- no implicit array coercion risk there.

## 7. Work list

1. **Split `executor.c:12434-12437`** so `${arr[*]}` joins and
   `${arr[@]}` yields a vector in the general expansion path. The one
   genuine §3.5 conformance fix. Settle the vector-meets-scalar-slot
   behavior alongside SEMANTICS.md §8.
2. **Bare `${arr}`** (`expand_array_unsubscripted`) -- blocked on the
   §8 decision; update once §8 resolves.
3. **Close the coverage gaps** (§6) -- trace arithmetic.c and process
   substitution.

The headline result: the expansion engine is substantially conformant
already. The list→string surface reduces to a single join helper, and
of its call sites exactly one (`[@]` at 12437) is a genuine gap; the
rest are explicit joins or the §8-deferred bare reference. SEMANTICS.md
§3.4 is closer to met than the spec's §7 gap table assumed.

## See also

- [../SEMANTICS.md](../SEMANTICS.md) -- §3.4, §3.5, §8.
- `src/symtable.c` `symtable_array_expand` -- the join helper.
