# Typed Functions

**A first-class function form for lush with kind-annotated parameters,
a kind-annotated return value, lexical scoping, and a `let`-capture
expression. Coexists with classical POSIX functions; both forms run
side by side in the same script.**

**Status**: shipped.
**Spec lineage**: `docs/SEMANTICS.md` S5 (scoping discipline), S8
(formerly deferred line "The typed-function form").
**Implementer reference**: `docs/development/TYPED_FUNCTIONS_DESIGN.md`.

---

## What it is

A new declaration form:

```
fn NAME(PARAM: KIND, ...) [-> RETURN_KIND] { BODY }
```

- Each parameter declares a **kind annotation**: `scalar`, `list`,
  or `map`. Arguments at the call site are checked against the
  declared kind; a mismatch raises `SHELL_ERR_TYPE_MISMATCH` at the
  call.
- Optional `-> KIND` declares the **return kind**. If present, the
  body's `return EXPR` is checked against that kind statically (by
  `debug analyze`) and dynamically (at the return site).
- The body resolves free names through the function's
  **declaration-site lexical scope**, not the dynamic caller's
  local frame. This is the central break from POSIX functions.

A call expression captures the typed return value via `let`:

```
let RESULT = NAME(arg1, arg2, ...)
```

A bare statement-position call is allowed (e.g. `greet("world")`);
its return value is discarded.

### Example

```bash
fn area(width: scalar, height: scalar) -> scalar {
    return "$((width * height))"
}

let result = area("3", "4")
echo "$result"        # 12

fn elements(values: list) -> scalar {
    return "${#values[@]}"
}

arr=(a b c d)
let n = elements(@arr)
echo "$n"             # 4
```

## Why it exists -- the Bourne pitfall this kills

POSIX shell functions have three structural defects that have
shipped every script bug you've ever seen:

### 1. No type discipline at the boundary

A POSIX function takes whatever positional arguments it's given, no
declared shape, no enforcement:

```bash
greet() { echo "hello $1"; }
greet world           # ok
greet                 # silent: $1 is empty, output is "hello "
greet a b c d e       # extras silently ignored
```

Typed functions name and check the inputs:

```bash
fn greet(who: scalar) -> scalar {
    return "hello $who"
}

let r = greet("world")           # ok
let r = greet()                  # parse error: missing arg
let r = greet("a", "b")          # error: too many args
```

### 2. No structured return value

A POSIX function returns an **exit status** (0-255), not a value.
"Returning" data means writing to stdout and using command
substitution, which forks a subshell and stringifies everything:

```bash
square() { echo "$(( $1 * $1 ))"; }
result="$(square 4)"            # fork, capture, string-coerce
```

That subshell loses scope state, costs an `fork`/`exec`, and turns
every return value into a scalar string regardless of what the
caller actually wanted.

Typed functions return real, kind-tagged values in-process:

```bash
fn square(n: scalar) -> scalar {
    return "$((n * n))"
}

let result = square("4")        # no fork, no subshell, kind preserved
```

### 3. Dynamic scoping leaks state

POSIX functions resolve free names against the **caller's** scope.
A function reading `$config_dir` reads whichever caller-frame's
`config_dir` is currently active. Two callers with different locals
see different behavior from the same function. The classical
example:

```bash
v=GLOBAL

show() { echo "$v"; }

outer() {
    local v=LOCAL
    show          # prints LOCAL -- captured the caller's local
}
```

Typed functions resolve free names against the **declaration-site**
scope, captured at `fn` definition time. The same function, same
caller, different binding rule:

```bash
v=GLOBAL

fn show() -> scalar {
    return "$v"
}

outer() {
    local v=LOCAL
    let r = show()
    echo "$r"     # prints GLOBAL -- the declaration site, not LOCAL
}
```

This is exactly the dynamic-vs-lexical scope distinction every other
modern language settled in the 1970s. Bourne shell never got it; lush
does, and the two forms coexist so existing POSIX functions keep
their dynamic-scope behavior.

## Syntax forms

### Declaration

```
fn NAME(PARAM: KIND, ...) [-> RETURN_KIND] { BODY }
```

- `NAME` is an identifier.
- Each parameter is `name: kind` where kind is one of `scalar`,
  `list`, `map`.
- The parameter list may be empty: `fn now() -> scalar { ... }`.
- The return annotation is optional. Omitted: the body must not
  `return EXPR` -- only `return` (void) is legal. Specified: the
  body's returns are checked against the kind.

### Call (statement position)

```
NAME(ARG, ARG, ...)
```

Runs the function. Return value discarded.

### Call (let-capture)

```
let RESULT = NAME(ARG, ARG, ...)
```

Runs the function and binds `RESULT` to the typed return value. The
binding lives in the **caller's** scope. The kind of `RESULT`
matches the function's declared `RETURN_KIND`.

### Return statement

Inside a typed function body:

- `return EXPR` -- return a value (only legal when `-> KIND` was
  declared).
- `return` -- return void (only legal when `-> KIND` was omitted).

