# Fuzzing Plan

**Branch:** `grammar-fuzzing`
**Status:** Phase 1 baselined and Phase 2 gap-seeds added 2026-05-19. Phase 3 differential harness operational (76 mode-tagged seeds, all green). Phase 4 not started.
**Predecessor:** `parser-grammar-spec` branch (merged to master). The grammar artifact `docs/development/grammar/LUSH_GRAMMAR.ebnf` is the input for several phases below.

## Why this branch exists

The previous branch produced a written grammar of what the parser accepts. Step 2 of `docs/development/PARSER_GRAMMAR_PLAN.md` is grammar-driven fuzzing — using that grammar to systematically prove (or disprove) correctness across the shell.

The bugs surfaced during the previous branch's manual reading (#43, #44, #45, #46, #47, #48) are evidence that more bugs exist. Two of them — #47 (local-variable assignment silently dropped) and #48 (function-level redirections silently ignored) — would never be found by crash-fuzzing, because they produce the wrong answer rather than crashing. Finding that bug class systematically requires *differential testing* against the appropriate reference implementation per lush mode (POSIX → `dash`, bash mode → `bash`, zsh mode → `zsh`; lush mode is tested by intersection across the available oracles, not against any single shell).

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
- **POSIX, bash, and zsh are first-class.** Differential testing per lush mode targets the matching oracle (`dash`/`bash --posix`, `bash`, `zsh`). Fish, ksh, tcsh, csh remain out of scope. Lush mode itself is the polyglot superset and has no single oracle.
- **Crashes vs correctness are different problems.** Phases 1, 2, 4 find crashes. Phase 3 finds wrong-answer bugs. Both matter; do both.

## Phased plan

### Phase 1 — Baseline the existing fuzzers (free, hours of CPU)

Set up build with `-Denable_fuzzing=true -Dfuzzer=libfuzzer` (requires Clang). Run `fuzz_parser` and `fuzz_tokenizer` against current code with the existing 2,471-entry corpus seed. Previous fuzzing predates the recent parser changes (function trailing redirections, while/until logical conditions, function compound-body dispatch, heredoc EOF diagnostic) — new surface area = potentially new crashes worth finding before they ship.

**Cost:** ~minutes human, ~hours CPU per fuzzer
**Deliverable:** crash inputs (if any) filed as issues; "no crashes after N hours" recorded as a baseline number

**Done when:** each fuzzer has run for a planned duration with no new crashes, or every crash found has been triaged.

**Baseline 2026-05-19** (post-`bf0c11d5` end_position fix, macOS, Apple clang 17 via Homebrew LLVM):

| Fuzzer         | Wall  | Executions | Avg exec/s | Peak RSS | New units | Crashes |
|----------------|-------|------------|-----------:|---------:|----------:|--------:|
| fuzz_parser    | 601 s |  2 572 523 |       4280 | 1113 MB  |      5585 |       0 |
| fuzz_tokenizer | 601 s |  2 176 530 |       3621 |  741 MB  |      1788 |       0 |

Corpus minimized via libFuzzer `-merge=1` afterwards. Human-named seeds (`NNN_*.sh`) preserved; hex-name corpus refreshed with the minimal coverage-equivalent set plus new-coverage units from the run. New corpus sizes: parser 4172 (4107 hex + 65 human), tokenizer 2360 (2350 hex + 10 human).

### Phase 2 — Grammar-derived seed corpus (≈ 1 day, high value)

Translate every production in `docs/development/grammar/LUSH_GRAMMAR.ebnf` into one canonical valid input. Drop those into `tests/fuzz/corpus/parser/`. libFuzzer's coverage-guided mutations work much better with diverse valid starting inputs than with only what previous random fuzzing accidentally produced.

This is mechanical translation work, not invention — the grammar already exists.

**Cost:** ~1 day mechanical
**Deliverable:** `tests/fuzz/corpus/parser/grammar_seeds/` directory with one file per production, each named after the production it exercises

**Done when:** every production in `LUSH_GRAMMAR.ebnf` has at least one seed input that exercises it.

**Phase 2 seeds added 2026-05-19** (16 inputs, `066_*.sh` through `081_*.sh`, in `tests/fuzz/corpus/parser/`):

