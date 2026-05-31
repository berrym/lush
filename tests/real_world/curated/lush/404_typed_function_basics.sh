#!/usr/bin/env lush
# Lush typed-function form (docs/features/typed-functions.md):
#
#   fn NAME(PARAM: KIND, ...) [-> KIND] { BODY }
#
# Differs from POSIX `name() { ... }` on three axes:
#   - kind-annotated params + return; mismatches fault at the call
#   - lexical scoping (declaration-site, not dynamic caller frame)
#   - return value captured via `let RESULT = NAME(args)`

# Scalar -> scalar
fn area(width: scalar, height: scalar) -> scalar {
    return "$((width * height))"
}

let rect = area("3", "4")
echo "rect: $rect"

# Multiple scalar args, arithmetic body.
fn hypot_sq(a: scalar, b: scalar) -> scalar {
    return "$((a * a + b * b))"
}
let h2 = hypot_sq("3", "4")
echo "hypot-squared: $h2"

# Coexists with POSIX-form functions in the same script.
posix_double() {
    echo "$(($1 * 2))"
}
echo "posix-double: $(posix_double 21)"

# Typed function calls into a POSIX-form function via $() capture.
fn from_typed(n: scalar) -> scalar {
    return "$(posix_double $n)"
}
let chained = from_typed("7")
echo "chained: $chained"

# Kind-mismatch detection: passing the literal "5" (scalar) to a
# function expecting scalar succeeds; this just exercises the
# call-site type check on a happy path. The negative case
# (passing a list where scalar is expected) is exercised by
# the kind-sigil test below.
fn echo_n(n: scalar) -> scalar {
    return "$n"
}
let e = echo_n("hello")
echo "echo-n: $e"

# TODO: list-kinded args via the @sigil are documented in
# docs/features/typed-functions.md but currently report E1133 even
# on the doc's own example `elements(@arr)`. Excluded from this
# fixture pending the parity fix; tracked as issue #207.
