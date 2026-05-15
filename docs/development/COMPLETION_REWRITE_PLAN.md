# Completion Rewrite Plan

**Status:** Design ready for implementation. Drafted from Q&A session 2026-05-02.
**Provenance:** Replaces the scratched `completion-rewrite` branch (nuked 2026-05-01); supersedes the deleted `COMPLETION_ARCHITECTURE.md` v1/v2 from that branch. The previous attempt's failure modes are captured in the project memory `project-completion-rewrite-failure-postmortem.md` and informed this design — but the design's correctness rests on lush's own architectural standards, not on the postmortem's prescriptions.

## 1. The Bugs This Closes

Two release-blocking defects in the current completion subsystem:

**B1 — TAB does not handle filenames containing spaces.** A user who types `cat "my fi<TAB>` or `cat my\ fi<TAB>` does not get the partial `my fi` matched against filenames; the engine's word-boundary detection at `src/lle/completion/context_analyzer.c:23` and `src/lle/completion/completion_generator.c:43` is quote-blind and treats every space as a hard word terminator regardless of quote/escape state. After candidate selection, `replace_word_at_cursor` at `src/lle/keybinding/keybinding_actions.c:326` splices raw bytes; no escape rendering is performed so even if word boundaries were right, inserting a literal space would re-break the parse on the next line.

**B2 — TAB does not continue into directories.** When a candidate is a directory, the file source at `src/lle/completion/completion_sources.c:540` correctly sets `suffix = "/"` on the item, but all three insertion sites — `keybinding_actions.c:2545` (single-match), `:2688` (ENTER pick), `:411` (TAB cycling preview) — splice only `item->text`, ignoring `item->suffix`. The directory completes without a trailing slash, so the user's next TAB walks back from cursor past the directory name and the file source is queried with the wrong working directory.

Both bugs are surface symptoms of the same architectural gap: the engine has no model of "this byte is inside a quoted region" and no opinion about shell-escaping its output. The two parallel context analyzers (`lle_context_analyzer_t` in `context_analyzer.c` and `lle_completion_context_info_t` in `completion_generator.c`) compute different word boundaries from the same buffer, so any one-place fix to boundary handling silently fails to take effect everywhere.

## 2. Why a Fresh Rebuild

Per `feedback-architectural-correctness-over-expediency.md`, the right move when an existing module has the wrong shape is to rebuild it, not to bolt corrections onto a quote-blind walker that wasn't designed for the problem. The current analyzers ship a two-pass design — one walk for word boundary, a separate walk for `is_inside_quotes`, a third for `is_in_assignment` — which is itself a fingerprint of a module written without quote-state in mind. Bolting a fourth pass on top reliably leaves residual inconsistencies between passes.

Per `feedback-engineer-fixes-properly.md`, the splice/escape behavior is one operation. The current code replicates pieces of it across three insertion sites and embeds path-prefix preservation logic inside the file source itself. The right shape is one engine-side splicer that every source feeds into.

## 3. Architectural Principles

These are the non-negotiable principles all subsequent decisions hang from. They were established in the 2026-05-02 Q&A and are recorded in the project memory.

1. **Engine smart, sources dumb.** Sources emit candidates as literals only — no path prefix, no quote machinery, no escaping. The engine owns word-boundary detection, quote-state, expansion evaluation, escape rendering, splice math, and the close-quote/space/`/` finalization on accept.
2. **Preserve the user's typed prefix byte-for-byte**, with one structured exception: TAB at the end of a fully-typed expansion (variable, tilde, parameter, arithmetic, brace, glob) with no further word content resolves the expansion to its value(s) and appends a space. TAB after `<expansion>/<filename-prefix>` preserves the user's typed expansion bytes and completes the filename.
3. **One context analyzer.** The two parallel analyzers are deleted; one new primitive feeds both initial generation and TAB-cycling re-analysis.
4. **NFC unicode throughout** (per `project-unicode-normalization-policy.md`). Comparisons use `src/lle/unicode/unicode_compare.c`. Byte-`strncmp` is allowed only when both sides are 100 % ASCII-guaranteed.
5. **No "byte-by-byte" thinking** (per `feedback-unicode-mandatory.md`). Walks step through codepoints or graphemes, mediated by `src/lle/unicode/*` primitives.
6. **Lush opinionated default + always configurable** (per `project-config-patterns.md` and `project-central-config-architecture.md`). Every behavior added in this rewrite that has more than one reasonable answer is registered as a central-config key with `display lle <feature> <value>` builtin sugar.
7. **No punting on scope** (per `feedback-no-punting-out-of-scope.md`). Every case in the surface is designed in this doc, not deferred to a follow-up.
8. **`src/strings.c` is not used** (per `feedback-strings-c-deprecated.md`). Where a quote/escape primitive is needed it lives in unicode-aware code under `src/lle/unicode/` or in this rewrite's new modules.
9. **`src/expand.c` is the canonical expansion machinery.** The engine evaluates expansions by calling into it; it does not duplicate its own `lle_completion_expand_*` mini-helpers (those are deleted).

## 4. Decision Log

Every significant decision from the Q&A, indexed for traceability.

