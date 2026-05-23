# Typed Functions and Lexical Scoping -- Design

**Status (2026-05-23):** design draft. The two load-bearing surface
decisions (declaration syntax and call-site capture) were settled by
user choice on 2026-05-23. This document expands those into a full
spec covering grammar, types, scoping, the `return` statement, the
debugger surface, and a phased commit plan.

**Scope:** SEMANTICS.md §8 item "The typed-function form" (and the
coupled §5.3 lexical-scope obligation). On landing, the §7 row
"Typed-function form" moves from `large -- form not yet designed`
to `match`, and the §8 list loses this item.

**Authority:** this document, once landed, is *informative* (a
record of the design rationale). The *normative* statements live in
SEMANTICS.md once §5.3 and §7 are updated. Where this doc and
SEMANTICS.md ever disagree, SEMANTICS.md wins.

---

## 1. The two surface decisions, restated

### 1.1 Declaration

```
fn greet(name: scalar) -> scalar {
    return "Hello, $name"
}

fn collect(words: list) -> scalar {
    return "${(j: :)words}"
}

fn empty() {
    echo "no return"
}
```

The form is `fn name(params) -> ret_kind { body }`. The `fn` keyword
is the single visible marker; a reader knows from the header alone
that this function is lexically scoped (§5.3), distinct from a
POSIX-form `name() { ... }` or `function name { ... }` which carry
dynamic scoping (§5.2).

### 1.2 Call-site capture

```
let result = greet("Alice")
echo "$result"          # Hello, Alice

let words = parts()     # parts() returns a list
echo "${words[@]}"
```

`let result = name(args)` is the call-site form. The existing
arithmetic-only `let "x = 5+3"` form is preserved (§2.4).

---

## 2. Grammar

### 2.1 Declaration grammar (EBNF)

```ebnf
fn_declaration  = "fn" identifier "(" [ param_list ] ")"
                  [ "->" type_kind ]
                  compound_command ;

param_list      = param { "," param } ;
param           = identifier ":" type_kind ;
type_kind       = "scalar" | "list" | "map" ;
```

Notes:

- The `fn` keyword is **only** recognized in command-word position,
  same context-sensitive treatment as `function`. Identifiers named
  `fn` in argument positions remain unaffected (`echo fn` still
  prints the literal text `fn`).
- An empty parameter list (`fn foo() { ... }`) is legal.
- The return-kind annotation is optional. Absence means "no return
  value" -- a void fn (§2.5).
- Default values, variadic parameters, and `nameref` /
  polymorphic-`any` parameter kinds are **out of scope** for the
  initial form (§9 deferred).
- A `fn` body is a `compound_command` -- the same body grammar as a
  POSIX function body. Inside the body, `return expression` becomes
  available (§2.5); outside, `return` retains its POSIX
  exit-status-setting semantics.

### 2.2 Call-expression grammar

```ebnf
call_expression = identifier "(" [ arg_list ] ")" ;
arg_list        = argument { "," argument } ;
argument        = word ;     (* same word/expansion as elsewhere *)
```

A call expression is **only** recognized as the right-hand side of a
`let` statement (§2.3) for the initial form. Bare `name(args)` at
statement position is not added; users who don't need the return
value can call the fn as `name args` exactly like a POSIX function
(see §6 for the call-site discriminator).

### 2.3 `let` grammar extension

The current `let` grammar accepts arithmetic expressions:

```ebnf
let_arith       = "let" arith_expr { arith_expr } ;
```

The new extension adds:

```ebnf
let_fn_call     = "let" identifier "=" call_expression ;
```

Disambiguation at the parser:

- If the right-hand side parses as `identifier(` (an identifier
  immediately followed by `(` with **no intervening whitespace,
  comment, or line continuation**), treat as `let_fn_call`.
