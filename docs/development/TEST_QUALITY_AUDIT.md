# Test Suite Quality Audit

**Scope:** the 33 LLE unit test files under `tests/lle/unit/` (excluding the
four new test files that already use the shared framework introduced this
branch: `test_char_width.c`, `test_grapheme_detector.c`, `test_utf8_index.c`,
`test_theme_parser.c`).

**Status (2026-05-23):** findings still valid; migration in
progress. `tests/test_framework.h` (the shared framework with
setjmp/longjmp isolation + canonical signatures) is now the
authoritative test framework. Phase A migration is underway --
re-grep `tests/` for `define TEST` and `exit(1)` before relying on
the counts below; they are point-in-time. Each migration is also a
test-strengthening pass -- tests must verify real behavior, no
filler.

**Original status:** findings only. No migrations yet. Migrating tests is its own
work and is best done file-by-file with each migration also strengthening
the tests.

## Findings summary

| Issue | Count |
|---|---|
| LLE test files defining their own `TEST` macro | **30 / 33** |
| LLE test files using `exit(1)` on first failed assertion | **5 / 33** |
| Distinct `ASSERT_EQ` macro signatures in the LLE tests alone | **5** |
| Bare `ASSERT_NOT_NULL` calls (often the *only* assertion in a test) | **208** |

## Foundational problems

### 1. Every test file is its own framework

30 of the 33 LLE unit test files define their own copy of `TEST`,
`RUN_TEST`, `ASSERT`, and friends. This means:

- Bug fixes to one test file's framework do not propagate to others.
- Improvements (better failure messages, structured output, etc.) must be
  re-implemented per file.
- The framework itself drifts file-to-file: see the next item.

The new shared framework at `tests/test_framework.h` exists specifically to
address this. New tests use it; existing tests should migrate
incrementally.

### 2. ASSERT_EQ has at least five different signatures

Across the LLE tests alone (the broader codebase has more):

```c
#define ASSERT_EQ(a, b, msg)                       /* message third */
#define ASSERT_EQ(a, b, msg) ASSERT_TRUE(...)      /* delegated form */
#define ASSERT_EQ(a, b)                            /* NO message at all */
#define ASSERT_EQ(a, b) ASSERT((a) == (b))         /* delegated, no msg */
#define ASSERT_EQ(actual, expected, message)       /* (actual, expected) */
```

The order disagreement between `(a, b, msg)` and `(actual, expected, msg)`
is dangerous: a test author who writes `ASSERT_EQ(actual, expected, ...)`
in a file that defines the parameters as `(a, b, msg)` is silently fine
(both sides are just compared), but the failure message will print
"Expected X, got Y" with X and Y swapped — making debugging confusing.
That confusion has cost real time.

The shared framework standardizes on `(actual, expected, msg)` with
consistent failure messages and refuses to be mistaken for any other
order.

### 3. `exit(1)` on first failure hides cascading bugs

Five LLE files still call `exit(1)` from a failed assertion. In a binary
with N tests where the first test triggers an unrelated bug, every later
test never runs. CI reports "1 failure" when there might be 10. The
developer fixes the first bug, reruns, gets the next, fixes that, reruns,
and so on. This costs entire CI cycles per round.

The shared framework's `setjmp`/`longjmp` isolation prevents this: a
failed assertion in test X marks X as FAIL and resumes at the next
`RUN_TEST`. One run reports every failure across the binary.

### 4. Many tests are "smoke tests" that prove nothing

208 bare `ASSERT_NOT_NULL` calls exist across the LLE unit tests. Many
appear in tests of this shape:

```c
TEST(thing_init_success) {
    thing_t *t = NULL;
    lle_result_t r = thing_init(&t);
    ASSERT_EQ(r, LLE_SUCCESS, "init returns success");
    ASSERT_NOT_NULL(t, "t was allocated");
    thing_cleanup(t);
}
```

