# Parser Grammar & Quality Plan

**Branch:** `parser-grammar-spec` (merged) / `grammar-fuzzing` (merged)
**Status (2026-05-23):**
- **Step 1 (document the grammar) — DONE.** Synthesis lives at
  [`grammar/LUSH_GRAMMAR.ebnf`](grammar/LUSH_GRAMMAR.ebnf) and
  [`grammar/PARSER_NOTES.md`](grammar/PARSER_NOTES.md), backed by
  nine per-section research files under `grammar/sections/`.
- **Step 2 (grammar-driven fuzzing) — DONE.** See
  [`FUZZING_PLAN.md`](FUZZING_PLAN.md) (all four phases baselined
  2026-05-19 / 2026-05-20; phase-1 + 4 libfuzzer targets clean,
  phase-3 differential harness operational).
- **Follow-on grammar fixes landed:** POSIX `cmd_prefix` grammar
  (`daeb658d`, #105) is the most recent example — the documented
  grammar now drives real parser corrections.

**Scope:** lush as a polyglot bash + zsh shell. Fish, ksh, tcsh, csh are explicitly **out of scope**.

This document is preserved as the original plan that drove both
the grammar specification and the fuzzing infrastructure. Refer to
the linked artifacts above for current state; update those rather
than re-editing this plan.

## Why this branch exists

`src/parser.c` has grown to 4,689 lines of recursive descent. There is no formal specification of the language lush actually accepts — the parser is the spec. That makes three things hard:

1. **Reasoning about correctness.** "Does lush accept this?" can only be answered by reading C, not by reading a grammar.
2. **Testing.** The existing fuzz scaffolding (`tests/fuzz/fuzz_parser.c`) generates random bytes; almost all inputs are syntactically invalid noise that never reaches interesting parser paths.
3. **Evolving the language.** When we want to add a feature, we have no concise statement of what's already there to reason about conflicts.

This branch produces a written grammar of what lush currently accepts, then uses that grammar to drive better fuzzing. Bigger architectural moves (parser generation, dialect splits) are deliberately deferred — they may or may not happen, and they should not happen before we know what we have.

## Recommended order of work

### Step 1 — Document the actual grammar (this step)

**Goal:** Produce `docs/development/grammar/LUSH_GRAMMAR.ebnf` plus `PARSER_NOTES.md` that together describe exactly what `parser.c` accepts, including all the context-sensitive bits that a pure CFG can't capture.

**Method:** Slice `parser.c` and `tokenizer.c` into nine logical sections and dispatch a research agent per section in parallel. Each agent writes a section file under `docs/development/grammar/sections/` documenting:
- EBNF productions for everything that section parses
- Which AST nodes (`node_type_t`) it produces
- Lexer interactions and context-sensitive behavior (positional reserved words, lexer modes, alias expansion timing, heredoc state, quote-context tokenization)
- Surprises, lush-specific extensions beyond bash/zsh, and any divergences from POSIX

A final synthesis pass merges the section files into a single EBNF document and a prose companion that captures everything pure EBNF can't (the lexer-state interactions are the hard part).

**Deliverables:**
- `docs/development/grammar/sections/01-top-level.md` … `09-tokenizer.md` (per-section research)
- `docs/development/grammar/LUSH_GRAMMAR.ebnf` (synthesized grammar)
- `docs/development/grammar/PARSER_NOTES.md` (lexer modes, context-sensitivity, surprises)

**Done when:** A reader who does not know `parser.c` can predict whether a given input will parse, and the prose notes explain every place where a pure CFG falls short.

### Step 2 — Grammar-driven fuzzing

**Goal:** Replace random-byte fuzzing with grammar-driven input generation so every fuzz input exercises a real parser path. Existing `tests/fuzz/fuzz_parser.c` and `fuzz_tokenizer.c` stay (they catch tokenizer-level bugs), but a new grammar fuzzer becomes the primary tool.

**Approach (subject to revision after Step 1):** the user has indicated grammarinator-as-described may not be the right tool. Decision deferred to after Step 1, when we know the grammar's shape. Candidates to evaluate:

- **Grammarinator** (Python, ANTLR grammars) — easiest start, but adds Python to the build.
- **Hand-rolled C generator** consuming the EBNF directly — no new dependencies, full control over biasing toward gnarly cases (heredocs, nested quotes, extended tests).
- **Nautilus-style coverage-guided grammar fuzzing** — best results, more setup.
- **Differential fuzzing against `bash -n`** — generate inputs valid under the lush grammar, check that bash agrees on accept/reject for the bash-compatible subset.

**Deliverables (TBD):** to be designed after Step 1 lands.

**Done when:** the fuzzer runs in CI for a bounded budget and finds zero new bugs on a clean input corpus over a multi-hour run.

### Steps 3+ — Open

The recommendations doc proposed several further moves (negative test corpus, AST round-trip testing, parser state-machine assertions, eventual generated parser). They remain on the table, but the user has indicated none of them are the focus right now. Decisions deferred until after Steps 1 and 2 produce evidence about where the real failure modes are.

## Scope discipline

- **No fish, ksh, tcsh, csh support.** Lush's polyglot ambition is bash + zsh, and that's the universe this work covers.
- **No parser replacement in this branch.** The grammar is descriptive, not generative — it documents the hand-written parser, it does not become the parser.
- **No dialect split yet.** A bash-grammar / zsh-grammar separation may eventually make sense, but Step 1 produces one grammar describing the union of what `parser.c` accepts today.

## Section map (Step 1 work breakdown)

| # | Section | Source range | What it covers |
|---|---|---|---|
| 1 | Top-level grammar | `parser.c:496–866` | `parser_parse`, `parse_command_line`, `parse_command_list`, `parse_pipeline`, `parse_logical_expression`, `parse_command_body`, `skip_separators` |
| 2 | Simple commands | `parser.c:867–1535` | `parse_simple_command` — words, assignments, expansions, command-position recognition |
| 3 | Brace, subshell, if/while/until | `parser.c:599–650`, `1536–1705`, `2237–2568` | Compound commands and the basic loop / conditional families |
| 4 | For, select, time, coproc, anonymous functions | `parser.c:2569–3422` | Iteration and zsh/bash-specific compound commands |
| 5 | Case statements | `parser.c:3423–3751` | Pattern lists, terminator types (`;;`, `;&`, `;;&`) |
| 6 | Function definitions | `parser.c:3752–4100` | POSIX `name()` and `function name` forms, name validation |
| 7 | Extended features | `parser.c:4101–end` | `(( ))`, array literals `(a b c)`, `[[ ]]`, process substitution `<()`/`>()` |
| 8 | Redirections and heredocs | `parser.c:1706–2236` | Every redirection operator plus `collect_heredoc_content` |
| 9 | Tokenizer | `src/tokenizer.c` (full) | Token classes, reserved words, lexer modes, quote contexts, expansion lex points |

Each section's agent writes to `docs/development/grammar/sections/0N-<name>.md`. Synthesis merges them.