| # | Decision | Reasoning |
|---|----------|-----------|
| D1 | File single-match accept = candidate + matching close-char (if a quote was open) + trailing space. | Most natural final state; user has finished this argument. |
| D2 | Directory single-match accept = candidate + `/` (no close-char, no trailing space). | User typically tabs into the directory next; closing the quote prematurely would block continuation. |
| D3 | Quote style is preserved. `'my fi<TAB>` matching `my file.txt` produces `'my file.txt' `, not double-quoted or backslash-escaped. | Engine never modifies bytes the user typed in the prefix. |
| D4 | No-quote case backslash-escapes the appended tail. `cat my<TAB>` matching `my file.txt` produces `cat my\ file.txt `. | Same preserve-prefix rule extended; the appended tail must be valid in the current escape context. |
| D5 | Multi-match: menu opens immediately, first candidate previewed in buffer (rendered in current escape context), TAB cycles selection by deleting previewed candidate and inserting next, ENTER finalizes by appending the close+space (file) or `/` (dir). ESC restores user's original typed prefix. | One-press model with visible preview; original buffer state remembered for ESC-restore. |
| D6 | Sources emit filename literals only. Engine deletes `buffer[filename_portion_start .. cursor)` and inserts the rendered candidate. For path-shaped completion `filename_portion_start` is the byte after the last `/` in the dequoted word content; for non-path completion it's `word_start`. Same code path. | Single source contract, single splice rule, no per-source path-prefix logic. |
| D7 | Context analyzer rebuilt from scratch. Both `lle_context_analyzer_t` and `lle_completion_context_info_t` deleted. | The old analyzers' shape is wrong for the new model and rebuilding is the project's standing standard. |
| D8 | Single-value expansions (`~`, `~user`, `$VAR`, `${VAR}`, `${VAR:-default}` and friends, `$((...))`) follow uniform rules: TAB-alone at expansion end resolves to value; TAB after `<expansion>/<prefix>` preserves the typed expansion and completes the filename inside the resolved directory. | Consistent semantics across all single-value expansions. |
| D9 | `$(...)` and backticks default to bash/zsh-style evaluation during completion (matches the bash/zsh consensus behavior); configurable to safe-mode (no evaluation) via `display lle completion eval_command_subst off` for users who specifically want footgun protection from accidental TAB on pasted command substitutions. | Bash and zsh agree on evaluation; per `project-defaults-bash-zsh-consensus.md` lush defaults to the consensus. Safe-mode remains available as the lush opinion via one config flip. |
| D10 | Brace expansion `{a,b}/Doc<TAB>` uses smart-when-possible synthesis: engine evaluates the brace, runs file completion in each branch, splices candidate after typed `{a,b}/` if the **intersection** of per-branch matches is non-empty (preserves brace shorthand); falls back to bash/zsh-style inline expansion (`a/Doc b/Doc `) when intersection is empty. Configurable to pure bash mode, pure intersect mode, or union mode. | Outperforms bash/zsh when smart is possible, matches them when it isn't. |
| D11 | Brace `{a,b}<TAB>` (no path content after) resolves to space-joined values (`a b `), matching bash/zsh and consistent with the resolve-on-TAB-alone rule for other expansions. | The brace-shorthand-loss is the same trade-off `$HOME<TAB> → /home/user` accepts. |
| D12 | Globs (`*`, `?`, `[...]`) inline-expand on TAB by default, matching bash/zsh. Configurable to preserve-glob-form. | Globs don't execute commands, so the safety-first argument doesn't apply with the same force; users typed the glob to refer to a set, and making the set explicit is what they wanted to see. |
| D13 | Custom / plugin sources use the same literal-only contract. No special opt-out. | Single contract; they get splice + escape behavior for free. |
| D14 | Multiple expansions in one word route through `src/expand.c` for full evaluation. The result drives the per-expansion-type rule (e.g., a brace inside a path becomes the brace handling case). | Engine doesn't re-derive expansion semantics; reuses the canonical machinery. |
| D15 | Zero-candidate or unreadable-directory: silent no-op (no buffer change, no beep). Configurable to enable terminal bell. | Quiet by default, configurable. |
| D16 | Heredoc body context: TAB refuses to complete (heredoc bodies are literal text from the shell's perspective). | Reflects shell semantics. |

## 5. Configuration Keys Introduced

All registered in the central config registry per `project-central-config-architecture.md`. The `display lle ...` builtin gets matching sugar where appropriate.

| Key | Default | Effect when changed |
|-----|---------|---------------------|
| `completion.eval_command_subst` | `true` | Default matches bash/zsh: `$(...)` and `` `...` `` are evaluated during TAB. Set to `false` for safe-mode (treat command substitutions as opaque, never run them during completion). |
| `completion.brace_expansion_mode` | `intersect` | `intersect` (default), `union`, `bash` (always inline-expand), `lush_only` (intersect with no fallback). |
| `completion.glob_mode` | `inline_expand` | `inline_expand` (default, bash/zsh parity) or `preserve_form` (treat glob as opaque, just append space at end-of-word). |
| `completion.no_match_bell` | `false` | When `true`, terminal bell on zero-candidate / unreadable-directory. |

## 6. The New Context Analyzer

**Location:** `include/lle/completion/word_context.h`, `src/lle/completion/word_context.c`.

**Responsibility:** Given `(buffer, cursor_byte_offset)`, produce a structured description of the completion context — what's being completed, what shape it has, what was typed, what's still being typed.

**Output struct (conceptual fields):**

| Field | Meaning |
|-------|---------|
| `word_start` | Byte offset where the current shell-word begins (including any open quote char). |
| `word_end` | Equals `cursor_byte_offset`. |
| `quote_state` | One of: `NONE`, `SINGLE`, `DOUBLE`, `BACKTICK`, `ESCAPE_PENDING` (cursor is right after a `\`). |
| `expansion_prefix_end` | Byte offset where the *expansion* portion of the word ends (i.e., after `~/`, `$VAR/`, `${VAR}/`, `$(...)/`). Equals `word_start` if no expansion is present. |
| `filename_portion_start` | Byte offset where the filename-prefix portion begins. For path-shaped words this is the byte after the last `/` in the dequoted word content; for non-path words it equals `word_start` (or `word_start + 1` to skip an open quote, when applicable). |
| `dequoted_filename_prefix` | NFC-normalized, dequoted, post-`/` literal prefix (the string a source uses for prefix matching). |
| `context_type` | One of: `COMMAND_POSITION`, `ARGUMENT`, `REDIRECT_TARGET`, `VARIABLE_NAME`, `BRACED_VARIABLE_NAME`, `ASSIGNMENT_VALUE`, `FOR_IN_LIST`, `CASE_PATTERN`, `FUNCTION_BODY`, `HEREDOC_BODY`, `UNKNOWN`. |
| `command_name` | Owner command for builtin-arg dispatch (e.g., `cd`, `set`, `theme`). NULL for command-position contexts. |
| `arg_index` | Zero-based index of the current argument within its command. |
| `expansion_kind` | If cursor is *inside* an expansion (not after one): `VARIABLE_NAME`, `BRACED_VARIABLE_NAME`, `COMMAND_SUBST`, `ARITHMETIC`, `BRACE_LIST`, `GLOB`. Otherwise `NONE`. |
| `multivalue_expansion_set` | If the dequoted word contains brace expansion or glob with content after, the per-branch resolved paths (each as a directory + filename-prefix pair). Empty otherwise. |

**Walker semantics:** the analyzer walks the buffer from `word_start_search_anchor` (any earlier known-safe boundary) up to the cursor, tracking single/double/backtick/escape state grapheme by grapheme using `src/lle/unicode/*` primitives. It tracks paren/brace/bracket nesting for command-substitution / arithmetic / brace-expression detection. Quote and escape state respect the standard shell rules (single quotes suppress everything, double quotes suppress whitespace and most metacharacters but allow `$` and backtick, backslash escapes the next byte except inside single quotes). Multiline buffers are walked the same way; newlines inside an open quote are not boundaries.

**Anchor selection:** the analyzer doesn't always need to walk from byte 0. It walks from the most recent unambiguous statement boundary (after `;`, `&`, `|`, `&&`, `||`, newline outside any open quote/expression, or start of buffer). For interactive single-line buffers this is byte 0; for multiline buffers it's the start of the current statement.

**Public functions:**
- `lle_word_context_analyze(buffer, cursor_byte_offset, pool, &out_context) → lle_result_t`
- `lle_word_context_free(context) → void`

## 7. The New Source Contract

**Source signature:**

```
lle_result_t source(const lle_completion_query_t *query,
                    lle_completion_result_t *result);
```

`lle_completion_query_t` is a struct holding the inputs every source might need:

| Field | Meaning |
|-------|---------|
| `prefix` | NFC-normalized, dequoted filename prefix the user has typed. |
| `expanded_directory` | For file/directory sources: the absolute path to scan (with `~`, `$VAR`, etc. already resolved by the engine via `src/expand.c`). NULL for non-path sources. |
| `command_name` | Owner command for builtin-arg dispatch. May be NULL. |
| `arg_index` | Zero-based argument index. |
| `context_type` | Copy of the analyzer's `context_type` field, for sources that vary behavior by context. |
| `pool` | Memory pool for allocations. |
| `unicode_compare` | Pointer to the canonical NFC-normalized prefix-match primitive (so sources don't import `src/lle/unicode/...` directly; they call through the supplied function pointer). |

**Source returns** a list of items where each item carries:
- `text`: the candidate as a filename literal (or, for non-file sources, a plain text literal).
- `type`: classification (FILE, DIRECTORY, BUILTIN, COMMAND, ALIAS, VARIABLE, HISTORY, CUSTOM).
- `relevance_score`: as today.
- `description`: optional secondary text shown in menu.

**What sources do not do** anymore:
- They do not see, read, or preserve the user's typed shell-source prefix.
- They do not perform tilde or variable expansion (the engine has done it).
- They do not perform any quoting or escaping of their candidates.
- They do not compute `filename_portion_start` or splice math.
- They do not append `/` for directories or anything else; the `type` field carries the classification and the engine appends the right thing.

The `suffix` field on `lle_completion_item_t` is removed. Behavior is driven by `type` instead.

## 8. The Splicer

**Location:** `include/lle/completion/splicer.h`, `src/lle/completion/splicer.c`.

**Responsibility:** Given the analyzer's context, a chosen candidate, and a phase (preview vs. accept), produce the buffer mutation.

**Algorithm (accept phase):**

1. Compute `delete_range = [filename_portion_start .. cursor)`.
2. Render the candidate's `text` field for the current `quote_state` (call `splicer_render_for_context(text, quote_state) → rendered_bytes`):
   - `quote_state == NONE`: backslash-escape every byte that is shell-special in unquoted context — space, tab, `;`, `|`, `&`, `<`, `>`, `(`, `)`, `$`, `` ` ``, `\`, `"`, `'`, `*`, `?`, `[`, `{`, `~` (only at position 0 of the rendered output), `#` (only at position 0), `!`, newline.
   - `quote_state == DOUBLE`: backslash-escape only `$`, `` ` ``, `\`, `"`. Spaces and other metacharacters become literal.
   - `quote_state == SINGLE`: no escaping is possible inside single quotes (POSIX); if the candidate contains a single-quote byte, the splicer must close the single quote, emit `\'`, and re-open the single quote (`...'` → `...'\''...`). The user's typed open quote remains the open quote.
   - `quote_state == BACKTICK`: same rules as `DOUBLE` for backslash escaping (`$`, `` ` ``, `\`).
   - `quote_state == ESCAPE_PENDING`: the user's typed `\` ate one byte; emit nothing for the first rendered byte (it gets eaten by the pending escape) — practically this state should be rare; if cursor lands here the analyzer treats the next user keystroke as the escapee.
3. Compute the suffix for accept-phase based on candidate type and `quote_state`:
   - `type == DIRECTORY`: append `/`. Do not close any open quote. Do not append a trailing space.
   - `type == FILE | COMMAND | BUILTIN | ALIAS | VARIABLE | HISTORY | CUSTOM`: if `quote_state ∈ {SINGLE, DOUBLE, BACKTICK}`, append the matching close character. Append a trailing space.
4. Atomic buffer mutation via `lle_buffer_replace_text(buffer, filename_portion_start, cursor - filename_portion_start, rendered_bytes + suffix, length)`.
5. Move cursor to `filename_portion_start + length(rendered_bytes + suffix)`.

**Algorithm (preview phase, multi-match cycling):** as accept-phase but step 3 emits no suffix — the close-char and trailing space are added only on accept. ESC-during-preview restores `buffer[delete_range]` to the original `dequoted_filename_prefix` rendering (which equals the user's original typed bytes for that range — already preserved on the session-state object).

**Public functions:**
- `lle_splicer_splice_accept(buffer, cursor_mgr, context, item) → lle_result_t`
- `lle_splicer_splice_preview(buffer, cursor_mgr, context, item, preview_state) → lle_result_t`
- `lle_splicer_render_for_context(text, quote_state, pool, &rendered) → lle_result_t`
- `lle_splicer_close_char(quote_state) → char` (returns `'\0'` for NONE/ESCAPE_PENDING)

## 9. Expansion Pipeline

When the analyzer determines the typed shell-word contains expansion operators, the engine routes through `src/expand.c` to evaluate them, with these mode flags:
- `EXPAND_NOCMD` defaults to OFF for completion (matching `completion.eval_command_subst = true`, which is the default). When the user opts into safe-mode by setting that key to `false`, the flag is set so command substitutions are not evaluated during completion.
- `EXPAND_NOGLOB` for the brace-handling smart path — the engine wants the brace expansion separately from glob expansion when computing per-branch directory targets.

**Single-value expansions** (`~`, `~user`, `$VAR`, `${VAR}`, `${VAR:-...}`, `$((...))`):
- For TAB-alone at expansion end: ask `src/expand.c` to fully resolve, replace the user's typed expansion bytes with the resolved value, append space.
- For TAB after `<expansion>/<prefix>`: ask `src/expand.c` to resolve only the directory portion, use the result as `query.expanded_directory`, leave the user's typed bytes alone.

**Brace expansion** (`{a,b}`):
- Evaluate via `src/expand.c` to obtain the branch set.
- For TAB-alone at end of brace expression: emit each branch space-joined as the resolved value, append space.
- For TAB after `{a,b}/<prefix>`: run a file completion query in each branch's resolved directory; intersect candidate sets (default), union them (configured), or skip and inline-expand (configured). On intersection-empty fall back to bash/zsh-style inline expansion preserving the typed prefix in each branch.

**Glob expansion** (`*`, `?`, `[...]` in mid-word):
- Default (`completion.glob_mode = inline_expand`): on TAB, evaluate the glob in its enclosing directory (which itself may be the result of upstream expansion), inline-expand to the match list space-joined, append space.
- Alt (`completion.glob_mode = preserve_form`): treat glob as opaque, append space at end-of-word, refuse mid-word.

**Command substitution / backticks**:
- Default (matches bash/zsh): evaluate via `src/expand.c` (which spawns the subshell). TAB-alone at end of `$(...)` resolves the subshell output and appends a space. TAB after `$(...)/<prefix>` evaluates the subshell, uses the result as the resolved directory, completes the filename inside it. The user's typed `$(...)` bytes are preserved per the standing rule (only the post-`/` filename portion is replaced).
- Alt (`completion.eval_command_subst = false`, safe-mode): treat as opaque. TAB-alone at end of `$(...)` appends a space without evaluation. TAB after `$(...)/<prefix>` refuses (no buffer change) since the engine cannot know the resolved directory without running the subshell.
- TAB inside the subshell (cursor between the parens, completing the command name within `$(ls<TAB>...)`) is a future feature regardless of mode — see Section 16.

**Cursor inside an in-progress expansion** (e.g., `echo $HO<TAB>` — cursor mid-variable-name):
- Analyzer's `expansion_kind = VARIABLE_NAME` (or `BRACED_VARIABLE_NAME`, etc.).
- Engine dispatches to the variable-name source (not the file source). Splicer treats this as non-path completion (no `/` splitting); replaces user's typed `HO` with `HOME`. The leading `$` is preserved (it's outside `expansion_prefix_end`).

## 10. Walkthroughs

Concrete traces of every defining scenario.

### 10.1 `cat my<TAB>` — single match, no quote

- Buffer: `cat my`, cursor at byte 6.
- Analyzer: `word_start=4`, `quote_state=NONE`, `expansion_prefix_end=4`, `filename_portion_start=4`, `dequoted_filename_prefix="my"`, `context_type=ARGUMENT`, `command_name="cat"`.
- Engine dispatches to file source with `query.prefix="my"`, `query.expanded_directory=getcwd()`.
- Source returns `[{text: "my file.txt", type: FILE}]`.
- Single match → splicer accept-phase. `delete_range=[4,6)`, render `"my file.txt"` for `NONE` → `"my\ file.txt"`, suffix = `" "` (file, no quote). Result: `cat my\ file.txt `.
- Cursor at byte 17.

### 10.2 `cat "my fi<TAB>` — single match, double-quote open

- Buffer: `cat "my fi`, cursor at byte 10.
- Analyzer: `word_start=4` (the open `"`), `quote_state=DOUBLE`, `filename_portion_start=5` (one past the open `"`), `dequoted_filename_prefix="my fi"`, `context_type=ARGUMENT`, `command_name="cat"`.
- Engine: file source, `query.prefix="my fi"`.
- Source returns `[{text: "my file.txt", type: FILE}]`.
- Splicer: `delete_range=[5,10)`, render `"my file.txt"` for `DOUBLE` → `"my file.txt"` (no escape needed inside double quotes for spaces), suffix = `"\" "` (close-double-quote + space). Result: `cat "my file.txt" `.

### 10.3 `cat 'my fi<TAB>` — single match, single-quote open

- Same as 10.2 but `quote_state=SINGLE`, suffix = `"' "` (close-single-quote + space). Result: `cat 'my file.txt' `.

### 10.4 `cat my\ fi<TAB>` — single match, backslash-escape style

- Buffer: `cat my\ fi`, cursor at byte 10.
- Analyzer: `word_start=4`, `quote_state=NONE`, the analyzer walks the `\ ` sequence and treats them as part of the same shell-word; `filename_portion_start=4`, `dequoted_filename_prefix="my fi"` (the `\ ` dequoted to a literal space), `context_type=ARGUMENT`.
- Engine: file source, `query.prefix="my fi"`.
- Source returns `[{text: "my file.txt", type: FILE}]`.
- Splicer: `delete_range=[4,10)`, render for `NONE` → `"my\ file.txt"`, suffix = `" "`. Result: `cat my\ file.txt `.

Note: the user's chosen escape style (backslash) is reflected in the splicer output because the rendering rule for `NONE` happens to produce backslash-escape — the engine doesn't track "user prefers backslash" as a separate flag; the `quote_state=NONE` rule produces the same byte shape the user already typed.

### 10.5 `cd "my <TAB>` — single match, directory, double-quote open

- Buffer: `cd "my `, cursor at byte 7.
- Analyzer: `word_start=3`, `quote_state=DOUBLE`, `filename_portion_start=4`, `dequoted_filename_prefix="my "`, `context_type=ARGUMENT`, `command_name="cd"`.
- Engine: file source (filtered to directories because `command_name == "cd"`), `query.prefix="my "`.
- Source returns `[{text: "My Documents", type: DIRECTORY}]`.
- Splicer: `delete_range=[4,7)`, render `"My Documents"` for `DOUBLE` → `"My Documents"`, suffix = `"/"` (dir → no close, just slash). Result: `cd "My Documents/`. Cursor inside the still-open quote, ready for next TAB.

### 10.6 `cat ~/Doc<TAB>` — single match, tilde + path

- Buffer: `cat ~/Doc`, cursor at byte 9.
- Analyzer: `word_start=4`, `quote_state=NONE`, `expansion_prefix_end=6` (after `~/`), `filename_portion_start=6`, `dequoted_filename_prefix="Doc"`, expansion-prefix bytes (`~/`) preserved.
- Engine resolves `~/` via `src/expand.c` to `/home/user/`, sets `query.expanded_directory="/home/user/"`, `query.prefix="Doc"`.
- Source returns `[{text: "Documents", type: DIRECTORY}]`.
- Splicer: `delete_range=[6,9)`, render `"Documents"` for `NONE` → `"Documents"`, suffix = `"/"`. Result: `cat ~/Documents/`. The user's `~/` is untouched.

### 10.7 `cat $HOME/Doc<TAB>` — single match, variable + path

- Same as 10.6 with `expansion_prefix_end=11` (after `$HOME/`). Engine resolves `$HOME` via `src/expand.c`. Result: `cat $HOME/Documents/`. The user's `$HOME/` is preserved.

### 10.8 `echo $HO<TAB>` — variable name completion

- Buffer: `echo $HO`, cursor at byte 8.
- Analyzer: `word_start=5` (the `$`), `expansion_kind=VARIABLE_NAME`, `filename_portion_start=6` (one past the `$`), `dequoted_filename_prefix="HO"`.
- Engine dispatches to the variable-name source. Source returns `[{text: "HOME", type: VARIABLE}, {text: "HOSTNAME", type: VARIABLE}]`.
- Multi-match → menu. Preview phase splices `HOME` after the `$`. ENTER: variables get `" "` suffix (no path semantics, treat as accepted word). Result: `echo $HOME `.

### 10.9 `echo $HOME<TAB>` — TAB at end of variable name (resolved by engine env-lookup)

- Buffer: `echo $HOME`, cursor at byte 10.
- Analyzer: `word_start=5`, `quote_state=NONE`, `expansion_prefix_end=10`, `filename_portion_start=10` (no further word content), `expansion_kind=VARIABLE_NAME`, `context_type=VARIABLE_NAME`. The analyzer is purely structural — it cannot tell whether `$HOME` is a "complete" env variable or a partial the user might extend; it always reports VARIABLE_NAME for `$NAME` at end of word.
- Engine: this is the TAB-on-variable-name case. Engine queries the variables source with prefix `"HOME"`. Source returns exactly one match equal to the typed prefix (`HOME`). Engine treats this as the resolve-on-complete case: resolves `$HOME` via `src/expand.c` to `/home/user`, replaces user's typed `$HOME` (bytes 5–10) with `/home/user `, suffix = `" "`. Result: `echo /home/user `. If the source had returned multiple matches or none equal-prefix, the engine would offer a completion menu instead.

### 10.10 `cat {a,b}/Doc<TAB>` — brace expansion with intersection completion

- Buffer: `cat {a,b}/Doc`, cursor at byte 13.
- Analyzer: `word_start=4`, `quote_state=NONE`, `expansion_kind=BRACE_LIST`, the brace evaluates to branches `{a, b}`, `expansion_prefix_end=10` (after `{a,b}/`), `filename_portion_start=10`, `dequoted_filename_prefix="Doc"`, `multivalue_expansion_set=[(a/, "Doc"), (b/, "Doc")]`.
- Engine queries the file source once per branch. Results: `a/` returns `[{text: "Documents", type: DIRECTORY}]`; `b/` returns `[{text: "Documents", type: DIRECTORY}]`. **Intersection** non-empty → smart completion path.
- Splicer: `delete_range=[10,13)`, render `"Documents"`, suffix = `"/"`. Result: `cat {a,b}/Documents/`. Brace shorthand preserved.

If only `a/Documents/` existed and `b/Documents/` did not, the intersection would be empty. Engine falls back to bash/zsh-style inline expansion: replace user's typed `{a,b}/Doc` with `a/Doc b/Doc `.

### 10.11 `cat *.tx<TAB>` — glob inline expansion (default)

- Buffer: `cat *.tx`, cursor at byte 8.
- Analyzer: `expansion_kind=GLOB`, glob matches in `getcwd()` are e.g. `foo.txt, bar.txt, baz.txt`.
- Engine inline-expands: replace user's typed `*.tx` with `foo.txt bar.txt baz.txt `, append space. Result: `cat foo.txt bar.txt baz.txt `.

When `completion.glob_mode = preserve_form`, engine instead just appends a space → `cat *.tx ` (treats as opaque).

### 10.12 `git checkout feat<TAB>` — custom source, candidate may contain `/`

- Buffer: `git checkout feat`, cursor at byte 17.
- Analyzer: `word_start=13`, `quote_state=NONE`, `filename_portion_start=13` (no `/` in user's word), `dequoted_filename_prefix="feat"`, `context_type=ARGUMENT`, `command_name="git"`.
- Engine dispatches to a custom git source. Source returns `[{text: "feature/foo", type: CUSTOM}, {text: "feature/bar", type: CUSTOM}]`.
- Multi-match → menu. Preview splices `feature/foo` for `NONE` (slashes are not shell-special so no escape; only spaces, etc., would). On accept: suffix = `" "`. Result: `git checkout feature/foo `.

### 10.13 `set -o err<TAB>` — builtin arg, no path

- Buffer: `set -o err`, cursor at byte 10.
- Analyzer: `context_type=ARGUMENT`, `command_name="set"`, `arg_index=1`, `dequoted_filename_prefix="err"`.
- Engine dispatches to the builtin-args source for `set`. Source returns `[{text: "errexit", type: BUILTIN}]`.
- Single match → accept-phase. Result: `set -o errexit `.

## 11. What Gets Deleted

| Path | Reason |
|------|--------|
| `src/lle/completion/context_analyzer.c` and `.h` | Replaced by `word_context.c/.h`. |
| The entire `lle_completion_context_info_t` type and `lle_completion_analyze_context` from `src/lle/completion/completion_generator.c/.h` | Replaced by `lle_word_context_t` and `lle_word_context_analyze`. |
| `lle_completion_expand_path`, `lle_completion_expand_tilde`, `lle_completion_expand_variable` in `src/lle/completion/completion_sources.c` | Engine uses `src/expand.c` instead; sources don't expand. |
| The path-prefix-preservation block at `src/lle/completion/completion_sources.c:553-579` | Sources emit only filename literals; engine reconstructs. |
| The `suffix` field on `lle_completion_item_t` and all code that sets it (`completion_sources.c:540`, etc.) | Behavior driven by `type` instead. |
| The internal helpers `find_word_start`, `find_word_end`, `find_prev_grapheme_start`, `find_next_grapheme_end`, `decode_codepoint_at` in `src/lle/keybinding/keybinding_actions.c` (currently used by `lle_forward_word` etc., **only the completion-related callers; word-movement helpers stay**). | These were quote-blind and duplicated the analyzer's work; word-movement keybinding actions get retargeted to use the new analyzer's grapheme-aware walker. |
| `replace_word_at_cursor` in `keybinding_actions.c` | Replaced by the splicer's public functions. |

## 12. What Gets Created or Modified

**Created:**
- `include/lle/completion/word_context.h`, `src/lle/completion/word_context.c` — new analyzer.
- `include/lle/completion/splicer.h`, `src/lle/completion/splicer.c` — new splicer + escape rendering.
- `include/lle/completion/completion_query.h` — new `lle_completion_query_t` type and constructors. (Could live inside `word_context.h`; separation is for testability.)

**Modified:**
- All sources in `src/lle/completion/completion_sources.c` rewritten to:
  - Take the new `lle_completion_query_t *query` argument (singular) plus result.
  - Return literals only.
  - Use `query.unicode_compare` for prefix matching.
  - For file/directory sources: use `query.expanded_directory` directly (no internal expansion).
- `src/lle/completion/completion_system.c::lle_completion_system_generate` rewritten to call the new analyzer + new query construction + dispatch sources via the new contract.
- `src/lle/completion/source_manager.c` — adapt source registration to the new signature.
- `src/lle/keybinding/keybinding_actions.c::lle_complete` and `lle_accept_line` rewritten to use the analyzer + splicer.
- `src/lle/completion/builtin_completions.c` — adapt builtin-arg sources to the new contract.
- `src/lle/completion/custom_source.c` — adapt the plugin source registration.
- `src/lle/completion/completion_menu_*.c` — adapt menu rendering to render each candidate in the active `quote_state` (calls the splicer's render function).
- `src/config.c` — register the four new config keys (D-9, D-10, D-12, D-15).

**Untouched (continue to work as today):**
- `src/lle/completion/ssh_hosts.c` (data layer; gets adapted to new source signature, internals unchanged).
- The buffer / cursor / event subsystems.
- The display pipeline.

## 13. Migration Plan

Ordered atomic commits. Each one compiles and passes the existing test suite. Each commit prefix is `LLE:` per `feedback-commit-review.md`.

| # | Commit | Notes |
|---|--------|-------|
| 1 | `LLE: add word_context analyzer (no callers)` | New files only. Unit-tested in isolation against every walkthrough scenario. Existing analyzers untouched. |
| 2 | `LLE: add splicer + escape renderer (no callers)` | New files only. Unit-tested against the rendering rules from Section 8. |
| 3 | `LLE: add lle_completion_query_t and adapt source signature` | New query type. Adapt every existing source's signature; internals continue to work because old `prefix` parameter maps directly to `query.prefix` and old internal expansion still runs (transitional). All existing tests pass. |
| 4 | `LLE: rewrite file source to use engine-supplied directory` | File source stops doing its own expansion; takes `query.expanded_directory`. Engine generation path temporarily computes that itself using the new analyzer (the analyzer is now wired into `lle_completion_system_generate` for the directory-resolution step only; word-boundary detection still uses the old analyzer). |
| 5 | `LLE: route lle_complete and lle_accept_line through splicer` | The single-match and ENTER paths use the splicer for buffer mutation. `update_inline_completion` re-targets to splicer-preview. The old `replace_word_at_cursor` is now unused but kept temporarily. |
| 6 | `LLE: route generation through new analyzer (delete old analyzers)` | `lle_completion_system_generate` now uses `lle_word_context_analyze` for word boundaries, expansion handling, and context type. Old `lle_context_analyzer_t`, `lle_completion_context_info_t`, and their `_analyze` functions are deleted. |
| 7 | `LLE: delete suffix field, drive accept behavior from item type` | Remove `lle_completion_item_t::suffix` and every site that sets it. |
| 8 | `LLE: register completion config keys` | The four new keys + `display lle completion ...` builtin sugar. Defaults match the doc. |
| 9 | `LLE: implement brace and glob expansion paths` | Tests cover D-10, D-11, D-12 walkthroughs. |
| 10 | `LLE: implement $(...) evaluation default and safe-mode config opt-in` | Tests cover D-9 walkthrough — bash/zsh-style evaluation is default; `completion.eval_command_subst = false` switches to safe-mode (opaque, no evaluation). |
| 11 | `LLE: delete lle_completion_expand_*, replace_word_at_cursor` | Final cleanup. After this, no completion-related code exists outside the new modules. |
| 12 | `LLE: cross-platform NFC normalization in source prefix matching` | Verifies that NFD names from macOS readdir get normalized to NFC at ingest before comparison. Tests on a macOS-style fixture. |

Each commit lands `Refs #<umbrella issue>` (multi-commit closure pattern per `feedback-multi-commit-issue-closure.md`); the umbrella issue is closed manually after commit 12 lands on master.

## 14. Test Plan

Tests are added under `tests/lle/unit/`, `tests/lle/functional/`, and `tests/lle/compliance/` per the project's existing testing conventions (`project-test-framework.md`).

**Unit tests (analyzer):**
- For each of the 13 walkthrough scenarios in Section 10, a test that constructs the buffer + cursor and asserts on every analyzer field.
- Multiline buffer cases: `if test -f /tmp/foo\nthen\n  cat /tmp/<TAB>\nfi` — analyzer correctly identifies the cursor's enclosing context.
- Quote-state edge cases: cursor right at an open quote (`cat "<TAB>`), cursor right after a close quote, cursor inside an escape-pending state (`cat \<TAB>`), cursor inside `$(echo "nested")<TAB>`, cursor inside heredoc body.
- NFC normalization: a candidate filename in NFD bytes is matched against an NFC-typed prefix.

**Unit tests (splicer):**
- Render-for-context: every rendering rule from Section 8, including the awkward single-quote case (candidate containing `'`).
- Suffix logic: every (`type`, `quote_state`) combination produces the expected suffix.
- Atomic mutation: buffer state and cursor position match expectation after splice.

**Functional tests:**
- End-to-end: each walkthrough scenario simulated through `lle_complete` on a test buffer; final buffer state asserted.
- Multi-match cycling: TAB cycles, ESC restores, ENTER finalizes — each transition asserted.
- Configuration toggles: every config key from Section 5 toggled; the corresponding behavior change asserted.

**Compliance test (`spec_12_completion_compliance.c`):** updated to assert the architectural invariants — sources never see the user's typed shell-source prefix, splicer is the only function mutating the buffer in completion code paths, etc.

**Cross-shell compatibility tests** (per `project-polyglot-identity.md`): a small corpus of completion scenarios exercised in lush, bash (subset), and zsh (subset); assertions on the user-visible buffer state on accept.

## 15. Open Implementation Questions

These don't gate the design — they're items to settle in code review.

1. The exact API by which the splicer accesses the buffer's change-tracking. Probably the existing `lle_buffer_replace_text` is enough since it already creates a single atomic change-tracker entry; verify before committing.
2. Whether `lle_completion_query_t` carries the analyzer's full `lle_word_context_t` by reference, or copies the relevant fields. Reference is cheaper but couples query lifetime to context lifetime.
3. The precise enumeration of `expansion_kind` for backslash-escape-pending state — there's no expansion underway, but the cursor is in a state where the next byte will be escaped. Treating this as `expansion_kind=NONE` with `quote_state=ESCAPE_PENDING` may be cleaner than introducing an `ESCAPE_PENDING` expansion kind.
4. How the menu renderer is told which `quote_state` to render against. Probably part of the menu state that's already kept across cycles; verify in the existing menu code before committing.
5. Whether the unicode comparison primitive needs a dedicated "filename prefix match" function in `src/lle/unicode/unicode_compare.c` or whether existing functions suffice. Investigate before adding new ones (per `feedback-lush-already-has-it.md`).

## 16. Out of the Way (Not in This Rewrite)

Things that are real future features but explicitly not part of this work — listed so they don't leak in by accident.

- A `hash -d`-equivalent named-directory feature (zsh-style). When added later, `~name<TAB>` could menu the named directories. The completion engine's analyzer is designed to accommodate it (`expansion_kind` would gain `NAMED_DIR`), but the feature itself is not built here.
- Fuzzy matching across candidates. The current architecture supports adding it later as a result-filtering layer; not built here.
- Completion of contents *inside* a `$(...)` subshell expression. Configurable evaluation of `$(...)` produces the resolved value as a single text; completing *inside* the parens (e.g., completing the command name within `$(ls<TAB>...)`) would be a recursive completion session, treated as a future feature.

---

**Reviewer:** Michael Berry. Sign-off blocks commit 1.