This proves init returns success and produces a non-NULL pointer. It
does **not** prove the thing's *behavior* is correct — that
`thing_store(t, key, value)` followed by `thing_lookup(t, key)` returns
`value`, that `thing_invalidate(t, key)` removes it, that `thing_count(t)`
is consistent with what was stored, etc. Many test files for stateful
modules (caches, registries, queues, key-value stores) test only the
existence of an instance, not its behavior.

This pattern inflates coverage statistics without providing the
guarantee coverage is supposed to represent. **A 90%-line-coverage test
file that only checks "did it not crash" is worse than a 30%-coverage
file that checks "did it produce the right answer", because the high
number creates false confidence.**

## Concrete examples

### test_render_cache.c — surface-only

The cache module exposes `init`, `cleanup`, `store`, `lookup`,
`invalidate`, `invalidate_all`. The test file exercises only `init`
(success and NULL parameters) and `cleanup`. There is no test that:

- stores a value and then looks it up
- stores N values and verifies all N are retrievable
- stores past the cache size limit and verifies eviction
- invalidates a stored key and verifies subsequent lookup misses
- invalidate_all clears every entry

In other words, the cache could not actually cache and the tests
would still pass.

### test_async_worker.c — initialization-heavy

33 assertions, most clustered around init/destroy and parameter
validation. Once the worker is created, what it actually *does* (queue
a task, run it, deliver the result, handle a worker thread error) is
tested thinly or not at all.

### Other files in the same shape

`test_render_pipeline.c`, `test_display_bridge.c`, `test_keybinding.c`
(only 5 assertions excluding the framework macros), several others.

### test_unicode_case_compare.c — the counterexample

This file does it right: each TEST drives the function with concrete
input and asserts the concrete output. ASCII upper/lower, mixed case,
Latin-1 with combining marks, every assertion is "did it produce this
exact byte sequence". That is what tests are for.

## Recommendations

1. **Migrate to the shared framework incrementally.** Each migration is
   mostly mechanical: delete the local macro definitions, add
   `#include "test_framework.h"`, fix any reversed `(expected, actual)`
   calls. The 5 files using `exit(1)` are the highest priority because
   they currently mask cascading failures.

2. **When migrating, also strengthen.** Migration is the right moment to
   read the test critically. If a test only asserts the object was
   created, add the operations and behavioral checks the test should
   have done in the first place. The point of testing is to prove the
   code is correct, not to prove the binary did not crash.

3. **Stop adding "did the function return success" tests in isolation.**
   For any function with a return code, also assert the *effect* the
   function was supposed to have. If the effect is internal state, expose
   a getter so the test can read it back.

4. **Defer files that mostly exercise visual/IO behavior.** LLE has
   modules whose correctness is genuinely hard to express in a unit
   test (the line editor, the terminal abstraction, the rendering
   pipeline). Those need integration or visual-regression strategies,
   not better unit tests. Mark them as such and stop pretending the
   existing thin tests cover them.

## Migration order suggestion

Highest leverage (worst current quality, clearest API to test
properly):

1. `test_render_cache.c` — replace surface tests with real cache
   semantics (store, lookup, eviction, invalidation)
2. `test_render_pipeline.c` — same shape as render_cache, similar work
3. `test_async_worker.c` — exercise actual task submission and result
   delivery, not just init/destroy
4. The five `exit(1)`-using files — at minimum, migrate the framework
   so failures stop hiding each other
5. `test_keybinding.c` — only 5 assertions in 499 lines; almost
   certainly a smoke test wearing the clothes of a real test suite

The other files that are *already* testing real behavior
(`test_unicode_case_compare.c`, `test_powerline_renderer.c`,
`test_event_phase2.c`) should migrate framework-only and otherwise be
left alone — they are doing it right.

## What this audit does not say

It does not assert that any specific test is *wrong*, only that the
collective pattern produces test counts and coverage percentages that
overstate the actual guarantee the test suite provides. The path
forward is to migrate the framework (mechanical) and to strengthen
the surface tests during migration (judgment). Both serve the same
goal: make tests real proof, not statistical decoration.
