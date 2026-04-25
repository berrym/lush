# Fuzzing Plan

**Branch:** `grammar-fuzzing`
**Status:** Planning complete; Phase 1 not yet started.
**Predecessor:** `parser-grammar-spec` branch (merged to master). The grammar artifact `docs/development/grammar/LUSH_GRAMMAR.ebnf` is the input for several phases below.

## Why this branch exists

The previous branch produced a written grammar of what the parser accepts. Step 2 of `docs/development/PARSER_GRAMMAR_PLAN.md` is grammar-driven fuzzing — using that grammar to systematically prove (or disprove) correctness across the shell.

The bugs surfaced during the previous branch's manual reading (#43, #44, #45, #46, #47, #48) are evidence that more bugs exist. Two of them — #47 (local-variable assignment silently dropped) and #48 (function-level redirections silently ignored) — would never be found by crash-fuzzing, because they produce the wrong answer rather than crashing. Finding that bug class systematically requires *differential testing* against a reference implementation (bash).

This branch is the toolkit for that.

## What already exists

The repo is not starting from zero on fuzzing:

- **`tests/fuzz/fuzz_parser.c`** — libFuzzer + AFL++ entry point for the parser
- **`tests/fuzz/fuzz_tokenizer.c`** — same for the tokenizer
- **`tests/fuzz/corpus/parser/`** — **2,471 corpus entries** from prior coverage-guided fuzzing
- **`tests/fuzz/corpus/tokenizer/`** — 12 entries
- **`meson_options.txt`** — `-Denable_fuzzing=true -Dfuzzer={libfuzzer,afl}` toggles the fuzz targets
- **Build wiring** — `meson.build` lines ~1652–1770 link fuzz targets with `-fsanitize=fuzzer,address,undefined`

The infrastructure fits the project ethos: pure C, no Python or Rust deps, single binary.

## Scope discipline

- **No new runtime dependencies.** Whatever generators or harnesses we build are C, or shell scripts using tools already in the project's expected dev environment.
- **bash + zsh polyglot only.** Differential testing targets bash for the bash-compatible subset of `LUSH_GRAMMAR.ebnf`. Fish, ksh, tcsh, csh remain out of scope.
- **Crashes vs correctness are different problems.** Phases 1, 2, 4 find crashes. Phase 3 finds wrong-answer bugs. Both matter; do both.

## Phased plan

### Phase 1 — Baseline the existing fuzzers (free, hours of CPU)

Set up build with `-Denable_fuzzing=true -Dfuzzer=libfuzzer` (requires Clang). Run `fuzz_parser` and `fuzz_tokenizer` against current code with the existing 2,471-entry corpus seed. Previous fuzzing predates the recent parser changes (function trailing redirections, while/until logical conditions, function compound-body dispatch, heredoc EOF diagnostic) — new surface area = potentially new crashes worth finding before they ship.

**Cost:** ~minutes human, ~hours CPU per fuzzer
**Deliverable:** crash inputs (if any) filed as issues; "no crashes after N hours" recorded as a baseline number

**Done when:** each fuzzer has run for a planned duration with no new crashes, or every crash found has been triaged.

### Phase 2 — Grammar-derived seed corpus (≈ 1 day, high value)

Translate every production in `docs/development/grammar/LUSH_GRAMMAR.ebnf` into one canonical valid input. Drop those into `tests/fuzz/corpus/parser/`. libFuzzer's coverage-guided mutations work much better with diverse valid starting inputs than with only what previous random fuzzing accidentally produced.

This is mechanical translation work, not invention — the grammar already exists.

**Cost:** ~1 day mechanical
**Deliverable:** `tests/fuzz/corpus/parser/grammar_seeds/` directory with one file per production, each named after the production it exercises

**Done when:** every production in `LUSH_GRAMMAR.ebnf` has at least one seed input that exercises it.

### Phase 3 — Differential test harness against bash (2-3 days, the architectural piece)

This is the tool that would have caught #47 and #48 on the first run. Architecture:

1. **Generator** — consumes `LUSH_GRAMMAR.ebnf`, produces inputs that conform to the bash-compatible subset. Either grammar-walking with random choices, or mutation-based on existing corpus, or both.
2. **Runner** — for each input, executes `lush -c <input>` and `bash -c <input>` in parallel with timeouts. Captures exit code, stdout, stderr.
3. **Comparator** — compares the two results. Mismatches go in a bug queue. Filters out known divergences (lush is intentionally polyglot in places — those need a curated allow-list).
4. **Minimizer** — for each divergence, shrinks the input to the smallest form that still triggers it. Critical for bug filing.
5. **Persistent corpus** — known-divergent inputs and known-clean inputs both cached so reruns don't re-discover the same bugs.

This is C plus shell scripting; no new deps.

**Cost:** 2-3 days for a robust harness
**Deliverable:** `tests/fuzz/differential/` with the harness and a runnable target (`./build/fuzz_differential` or similar). CI integration as a separate follow-up.

**Done when:** harness runs continuously on a clean corpus and finds zero new divergences over a multi-hour budget.

### Phase 4 — Executor fuzz target (≈ 1 day)

Add `tests/fuzz/fuzz_executor.c` that parses *and* executes (with sandboxing — no actual fork/exec, no file I/O outside a temp dir). The existing fuzzers stop at AST construction; this extends coverage into the executor itself, finding crashes during execution that the parser fuzzers miss.

**Cost:** ~1 day for the target + sandboxing
**Deliverable:** `fuzz_executor` target wired into meson, alongside the existing `fuzz_parser` and `fuzz_tokenizer`

**Done when:** target builds, runs, and has been exercised against the corpus for at least one full session without finding crashes (or every crash found has been filed).

## Order of operations

1. Phase 1 (free baseline) — *do first*
2. Phase 2 (corpus expansion) — feeds Phase 3
3. Phase 3 (differential harness) — the real win
4. Phase 4 (executor target) — supplementary

## Branch granularity

All four phases on `grammar-fuzzing` is the simplest. The user has expressed preference for fewer merges and linear history, so this branch will accumulate the full set and merge to master as one chunk.

If a phase produces something with independent value (e.g. the grammar-derived corpus is useful even without the differential harness), it can be cherry-picked or fast-forwarded earlier.

## What this enables when complete

- **Crash-free guarantee** for the inputs the corpus and grammar describe.
- **Behavioral parity oracle** — every change to the parser or executor is auto-checked against bash for the bash-compatible subset. Regressions like #47 / #48 become impossible to ship without warning.
- **Continuous fuzzing in CI** as a final follow-up — every PR / commit gets a fuzzing budget. Not in this plan but the natural next step.

## Pointers to related work

- `docs/development/PARSER_GRAMMAR_PLAN.md` — Step 2 of that plan is what this branch implements
- `docs/development/grammar/LUSH_GRAMMAR.ebnf` — input to Phases 2 and 3
- `docs/development/grammar/PARSER_NOTES.md` — context-sensitive bits the grammar can't say (informs Phase 3 false-positive filtering)
- Existing fuzz scaffolding: `tests/fuzz/`, `meson_options.txt`, `meson.build:1652-1770`