- If there is any whitespace, tab, newline, comment, or `\`
  continuation between the identifier and the `(`, the form is NOT a
  call expression. The parser falls through to `let_arith` and the
  arithmetic parser then takes responsibility for whatever follows.
- Otherwise, treat as `let_arith` (existing behavior, unchanged).

This means `let result = greet("Alice")` is parsed as
`let_fn_call`, but `let x = 5+3`, `let "x = y * 2"`, and
`let r = greet ("Alice")` (note the space) all go through the
arithmetic path. The last form is a deliberate user error caught at
arithmetic-parse time (the `(...)` is parsed as a grouped
sub-expression and `greet` is treated as an arithmetic identifier),
producing a structured-error diagnostic that includes a suggestion
to drop the space if a typed-fn call was intended.

The discriminator is purely lexical (the `(` token, *adjacent* to the
identifier), so the parser does not need to consult the symbol table
at parse time. Phase A5 tests cover each whitespace variation
(`fn(`, `fn (`, `fn\n(`, `fn /*x*/ (`, `fn \<newline>(`) to lock the
rule.

A `let_fn_call` with an identifier that does not resolve to a
declared `fn` at execution time raises a structured error
(`SHELL_ERR_NOT_TYPED_FUNCTION`) rather than falling back to
arithmetic. Misuse is caught loudly.

### 2.4 `return expression` grammar

Inside a `fn` body, `return` accepts an optional expression:

```ebnf
fn_return       = "return" [ expression ] ;
```

Outside a `fn` body (in POSIX functions, in `for` loops, etc.),
`return N` retains its existing POSIX exit-status semantics. The
parser tracks whether the current command is inside a `fn` body via
a context flag; `return expression` is only parsed there.

This **replaces the `__LUSH_RETURN__` marker hack** for typed
functions. The marker mechanism stays in place for the existing
`return_value` builtin used by POSIX-form functions (which has no
typed return concept and emits the marker to communicate non-zero
values back through stdout).

### 2.5 Kind taxonomy

Exactly three kinds are admitted in parameter and return positions:

| Kind | Maps to SEMANTICS §7 |
|------|----------------------|
| `scalar` | `LUSH_VALUE_SCALAR` |
| `list`   | `LUSH_VALUE_LIST` |
| `map`    | `LUSH_VALUE_MAP` |

Absent return annotation indicates a **void** fn -- no return
value. Calling `let r = void_fn()` is a type error
(`SHELL_ERR_TYPE_MISMATCH`, "void function has no return value").

Three deliberate omissions for the initial form:

- **No `any`.** Gradual typing is an anti-feature for the typed
  form -- the point of `fn` is that types are checked. Helpers
  that want to be untyped use the POSIX form. Reintroducing `any`
  is reversible later if a real need emerges (§9 deferred).
- **No `nameref`.** A typed-fn parameter cannot be a nameref. Use
  `list` or `map` for shared-state patterns.
- **No `func` parameter kind.** Higher-order functions are
  deferred. The motivation (callbacks, transformers) is real but
  not blocking; deferred to a follow-on design with closures.

---

## 3. Static type checking

### 3.1 At fn declaration

When a `fn` is parsed, the resolver pass (§5) walks its body and
checks:

1. Every parameter reference (`$p` where `p` is a declared param)
   is used in a context compatible with its declared kind.
2. Every `return expression` evaluates to the declared return kind.
3. A `fn` declared `-> scalar` cannot have `return` with no
   expression (void return).
4. A `fn` with no return annotation cannot have `return expression`
   (void declared, value returned).

Type-mismatch findings are reported via the structured-error system
(`SHELL_ERR_TYPE_MISMATCH`) with file:line citations. Existing
SEMANTICS §3.9 type errors compose: a `fn` body that does `echo
"$arr_param"` where `arr_param: list` is a §3.9 violation reported
at parse-and-resolve time, not runtime.

### 3.2 At call site

For each `let result = name(args)` site, the resolver pass checks:

1. `name` resolves to a declared `fn` (otherwise a parse-time
   warning; runtime raises `SHELL_ERR_NOT_TYPED_FUNCTION`).
2. The number of arguments matches the parameter count.
3. Each argument's inferred kind matches the parameter's declared
   kind. Inference rules:
   - A literal like `"text"` is `scalar`.
   - A bare list expansion like `${arr[@]}` where `arr` is a List is
     `list`.
   - A bare scalar expansion like `$x` where `x` is a Scalar is
     `scalar`.
   - When the kind can't be inferred statically, the check is
     deferred to runtime.
4. The LHS variable's resulting binding kind matches the declared
   return kind. (`let scalar_var = list_returning_fn()` is rejected;
   use `let listy = list_returning_fn()`.)

### 3.3 `debug analyze` integration

The static checks above run as part of the existing analyzer
(`debug_analysis.c`, the same engine `debug analyze` and `lint` use).
Findings carry the `type` category, same as existing SEMANTICS §3.9
type-mismatch flags. Their format is identical to other
type-category findings.

---

## 4. Runtime semantics

### 4.1 Declaration

Executing a `fn` declaration:

1. Allocates a `typed_fn_t` record holding: name, parameter list,
   return kind, body AST pointer, and a pointer to the **captured
   program scope** (the scope active at declaration time).
2. Registers the record in a per-executor `typed_fn_registry`
   (keyed by name; distinct from the POSIX `function_t` table).
3. Re-declaring an existing fn name replaces the previous record
   (same semantics as POSIX `function` redeclaration).

The captured program scope is the lexical-closure mechanism (§5).
The registry is a per-executor table, not per-shell-process; a
sub-shell forking does not get a parent's typed fns unless they were
declared in a scope the sub-shell still has access to.

### 4.2 Call

Executing a `name(args)` call expression:

1. Looks up `name` in the `typed_fn_registry` (raises
   `SHELL_ERR_NOT_TYPED_FUNCTION` on miss).
2. Evaluates each argument:
   - If the parameter kind is `scalar`, the argument is forced to a
     scalar (existing word-expansion path).
   - If the parameter kind is `list`, the argument must expand to a
     vector (`${arr[@]}`, a literal `(a b c)`, or a `list`-kind
     value); otherwise type mismatch.
   - If the parameter kind is `map`, the argument must expand to a
     map; otherwise type mismatch.
3. Pushes a new **lexical** scope frame whose parent pointer is the
   captured program scope, NOT the caller's frame.
4. Binds each parameter as a local in this new frame with the
   declared kind.
5. Executes the body.
6. On `return expression`, evaluates the expression, checks the
   resulting kind matches the declared return kind, captures the
   value, unwinds the body, and yields the value to the caller.
7. On body fall-through (no explicit return), the fn must have no
   return annotation; otherwise a missing-return type error is
   raised.

### 4.3 `let result = name(args)` binding

The executor:

1. Performs the call per §4.2 to obtain a typed return value.
2. Binds `result` in the current scope (lexical if inside another
   `fn`, dynamic otherwise) with the kind the fn returned.

This is where lush diverges from `result=$(name args)` (which would
fork a subshell, lose the kind, and break lexical scoping). The
typed call path is in-process and kind-preserving.

---

## 5. Lexical scope resolution

### 5.1 The resolution model

Inside a `fn` body, name resolution is **lexical (block) scoped**:

1. Each block (`{ ... }`, `if/then/else`, `for/do/done`, etc.)
   forms a lexical block.
2. A name reference resolves to the **innermost enclosing block**
   that defines that name.
3. If no enclosing block defines it, resolution falls through to the
   fn's captured program scope (§4.1).
4. If even the captured scope has no binding, the name is unset; the
   `set -u` / `errunset` policy applies as usual.

Critically, name resolution **does not consult the caller's frame**.
A typed fn that references `$x` resolves `x` via its own block
structure + captured program scope; the variable `x` in the caller
is invisible.

### 5.2 Implementation: compile-time resolver pass

A new `lexical_resolver` module runs after parsing, before
execution:

1. For each parsed `fn` declaration, walk the body AST.
2. Build a per-block scope tree where each block carries its locally
   declared names (`local`, parameter names, `for` loop variables,
   `let result = ...` bindings, etc.).
3. For each identifier reference in the body, annotate the AST node
   with the resolved binding: a tuple `(scope_kind, scope_depth,
   slot_id)`.
   - `scope_kind` is `LOCAL` (resolved in an enclosing block) or
     `CAPTURED` (resolved in the captured program scope, looked up
     by name at call time).
4. Free names (not resolved anywhere) are recorded as
   `RESOLVED_FREE` and looked up dynamically in the captured
   program scope at call time. A warning is emitted at resolve time
   if `set -u` is on.

This produces an AST that the executor can walk without dynamic
scope-chain lookups inside fn bodies -- the resolutions are
pre-computed. The dynamic scope chain remains in place for POSIX-form
functions.

### 5.3 Executor changes

A new scope-frame kind `SCOPE_LEXICAL` joins the existing
`SCOPE_DYNAMIC`:

- `symtable_push_lexical_scope(captured_parent)` creates a frame
  whose `parent` is the captured scope, not the caller.
- Variable lookups inside a `SCOPE_LEXICAL` frame use the resolved
  bindings from §5.2 instead of the dynamic walk.
- `symtable_pop_scope` works identically for both kinds.

### 5.4 Why compile-time over runtime tagging

Two reasons:

1. **Debugger story.** PHILOSOPHY §7 obligates the debugger to
   render typed-fn frames truthfully. Compile-time resolution means
   `debug print x` inside a typed-fn body has a deterministic
   answer that the debugger can render without ambiguity. A runtime
   tagged walk could produce the same answer but with less
   confidence -- and the integration-test gate would be harder to
   write.
2. **Static analysis.** §3 type checks are a natural extension of
   the resolver pass. A purely-runtime model would split the type
   checking across two passes (one at parse, one at runtime) and
   produce diagnostics with worse source citations.

The cost is a new resolver module (~500 lines) and AST annotation
storage. Both are acceptable.

### 5.5 Dynamic code execution inside fn bodies

Some constructs introduce code that is not present in the AST at
parse time and therefore cannot be resolved by the §5.2 compile-
time pass: `source FILE`, `eval STRING`, command substitution
`$(...)` that runs at fn-execution time, and anything else that
constructs and executes shell text on the fly.

The rule:

1. Code introduced by `source`, `eval`, or runtime-constructed
   command strings inside a fn body is **NOT** lexically resolved
   against the fn. The compile-time resolver has not seen it.
2. Such code runs with its own scope-chain walk that lands on the
   fn's local scope *at the top of the chain*. The fn's lexically-
   declared locals are therefore visible to sourced/evaled code, by
   the natural symtable_lookup walk.
3. Type checking does not extend into sourced/evaled code. Free
   names inside it are looked up dynamically; type-mismatch
   diagnostics may fire at runtime rather than at resolve time.
4. The fn body's own (parsed) code remains fully lexically resolved
   regardless of whether the fn also sources or evals.

This boundary is intentional: it preserves the static-analysis
benefit for hand-written fn bodies while honoring the
ecosystem-established behavior of `source` (sourced code sees the
caller's scope, in-process). The behavior matches how POSIX shells
expose sourced-file visibility today; the only difference is which
*locals* are visible (the fn's lexically-declared ones, not a
dynamic-chain accumulation).

The §7 gate covers this with a dedicated test
(`test_typed_fn_source_sees_locals`) that asserts a sourced file
inside a fn body can read and write the fn's declared locals via
the standard scope-chain walk. `debug analyze` does NOT report
the static type-check gap for sourced code as an error; it remains
silent or warns at most, matching the established convention.

A future hardening pass could add an opt-in
`set -o strict-fn-isolation` mode that forbids `source` / `eval`
inside `fn` bodies entirely, restoring full static analysis. Not
part of the initial form (§9 deferred).

---

## 6. The discriminator: POSIX-form call vs typed-form call

A reader looking at `greet "Alice"` cannot immediately tell whether
`greet` is a POSIX function or a typed fn. Lush's rule:

- **`greet "Alice"`** -- as a command at statement position. If
  `greet` is a typed fn, the fn is called with its return value
  *discarded* (only side-effects matter). If `greet` is a POSIX
  function, normal POSIX function call semantics apply.
- **`let r = greet("Alice")`** -- only typed fns. POSIX functions
  cannot be called this way; `let r = posix_func("x")` raises
  `SHELL_ERR_NOT_TYPED_FUNCTION`.

In practice the discriminator at the call site is the
`let result = name(...)` form. Authors who want the typed return
value use `let`. Authors who want side-effect-only or POSIX
call-style use bare `name args`.

A future linting rule (deferred) could warn when a typed fn with a
non-void return is called bare (the return value is being
discarded). Not part of the initial form.

---

## 7. Debugger obligations (PHILOSOPHY §7)

The integration-test gate (`tests/unit/test_debug_integration.c`)
must turn red until each of these is implemented:

### 7.1 `debug stack`

Frames are reported with their discipline. Each frame line gets a
`[lexical]` or `[dynamic]` annotation:

```
#0  greet              demo.sh:3   [lexical]
#1  main               demo.sh:8   [dynamic]
```

### 7.2 `debug vars` inside a typed-fn body

Only lexically-visible bindings are listed. The dynamic call chain's
variables are NOT enumerated. The framed-gutter output remains
identical in shape; only the contents differ.

### 7.3 `debug print x` and `type x` / `t x`

Resolution uses the lexical chain. The output identifies the binding
site by file:line:

```
│ ┌─ [Variable: words] ────────────────────────────────────────────────
│ Type:  List
│ Count: 3 elements
│ Scope: lexical (declared at demo.sh:4)
│ └──────────────────────────────────────────────────────────────────
```

### 7.4 `debug analyze` new checks

Four new findings, all in the `type` category:

1. parameter-vs-body kind mismatch (e.g., scalar param used in a
   list-only operator)
2. return-expression kind mismatch
3. call-site argument kind mismatch
4. call-site result kind mismatch (`let scalar_v = list_fn()`)

### 7.5 The §7 gate test

`tests/unit/test_debug_integration.c` gains a `test_typed_fn_*`
group covering each obligation. The gate is RED until every test
passes. Per PHILOSOPHY §7, this means the typed-fn work is not
"done" until the test group is green.

---

## 8. Phased commit plan

Each phase is a self-contained commit (or small commit series). The
test suite stays green at every phase boundary.

### Phase A: parser grammar (no execution)

- A1: Tokenizer recognizes `fn` as a context-sensitive keyword.
  Existing identifiers containing `fn` are unaffected.
- A2: Parser handles `fn` declaration; produces `NODE_FN_DECL`.
- A3: Parser handles `let identifier = call_expression`; produces
  `NODE_LET` with a `NODE_FN_CALL` child.
- A4: Parser handles `return expression` inside fn bodies; produces
  `NODE_FN_RETURN`. Outside fn bodies, behavior unchanged.
- A5: Parser tests (positive and diagnostic) for each form.

Commit message prefix: `parser:` (or `LLE:` -- none of phase A
touches LLE).

### Phase B: runtime execution

- B1: `typed_fn_registry` table added to the executor; populated by
  `NODE_FN_DECL` execution.
- B2: Executor handles `NODE_FN_CALL`: argument kind-binding,
  lexical-frame push (with placeholder captured scope), body
  execution, return handling.
- B3: Executor handles `NODE_FN_RETURN`: value capture and unwind.
- B4: Executor handles `NODE_LET` with `NODE_FN_CALL` child: kind-
  preserving binding to LHS.
- B5: Tests covering positive call paths + kind-mismatch errors.

Commit message prefix: `executor:` or split as needed.

### Phase C: lexical resolver

- C1: `lexical_resolver` module: AST walker that builds the scope
  tree and annotates references.
- C2: AST node annotation storage (a new field on identifier-
  reference nodes).
- C3: `symtable_push_lexical_scope` added; executor switches to
  resolved bindings inside `fn` bodies.
- C4: Cross-fn pollution tests (caller's variables must be
  invisible).

Commit message prefix: `parser+executor:` (touches both).

### Phase D: debugger surface (PHILOSOPHY §7 gate)

- D1: `debug stack` annotates frames with `[lexical]` / `[dynamic]`.
- D2: `debug vars` / `print` / `type` use the lexical chain inside
  fn bodies; existing kind rendering preserved.
- D3: `debug analyze` adds the four new `type`-category checks.
- D4: `test_debug_integration.c` gets the `test_typed_fn_*` group
  covering every obligation; gate turns green only when all pass.

Commit message prefix: `LLE+debug:` (debugger touches some LLE).

### Phase E: documentation

- E1: SEMANTICS.md §5.3 expanded with the resolution model; §7
  row "Typed-function form" moves to "match"; §8 list loses the
  item.
- E2: USER_GUIDE.md + EXTENDED_SYNTAX.md: new sections covering
  the `fn` form and `let` extension.
- E3: DEBUGGER_GUIDE.md: typed-fn break-prompt surface +
  `debug analyze` typed-fn checks.
- E4: This design doc gets a "Reading note: IMPLEMENTED" header
  similar to the other landed-design docs (e.g.,
  COMPLETION_REWRITE_PLAN.md).

Commit message prefix: `docs:`.

### Phase ordering and integration tests

Phases A-D each leave the test suite green. After each phase the
`test_typed_fn_*` group in `test_debug_integration.c` adds
incremental coverage (parsing-only in A; call execution in B;
lexical resolution in C; debugger obligations in D). The §7 gate is
only required to be green by end of phase D.

---

## 9. Deferred from the initial form

Recorded so they are not lost. None of these are decided by this
document; they are to be decided against it, as their own work.

- **Default parameter values:** `fn greet(name: scalar = "world") -> scalar { ... }`.
- **Variadic parameters:** `fn collect(first: scalar, rest: list) { ... }`
  using a final `list` parameter that gobbles remaining args. The
  inverse-spread on the call side is the harder design point.
- **`nameref` parameter kind:** for legitimate shared-state
  patterns. Needs interaction with lexical scope worked out.
- **`func` parameter kind / higher-order functions:** closures are
  the harder piece. Defer until there is a concrete motivating use.
- **`any` parameter / return kind:** explicit non-feature today (§2.5).
  Reversible if a real need emerges.
- **Anonymous typed fns / lambdas:** `let f = fn(x: scalar) -> scalar { ... }`.
- **`set -o strict-fn-isolation`** -- a strict mode that forbids
  `source` / `eval` inside `fn` bodies, restoring full static
  analysis at the cost of the §5.5 escape hatch. Reversible. Not
  required for the initial form.
- **Calling typed fns at statement position with discarded return**
  -- already legal per §6 but no lint warning for non-void returns
  being discarded.
- **Type aliases / type unions** -- not on the table; the §7 kind
  set is intentionally small.

---

## 10. Interactions with existing rules

| Rule | Effect on typed fns |
|------|---------------------|
| SEMANTICS §3.4 no implicit list-to-string | Applies inside fn bodies and at the call/return boundary. |
| SEMANTICS §3.9 list-in-scalar-slot type error | Applies; raised at parse-and-resolve time for fn bodies. |
| SEMANTICS §3.5 transform / presentation split | Applies; fn parameters are values, not text. |
| SEMANTICS §3.8 word splitting | Word splitting still gates command-substitution and the like *outside* fn calls; inside a `fn` body, declared kinds are authoritative. |
| SEMANTICS §5.2 POSIX-form dynamic scoping | Untouched. POSIX functions keep their existing scoping. |
| PHILOSOPHY §2 polyglot translation | The typed form is canonical lush. There is no bash or zsh spelling to bridge to. The form is a lush identity feature. |
| PHILOSOPHY §4 POSIX as baseline | POSIX scripts that don't use `fn` are unaffected. POSIX mode does not disable `fn` (presets are not restrictions). |
| PHILOSOPHY §6 architectural correctness over expediency | The compile-time resolver pass (§5.4) is the larger but correct call. |
| PHILOSOPHY §7 debugger keeps pace | The §7 gate test must pass before the typed-fn work is "done" (§7). |
| `set -u` / `errunset` | Applies to free names in fn bodies; the resolver warns at resolve time when `set -u` is active. |
| `errtrace` / `functrace` | ERR / DEBUG / RETURN trap inheritance applies inside `fn` bodies identically to POSIX functions. |
| Structured-error system | All new diagnostics use direct `shell_error_create` / `executor_error_report`. No helper wrappers. |

---

## See also

- [`../SEMANTICS.md`](../SEMANTICS.md) -- the engine spec; §5.3 and
  §7 are the normative home for this work once landed.
- [`../PHILOSOPHY.md`](../PHILOSOPHY.md) -- the design contracts;
  §7 names the gate that enforces the debugger obligation.
- [`../DEBUGGER_GUIDE.md`](../DEBUGGER_GUIDE.md) -- the debugger's
  current surface; the new typed-fn obligations extend it.
- [`COMPLETION_REWRITE_PLAN.md`](COMPLETION_REWRITE_PLAN.md) -- the
  reference for how a design doc reads after the work it specifies
  has landed.
