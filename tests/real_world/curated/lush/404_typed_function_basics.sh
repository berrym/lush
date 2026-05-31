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

# Kind-mismatch detection on the happy path: scalar in, scalar out.
fn echo_n(n: scalar) -> scalar {
    return "$n"
}
let e = echo_n("hello")
echo "echo-n: $e"

# List-kinded args via the @sigil. The call-site sigil resolves the
# named binding kind-aware, preserving the list kind across the call
# boundary so the typed-function body sees `values` as a list.
fn count_items(values: list) -> scalar {
    return "${#values[@]}"
}
arr=(apple banana cherry date)
let n = count_items(@arr)
echo "count-items: $n"

fn first_item(values: list) -> scalar {
    return "${values[0]}"
}
let f = first_item(@arr)
echo "first-item: $f"

# Map-kinded args via the %sigil.
fn map_size(m: map) -> scalar {
    return "${#m[@]}"
}
declare -A config
config[shell]="lush"
config[mode]="polyglot"
let sz = map_size(%config)
echo "map-size: $sz"