| Seed                           | Production / construct exercised                          |
|--------------------------------|-----------------------------------------------------------|
| 066_fd_alloc.sh                | fd_alloc_redirection `{name}>file` (bash 4.1+ / zsh)      |
| 067_clobber_redirect.sh        | file_redir_op `>|` (clobber override)                     |
| 068_combined_redirect.sh       | file_redir_op `&>` / `&>>`                                |
| 069_for_arith_empty.sh         | for_arith with empty arith_expr slots                     |
| 070_for_no_in.sh               | for_posix with `in` omitted (defaults to "$@")            |
| 071_subshell_redirect.sh       | subshell with trailing_redirections                       |
| 072_if_redirect.sh             | if_statement with trailing_redirections                   |
| 073_while_redirect.sh          | while/until with trailing_redirections                    |
| 074_case_redirect.sh           | case_statement with trailing_redirections                 |
| 075_proc_sub_redirect.sh       | process_substitution as redirection_target               |
| 076_fd_var_target.sh           | fd_target = TOK_VARIABLE in fd_dup_op                     |
| 077_lush_func_params.sh        | parameter with default (single-param form; #107)          |
| 078_array_mixed.sh             | array_literal mixed positional + indexed array_element    |
| 079_subscript_assign.sh        | NAME[subscript]=value subscript assignment                |
| 080_multi_heredoc.sh           | multiple heredoc_redirection on one command + quoted delims |
| 081_empty_bodies.sh            | empty / minimal command_body across compounds            |

Two real parser bugs were surfaced while constructing the seeds:

- `case x in a) echo b; ;; esac` was rejected because the parser treated a non-adjacent pair of `;` as the `;;` terminator. Fixed in this commit by requiring `;;` to be positionally adjacent via `token->end_position`.
- `f(a, b) { :; }` (multi-parameter lush functions) is broken because the tokenizer absorbs `,` into the preceding TOK_WORD. Tracked as #107.

### Phase 3 — Mode-aware differential test harness (2-3 days, the architectural piece)

This is the tool that would have caught #47 and #48 on the first run.

Lush is a polyglot superset of POSIX, bash, and zsh — none of those is "the" reference. Differential testing must respect that: each lush mode has its own oracle, and lush mode (the default polyglot superset) has none and is tested by intersection. Architecture:

1. **Generator** — consumes `LUSH_GRAMMAR.ebnf` plus per-mode subset filters; produces inputs tagged with the mode they target. Three subsets are first-class:
   - **POSIX** subset (productions also in IEEE 1003.1)
   - **bash-compatible** subset (POSIX ∪ documented bash extensions)
   - **zsh-compatible** subset (POSIX ∪ documented zsh extensions)
   - **Lush-only extensions** (anything in LUSH_GRAMMAR.ebnf marked as not in either reference shell — no oracle for these; they are tested for crash/correctness against lush's own spec).

2. **Runner** — for each tagged input, executes `lush -c <input>` plus the appropriate oracle in parallel with timeouts. Oracles per mode:
   - POSIX-tagged input: oracle is `dash` (or `bash --posix` as fallback)
   - bash-tagged input: oracle is `bash`
   - zsh-tagged input: oracle is `zsh`
   - lush-only-tagged input: no oracle (record lush's behavior; verify no crash/timeout/UB)
   - Lush is run in a matching mode for each (e.g., `set -o posix` for POSIX inputs) so the comparison is apples-to-apples.

3. **Comparator** — compares lush's result to the oracle's. Mismatches go in a bug queue. Filters out known divergences via a curated allow-list (lush deliberately extends some constructs; those go in `tests/fuzz/differential/known_divergences/`). For lush-only inputs there is no comparison; they exercise crash/timeout/UB detection only.

4. **Minimizer** — for each divergence, shrinks the input to the smallest form that still triggers it.

5. **Persistent corpus** — known-divergent inputs and known-clean inputs cached so reruns don't re-discover the same bugs. Per-mode corpus directories.

Oracle availability: `dash`, `bash`, and `zsh` are runtime-detected; missing oracles cause the matching subset to be skipped with a notice (not a hard failure). On Linux with the project's expected dev environment all three are typically present.

This is C plus shell scripting; no new deps.

**Cost:** 2-3 days for a robust harness
**Deliverable:** `tests/fuzz/differential/` with the harness, mode-tagged corpus subdirs, and a runnable target (`./build/fuzz_differential` or similar). CI integration as a separate follow-up.

**Done when:** harness runs continuously on a clean per-mode corpus and finds zero new divergences over a multi-hour budget for every mode whose oracle is available.

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
- **Behavioral parity oracles per mode** — every change to the parser or executor is auto-checked against the matching reference (`dash` for POSIX, `bash` for bash mode, `zsh` for zsh mode). Lush mode is checked against the intersection where oracles agree. Regressions like #47 / #48 become impossible to ship without warning.
- **Continuous fuzzing in CI** as a final follow-up — every PR / commit gets a fuzzing budget. Not in this plan but the natural next step.

## Pointers to related work

- `docs/development/PARSER_GRAMMAR_PLAN.md` — Step 2 of that plan is what this branch implements
- `docs/development/grammar/LUSH_GRAMMAR.ebnf` — input to Phases 2 and 3
- `docs/development/grammar/PARSER_NOTES.md` — context-sensitive bits the grammar can't say (informs Phase 3 false-positive filtering)
- Existing fuzz scaffolding: `tests/fuzz/`, `meson_options.txt`, `meson.build:1652-1770`
