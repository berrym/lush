# Comment-Style Audit Worklist

This is a **catalog-only** worklist for the comment-style unification across the lush codebase. No code changes are made by this audit -- the file lists violations so the user can pick subdirectories for wave-based commits.

## The unified ruleset (the bar for "violation")

| Form | Where it goes | When to use |
|---|---|---|
| `/** ... */` | Above **non-trivial exported declarations in headers ONLY** | Full Javadoc form: mandatory `@brief` first, then detail, then `@param` / `@return` / `@retval` / `@note` / `@see`. Markdown allowed. |
| `///` | Everywhere else a comment goes | Single-line above trivial header declarations; above static/internal functions in `.c` files; above struct members on their own line; inline code-reader notes inside function bodies; any single-line comment. |
| `///<` | Trailing same-line | Struct members, enum values, anywhere a one-line description belongs after the declaration. |
| `/* ... */` | Outside function bodies only | Plain prose: module-level explainers between functions, longer why-notes that don't belong in API docs. |

**Retired:** `//` (-> `///`), `/**< ... */` (-> `///<`).

**`@` over `\`** for command prefixes. Every `.c` and `.h` should start with a `@file` block.

## Scope

- Walked all `.c` and `.h` files under `src/`, `include/`, and `tests/`.
- Skipped `src/strings.c` (deprecated, being phased out -- see memory `feedback-strings-c-deprecated`).
- Skipped `build/`, `build-asan/`, `build-fuzz/`, `build-linux/`, and any generated files (none found in scope).
- Skipped `tests/real_world/corpus/*` data fixtures; included `tests/fuzz/*.c` test source.
- **Total files audited: 529.**

## Catalog method

This audit is **count-driven** rather than line-by-line itemized, for the simple reason that the codebase contains ~22 000 `//` comments and ~3 800 `/**< */` trailing comments. Line-itemizing every site would balloon the worklist past 100 000 lines and obscure rather than clarify the work. Instead, each per-file row carries a count for each violation category -- enough to gauge effort and to drive `sed`-style mechanical sweeps for the bulk categories.

### Violation categories tracked

| Column | Meaning | Mechanical or judgment-required |
|---|---|---|
| `//` | Single-line `//` comments (must become `///`) | Mechanical (`sed`) |
| `/**<` | Trailing `/**< */` (must become `///<`) | Mechanical (`sed`) |
| `/**` blocks | Count of `/**`-style block comments. **NOT a direct violation count** -- needs judgment per occurrence. A `/** */` block above an exported header declaration is correct; the same form above a static function in a `.c` file, above a trivial header decl, or inside a function body is a violation. Use this column as an "attention budget" indicator. |
| static-fn `/**` | `/**`-block comments immediately preceding a `static` function definition. Should collapse to `///` brief / `///` run. | Judgment, mostly mechanical |
| in-fn `/* */` | Multi-line `/* */` block comments inside function bodies (heuristic: starts with indented `/*` not followed by `*`, and not closed on the same line). Should become `///` runs. | Judgment per site |
| no @file | File is missing a `@file` block in its first 40 lines. | Mechanical (add header) |

### Out of scope (verified zero occurrences)

- `\command` backslash Doxygen prefixes -- `grep` reports **0** across the audited tree.

### Note on duplicate Doxygen between `.h` and `.c`

This pattern is endemic -- virtually every `.c` file that implements exported declarations from a paired `.h` repeats the full `/** @brief / @param / @return */` block above the implementation. Per-function itemization at this scale is infeasible; the **per-file `/**` block count for `.c` files** is the proxy. For each non-static exported function the source-side `/** */` block should be replaced with a single-line `/* See foo.h for contract. */` (or simply nothing), and the contract lives only in the header.

A wave-time heuristic when sweeping a `.c` file: every `/** */` block above a non-`static` function whose name matches a `/** */` block in the paired header is duplicate Doxygen -- replace with a `/* */` pointer.

---

| Dir | Files | Lines | `//` | `/**<` | trivial `/**` | static-fn `/**` | in-fn `/* */` | no-@file | Total violations |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `src/` | 33 | 58863 | 5383 | 21 | 779 | 329 | 252 | 0 | **5985** |
| `include/lle/` | 49 | 23303 | 1142 | 2104 | 1469 | 0 | 8 | 0 | **3254** |
| `tests/unit/` | 52 | 38361 | 1516 | 1 | 66 | 12 | 78 | 2 | **1609** |
| `src/display/` | 10 | 13881 | 1300 | 0 | 215 | 101 | 55 | 0 | **1456** |
| `tests/lle/unit/` | 40 | 20983 | 1251 | 3 | 60 | 15 | 112 | 21 | **1402** |
| `include/` | 38 | 13669 | 276 | 1001 | 1056 | 0 | 5 | 0 | **1282** |
| `src/lle/unicode/` | 7 | 3759 | 1130 | 4 | 69 | 10 | 5 | 0 | **1149** |
| `include/display/` | 9 | 6024 | 1039 | 4 | 339 | 0 | 0 | 0 | **1043** |
| `src/builtins/` | 63 | 13322 | 919 | 8 | 157 | 22 | 44 | 0 | **993** |
| `src/lle/` | 10 | 7353 | 690 | 11 | 154 | 67 | 124 | 0 | **892** |
| `src/lle/core/` | 8 | 9633 | 805 | 6 | 127 | 24 | 32 | 0 | **867** |
| `src/lle/prompt/` | 8 | 9801 | 664 | 6 | 196 | 74 | 20 | 0 | **764** |
| `src/lle/history/` | 13 | 8750 | 609 | 76 | 206 | 44 | 20 | 0 | **749** |
| `src/lle/completion/` | 14 | 9271 | 550 | 18 | 143 | 43 | 76 | 0 | **687** |
| `src/lle/display/` | 6 | 4900 | 479 | 8 | 96 | 50 | 46 | 0 | **583** |
| `src/lle/keybinding/` | 4 | 5860 | 400 | 16 | 144 | 38 | 81 | 0 | **535** |
| `tests/lle/compliance/` | 23 | 7209 | 397 | 0 | 122 | 38 | 26 | 9 | **470** |
| `tests/lle/functional/` | 15 | 5860 | 430 | 0 | 15 | 0 | 3 | 10 | **443** |
| `src/lle/terminal/` | 10 | 3999 | 340 | 0 | 83 | 30 | 50 | 0 | **420** |
| `src/lle/input/` | 10 | 4351 | 342 | 6 | 121 | 26 | 10 | 0 | **384** |
| `src/debug/` | 6 | 4740 | 308 | 7 | 99 | 17 | 9 | 0 | **341** |
| `include/lle/prompt/` | 9 | 2812 | 55 | 283 | 177 | 0 | 1 | 0 | **339** |
| `include/lle/completion/` | 13 | 2849 | 131 | 194 | 172 | 0 | 0 | 0 | **325** |
| `src/lle/adaptive/` | 6 | 4497 | 248 | 1 | 140 | 62 | 6 | 0 | **317** |
| `src/lle/multiline/` | 8 | 4353 | 270 | 0 | 108 | 30 | 5 | 0 | **305** |
| `src/lle/buffer/` | 4 | 2494 | 255 | 0 | 33 | 12 | 3 | 0 | **270** |
| `tests/lle/integration/` | 7 | 2668 | 244 | 0 | 23 | 11 | 4 | 2 | **261** |
| `src/lle/event/` | 6 | 3024 | 221 | 0 | 72 | 4 | 4 | 0 | **229** |
| `src/lle/widget/` | 3 | 1354 | 67 | 0 | 35 | 30 | 4 | 0 | **101** |
| `src/builtins/display/` | 16 | 2676 | 72 | 0 | 23 | 5 | 8 | 0 | **85** |
| `tests/fuzz/` | 5 | 1380 | 60 | 9 | 11 | 2 | 15 | 0 | **86** |
| `tests/manual/` | 4 | 661 | 76 | 0 | 1 | 0 | 0 | 4 | **80** |
| `tests/lle/stress/` | 2 | 1048 | 73 | 0 | 2 | 0 | 0 | 0 | **73** |
| `tests/lle/` | 1 | 509 | 58 | 0 | 0 | 0 | 0 | 1 | **59** |
| `src/libfuzzy/` | 1 | 939 | 43 | 0 | 24 | 8 | 3 | 0 | **54** |
| `tests/lle/e2e/` | 1 | 551 | 48 | 0 | 1 | 0 | 0 | 0 | **48** |
| `tests/lle/benchmarks/` | 2 | 587 | 37 | 0 | 2 | 0 | 7 | 0 | **44** |
| `tests/lle/manual/` | 1 | 277 | 21 | 0 | 0 | 0 | 0 | 1 | **22** |
| `src/libhashtable/` | 7 | 1288 | 7 | 0 | 73 | 14 | 0 | 0 | **21** |
| `include/libhashtable/` | 1 | 560 | 3 | 7 | 68 | 0 | 0 | 0 | **10** |
| `tests/` | 2 | 633 | 7 | 0 | 12 | 0 | 2 | 0 | **9** |
| `include/builtins/` | 1 | 41 | 1 | 0 | 1 | 0 | 0 | 0 | **1** |

### Per-directory density (violations / file)

| Dir | Files | Total | Density |
|---|---:|---:|---:|
| `src/` | 33 | 5985 | 181.4 |
| `src/lle/unicode/` | 7 | 1149 | 164.1 |
| `src/display/` | 10 | 1456 | 145.6 |
| `src/lle/keybinding/` | 4 | 535 | 133.8 |
| `include/display/` | 9 | 1043 | 115.9 |
| `src/lle/core/` | 8 | 867 | 108.4 |
| `src/lle/display/` | 6 | 583 | 97.2 |
| `src/lle/prompt/` | 8 | 764 | 95.5 |
| `src/lle/` | 10 | 892 | 89.2 |
| `src/lle/buffer/` | 4 | 270 | 67.5 |
| `include/lle/` | 49 | 3254 | 66.4 |
| `tests/lle/` | 1 | 59 | 59.0 |
| `src/lle/history/` | 13 | 749 | 57.6 |
| `src/debug/` | 6 | 341 | 56.8 |
| `src/libfuzzy/` | 1 | 54 | 54.0 |
| `src/lle/adaptive/` | 6 | 317 | 52.8 |
| `src/lle/completion/` | 14 | 687 | 49.1 |
| `tests/lle/e2e/` | 1 | 48 | 48.0 |
| `src/lle/terminal/` | 10 | 420 | 42.0 |
| `src/lle/input/` | 10 | 384 | 38.4 |
| `src/lle/event/` | 6 | 229 | 38.2 |
| `src/lle/multiline/` | 8 | 305 | 38.1 |
| `include/lle/prompt/` | 9 | 339 | 37.7 |
| `tests/lle/integration/` | 7 | 261 | 37.3 |
| `tests/lle/stress/` | 2 | 73 | 36.5 |