The legacy `return_value` builtin and its `__LUSH_RETURN__` marker
were retired when the typed form landed; `return EXPR` is the only
path to a structured return value.

## Lexical vs dynamic scope

The two forms coexist with different scope rules:

| Function form | Resolution rule | When defined |
|---------------|-----------------|--------------|
| `fn NAME(...) { ... }` (typed) | Lexical: declaration-site scope captured at definition | When the `fn` declaration is executed |
| `NAME() { ... }` or `function NAME { ... }` (POSIX) | Dynamic: caller's scope chain | When the `()` declaration is executed |

Picking which to use:

- Reach for **typed `fn`** when you want predictable resolution and
  structured return values. This is the right default for new code.
- Reach for **POSIX `()`** when you genuinely need dynamic scope --
  most commonly for environment-mutating helpers like `cd` wrappers,
  or when porting existing scripts.

## Argument-kind checking

Arguments are checked at the call:

```bash
fn first(items: list) -> scalar {
    return "${items[0]}"
}

arr=(a b c)
let r = first(@arr)      # ok -- @arr is list-vector context

let r = first("hi")
# error[E1133]: type mismatch: argument 1 of `first` expects list, got scalar
```

The check is positional. There is no name-based argument-binding
syntax (`first(items=@arr)`) -- arguments bind by position to the
declared parameter list, left to right.

### How arguments evaluate

Arguments evaluate in the **caller's** scope, not the function's.
This is the standard rule from every other language and from POSIX
functions too. The sigil at the argument site (`@arr`, `%map`,
`$x`) selects the presentation context for evaluating the argument,
which then binds to the parameter under the parameter's declared
kind.

## Debugger surface

A typed-function frame on the debug stack is marked `[lexical]` to
distinguish it from a POSIX-function frame marked `[dynamic]`. The
`debug stack` command renders the annotation:

```
(lush-debug) debug stack
  #0 area [lexical]    -- captured: <global>
  #1 main [dynamic]    -- caller chain: <global>
```

Variable inspection from inside a typed-function frame walks the
captured scope chain, not the dynamic caller's chain. `inspect
$config_dir` from inside `area` resolves through the declaration
site, regardless of who called `area`.

## Static type checks (`debug analyze`)

`debug analyze` walks every `fn` declaration and checks:

- A `return EXPR` exists when `-> KIND` is declared.
- A bare `return` (no value) exists when `-> KIND` was omitted.
- The return expression's statically-inferrable kind matches the
  declared kind. String literals infer to `scalar`, `name=(...)`
  array literals to `list`. Variable references and nested calls
  are deferred (they need cross-procedure resolution).

```bash
fn bad() -> scalar {
    return (a b c)
}

# debug analyze reports:
#   error    line 2  typed function 'bad' declared '-> scalar' but
#                    a return statement yields a list value
#                    Suggestion: return a value of the declared kind, or
#                                change the fn's return annotation to match
```

## Curated defaults

Typed functions are **engine-level**, not mode-gated. Every shell
mode parses and executes `fn`. The form is unique to lush -- it
doesn't conflict with any classical syntax -- so there's no
compatibility reason to gate it.

POSIX functions remain fully supported. A script can use both forms
side by side.

## Gotchas

- **`fn` is a keyword now.** A variable or function named `fn` in
  existing scripts will conflict with the declaration syntax. The
  word `fn` is short, distinctive, and (per a survey of bash
  scripts on this filesystem) not in widespread script use, but
  rename collisions if needed.

- **Bare statement-position calls discard the return value.**
  `greet("world")` runs the function but throws away whatever
  `return` produced. Use `let r = greet("world")` to capture it.

- **POSIX `return EXIT_STATUS` is unchanged.** Inside a POSIX
  function, `return 1` still sets the exit status. Inside a typed
  function, `return 1` returns the scalar `"1"` per the typed form.
  The two forms have separate `return` semantics; they don't bleed
  into each other.

- **Lexical capture is one-shot.** A typed function captures its
  declaration-site scope chain at the `fn` statement's execution.
  Re-declaring `fn` later captures the new scope. This is the same
  rule every other language uses; no closures over mutable cells.

- **Arguments are positional, not named.** No `greet(who="world")`
  syntax. If a function has many parameters and you want named
  binding, use a map argument: `fn config(opts: map) -> scalar` and
  pass `config(%my_opts)`.

## See also

- `docs/SEMANTICS.md` S3 -- the kind-tagged value model typed
  functions sit on top of.
- `docs/SEMANTICS.md` S5 -- the scoping discipline; lexical vs
  dynamic.
- `docs/features/sigil-conventions.md` -- the `@`/`%` sigils used
  for passing list/map arguments to typed-function calls.
- `docs/DEBUGGER_GUIDE.md` -- the `[lexical]` / `[dynamic]` frame
  annotation; `debug analyze` static-check coverage.
- `docs/development/TYPED_FUNCTIONS_DESIGN.md` -- implementer-level
  design notes: parser grammar, AST shapes, the captured-scope
  mechanism, retirement of the legacy `return_value` builtin.