---

## Suggested wave ordering

These recommendations balance density (violations per file), risk (small isolated subsystems vs. cross-cutting infrastructure), and review-unit clarity. Pick first-wave targets that are dense AND isolated.

### Wave 1 -- small, isolated, high signal-to-review-cost (best first commits)

1. **`src/libhashtable/`** (7 files, total 21 violations) -- third-party-pedigree code (Michael Berry's libhashtable, vendored). Only `//` comments to flip; no `/**< */`; no header drift. Single mechanical `sed` pass, low review burden, isolated subsystem. The matching `include/libhashtable/ht.h` (10 violations) is one more file.
2. **`src/libfuzzy/`** (1 file, 54 violations) -- single file. Easy one-shot commit.
3. **`src/builtins/display/`** (16 files, 85 violations) -- already 2 of the 16 are CLEAN (`lle_hook.c`, `lle_status.c`). Tiny per-file footprint; isolated from execution path.
4. **`tests/fuzz/`** (5 files, 86 violations) -- small, no production risk.
5. **`tests/manual/`** + **`tests/lle/manual/`** + **`tests/lle/benchmarks/`** + **`tests/lle/stress/`** + **`tests/lle/e2e/`** -- all small, mostly `//` and missing-`@file` only. Bundle as a single "tests-housekeeping" sweep.
6. **`include/lle/prompt/`** + **`include/lle/completion/`** -- almost pure header-form drift: `/**< */` -> `///<` and `/** */`-on-trivial-decl -> `///` brief. No code semantics involved; tightly scoped per subsystem.

### Wave 2 -- focused subsystems (each its own commit)

- **`src/lle/buffer/`** (4 files) -- small, foundational, well-scoped.
- **`src/lle/widget/`** (3 files) -- fresh subsystem, low cross-pollination.
- **`src/lle/event/`** + **`src/lle/multiline/`** + **`src/lle/adaptive/`** -- moderate, each independently reviewable.
- **`src/lle/input/`** + **`src/lle/terminal/`** -- input pipeline, can land as one commit if reviewed together.
- **`src/debug/`** -- 6 files, isolated. Good unit.

### Wave 3 -- large but contained LLE subsystems

- **`src/lle/history/`** (13 files, 749 violations + 76 `/**< */`) -- history has `/**< */` in its source files (uncommon), worth a careful sweep.
- **`src/lle/completion/`** (14 files, 687 violations) -- completion-subsystem-internal; respects the postmortem boundary.
- **`src/lle/keybinding/`** (4 files, 535 violations) -- but very dense per file (~134/file).
- **`src/lle/display/`** + **`src/lle/prompt/`** + **`src/lle/core/`** -- display/prompt/core; each its own wave.
- **`src/lle/unicode/`** (7 files, 1 149 violations, 164/file) -- densest LLE subsystem; almost pure `//` -> `///` flips, but the volume per file means each file is its own commit.

### Wave 4 -- central headers (high blast radius, careful review required)

- **`include/lle/`** (49 files, 3 254 violations, 2 104 of them `/**< */`) -- the trailing-comment hotspot of the codebase. Almost mechanical, but every consumer header re-reads these so commit hygiene matters. Recommend sweeping in groups of ~10 headers per commit.
- **`include/`** (38 files, 1 282 violations, 1 001 `/**< */`) -- same shape: dense in `/**< */`, mechanical.
- **`include/display/`** (9 files, 1 043 violations) -- see below.

### Wave 5 -- the core trio (separate commits, hardest review)

- **`src/`** (33 root-level files, 5 985 violations) -- anchored by `src/executor.c` (2 020 violations alone), `src/parser.c` (794), `src/tokenizer.c` (349), `src/config.c` (286), `src/symtable.c` (226). Each of these top-five files deserves its own commit; the remaining 28 root files can be bundled into 2-3 commits.
- **`src/display/`** (10 files, 1 456 violations, 145.6/file) and the paired **`include/display/`** (9 files, 1 043 violations) -- the display layer's clear-and-redraw architecture means heavy in-fn `/* */` blocks explaining cursor math. Should be reviewed by someone who knows screen_buffer.c.
- **`tests/unit/`** (52 files, 1 609 violations) and **`tests/lle/unit/`** (40 files, 1 402 violations, 21 missing `@file`) -- large test corpora, can be batched into 3-4 commits each by feature area.

### Mechanical-pass note

For categories `//` and `/**< */`, the transformation is unambiguous:

```bash
# (illustrative only -- DO NOT run from this audit)
sed -i 's|/\*\*<\([^*]\)|///<\1|g' file.h     # trailing dox
sed -i 's|^\(\s*\)//\([^/]\)|\1///\2|g' file  # single-line //
```

Use `git diff` after each pass -- line counts should be identical, only comment-leader characters change.

---

## Files missing `@file` block (50 files)

Almost all are test files. Adding a `@file` block is purely additive.

- `tests/lle/compliance/spec_03_atomic_operations_test.c`
- `tests/lle/compliance/spec_03_atomic_simple_test.c`
- `tests/lle/compliance/spec_03_buffer_validator_test.c`
- `tests/lle/compliance/spec_03_cursor_manager_test.c`
- `tests/lle/compliance/spec_03_utf8_index_test.c`
- `tests/lle/compliance/spec_03_utf8_unicode_compliance.c`
- `tests/lle/compliance/spec_22_history_buffer_compliance.c`
- `tests/lle/compliance/spec_25_keybinding_compliance.c`
- `tests/lle/compliance/spec_26_adaptive_terminal_compliance.c`
- `tests/lle/functional/buffer_operations_test.c`
- `tests/lle/functional/test_completion_mock.c`
- `tests/lle/functional/test_history_phase1_day1.c`
- `tests/lle/functional/test_history_phase1_day2.c`
- `tests/lle/functional/test_history_phase1_day3.c`
- `tests/lle/functional/test_history_phase3_day8.c`
- `tests/lle/functional/test_history_phase3_day9.c`
- `tests/lle/functional/test_history_phase4_complete.c`
- `tests/lle/functional/test_memory_mock.c`
- `tests/lle/functional/test_memory_mock.h`
- `tests/lle/integration/test_fkey_detection.c`
- `tests/lle/integration/test_history_phase1_integration.c`
- `tests/lle/manual/test_fkey_manual.c`
- `tests/lle/test_utf8_movement.c`
- `tests/lle/unit/test_adaptive_controllers.c`
- `tests/lle/unit/test_adaptive_detection.c`
- `tests/lle/unit/test_adaptive_fallback.c`
- `tests/lle/unit/test_completion_types.c`
- `tests/lle/unit/test_input_stream.c`
- `tests/lle/unit/test_input_utf8_processor.c`
- `tests/lle/unit/test_key_detector.c`
- `tests/lle/unit/test_keybinding.c`
- `tests/lle/unit/test_kill_ring.c`
- `tests/lle/unit/test_mouse_parser.c`
- `tests/lle/unit/test_parser_state_machine.c`
- `tests/lle/unit/test_prompt_expansion.c`
- `tests/lle/unit/test_segment_system.c`
- `tests/lle/unit/test_sequence_parser.c`
- `tests/lle/unit/test_splicer.c`
- `tests/lle/unit/test_ssh_completion.c`
- `tests/lle/unit/test_template_engine.c`
- `tests/lle/unit/test_terminal_capabilities.c`
- `tests/lle/unit/test_terminal_event_reading.c`
- `tests/lle/unit/test_terminal_state.c`
- `tests/lle/unit/test_word_context.c`
- `tests/manual/debug_grapheme.c`
- `tests/manual/demo_completion_menu_themed.c`
- `tests/manual/demo_completion_menu.c`
- `tests/manual/test_grapheme_nav.c`
- `tests/unit/test_lush_stubs.c`
- `tests/unit/test_shell_quoting.c`

---

## CLEAN files (29 files -- no violations across any category)

These files conform to the unified ruleset. **Do not re-edit.**

- `include/alias.h`
- `include/autoload.h`
- `include/errors.h`
- `include/history.h`
- `include/input.h`
- `include/pattern_match.h`
- `include/strings.h`
- `src/builtins/bin_bg.c`
- `src/builtins/bin_clear.c`
- `src/builtins/bin_colon.c`
- `src/builtins/bin_config.c`
- `src/builtins/bin_false.c`
- `src/builtins/bin_fg.c`
- `src/builtins/bin_help.c`
- `src/builtins/bin_history.c`
- `src/builtins/bin_jobs.c`
- `src/builtins/bin_mode.c`
- `src/builtins/bin_terminal.c`
- `src/builtins/bin_true.c`
- `src/builtins/display/lle_hook.c`
- `src/builtins/display/lle_status.c`
- `src/globals.c`
- `src/libhashtable/ht_fnv1a.c`
- `src/libhashtable/ht_strblob.c`
- `src/libhashtable/ht_strdouble.c`
- `src/libhashtable/ht_strfloat.c`
- `src/libhashtable/ht_strint.c`
- `src/libhashtable/ht_strstr.c`
- `src/lle/lle_debug_prompt_state.c`

---

## Per-file catalog

Each directory below lists every audited file with violation counts.
- `//` and `/**<` columns are exact violation counts.
- `/**` blocks column is informational (not all `/** */` blocks are violations -- see "Catalog method" above).
- `static-fn /**` is a judgment-required violation (collapse to `///` brief).
- `in-fn /* */` is a judgment-required violation (convert to `///` run).
- `no @file` = YES means add a `@file` block.
- A file with all zeros across `//`, `/**<`, `static-fn /**`, `in-fn /* */`, and `no @file` is CLEAN regardless of `/**` block count.

### `include/` (38 files, 31 need work, 7 clean)

Totals: `//`=276  `/**< */`=1001  trivial-`/**`-blocks=1056  static-fn-`/**`=0  in-fn `/* */`=5  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/config.h` | 877 | 14 | 134 | 72 | 0 | 0 |  |
| `include/shell_error.h` | 485 | 16 | 98 | 29 | 0 | 0 |  |
| `include/symtable.h` | 1375 | 54 | 47 | 113 | 0 | 0 |  |
| `include/tokenizer.h` | 340 | 17 | 75 | 22 | 0 | 0 |  |
| `include/node.h` | 212 | 37 | 51 | 7 | 0 | 0 |  |
| `include/debug.h` | 977 | 10 | 73 | 85 | 0 | 0 |  |
| `include/shell_mode.h` | 472 | 22 | 60 | 29 | 0 | 0 |  |
| `include/display_integration.h` | 709 | 17 | 62 | 56 | 0 | 0 |  |
| `include/lush.h` | 763 | 5 | 57 | 69 | 0 | 1 |  |
| `include/lush_memory_pool.h` | 355 | 15 | 44 | 36 | 0 | 0 |  |
| `include/executor.h` | 679 | 13 | 36 | 45 | 0 | 4 |  |
| `include/posix_history.h` | 646 | 7 | 45 | 47 | 0 | 0 |  |
| `include/compat.h` | 503 | 2 | 42 | 39 | 0 | 0 |  |
| `include/input_continuation.h` | 180 | 8 | 32 | 12 | 0 | 0 |  |
| `include/fixer.h` | 417 | 1 | 38 | 34 | 0 | 0 |  |
| `include/lush_plugin.h` | 673 | 14 | 19 | 39 | 0 | 0 |  |
| `include/config_registry.h` | 528 | 2 | 27 | 38 | 0 | 0 |  |
| `include/toml_parser.h` | 395 | 1 | 16 | 26 | 0 | 0 |  |
| `include/autocorrect.h` | 294 | 1 | 14 | 28 | 0 | 0 |  |
| `include/parser.h` | 335 | 5 | 6 | 24 | 0 | 0 |  |
| `include/builtins.h` | 571 | 7 | 3 | 49 | 0 | 0 |  |
| `include/fuzzy_match.h` | 278 | 1 | 7 | 20 | 0 | 0 |  |
| `include/redirection.h` | 95 | 1 | 6 | 8 | 0 | 0 |  |
| `include/expand.h` | 70 | 1 | 3 | 4 | 0 | 0 |  |
| `include/init.h` | 88 | 0 | 3 | 7 | 0 | 0 |  |
| `include/signals.h` | 223 | 0 | 3 | 23 | 0 | 0 |  |
| `include/arithmetic.h` | 126 | 1 | 0 | 6 | 0 | 0 |  |
| `include/dirstack.h` | 105 | 1 | 0 | 12 | 0 | 0 |  |
| `include/lush_fork.h` | 59 | 1 | 0 | 2 | 0 | 0 |  |
| `include/node_to_source.h` | 41 | 1 | 0 | 3 | 0 | 0 |  |
| `include/version.h` | 28 | 1 | 0 | 1 | 0 | 0 |  |
| `include/alias.h` | 145 | 0 | 0 | 16 | 0 | 0 |  |
| `include/autoload.h` | 69 | 0 | 0 | 5 | 0 | 0 |  |
| `include/errors.h` | 68 | 0 | 0 | 5 | 0 | 0 |  |
| `include/history.h` | 66 | 0 | 0 | 7 | 0 | 0 |  |
| `include/input.h` | 126 | 0 | 0 | 10 | 0 | 0 |  |
| `include/pattern_match.h` | 42 | 0 | 0 | 2 | 0 | 0 |  |
| `include/strings.h` | 254 | 0 | 0 | 26 | 0 | 0 |  |

### `include/builtins/` (1 files, 1 need work, 0 clean)

Totals: `//`=1  `/**< */`=0  trivial-`/**`-blocks=1  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/builtins/display.h` | 41 | 1 | 0 | 1 | 0 | 0 |  |

### `include/display/` (9 files, 9 need work, 0 clean)

Totals: `//`=1039  `/**< */`=4  trivial-`/**`-blocks=339  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/display/command_layer.h` | 650 | 158 | 0 | 45 | 0 | 0 |  |
| `include/display/display_controller.h` | 1046 | 142 | 4 | 59 | 0 | 0 |  |
| `include/display/layer_events.h` | 731 | 141 | 0 | 38 | 0 | 0 |  |
| `include/display/composition_engine.h` | 657 | 122 | 0 | 36 | 0 | 0 |  |
| `include/display/terminal_control.h` | 800 | 121 | 0 | 45 | 0 | 0 |  |
| `include/display/prompt_layer.h` | 582 | 118 | 0 | 26 | 0 | 0 |  |
| `include/display/autosuggestions_layer.h` | 596 | 108 | 0 | 41 | 0 | 0 |  |
| `include/display/base_terminal.h` | 493 | 83 | 0 | 19 | 0 | 0 |  |
| `include/display/screen_buffer.h` | 469 | 46 | 0 | 30 | 0 | 0 |  |

### `include/libhashtable/` (1 files, 1 need work, 0 clean)

Totals: `//`=3  `/**< */`=7  trivial-`/**`-blocks=68  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/libhashtable/ht.h` | 560 | 3 | 7 | 68 | 0 | 0 |  |

### `include/lle/` (49 files, 49 need work, 0 clean)

Totals: `//`=1142  `/**< */`=2104  trivial-`/**`-blocks=1469  static-fn-`/**`=0  in-fn `/* */`=8  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/lle/error_handling.h` | 1093 | 61 | 294 | 69 | 0 | 0 |  |
| `include/lle/input_parsing.h` | 1726 | 68 | 267 | 132 | 0 | 1 |  |
| `include/lle/display_integration.h` | 956 | 50 | 264 | 48 | 0 | 1 |  |
| `include/lle/buffer_management.h` | 1327 | 101 | 169 | 82 | 0 | 0 |  |
| `include/lle/event_system.h` | 1453 | 46 | 215 | 62 | 0 | 0 |  |
| `include/lle/terminal_abstraction.h` | 834 | 176 | 58 | 57 | 0 | 1 |  |
| `include/lle/performance.h` | 1937 | 177 | 14 | 79 | 0 | 0 |  |
| `include/lle/memory_management.h` | 1600 | 171 | 0 | 131 | 0 | 0 |  |
| `include/lle/history.h` | 1788 | 48 | 120 | 148 | 0 | 0 |  |
| `include/lle/syntax_highlighting.h` | 385 | 36 | 94 | 15 | 0 | 3 |  |
| `include/lle/hashtable.h` | 396 | 13 | 74 | 36 | 0 | 0 |  |
| `include/lle/command_structure.h` | 205 | 1 | 67 | 14 | 0 | 0 |  |
| `include/lle/history_buffer_integration.h` | 433 | 14 | 48 | 27 | 0 | 0 |  |
| `include/lle/lle_editor.h` | 242 | 13 | 38 | 12 | 0 | 1 |  |
| `include/lle/async_worker.h` | 330 | 9 | 37 | 19 | 0 | 0 |  |
| `include/lle/testing.h` | 1334 | 45 | 0 | 56 | 0 | 0 |  |
| `include/lle/adaptive_terminal_integration.h` | 562 | 15 | 26 | 38 | 0 | 0 |  |
| `include/lle/edit_session_manager.h` | 213 | 2 | 39 | 16 | 0 | 0 |  |
| `include/lle/lle_shell_integration.h` | 381 | 8 | 24 | 22 | 0 | 0 |  |
| `include/lle/arena.h` | 420 | 12 | 19 | 26 | 0 | 0 |  |
| `include/lle/widget_hooks.h` | 284 | 4 | 22 | 14 | 0 | 0 |  |
| `include/lle/widget_system.h` | 302 | 10 | 16 | 16 | 0 | 0 |  |
| `include/lle/edit_cache.h` | 163 | 2 | 23 | 13 | 0 | 0 |  |
| `include/lle/multiline_parser.h` | 159 | 2 | 22 | 12 | 0 | 0 |  |
| `include/lle/formatting_engine.h` | 177 | 2 | 21 | 13 | 0 | 0 |  |
| `include/lle/keybinding.h` | 522 | 7 | 16 | 33 | 0 | 0 |  |
| `include/lle/structure_analyzer.h` | 176 | 2 | 21 | 13 | 0 | 0 |  |
| `include/lle/lle_shell_event_hub.h` | 305 | 2 | 17 | 18 | 0 | 0 |  |
| `include/lle/grapheme_detector.h` | 73 | 1 | 15 | 5 | 0 | 0 |  |
| `include/lle/history_buffer_bridge.h` | 164 | 2 | 13 | 13 | 0 | 0 |  |
| `include/lle/reconstruction_engine.h` | 152 | 2 | 13 | 11 | 0 | 0 |  |
| `include/lle/keybinding_config.h` | 220 | 1 | 13 | 12 | 0 | 0 |  |
| `include/lle/secure_memory.h` | 149 | 11 | 0 | 4 | 0 | 1 |  |
| `include/lle/notification.h` | 204 | 1 | 10 | 13 | 0 | 0 |  |
| `include/lle/lle_watchdog.h` | 149 | 7 | 3 | 12 | 0 | 0 |  |
| `include/lle/lle_shell_hooks.h` | 198 | 1 | 5 | 13 | 0 | 0 |  |
| `include/lle/unicode_compare.h` | 181 | 1 | 3 | 13 | 0 | 0 |  |
| `include/lle/git_command.h` | 85 | 1 | 2 | 4 | 0 | 0 |  |
| `include/lle/keybinding_actions.h` | 702 | 1 | 2 | 58 | 0 | 0 |  |
| `include/lle/lle_readline.h` | 110 | 3 | 0 | 5 | 0 | 0 |  |
| `include/lle/widget_hooks_bridge.h` | 44 | 3 | 0 | 2 | 0 | 0 |  |
| `include/lle/kill_ring.h` | 303 | 2 | 0 | 19 | 0 | 0 |  |
| `include/lle/lle_readline_state.h` | 230 | 2 | 0 | 16 | 0 | 0 |  |
| `include/lle/char_width.h` | 40 | 1 | 0 | 3 | 0 | 0 |  |
| `include/lle/lle_safety.h` | 121 | 1 | 0 | 7 | 0 | 0 |  |
| `include/lle/unicode_case.h` | 163 | 1 | 0 | 10 | 0 | 0 |  |
| `include/lle/unicode_grapheme.h` | 55 | 1 | 0 | 3 | 0 | 0 |  |
| `include/lle/utf8_index.h` | 138 | 1 | 0 | 13 | 0 | 0 |  |
| `include/lle/utf8_support.h` | 119 | 1 | 0 | 12 | 0 | 0 |  |

### `include/lle/completion/` (13 files, 13 need work, 0 clean)

Totals: `//`=131  `/**< */`=194  trivial-`/**`-blocks=172  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/lle/completion/completion_types.h` | 345 | 25 | 36 | 23 | 0 | 0 |  |
| `include/lle/completion/builtin_completions.h` | 164 | 22 | 24 | 10 | 0 | 0 |  |
| `include/lle/completion/word_context.h` | 303 | 11 | 35 | 11 | 0 | 0 |  |
| `include/lle/completion/completion_menu_state.h` | 233 | 19 | 19 | 16 | 0 | 0 |  |
| `include/lle/completion/completion_menu_renderer.h` | 219 | 19 | 13 | 10 | 0 | 0 |  |
| `include/lle/completion/completion_system.h` | 149 | 14 | 7 | 10 | 0 | 0 |  |
| `include/lle/completion/custom_source.h` | 418 | 5 | 16 | 28 | 0 | 0 |  |
| `include/lle/completion/source_manager.h` | 166 | 4 | 17 | 10 | 0 | 0 |  |
| `include/lle/completion/completion_state.h` | 109 | 8 | 10 | 7 | 0 | 0 |  |
| `include/lle/completion/ssh_hosts.h` | 166 | 1 | 12 | 14 | 0 | 0 |  |
| `include/lle/completion/splicer.h` | 217 | 1 | 5 | 6 | 0 | 0 |  |
| `include/lle/completion/completion_menu_logic.h` | 200 | 1 | 0 | 16 | 0 | 0 |  |
| `include/lle/completion/completion_sources.h` | 160 | 1 | 0 | 11 | 0 | 0 |  |

### `include/lle/prompt/` (9 files, 9 need work, 0 clean)

Totals: `//`=55  `/**< */`=283  trivial-`/**`-blocks=177  static-fn-`/**`=0  in-fn `/* */`=1  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `include/lle/prompt/theme.h` | 684 | 28 | 154 | 41 | 0 | 1 |  |
| `include/lle/prompt/segment.h` | 550 | 18 | 54 | 43 | 0 | 0 |  |
| `include/lle/prompt/composer.h` | 367 | 2 | 33 | 22 | 0 | 0 |  |
| `include/lle/prompt/template.h` | 334 | 2 | 18 | 21 | 0 | 0 |  |
| `include/lle/prompt/theme_loader.h` | 283 | 1 | 12 | 18 | 0 | 0 |  |
| `include/lle/prompt/theme_parser.h` | 373 | 1 | 10 | 23 | 0 | 0 |  |
| `include/lle/prompt/powerline.h` | 59 | 1 | 2 | 3 | 0 | 0 |  |
| `include/lle/prompt/prompt_expansion.h` | 73 | 1 | 0 | 3 | 0 | 0 |  |
| `include/lle/prompt/transient.h` | 89 | 1 | 0 | 3 | 0 | 0 |  |

### `src/` (33 files, 32 need work, 1 clean)

Totals: `//`=5383  `/**< */`=21  trivial-`/**`-blocks=779  static-fn-`/**`=329  in-fn `/* */`=252  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/executor.c` | 19257 | 1863 | 5 | 161 | 111 | 152 |  |
| `src/parser.c` | 6741 | 762 | 2 | 62 | 46 | 30 |  |
| `src/tokenizer.c` | 2787 | 338 | 0 | 24 | 7 | 11 |  |
| `src/config.c` | 4138 | 282 | 0 | 78 | 17 | 4 |  |
| `src/display_integration.c` | 2385 | 268 | 0 | 43 | 8 | 1 |  |
| `src/symtable.c` | 3151 | 220 | 0 | 116 | 7 | 6 |  |
| `src/init.c` | 1203 | 165 | 0 | 13 | 7 | 5 |  |
| `src/shell_mode.c` | 977 | 166 | 0 | 1 | 0 | 1 |  |
| `src/input.c` | 1149 | 152 | 1 | 18 | 10 | 2 |  |
| `src/redirection.c` | 1943 | 154 | 0 | 16 | 9 | 0 |  |
| `src/arithmetic.c` | 1760 | 129 | 0 | 18 | 17 | 2 |  |
| `src/lush_memory_pool.c` | 1072 | 96 | 0 | 35 | 12 | 0 |  |
| `src/input_continuation.c` | 819 | 92 | 0 | 14 | 5 | 0 |  |
| `src/fixer.c` | 1228 | 85 | 2 | 6 | 4 | 1 |  |
| `src/compat.c` | 1448 | 69 | 2 | 23 | 17 | 3 |  |
| `src/lush.c` | 505 | 81 | 0 | 8 | 1 | 5 |  |
| `src/posix_opts.c` | 728 | 62 | 3 | 14 | 6 | 5 |  |
| `src/posix_history.c` | 772 | 66 | 0 | 9 | 7 | 0 |  |
| `src/autocorrect.c` | 890 | 56 | 0 | 28 | 3 | 6 |  |
| `src/config_registry.c` | 939 | 43 | 6 | 14 | 7 | 1 |  |
| `src/lush_plugin.c` | 788 | 42 | 0 | 4 | 3 | 4 |  |
| `src/toml_parser.c` | 853 | 46 | 0 | 2 | 1 | 0 |  |
| `src/signals.c` | 586 | 34 | 0 | 25 | 7 | 5 |  |
| `src/node_to_source.c` | 703 | 29 | 0 | 6 | 3 | 4 |  |
| `src/shell_error.c` | 516 | 26 | 0 | 6 | 3 | 0 |  |
| `src/pattern_match.c` | 450 | 13 | 0 | 7 | 6 | 2 |  |
| `src/autoload.c` | 328 | 11 | 0 | 5 | 4 | 2 |  |
| `src/dirstack.c` | 232 | 13 | 0 | 1 | 0 | 0 |  |
| `src/expand.c` | 176 | 10 | 0 | 7 | 0 | 0 |  |
| `src/errors.c` | 79 | 4 | 0 | 2 | 1 | 0 |  |
| `src/node.c` | 155 | 5 | 0 | 6 | 0 | 0 |  |
| `src/opts.c` | 46 | 1 | 0 | 4 | 0 | 0 |  |
| `src/globals.c` | 59 | 0 | 0 | 3 | 0 | 0 |  |

### `src/builtins/` (63 files, 51 need work, 12 clean)

Totals: `//`=919  `/**< */`=8  trivial-`/**`-blocks=157  static-fn-`/**`=22  in-fn `/* */`=44  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/builtins/alias.c` | 864 | 71 | 0 | 21 | 3 | 0 |  |
| `src/builtins/bin_declare.c` | 637 | 52 | 0 | 2 | 0 | 2 |  |
| `src/builtins/bin_zsh_stubs.c` | 195 | 51 | 0 | 1 | 0 | 1 |  |
| `src/builtins/fc.c` | 818 | 33 | 0 | 15 | 13 | 2 |  |
| `src/builtins/bin_cd.c` | 344 | 37 | 0 | 3 | 2 | 1 |  |
| `src/builtins/bin_test.c` | 230 | 38 | 0 | 4 | 2 | 0 |  |
| `src/builtins/bin_zstyle.c` | 350 | 36 | 3 | 1 | 0 | 0 |  |
| `src/builtins/bin_getopts.c` | 292 | 38 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_read.c` | 447 | 31 | 0 | 2 | 1 | 4 |  |
| `src/builtins/bin_local.c` | 325 | 31 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_command.c` | 232 | 30 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_printf.c` | 405 | 29 | 0 | 3 | 0 | 1 |  |
| `src/builtins/bin_mapfile.c` | 277 | 26 | 0 | 2 | 0 | 2 |  |
| `src/builtins/bin_env.c` | 263 | 25 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_readonly.c` | 135 | 26 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_shift.c` | 180 | 24 | 0 | 3 | 1 | 0 |  |
| `src/builtins/bin_export.c` | 160 | 24 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_bindkey.c` | 300 | 20 | 3 | 1 | 0 | 0 |  |
| `src/builtins/bin_shopt.c` | 258 | 18 | 0 | 2 | 0 | 2 |  |
| `src/builtins/bin_display.c` | 635 | 19 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_ulimit.c` | 325 | 19 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_zle.c` | 201 | 16 | 2 | 1 | 0 | 0 |  |
| `src/builtins/bin_exec.c` | 212 | 16 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_lint.c` | 211 | 17 | 0 | 2 | 0 | 0 |  |
| `src/builtins/builtins.c` | 451 | 14 | 0 | 1 | 0 | 3 |  |
| `src/builtins/bin_disown.c` | 213 | 16 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_print.c` | 360 | 1 | 0 | 1 | 0 | 13 |  |
| `src/builtins/bin_trap.c` | 197 | 13 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_wait.c` | 195 | 14 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_source.c` | 153 | 12 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_type.c` | 186 | 13 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_analyze.c` | 137 | 10 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_pushd.c` | 319 | 10 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_return.c` | 123 | 10 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_hash.c` | 115 | 9 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_times.c` | 66 | 8 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_umask.c` | 171 | 8 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_let.c` | 146 | 4 | 0 | 2 | 0 | 3 |  |
| `src/builtins/bin_pwd.c` | 113 | 7 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_setopt.c` | 132 | 5 | 0 | 2 | 0 | 2 |  |
| `src/builtins/bin_echo.c` | 95 | 5 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_unset.c` | 79 | 5 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_unsetopt.c` | 86 | 5 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_debug.c` | 318 | 5 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_popd.c` | 295 | 5 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_exit.c` | 38 | 4 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_eval.c` | 53 | 2 | 0 | 2 | 0 | 1 |  |
| `src/builtins/bin_break.c` | 140 | 2 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_continue.c` | 143 | 2 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_network.c` | 87 | 2 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_dirs.c` | 94 | 1 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_bg.c` | 31 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_clear.c` | 25 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_colon.c` | 30 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_config.c` | 25 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_false.c` | 24 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_fg.c` | 31 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_help.c` | 25 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_history.c` | 33 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_jobs.c` | 29 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_mode.c` | 113 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_terminal.c` | 131 | 0 | 0 | 2 | 0 | 0 |  |
| `src/builtins/bin_true.c` | 24 | 0 | 0 | 2 | 0 | 0 |  |

### `src/builtins/display/` (16 files, 14 need work, 2 clean)

Totals: `//`=72  `/**< */`=0  trivial-`/**`-blocks=23  static-fn-`/**`=5  in-fn `/* */`=8  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/builtins/display/lle_inspect_widget.c` | 290 | 14 | 0 | 6 | 4 | 0 |  |
| `src/builtins/display/lle_history.c` | 345 | 11 | 0 | 1 | 0 | 1 |  |
| `src/builtins/display/lle_theme.c` | 189 | 11 | 0 | 1 | 0 | 1 |  |
| `src/builtins/display/lle_completion.c` | 184 | 6 | 0 | 1 | 0 | 2 |  |
| `src/builtins/display/lle_diagnostics.c` | 132 | 5 | 0 | 1 | 0 | 2 |  |
| `src/builtins/display/lle_keybindings.c` | 251 | 6 | 0 | 1 | 0 | 1 |  |
| `src/builtins/display/lle_reset.c` | 53 | 4 | 0 | 1 | 0 | 1 |  |
| `src/builtins/display/lle_segment.c` | 372 | 4 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_syntax.c` | 58 | 3 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_transient.c` | 59 | 3 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_widget.c` | 353 | 2 | 0 | 3 | 1 | 0 |  |
| `src/builtins/display/lle_autosuggestions.c` | 46 | 1 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_hot_reload.c` | 49 | 1 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_newline_before.c` | 52 | 1 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_hook.c` | 202 | 0 | 0 | 1 | 0 | 0 |  |
| `src/builtins/display/lle_status.c` | 41 | 0 | 0 | 1 | 0 | 0 |  |

### `src/debug/` (6 files, 6 need work, 0 clean)

Totals: `//`=308  `/**< */`=7  trivial-`/**`-blocks=99  static-fn-`/**`=17  in-fn `/* */`=9  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/debug/debug_analysis.c` | 1577 | 111 | 0 | 13 | 7 | 3 |  |
| `src/debug/debug_breakpoints.c` | 900 | 68 | 0 | 27 | 0 | 2 |  |
| `src/debug/debug_trace.c` | 754 | 49 | 2 | 19 | 4 | 3 |  |
| `src/debug/debug_view.c` | 426 | 27 | 5 | 13 | 6 | 0 |  |
| `src/debug/debug_core.c` | 770 | 34 | 0 | 20 | 0 | 1 |  |
| `src/debug/debug_profile.c` | 313 | 19 | 0 | 7 | 0 | 0 |  |

### `src/display/` (10 files, 10 need work, 0 clean)

Totals: `//`=1300  `/**< */`=0  trivial-`/**`-blocks=215  static-fn-`/**`=101  in-fn `/* */`=55  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/display/display_controller.c` | 3563 | 324 | 0 | 25 | 16 | 40 |  |
| `src/display/composition_engine.c` | 1960 | 210 | 0 | 27 | 21 | 1 |  |
| `src/display/command_layer.c` | 1409 | 152 | 0 | 24 | 13 | 2 |  |
| `src/display/screen_buffer.c` | 1183 | 137 | 0 | 2 | 1 | 2 |  |
| `src/display/prompt_layer.c` | 1232 | 118 | 0 | 15 | 14 | 5 |  |
| `src/display/autosuggestions_layer.c` | 996 | 103 | 0 | 10 | 9 | 1 |  |
| `src/display/layer_events.c` | 1218 | 85 | 0 | 38 | 10 | 0 |  |
| `src/display/terminal_control.c` | 1299 | 77 | 0 | 47 | 11 | 0 |  |
| `src/display/base_terminal.c` | 639 | 57 | 0 | 21 | 6 | 0 |  |
| `src/display/screen_buffer_menu.c` | 382 | 37 | 0 | 6 | 0 | 4 |  |

### `src/libfuzzy/` (1 files, 1 need work, 0 clean)

Totals: `//`=43  `/**< */`=0  trivial-`/**`-blocks=24  static-fn-`/**`=8  in-fn `/* */`=3  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/libfuzzy/fuzzy_match.c` | 939 | 43 | 0 | 24 | 8 | 3 |  |

### `src/libhashtable/` (7 files, 6 need work, 1 clean)

Totals: `//`=7  `/**< */`=0  trivial-`/**`-blocks=73  static-fn-`/**`=14  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/libhashtable/ht.c` | 555 | 7 | 0 | 17 | 8 | 0 |  |
| `src/libhashtable/ht_strblob.c` | 192 | 0 | 0 | 11 | 2 | 0 |  |
| `src/libhashtable/ht_fnv1a.c` | 90 | 0 | 0 | 6 | 1 | 0 |  |
| `src/libhashtable/ht_strdouble.c` | 118 | 0 | 0 | 10 | 1 | 0 |  |
| `src/libhashtable/ht_strfloat.c` | 116 | 0 | 0 | 10 | 1 | 0 |  |
| `src/libhashtable/ht_strint.c` | 116 | 0 | 0 | 10 | 1 | 0 |  |
| `src/libhashtable/ht_strstr.c` | 101 | 0 | 0 | 9 | 0 | 0 |  |

### `src/lle/` (10 files, 9 need work, 1 clean)

Totals: `//`=690  `/**< */`=11  trivial-`/**`-blocks=154  static-fn-`/**`=67  in-fn `/* */`=124  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/lle_readline.c` | 3830 | 430 | 11 | 49 | 38 | 106 |  |
| `src/lle/lle_shell_integration.c` | 1146 | 94 | 0 | 26 | 9 | 15 |  |
| `src/lle/lle_shell_hooks.c` | 579 | 50 | 0 | 20 | 8 | 1 |  |
| `src/lle/lle_editor.c` | 406 | 50 | 0 | 7 | 0 | 1 |  |
| `src/lle/lle_shell_event_hub.c` | 469 | 26 | 0 | 12 | 1 | 0 |  |
| `src/lle/lle_watchdog.c` | 245 | 14 | 0 | 16 | 6 | 0 |  |
| `src/lle/lle_readline_state.c` | 264 | 14 | 0 | 12 | 2 | 0 |  |
| `src/lle/notification.c` | 190 | 11 | 0 | 2 | 1 | 1 |  |
| `src/lle/lle_safety.c` | 187 | 1 | 0 | 7 | 2 | 0 |  |
| `src/lle/lle_debug_prompt_state.c` | 37 | 0 | 0 | 3 | 0 | 0 |  |

### `src/lle/adaptive/` (6 files, 6 need work, 0 clean)

Totals: `//`=248  `/**< */`=1  trivial-`/**`-blocks=140  static-fn-`/**`=62  in-fn `/* */`=6  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/adaptive/adaptive_terminal_detection.c` | 795 | 58 | 0 | 26 | 7 | 4 |  |
| `src/lle/adaptive/adaptive_display_client_controller.c` | 881 | 53 | 1 | 25 | 11 | 0 |  |
| `src/lle/adaptive/adaptive_context_initialization.c` | 725 | 50 | 0 | 16 | 7 | 1 |  |
| `src/lle/adaptive/adaptive_multiplexer_controller.c` | 605 | 42 | 0 | 16 | 5 | 0 |  |
| `src/lle/adaptive/adaptive_native_controller.c` | 825 | 26 | 0 | 31 | 19 | 1 |  |
| `src/lle/adaptive/adaptive_minimal_controller.c` | 666 | 19 | 0 | 26 | 13 | 0 |  |

### `src/lle/buffer/` (4 files, 4 need work, 0 clean)

Totals: `//`=255  `/**< */`=0  trivial-`/**`-blocks=33  static-fn-`/**`=12  in-fn `/* */`=3  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/buffer/buffer_management.c` | 906 | 112 | 0 | 13 | 2 | 1 |  |
| `src/lle/buffer/cursor_manager.c` | 588 | 60 | 0 | 5 | 4 | 2 |  |
| `src/lle/buffer/change_tracker.c` | 640 | 49 | 0 | 6 | 5 | 0 |  |
| `src/lle/buffer/buffer_validator.c` | 360 | 34 | 0 | 9 | 1 | 0 |  |

### `src/lle/completion/` (14 files, 14 need work, 0 clean)

Totals: `//`=550  `/**< */`=18  trivial-`/**`-blocks=143  static-fn-`/**`=43  in-fn `/* */`=76  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/completion/builtin_completions.c` | 952 | 117 | 0 | 9 | 8 | 1 |  |
| `src/lle/completion/word_context.c` | 1801 | 54 | 11 | 1 | 0 | 44 |  |
| `src/lle/completion/completion_types.c` | 777 | 67 | 0 | 21 | 2 | 0 |  |
| `src/lle/completion/completion_config.c` | 922 | 60 | 0 | 17 | 8 | 0 |  |
| `src/lle/completion/completion_menu_renderer.c` | 593 | 55 | 0 | 11 | 3 | 2 |  |
| `src/lle/completion/completion_menu_logic.c` | 778 | 50 | 0 | 19 | 3 | 0 |  |
| `src/lle/completion/completion_system.c` | 346 | 34 | 0 | 12 | 3 | 3 |  |
| `src/lle/completion/ssh_hosts.c` | 635 | 33 | 0 | 18 | 6 | 1 |  |
| `src/lle/completion/completion_menu_state.c` | 414 | 36 | 0 | 16 | 2 | 0 |  |
| `src/lle/completion/custom_source.c` | 587 | 22 | 7 | 6 | 4 | 0 |  |
| `src/lle/completion/completion_sources.c` | 468 | 3 | 0 | 3 | 2 | 9 |  |
| `src/lle/completion/completion_state.c` | 173 | 12 | 0 | 6 | 0 | 2 |  |
| `src/lle/completion/source_manager.c` | 429 | 4 | 0 | 3 | 2 | 6 |  |
| `src/lle/completion/splicer.c` | 396 | 3 | 0 | 1 | 0 | 8 |  |

### `src/lle/core/` (8 files, 8 need work, 0 clean)

Totals: `//`=805  `/**< */`=6  trivial-`/**`-blocks=127  static-fn-`/**`=24  in-fn `/* */`=32  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/core/memory_management.c` | 3754 | 345 | 6 | 3 | 0 | 14 |  |
| `src/lle/core/error_handling.c` | 2091 | 179 | 0 | 51 | 5 | 7 |  |
| `src/lle/core/hashtable.c` | 1073 | 93 | 0 | 12 | 2 | 2 |  |
| `src/lle/core/performance.c` | 567 | 55 | 0 | 16 | 3 | 1 |  |
| `src/lle/core/arena.c` | 640 | 44 | 0 | 8 | 7 | 2 |  |
| `src/lle/core/testing.c` | 758 | 43 | 0 | 28 | 0 | 3 |  |
| `src/lle/core/async_worker.c` | 520 | 22 | 0 | 6 | 5 | 3 |  |
| `src/lle/core/git_command.c` | 230 | 24 | 0 | 3 | 2 | 0 |  |

### `src/lle/display/` (6 files, 6 need work, 0 clean)

Totals: `//`=479  `/**< */`=8  trivial-`/**`-blocks=96  static-fn-`/**`=50  in-fn `/* */`=46  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/display/syntax_highlighting.c` | 2023 | 177 | 8 | 32 | 20 | 21 |  |
| `src/lle/display/display_bridge.c` | 744 | 78 | 0 | 10 | 5 | 17 |  |
| `src/lle/display/render_controller.c` | 796 | 84 | 0 | 22 | 11 | 5 |  |
| `src/lle/display/render_cache.c` | 713 | 69 | 0 | 19 | 9 | 2 |  |
| `src/lle/display/render_pipeline.c` | 421 | 51 | 0 | 8 | 4 | 1 |  |
| `src/lle/display/display_integration.c` | 203 | 20 | 0 | 5 | 1 | 0 |  |

### `src/lle/event/` (6 files, 6 need work, 0 clean)

Totals: `//`=221  `/**< */`=0  trivial-`/**`-blocks=72  static-fn-`/**`=4  in-fn `/* */`=4  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/event/event_system.c` | 768 | 74 | 0 | 16 | 1 | 0 |  |
| `src/lle/event/event_timer.c` | 700 | 53 | 0 | 14 | 3 | 2 |  |
| `src/lle/event/event_filter.c` | 538 | 30 | 0 | 14 | 0 | 0 |  |
| `src/lle/event/event_handlers.c` | 352 | 27 | 0 | 8 | 0 | 1 |  |
| `src/lle/event/event_queue.c` | 265 | 20 | 0 | 8 | 0 | 1 |  |
| `src/lle/event/event_stats.c` | 401 | 17 | 0 | 12 | 0 | 0 |  |

### `src/lle/history/` (13 files, 13 need work, 0 clean)

Totals: `//`=609  `/**< */`=76  trivial-`/**`-blocks=206  static-fn-`/**`=44  in-fn `/* */`=20  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/history/history_core.c` | 955 | 108 | 0 | 18 | 2 | 3 |  |
| `src/lle/history/history_expansion.c` | 776 | 65 | 18 | 18 | 6 | 1 |  |
| `src/lle/history/history_dedup.c` | 831 | 57 | 11 | 16 | 3 | 9 |  |
| `src/lle/history/history_search.c` | 809 | 60 | 7 | 19 | 6 | 0 |  |
| `src/lle/history/history_lush_bridge.c` | 925 | 52 | 16 | 26 | 3 | 0 |  |
| `src/lle/history/history_buffer_integration.c` | 748 | 60 | 0 | 2 | 1 | 3 |  |
| `src/lle/history/history_events.c` | 657 | 26 | 24 | 21 | 1 | 0 |  |
| `src/lle/history/history_storage.c` | 599 | 41 | 0 | 10 | 6 | 0 |  |
| `src/lle/history/history_interactive_search.c` | 690 | 39 | 0 | 20 | 3 | 3 |  |
| `src/lle/history/history_multiline.c` | 631 | 41 | 0 | 16 | 3 | 0 |  |
| `src/lle/history/history_index.c` | 402 | 21 | 0 | 18 | 6 | 1 |  |
| `src/lle/history/history_forensics.c` | 345 | 21 | 0 | 11 | 2 | 0 |  |
| `src/lle/history/history_buffer_bridge.c` | 382 | 18 | 0 | 11 | 2 | 0 |  |

### `src/lle/input/` (10 files, 10 need work, 0 clean)

Totals: `//`=342  `/**< */`=6  trivial-`/**`-blocks=121  static-fn-`/**`=26  in-fn `/* */`=10  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/input/sequence_parser.c` | 687 | 60 | 0 | 16 | 5 | 1 |  |
| `src/lle/input/input_parser_error_recovery.c` | 470 | 50 | 0 | 11 | 5 | 3 |  |
| `src/lle/input/key_detector.c` | 530 | 39 | 6 | 13 | 5 | 0 |  |
| `src/lle/input/input_utf8_processor.c` | 404 | 38 | 0 | 13 | 0 | 0 |  |
| `src/lle/input/mouse_parser.c` | 494 | 34 | 0 | 12 | 4 | 0 |  |
| `src/lle/input/input_parser_integration.c` | 326 | 33 | 0 | 11 | 3 | 0 |  |
| `src/lle/input/input_stream.c` | 480 | 24 | 0 | 16 | 3 | 1 |  |
| `src/lle/input/input_widget_hooks.c` | 337 | 27 | 0 | 9 | 0 | 1 |  |
| `src/lle/input/input_keybinding_integration.c` | 294 | 23 | 0 | 7 | 0 | 2 |  |
| `src/lle/input/parser_state_machine.c` | 329 | 14 | 0 | 13 | 1 | 2 |  |

### `src/lle/keybinding/` (4 files, 4 need work, 0 clean)

Totals: `//`=400  `/**< */`=16  trivial-`/**`-blocks=144  static-fn-`/**`=38  in-fn `/* */`=81  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/keybinding/keybinding_actions.c` | 3185 | 297 | 0 | 80 | 26 | 48 |  |
| `src/lle/keybinding/keybinding.c` | 1274 | 40 | 4 | 28 | 5 | 22 |  |
| `src/lle/keybinding/kill_ring.c` | 711 | 37 | 12 | 22 | 4 | 0 |  |
| `src/lle/keybinding/keybinding_config.c` | 690 | 26 | 0 | 14 | 3 | 11 |  |

### `src/lle/multiline/` (8 files, 8 need work, 0 clean)

Totals: `//`=270  `/**< */`=0  trivial-`/**`-blocks=108  static-fn-`/**`=30  in-fn `/* */`=5  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/multiline/structure_analyzer.c` | 786 | 49 | 0 | 15 | 5 | 2 |  |
| `src/lle/multiline/multiline_manager.c` | 586 | 42 | 0 | 16 | 3 | 3 |  |
| `src/lle/multiline/edit_session_manager.c` | 563 | 41 | 0 | 15 | 4 | 0 |  |
| `src/lle/multiline/edit_cache.c` | 498 | 32 | 0 | 15 | 5 | 0 |  |
| `src/lle/multiline/formatting_engine.c` | 632 | 31 | 0 | 15 | 5 | 0 |  |
| `src/lle/multiline/reconstruction_engine.c` | 527 | 32 | 0 | 12 | 3 | 0 |  |
| `src/lle/multiline/multiline_parser.c` | 486 | 30 | 0 | 13 | 4 | 0 |  |
| `src/lle/multiline/command_structure.c` | 275 | 13 | 0 | 7 | 1 | 0 |  |

### `src/lle/prompt/` (8 files, 8 need work, 0 clean)

Totals: `//`=664  `/**< */`=6  trivial-`/**`-blocks=196  static-fn-`/**`=74  in-fn `/* */`=20  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/prompt/theme.c` | 1542 | 193 | 0 | 31 | 0 | 2 |  |
| `src/lle/prompt/segment.c` | 2378 | 111 | 5 | 57 | 32 | 5 |  |
| `src/lle/prompt/prompt_expansion.c` | 736 | 100 | 0 | 5 | 4 | 1 |  |
| `src/lle/prompt/composer.c` | 1218 | 66 | 0 | 27 | 12 | 7 |  |
| `src/lle/prompt/theme_loader.c` | 1313 | 64 | 0 | 22 | 7 | 0 |  |
| `src/lle/prompt/theme_parser.c` | 1445 | 52 | 1 | 28 | 8 | 1 |  |
| `src/lle/prompt/powerline_renderer.c` | 382 | 40 | 0 | 7 | 6 | 3 |  |
| `src/lle/prompt/template_engine.c` | 787 | 38 | 0 | 19 | 5 | 1 |  |

### `src/lle/terminal/` (10 files, 10 need work, 0 clean)

Totals: `//`=340  `/**< */`=0  trivial-`/**`-blocks=83  static-fn-`/**`=30  in-fn `/* */`=50  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/terminal/terminal_unix_interface.c` | 1408 | 133 | 0 | 21 | 12 | 27 |  |
| `src/lle/terminal/terminal_capabilities.c` | 506 | 59 | 0 | 15 | 11 | 0 |  |
| `src/lle/terminal/terminal_internal_state.c` | 489 | 39 | 0 | 13 | 1 | 0 |  |
| `src/lle/terminal/terminal_display_generator.c` | 453 | 31 | 0 | 9 | 3 | 1 |  |
| `src/lle/terminal/terminal_input_processor.c` | 249 | 16 | 0 | 6 | 1 | 6 |  |
| `src/lle/terminal/terminal_signature_database.c` | 235 | 17 | 0 | 3 | 1 | 4 |  |
| `src/lle/terminal/terminal_lush_client.c` | 251 | 13 | 0 | 6 | 1 | 7 |  |
| `src/lle/terminal/terminal_abstraction.c` | 198 | 17 | 0 | 3 | 0 | 3 |  |
| `src/lle/terminal/terminal_perf_monitor.c` | 153 | 12 | 0 | 5 | 0 | 2 |  |
| `src/lle/terminal/terminal_error_handler.c` | 57 | 3 | 0 | 2 | 0 | 0 |  |

### `src/lle/unicode/` (7 files, 7 need work, 0 clean)

Totals: `//`=1130  `/**< */`=4  trivial-`/**`-blocks=69  static-fn-`/**`=10  in-fn `/* */`=5  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/unicode/unicode_case.c` | 704 | 342 | 0 | 14 | 3 | 0 |  |
| `src/lle/unicode/unicode_compare.c` | 904 | 290 | 4 | 16 | 4 | 0 |  |
| `src/lle/unicode/unicode_grapheme.c` | 776 | 290 | 0 | 7 | 2 | 0 |  |
| `src/lle/unicode/utf8_support.c` | 474 | 79 | 0 | 12 | 0 | 0 |  |
| `src/lle/unicode/grapheme_detector.c` | 255 | 57 | 0 | 4 | 0 | 1 |  |
| `src/lle/unicode/char_width.c` | 159 | 47 | 0 | 3 | 0 | 2 |  |
| `src/lle/unicode/utf8_index.c` | 487 | 25 | 0 | 13 | 1 | 2 |  |

### `src/lle/widget/` (3 files, 3 need work, 0 clean)

Totals: `//`=67  `/**< */`=0  trivial-`/**`-blocks=35  static-fn-`/**`=30  in-fn `/* */`=4  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `src/lle/widget/builtin_widgets.c` | 668 | 16 | 0 | 30 | 27 | 3 |  |
| `src/lle/widget/widget_system.c` | 343 | 29 | 0 | 3 | 2 | 0 |  |
| `src/lle/widget/widget_hooks.c` | 343 | 22 | 0 | 2 | 1 | 1 |  |

### `tests/` (2 files, 2 need work, 0 clean)

Totals: `//`=7  `/**< */`=0  trivial-`/**`-blocks=12  static-fn-`/**`=0  in-fn `/* */`=2  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/test_shell_harness.h` | 392 | 6 | 0 | 10 | 0 | 2 |  |
| `tests/test_framework.h` | 241 | 1 | 0 | 2 | 0 | 0 |  |

### `tests/fuzz/` (5 files, 5 need work, 0 clean)

Totals: `//`=60  `/**< */`=9  trivial-`/**`-blocks=11  static-fn-`/**`=2  in-fn `/* */`=15  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/fuzz/diff_oracle.c` | 791 | 37 | 5 | 3 | 2 | 6 |  |
| `tests/fuzz/fuzz_executor.c` | 240 | 5 | 0 | 1 | 0 | 9 |  |
| `tests/fuzz/fuzz_tokenizer.c` | 130 | 8 | 0 | 3 | 0 | 0 |  |
| `tests/fuzz/fuzz_parser.c` | 124 | 7 | 0 | 3 | 0 | 0 |  |
| `tests/fuzz/fuzz_stubs.c` | 95 | 3 | 4 | 1 | 0 | 0 |  |

### `tests/lle/` (1 files, 1 need work, 0 clean)

Totals: `//`=58  `/**< */`=0  trivial-`/**`-blocks=0  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=1

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/test_utf8_movement.c` | 509 | 58 | 0 | 0 | 0 | 0 | YES |

### `tests/lle/benchmarks/` (2 files, 2 need work, 0 clean)

Totals: `//`=37  `/**< */`=0  trivial-`/**`-blocks=2  static-fn-`/**`=0  in-fn `/* */`=7  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/benchmarks/display_performance_benchmark.c` | 303 | 22 | 0 | 1 | 0 | 0 |  |
| `tests/lle/benchmarks/performance_benchmark.c` | 284 | 15 | 0 | 1 | 0 | 7 |  |

### `tests/lle/compliance/` (23 files, 23 need work, 0 clean)

Totals: `//`=397  `/**< */`=0  trivial-`/**`-blocks=122  static-fn-`/**`=38  in-fn `/* */`=26  missing-@file=9

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/compliance/spec_12_completion_compliance.c` | 575 | 35 | 0 | 16 | 0 | 2 |  |
| `tests/lle/compliance/spec_09_history_compliance.c` | 335 | 32 | 0 | 11 | 0 | 0 |  |
| `tests/lle/compliance/spec_03_buffer_validator_test.c` | 421 | 30 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/compliance/spec_22_history_buffer_compliance.c` | 260 | 23 | 0 | 1 | 0 | 5 | YES |
| `tests/lle/compliance/spec_03_utf8_index_test.c` | 323 | 27 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/compliance/spec_25_keybinding_compliance.c` | 250 | 22 | 0 | 1 | 0 | 5 | YES |
| `tests/lle/compliance/spec_25_composer_compliance.c` | 552 | 27 | 0 | 1 | 0 | 0 |  |
| `tests/lle/compliance/spec_03_buffer_management_compliance.c` | 430 | 9 | 0 | 19 | 17 | 0 |  |
| `tests/lle/compliance/spec_15_memory_management_compliance.c` | 259 | 26 | 0 | 1 | 0 | 0 |  |
| `tests/lle/compliance/spec_03_atomic_operations_test.c` | 298 | 24 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/compliance/spec_05_libhashtable_integration_compliance.c` | 272 | 19 | 0 | 10 | 0 | 1 |  |
| `tests/lle/compliance/spec_25_segment_compliance.c` | 517 | 20 | 0 | 1 | 0 | 0 |  |
| `tests/lle/compliance/spec_14_performance_compliance.c` | 263 | 12 | 0 | 9 | 7 | 0 |  |
| `tests/lle/compliance/spec_26_adaptive_terminal_compliance.c` | 180 | 15 | 0 | 4 | 2 | 0 | YES |
| `tests/lle/compliance/spec_16_error_handling_compliance.c` | 307 | 8 | 0 | 11 | 9 | 0 |  |
| `tests/lle/compliance/spec_03_cursor_manager_test.c` | 191 | 11 | 0 | 1 | 0 | 3 | YES |
| `tests/lle/compliance/spec_17_testing_framework_compliance.c` | 174 | 12 | 0 | 5 | 3 | 0 |  |
| `tests/lle/compliance/spec_02_terminal_abstraction_compliance.c` | 228 | 13 | 0 | 8 | 0 | 0 |  |
| `tests/lle/compliance/spec_04_event_system_compliance.c` | 254 | 3 | 0 | 1 | 0 | 10 |  |
| `tests/lle/compliance/spec_25_theme_compliance.c` | 471 | 10 | 0 | 1 | 0 | 0 |  |
| `tests/lle/compliance/spec_03_utf8_unicode_compliance.c` | 144 | 7 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/compliance/spec_03_atomic_simple_test.c` | 111 | 6 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/compliance/spec_08_display_integration_compliance.c` | 394 | 6 | 0 | 16 | 0 | 0 |  |

### `tests/lle/e2e/` (1 files, 1 need work, 0 clean)

Totals: `//`=48  `/**< */`=0  trivial-`/**`-blocks=1  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/e2e/realistic_scenarios_test.c` | 551 | 48 | 0 | 1 | 0 | 0 |  |

### `tests/lle/functional/` (15 files, 15 need work, 0 clean)

Totals: `//`=430  `/**< */`=0  trivial-`/**`-blocks=15  static-fn-`/**`=0  in-fn `/* */`=3  missing-@file=10

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/functional/test_history_phase4_complete.c` | 639 | 74 | 0 | 1 | 0 | 1 | YES |
| `tests/lle/functional/test_history_phase3_day9.c` | 636 | 62 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/functional/test_history_phase3_day8.c` | 701 | 47 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/functional/test_history_phase3_day10.c` | 493 | 42 | 0 | 1 | 0 | 0 |  |
| `tests/lle/functional/test_history_phase1_day1.c` | 543 | 32 | 0 | 0 | 0 | 0 | YES |
| `tests/lle/functional/test_history_phase1_day3.c` | 419 | 28 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/functional/multiline_manager_test.c` | 372 | 28 | 0 | 1 | 0 | 0 |  |
| `tests/lle/functional/test_history_phase1_day2.c` | 399 | 25 | 0 | 1 | 0 | 1 | YES |
| `tests/lle/functional/test_secure_memory.c` | 322 | 25 | 0 | 1 | 0 | 0 |  |
| `tests/lle/functional/display_test_stubs.c` | 268 | 21 | 0 | 2 | 0 | 1 |  |
| `tests/lle/functional/buffer_operations_test.c` | 505 | 13 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/functional/test_completion_mock.c` | 147 | 9 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/functional/test_error_handling_phase2.c` | 297 | 10 | 0 | 1 | 0 | 0 |  |
| `tests/lle/functional/test_memory_mock.c` | 66 | 7 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/functional/test_memory_mock.h` | 53 | 7 | 0 | 1 | 0 | 0 | YES |

### `tests/lle/integration/` (7 files, 7 need work, 0 clean)

Totals: `//`=244  `/**< */`=0  trivial-`/**`-blocks=23  static-fn-`/**`=11  in-fn `/* */`=4  missing-@file=2

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/integration/subsystem_integration_test.c` | 731 | 90 | 0 | 1 | 0 | 1 |  |
| `tests/lle/integration/display_integration_test.c` | 366 | 43 | 0 | 7 | 0 | 1 |  |
| `tests/lle/integration/input_parser_integration_test.c` | 402 | 32 | 0 | 11 | 10 | 1 |  |
| `tests/lle/integration/test_fkey_detection.c` | 261 | 29 | 0 | 0 | 0 | 1 | YES |
| `tests/lle/integration/test_history_phase1_integration.c` | 454 | 25 | 0 | 2 | 1 | 0 | YES |
| `tests/lle/integration/manual_input_test.c` | 361 | 19 | 0 | 1 | 0 | 0 |  |
| `tests/lle/integration/simple_input_test.c` | 93 | 6 | 0 | 1 | 0 | 0 |  |

### `tests/lle/manual/` (1 files, 1 need work, 0 clean)

Totals: `//`=21  `/**< */`=0  trivial-`/**`-blocks=0  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=1

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/manual/test_fkey_manual.c` | 277 | 21 | 0 | 0 | 0 | 0 | YES |

### `tests/lle/stress/` (2 files, 2 need work, 0 clean)

Totals: `//`=73  `/**< */`=0  trivial-`/**`-blocks=2  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=0

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/stress/watchdog_stress_test.c` | 459 | 37 | 0 | 1 | 0 | 0 |  |
| `tests/lle/stress/display_stress_test.c` | 589 | 36 | 0 | 1 | 0 | 0 |  |

### `tests/lle/unit/` (40 files, 40 need work, 0 clean)

Totals: `//`=1251  `/**< */`=3  trivial-`/**`-blocks=60  static-fn-`/**`=15  in-fn `/* */`=112  missing-@file=21

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/lle/unit/test_render_controller.c` | 1134 | 146 | 0 | 4 | 3 | 1 |  |
| `tests/lle/unit/test_word_context.c` | 869 | 57 | 0 | 0 | 0 | 35 | YES |
| `tests/lle/unit/test_widget_hooks.c` | 542 | 69 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_powerline_renderer.c` | 666 | 58 | 0 | 4 | 3 | 7 |  |
| `tests/lle/unit/test_kill_ring.c` | 734 | 64 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_input_stream.c` | 593 | 60 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_prompt_expansion.c` | 902 | 51 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_terminal_event_reading.c` | 577 | 43 | 0 | 0 | 0 | 3 | YES |
| `tests/lle/unit/test_terminal_state.c` | 445 | 39 | 0 | 0 | 0 | 4 | YES |
| `tests/lle/unit/test_event_phase2.c` | 553 | 40 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_keybinding.c` | 470 | 38 | 1 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_hashtable.c` | 725 | 38 | 0 | 1 | 0 | 1 |  |
| `tests/lle/unit/test_terminal_capabilities.c` | 407 | 38 | 0 | 0 | 0 | 0 | YES |
| `tests/lle/unit/test_display_bridge.c` | 325 | 34 | 0 | 3 | 2 | 0 |  |
| `tests/lle/unit/test_parser_state_machine.c` | 455 | 34 | 0 | 0 | 0 | 0 | YES |
| `tests/lle/unit/test_splicer.c` | 401 | 19 | 0 | 0 | 0 | 15 | YES |
| `tests/lle/unit/test_grapheme_detector.c` | 419 | 21 | 0 | 1 | 0 | 13 |  |
| `tests/lle/unit/test_mouse_parser.c` | 570 | 31 | 0 | 0 | 0 | 1 | YES |
| `tests/lle/unit/test_lle_syntax_highlighting.c` | 1015 | 24 | 0 | 1 | 0 | 6 |  |
| `tests/lle/unit/test_adaptive_controllers.c` | 403 | 25 | 0 | 1 | 0 | 3 | YES |
| `tests/lle/unit/test_utf8_index.c` | 472 | 23 | 0 | 1 | 0 | 5 |  |
| `tests/lle/unit/test_char_width.c` | 486 | 25 | 0 | 1 | 0 | 2 |  |
| `tests/lle/unit/test_prompt_composer.c` | 458 | 27 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_async_worker.c` | 390 | 25 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_sequence_parser.c` | 526 | 24 | 0 | 0 | 0 | 0 | YES |
| `tests/lle/unit/test_theme_registry.c` | 532 | 25 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_widget_system.c` | 464 | 20 | 0 | 18 | 5 | 0 |  |
| `tests/lle/unit/test_template_engine.c` | 531 | 22 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_input_utf8_processor.c` | 506 | 19 | 0 | 0 | 0 | 0 | YES |
| `tests/lle/unit/test_segment_system.c` | 456 | 19 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_key_detector.c` | 515 | 18 | 0 | 0 | 0 | 0 | YES |
| `tests/lle/unit/test_ssh_completion.c` | 377 | 9 | 2 | 0 | 0 | 7 | YES |
| `tests/lle/unit/test_adaptive_fallback.c` | 222 | 15 | 0 | 6 | 0 | 0 | YES |
| `tests/lle/unit/test_event_system.c` | 700 | 12 | 0 | 3 | 2 | 0 |  |
| `tests/lle/unit/test_adaptive_detection.c` | 171 | 10 | 0 | 1 | 0 | 0 | YES |
| `tests/lle/unit/test_render_pipeline.c` | 364 | 11 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_theme_parser.c` | 578 | 5 | 0 | 1 | 0 | 4 |  |
| `tests/lle/unit/test_unicode_case_compare.c` | 372 | 9 | 0 | 1 | 0 | 0 |  |
| `tests/lle/unit/test_render_cache.c` | 443 | 2 | 0 | 1 | 0 | 5 |  |
| `tests/lle/unit/test_completion_types.c` | 215 | 2 | 0 | 0 | 0 | 0 | YES |

### `tests/manual/` (4 files, 4 need work, 0 clean)

Totals: `//`=76  `/**< */`=0  trivial-`/**`-blocks=1  static-fn-`/**`=0  in-fn `/* */`=0  missing-@file=4

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/manual/demo_completion_menu.c` | 254 | 35 | 0 | 0 | 0 | 0 | YES |
| `tests/manual/demo_completion_menu_themed.c` | 214 | 26 | 0 | 0 | 0 | 0 | YES |
| `tests/manual/debug_grapheme.c` | 82 | 8 | 0 | 0 | 0 | 0 | YES |
| `tests/manual/test_grapheme_nav.c` | 111 | 7 | 0 | 1 | 0 | 0 | YES |

### `tests/unit/` (52 files, 52 need work, 0 clean)

Totals: `//`=1516  `/**< */`=1  trivial-`/**`-blocks=66  static-fn-`/**`=12  in-fn `/* */`=78  missing-@file=2

| File | Lines | `//` | `/**<` | `/**` blocks | static-fn `/**` | in-fn `/* */` | no @file |
|------|------:|-----:|-------:|-------------:|----------------:|--------------:|---------:|
| `tests/unit/test_executor.c` | 3356 | 168 | 0 | 1 | 0 | 23 |  |
| `tests/unit/test_parser_fuzzer.c` | 994 | 118 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_debug_trace.c` | 1092 | 70 | 0 | 6 | 3 | 5 |  |
| `tests/unit/test_debug_breakpoints.c` | 2031 | 70 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_posix_history.c` | 842 | 62 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_screen_buffer.c` | 1275 | 60 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_debug_analysis.c` | 953 | 55 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_config.c` | 771 | 53 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_shell_mode.c` | 477 | 52 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_parser_negative.c` | 874 | 47 | 0 | 3 | 0 | 3 |  |
| `tests/unit/test_config_registry.c` | 835 | 42 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_builtins.c` | 1324 | 41 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_tokenizer.c` | 857 | 38 | 0 | 1 | 0 | 3 |  |
| `tests/unit/test_fc.c` | 543 | 36 | 0 | 5 | 4 | 0 |  |
| `tests/unit/test_debug.c` | 701 | 32 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_display.c` | 852 | 33 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_alias.c` | 666 | 32 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_display_controller.c` | 1108 | 32 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_signals.c` | 347 | 31 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_posix_opts.c` | 583 | 29 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_fuzzy_match.c` | 494 | 26 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_lush_plugin.c` | 1025 | 25 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_toml_parser.c` | 1115 | 25 | 1 | 1 | 0 | 0 |  |
| `tests/unit/test_memory_pool.c` | 642 | 24 | 0 | 1 | 0 | 1 |  |
| `tests/unit/test_prompt_layer.c` | 1416 | 23 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_autocorrect.c` | 540 | 22 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_input_continuation.c` | 854 | 22 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_redirection.c` | 573 | 22 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_node.c` | 445 | 17 | 0 | 1 | 0 | 4 |  |
| `tests/unit/test_terminal_control.c` | 890 | 19 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_dirstack.c` | 499 | 20 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_expansion.c` | 835 | 18 | 0 | 1 | 0 | 2 |  |
| `tests/unit/test_symtable.c` | 589 | 12 | 0 | 1 | 0 | 6 |  |
| `tests/unit/test_compat.c` | 592 | 17 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_fixer.c` | 964 | 15 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_composition_engine.c` | 1185 | 13 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_strings.c` | 592 | 13 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_shell_error.c` | 541 | 12 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_parser.c` | 1100 | 8 | 0 | 1 | 0 | 3 |  |
| `tests/unit/test_shell_quoting.c` | 208 | 3 | 0 | 0 | 0 | 7 | YES |
| `tests/unit/test_debug_integration.c` | 461 | 3 | 0 | 5 | 4 | 3 |  |
| `tests/unit/test_parser_stubs.c` | 112 | 9 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_lush_stubs.c` | 120 | 7 | 0 | 0 | 0 | 0 | YES |
| `tests/unit/test_ast_roundtrip.c` | 415 | 6 | 0 | 2 | 1 | 0 |  |
| `tests/unit/test_expand.c` | 388 | 7 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_hashtable.c` | 454 | 6 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_pattern_match.c` | 209 | 5 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_symtable_stubs.c` | 29 | 5 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_autoload.c` | 188 | 4 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_node_to_source.c` | 359 | 4 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_executor_stubs.c` | 20 | 2 | 0 | 1 | 0 | 0 |  |
| `tests/unit/test_node_stubs.c` | 26 | 1 | 0 | 1 | 0 | 0 |  |


---

## Audit metadata

- Generated: 2026-05-25
- Tree: `grammar-fuzzing` branch
- Total files audited: 529
- Files skipped: `src/strings.c` (deprecated), `build/` and variants (build dirs), `tests/real_world/corpus/` (data fixtures)
- Method: `grep -cE` for `//` and `/**< */`; AWK heuristic for in-function `/* */` blocks and static-fn `/** */` blocks; head-40 scan for `@file`.
- No code modified.

