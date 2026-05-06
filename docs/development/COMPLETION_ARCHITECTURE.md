# Completion Architecture

**Authoritative developer reference for the lush completion subsystem.**

| | |
|---|---|
| **Spec** | replaces `docs/lle_specification/12_completion_system_complete.md` |
| **Implementation root** | `src/lle/completion/`, `include/lle/completion/` |
| **TAB entry point** | `src/lle/keybinding/keybinding_actions.c:2407` (`lle_complete`) |
| **Last verified** | 2026-05-05 |

This document supersedes spec 12 as the source of truth for what completion
*currently does*. Spec 12 describes an ambitious end-state with modules
(fuzzy matcher, plugin registry, security context, performance monitor) that
this rewrite has not yet built. The rewrite addressed fundamental
architectural issues in the previous spec-following attempt — the parallel
analyzers, the unclear source/engine boundary, the bugs that came from
sources owning escape policy — and put the foundation in place. The
remaining spec components are future work on top of that foundation, not
dropped scope. [§12 Spec divergences](#12-spec-divergences) and
[§14 Known gaps](#14-known-gaps-and-open-work) catalogue what's missing
and where it is on the path forward.

---

## Reading order

If you only have ten minutes, read [§2 The pipeline](#2-the-pipeline) and
[§3 Data structures](#3-data-structures). Those two sections cover 80% of
the surface area.

If you are debugging a specific TAB-press, follow the trace in
[§2.2 End-to-end](#22-end-to-end-tab-press-trace) — every numbered step
cites a file:line you can step through.

If you are *adding a new completion source*, jump to
[§15 How to add a new completion source](#15-how-to-add-a-new-completion-source).
The rest of the doc is reference material you can come back to.

---

## Table of contents

1. [Overview](#1-overview)
2. [The pipeline](#2-the-pipeline)
3. [Data structures](#3-data-structures)
4. [The word-context analyzer](#4-the-word-context-analyzer)
5. [Sources and the source/engine boundary](#5-sources-and-the-sourceengine-boundary)
6. [The splicer](#6-the-splicer)
7. [Built-in command completions](#7-built-in-command-completions)
8. [The menu system](#8-the-menu-system)
9. [Configuration](#9-configuration)
10. [Caching](#10-caching)
11. [Integration points](#11-integration-points)
12. [Spec divergences](#12-spec-divergences)
13. [Tests](#13-tests)
14. [Known gaps and open work](#14-known-gaps-and-open-work)
15. [How to add a new completion source](#15-how-to-add-a-new-completion-source)

---

## 1. Overview

The completion subsystem turns a TAB press into the right bytes inserted at
the cursor. It is structured as four cooperating layers:

| Layer | Job | Headers |
|---|---|---|
| **Analyzer** | Walk the buffer up to the cursor, produce a structured "what is being completed" description | `word_context.h` |
| **Sources** | Adapt shell data (builtins, aliases, files, history, hosts, custom-defined) into completion candidates | `completion_sources.h`, `custom_source.h`, `source_manager.h`, `builtin_completions.h`, `ssh_hosts.h` |
| **Engine** | Orchestrate sources, deduplicate, sort, manage session state, drive the menu | `completion_system.h`, `completion_state.h`, `completion_menu_state.h`, `completion_menu_logic.h`, `completion_menu_renderer.h` |
| **Splicer** | Render a chosen candidate with the correct quoting/escapes for the cursor's lexical context, and compute the buffer mutation | `splicer.h` |

The **strict source/engine boundary** is the most important architectural
property. Sources receive an analyzed context and emit *literal* candidates.
They do *not* preserve path prefixes, do *not* manipulate quotes, do *not*
escape special characters. The engine and splicer own all of that. This
keeps sources simple to write — see [§15](#15-how-to-add-a-new-completion-source).

This subsystem is the *second* completion implementation. The first
implementation accumulated parallel analyzers (`lle_context_analyzer_t` in
`context_analyzer.c` and `lle_completion_context_info_t` in
`completion_generator.c`) that disagreed on word-boundary detection for
non-ASCII input. The current implementation unifies them into a single
walker (`lle_word_context_analyze`) that is the sole source of truth.
Postmortem of the first attempt:
`memory/project-completion-rewrite-failure-postmortem.md`.

---

## 2. The pipeline

### 2.1 Diagram

```
                     +----------------------+
       TAB key  ---> | lle_complete()       |  src/lle/keybinding/
                     | (the entry point)    |  keybinding_actions.c:2407
                     +----------+-----------+
                                |
                                v
                     +----------------------+
                     | lle_completion_      |  src/lle/completion/
                     | system_generate()    |  completion_system.c:209
                     +----------+-----------+
                                |
                                | (1) analyze
                                v
                     +----------------------+
                     | lle_word_context_    |  src/lle/completion/
                     | analyze()            |  word_context.c
                     +----------+-----------+
                                |
                                | lle_word_context_t
                                v
                     +----------------------+
                     | lle_source_manager_  |  src/lle/completion/
                     | query()              |  source_manager.c
                     +----------+-----------+
                                |
                                | for each registered source:
                                | if applicable, generate()
                                v
                     +----------------------+
                     | sources[]            |  src/lle/completion/
                     |   builtins, aliases, |  completion_sources.c
                     |   external_commands, |  builtin_completions.c
                     |   files, variables,  |  custom_source.c
                     |   history,           |
                     |   builtin_args,      |
                     |   ssh_hosts,         |
                     |   custom (TOML/API)  |
                     +----------+-----------+
                                |
                                | lle_completion_result_t (items[])
                                v
                     +----------------------+
                     | dedupe + sort        |  completion_system.c:121-195
                     +----------+-----------+
                                |
                                | result with N items
                                v
              +-----------------+----------------+
              | N == 0          | N == 1         | N > 1
              | (clear state)   | (single splice)| (open menu)
              +-----------------+----------------+
                                                 |
                                                 v
                                       +---------------------+
                                       | lle_splicer_apply_  |  src/lle/
                                       | accept()  /         |  completion/
                                       | apply_preview()     |  splicer.c
                                       +---------------------+
                                                 |
                                                 v
                                          buffer mutation
```

### 2.2 End-to-end TAB-press trace

The user presses TAB. The keybinding manager invokes `lle_complete(editor)`
at `keybinding_actions.c:2407`. The function walks four cases:

**Case A — completion already active with a visible menu.** Cycle to the
next item. Calls `lle_completion_menu_move_next(menu)`
(`completion_menu_logic.c`), updates the inline preview via
`update_inline_completion(editor, menu, state)`, sets
`display_controller->menu_state_changed = true` so the next render redraws
the menu, returns. (`keybinding_actions.c:2423-2448`.)

**Case B — completion active but menu not visible.** This means a previous
single-match completion left stale state behind; clear it via
`lle_completion_system_clear()` (`keybinding_actions.c:2451-2453`) before
falling through to fresh generation.

**Case C — fresh generation.** Read the cursor byte offset from the cursor
manager (`keybinding_actions.c:2456-2458`), then call
`lle_completion_system_generate(system, buffer, cursor_pos, &result)`
(`keybinding_actions.c:2464`). That function:

1. **Analyzes context.** Calls `lle_word_context_analyze(buffer, cursor_pos,
   pool, &ctx)` (`completion_system.c:222`). Returns an
   `lle_word_context_t*` with `quote_state`, `expansion_kind`,
   `context_type`, `command_name`, `arg_index`, `arguments[]`,
   `expanded_directory`, `dequoted_filename_prefix`, `branches[]`.

2. **Creates a fresh result container.** `lle_completion_result_create(pool,
   64, &result)` (`completion_system.c:230`). Initial capacity 64, auto-grows.

3. **Queries sources.** `lle_source_manager_query(manager, ctx, result)`
   (`completion_system.c:240`). Loops every registered source, calls
   `is_applicable(ctx)` for each, and on `true` calls `generate(pool, ctx,
   result)`. The source appends candidates via
   `lle_completion_result_add(result, text, suffix, type, score)`. For
   brace-expansion contexts (`ctx->branch_count > 0`), the manager fans out:
   each source is called once per branch with a synthetic single-directory
   context (`source_manager.c:259-269`).

4. **Deduplicates.** `deduplicate_results(result)`
   (`completion_system.c:121-154`) collapses items with identical `(text,
   type)` pairs. Allows `echo` BUILTIN and `echo` COMMAND to both survive
   (different types). Removes duplicate references to the same item.

5. **Sorts.** `sort_results(result)` (`completion_system.c:186-195`) calls
   `qsort` with a comparator that orders by `relevance_score` descending,
   then `type` ascending, then `text` lexically.

6. **Tracks state.** `lle_completion_state_create(pool, buffer, cursor_pos,
   ctx, result, &state)` (`completion_system.c:268`) snapshots the buffer
   and cursor, takes ownership of the context and result, sets
   `current_index=-1`, `active=true`. The state's lifetime spans the
   completion session; freeing it frees the context and result.

7. **Creates menu if multi-match.** If `result->count >= 2`,
   `lle_completion_menu_state_create(pool, result, NULL, &menu)`
   (`completion_system.c:278`). Default config:
   `max_visible_items=20`, `show_category_headers=true`,
   `show_type_indicators=false`, `enable_scrolling=true`,
   `min_items_for_menu=2` (`completion_system.c:271-275`).

8. **Stores state on the system.** `system->current_state = state;
   system->menu = menu` (`completion_system.c:286-292`).

Back in `lle_complete()`, the result is dispatched by count:

**N == 0.** No completions. Clear state and return
(`keybinding_actions.c:2474-2477`).

**N == 1.** Single match. The chosen item is `result->items[0]`. If the
item is `LLE_COMPLETION_TYPE_COMMAND` and has a `description`, the splicer
is given a synthetic copy whose `text` is the description (the full
PATH-resolved path) — this disambiguates an external command that shadows a
builtin or alias (`keybinding_actions.c:2489-2493`). The buffer is
re-analyzed for a fresh splice context (`keybinding_actions.c:2500-2501`)
and `lle_splicer_apply_accept(buffer, cursor_mgr, ctx, item, pool)`
mutates the buffer (`keybinding_actions.c:2507-2510`). State is cleared.

**N > 1.** Multi-match. The menu is already in `system->menu`. Preview the
first candidate inline via `update_inline_completion(editor, menu, state)`,
then hand the menu to the display controller via
`display_controller_set_completion_menu(dc, menu)`
(`keybinding_actions.c:2526-2541`). On the user's next interaction:

- TAB: cycles selection forward (Case A above).
- Arrow keys / Page-up/-down / category-jump: routed through the display
  controller into `completion_menu_logic.c` movement functions, which
  update `selected_index` and re-preview.
- ENTER: `lle_accept_line` finalises the chosen candidate via
  `lle_splicer_apply_accept` (with the suffix this time), clears the menu.
- ESC: `lle_abort_line` cancels, reverts to the buffer snapshot, clears
  state.

---

## 3. Data structures

### 3.1 `lle_word_context_t` — analyzer output

`include/lle/completion/word_context.h:164`. The analyzer's output and the
single source of truth about what the user is completing.

| Field | Type | Meaning |
|---|---|---|
| `word_start` | `size_t` | Byte offset where the current shell-word begins, including any open quote character |
| `word_end` | `size_t` | Equals the cursor byte offset (the analyzer walks up to and stops at the cursor) |
| `expansion_prefix_end` | `size_t` | Byte offset where the expansion portion of the word ends |
| `filename_portion_start` | `size_t` | Byte offset where the filename-prefix portion begins. For path-shaped words: the byte after the last `/` in the dequoted word content. For non-path words: equals `word_start` (or `word_start+N` if an open quote/escape consumes leading bytes) |
| `quote_state` | `lle_quote_state_t` | `NONE` / `SINGLE` / `DOUBLE` / `BACKTICK` / `ESCAPE_PENDING` |
| `expansion_kind` | `lle_expansion_kind_t` | `NONE` / `VARIABLE_NAME` / `BRACED_VARIABLE_NAME` / `COMMAND_SUBST` / `ARITHMETIC` / `BRACE_LIST` / `GLOB` |
| `context_type` | `lle_word_context_type_t` | `COMMAND_POSITION` / `ARGUMENT` / `REDIRECT_TARGET` / `VARIABLE_NAME` / `ASSIGNMENT_VALUE` / `FOR_IN_LIST` / `CASE_PATTERN` / `HEREDOC_BODY` / `UNKNOWN` |
| `command_name` | `char *` | Owner command for builtin-arg dispatch (e.g. `cd`, `set`). `NULL` in command-position contexts |
| `arg_index` | `int` | Zero-based index of this argument within its command. `-1` if not applicable |
| `arguments` | `char **` | Pool-allocated array of completed argument strings *before* the cursor's current word, dequoted and NFC-normalized. Used by builtin-arg sources to walk subcommand hierarchies (e.g. `display lle theme set <here>`). `NULL` when no completed arguments exist |
| `argument_count` | `size_t` | Length of `arguments[]` |
| `expanded_directory` | `char *` | Absolute path to scan when the word is path-shaped and produces a *single* resolved directory. `NULL` for non-path words and for multi-value expansions |
| `dequoted_filename_prefix` | `char *` | NFC-normalized, dequoted, post-`/` literal prefix the source uses for prefix-matching against candidates |
| `branches` | `lle_word_context_branch_t *` | Per-branch resolved directory + prefix pairs for multi-value brace expansion. `NULL` when single-value |
| `branch_count` | `size_t` | Length of `branches[]` |
| `pool` | `lle_memory_pool_t *` | Pool used for all internal allocations |

The struct and all internally-allocated strings are released by
`lle_word_context_free()` (`word_context.h:275`). Freeing is pool-aware;
calling it with `NULL` is safe.

**NFC invariant.** `dequoted_filename_prefix` is NFC-normalized so sources
can pass it directly to `lle_unicode_is_prefix` for byte-level prefix
comparison. Sources do not perform their own normalization.

### 3.2 `lle_completion_item_t` — what sources emit

`include/lle/completion/completion_types.h:68`.

```c
typedef struct lle_completion_item {
    char *text;                  /* Completion text (literal) */
    char *suffix;                /* Optional suffix; usually NULL */
    lle_completion_type_t type;  /* BUILTIN/COMMAND/FILE/DIR/... */
    const char *type_indicator;  /* Symbol for menu rendering */
    int32_t relevance_score;     /* 0-1000, higher ranks first */
    char *description;           /* Optional second-line text */

    bool owns_text;              /* Pool/manual ownership flag */
    bool owns_suffix;
    bool owns_description;
} lle_completion_item_t;
```

**Type taxonomy** (`completion_types.h:47`):
`LLE_COMPLETION_TYPE_BUILTIN` / `COMMAND` / `FILE` / `DIRECTORY` /
`VARIABLE` / `ALIAS` / `HISTORY` / `CUSTOM` / `UNKNOWN`. There is also
`LLE_COMPLETION_TYPE_COUNT` as the iteration sentinel.

**Score guidance** (used by sources in `completion_sources.c`):

| Source | Default score | Rationale |
|---|---|---|
| Aliases | 950 | User-defined; higher than commands the user did not customise |
| Builtins | 900 | Always available |
| Commands (PATH) | 800 | External, common case |
| SSH hosts | 800–950 (incl. priority) | Configured hosts rank higher than known-only |
| Files | 600–700 | Range varies; directories rank above files |
| Variables | 500 | Less common in argument position |
| History | 400 | Tie-breaker tier |

Custom sources are free to pick scores in any range.

**Suffix is usually NULL.** Sources do *not* compute suffixes for normal
files/dirs — the engine adds the trailing space or `/` based on
`item->type` via the splicer (see [§6.2 Suffix rules](#62-suffix-rules)).
Sources that genuinely need a custom suffix (e.g. config-driven sources
where the user specified `suffix = "="` in TOML) may pass one.

### 3.3 `lle_completion_result_t` — container

`completion_types.h:85`. Holds an array of items plus per-category counts.
The category counts are bookkeeping for the menu's category-header
rendering; the engine does not dispatch on them.

```c
typedef struct lle_completion_result {
    lle_completion_item_t *items;
    size_t count;
    size_t capacity;          /* auto-grows on add */

    size_t builtin_count;
    size_t command_count;
    size_t file_count;
    size_t directory_count;
    size_t variable_count;
    size_t alias_count;
    size_t history_count;
    size_t custom_count;

    lle_memory_pool_t *memory_pool;
} lle_completion_result_t;
```

Sources do not touch the count fields directly — `lle_completion_result_add`
maintains them.

### 3.4 `lle_splicer_splice_t` — what the splicer computes

`splicer.h:98`.

```c
typedef struct lle_splicer_splice {
    size_t delete_start;     /* First byte to delete from buffer */
    size_t delete_length;    /* Bytes to delete */
    char  *insert_text;      /* Pool-allocated bytes to insert */
    size_t insert_length;
    size_t cursor_after;     /* Byte offset of cursor after splice */
} lle_splicer_splice_t;
```

The splice is consumed via `lle_buffer_replace_text` and a cursor move.
Both `apply_accept` and `apply_preview` perform the consumption; callers
that want to introspect first use `lle_splicer_compute` directly.

### 3.5 `lle_completion_state_t` — completion session

`completion_state.h:30`. Holds the buffer snapshot, cursor at session
start, the analyzed context (owned), the result (owned), the cycling
index, the original word, timing data, and `active`/`menu_mode` flags.
`lle_completion_state_free()` releases the context and result.

### 3.6 `lle_completion_menu_state_t` — menu UI state

`completion_menu_state.h:63`. Holds a pointer to the result (not owned),
`selected_index`, `first_visible`, `visible_count`, `target_column` (for
sticky UP/DOWN), terminal/column layout fields, the category position
table, the active flag, and a config struct.

The result is *not* owned by the menu state — it is owned by the
completion state. `lle_completion_menu_state_free()` does *not* free the
result (`completion_menu_state.h:120`). When destroying the completion
system, the menu must be freed *before* the completion state for this
reason (`completion_system.c:65-72`).

---

## 4. The word-context analyzer

`src/lle/completion/word_context.c` (1623 lines). The single largest file
in the subsystem. Implements `lle_word_context_analyze(buffer,
cursor_byte_offset, pool, &out_context)` declared at
`word_context.h:263`.

### 4.1 Walk strategy

A codepoint-by-codepoint scan via `lle_utf8_decode_codepoint`, never
byte-by-byte. The walker tracks:

**Quote state** (one of, exclusive): `in_single`, `in_double`,
`in_backtick`, `escape_pending`, or unquoted (none of those).

**Nesting depth** (only meaningful unquoted): `paren_depth` (for
`$(...)` and `$((...))` boundaries), `brace_depth` (for `${...}` and
`{a,b,c}`), `bracket_depth` (for glob `[...]`).

**Statement position**: byte offset of the most recent unambiguous
statement boundary (`;`, `&`, `&&`, `||`, `|`, newline, or `(` outside
quotes/nesting); plus an `at_command_position` flag set true when the
walker is between that boundary and the first non-whitespace byte after
it.

**Word position**: byte offset where the current shell-word starts (last
non-quoted whitespace boundary, or `SIZE_MAX` if the walker is on
whitespace); plus the byte range of the command word (first
non-whitespace word after a statement boundary), and the current argument
index.

**Redirect tracking**: a flag set by `>`, `<`, `>>`, etc. that promotes
the next non-whitespace word to `LLE_CONTEXT_REDIRECT_TARGET`.

**Keyword sequence state** for `for X in <list>` and `case X in
<pattern>)`:
`KW_NONE` → on `for` → `KW_AFTER_FOR` → next word → `KW_AFTER_FOR_VAR` →
on `in` → `KW_AFTER_FOR_IN`; statement terminators reset to `KW_NONE`.
The `case` flow is symmetric.

**Heredoc tracking**: a flag set by `<<` or `<<-`, the captured delimiter
string, and a "in body" state machine that treats body bytes as literal
until a line that exactly matches the delimiter. Inside the body the
analyzer reports `LLE_CONTEXT_HEREDOC_BODY` and (today) refuses to
complete — heredoc-body completion is a future enhancement.

**Argument capture**: byte ranges of completed arguments before the
cursor's word, up to `WALKER_MAX_CAPTURED_ARGS` entries. These become the
`arguments[]` field on the output context, NFC-normalized and dequoted,
for builtin-arg sources to walk subcommand hierarchies.

### 4.2 Expansion resolution

For path-shaped words, the analyzer resolves single-value expansions by
calling lush's expansion machinery (`src/expand.c`). This is what
populates `expanded_directory`. Tildes, parameter, arithmetic — all
resolve. For multi-value brace expansion, the analyzer populates
`branches[]` instead and leaves `expanded_directory` as `NULL`; the source
manager fans out across branches.

Command substitution `$(...)` and backticks are resolved only when
`completion.eval_command_subst` is true (the bash/zsh-consensus default,
documented in `word_context.h:246-250`). When false, the analyzer treats
them opaquely and may set `expansion_kind` to `COMMAND_SUBST`. This
matches the lush polyglot principle: the bash/zsh consensus is to
evaluate, so lush does too by default; users who want safer behavior
opt in via config.

### 4.3 Known gaps

The header header documents several context types as not yet populated:
`FOR_IN_LIST`, `CASE_PATTERN`, `HEREDOC_BODY` are detected but currently
fall through to source dispatch as if they were `ARGUMENT` or `UNKNOWN`.
Sources can refuse based on these types if they choose.

When the typed word contains in-progress expansion bytes (an unclosed
`${`, `$((`, etc.), `expanded_directory` is left `NULL` and sources that
need a directory fall back to `.` (cwd).

Recovery after a syntax-broken cursor (mismatched quotes earlier in the
buffer) is best-effort: the analyzer makes a single forward pass, and
malformed input produces a reasonable but possibly imprecise context.

---

## 5. Sources and the source/engine boundary

### 5.1 The contract

Every source has the signature

```c
lle_result_t source_fn(lle_memory_pool_t *pool,
                       const lle_word_context_t *context,
                       lle_completion_result_t *result);
```

declared at `source_manager.h:69`. The applicability gate has the
signature

```c
bool applicable_fn(const lle_word_context_t *context);
```

declared at `source_manager.h:84`.

**What sources receive:**

- `pool` — for transient allocations. Items added to `result` via
  `lle_completion_result_add` are duplicated into the result's pool, so
  the source's allocations are short-lived.
- `context->dequoted_filename_prefix` — the prefix to match against, NFC-
  normalized, no quote machinery, no escape backslashes.
- `context->expanded_directory` — for path sources, the absolute
  directory to scan. Already resolved (tilde expanded, variables
  expanded, parameter expanded). May be `NULL` for sources that don't
  need a directory or in unresolved-expansion contexts; sources can
  default to `"."`.
- `context->command_name`, `arg_index`, `arguments[]` — for builtin-arg
  sources to walk subcommand hierarchies.
- `context->context_type`, `quote_state`, `expansion_kind` — for the
  applicability gate to decide whether to fire. Sources read these but
  do *not* try to escape based on `quote_state` — that's the splicer's
  job.

**What sources emit:**

- `lle_completion_result_add(result, text, suffix, type, score)` — most
  common; suffix is usually `NULL`.
- `lle_completion_result_add_with_description(...)` — when the source
  has secondary metadata (e.g. external commands include the full
  PATH-resolved path as the description, used by the splicer to
  disambiguate command-vs-builtin shadowing).
- For custom sources written via `custom_source.h`, the helper
  `lle_completion_add_typed_item(result, text, suffix, description,
  type, score)` is the canonical entry point.

**Contract invariants (do not break these):**

1. **Literals only.** Emit the candidate text as it would appear in the
   filesystem / data, not as it would appear in shell source. No path
   prefix, no quotes, no escape backslashes.
2. **No quote-state inspection for emission.** The splicer reads
   `quote_state` and renders escapes per the rules in [§6](#6-the-splicer).
3. **No suffix unless special.** Use `NULL`. The engine appends `/` for
   directories and ` ` (plus close-quote if applicable) for everything
   else. Custom sources may pass a suffix when the contract is
   genuinely different (e.g. `=` for variable assignments).
4. **Scores in 0–1000.** Higher ranks first. Stay in the conventional
   bands (see the table in [§3.2](#32-lle_completion_item_t--what-sources-emit))
   so multi-source ordering is predictable.
5. **NFC for prefix comparison.** Use `lle_unicode_is_prefix` from
   `unicode_compare.h`. The prefix on the context is already NFC.

### 5.2 Default sources

Eight sources are registered at `lle_source_manager_create`
(`source_manager.c:159-209`):

| Type enum | Name | Generator | Applicability gate |
|---|---|---|---|
| `LLE_SOURCE_BUILTINS` | `"builtins"` | `lle_completion_source_builtins` | `context_type == COMMAND_POSITION` |
| `LLE_SOURCE_ALIASES` | `"aliases"` | `lle_completion_source_aliases` | `context_type == COMMAND_POSITION` |
| `LLE_SOURCE_EXTERNAL_COMMANDS` | `"external_commands"` | `lle_completion_source_commands` | `context_type == COMMAND_POSITION` |
| `LLE_SOURCE_FILES` | `"files"` | `lle_completion_source_files` *or* `_directories` (for `cd`/`rmdir`) | path-shaped argument; redirect target; argument to a builtin whose `default_arg_type` is `FILE` or `DIRECTORY`; `FOR_IN_LIST`; or path-shaped command-position word |
| `LLE_SOURCE_VARIABLES` | `"variables"` | `lle_completion_source_variables` | `context_type == VARIABLE_NAME` |
| `LLE_SOURCE_HISTORY` | `"history"` | `lle_completion_source_history` | always |
| `LLE_SOURCE_CUSTOM` | `"builtin_args"` | `lle_builtin_completions_generate` | `context_type == ARGUMENT` and `command_name` is a builtin with a registered spec |
| `LLE_SOURCE_SSH_HOSTS` | `"ssh_hosts"` | `ssh_hosts_source_generate` | `context_type == ARGUMENT` and `command_name` ∈ `{ssh, scp, sftp, ssh-copy-id, mosh, slogin, rsync}` |

The total source slot count is `MAX_COMPLETION_SOURCES = 16`
(`source_manager.h:23`). Eight slots are taken by defaults above,
leaving eight for custom sources registered via the API.

`lle_source_manager_free()` is a no-op (`source_manager.c:211-214`) —
all source data is pool-allocated.

### 5.3 The file source's special routing

`file_source_applicable` (`source_manager.c:65-98`) is the most complex
gate. It fires for:

- `REDIRECT_TARGET` (always — files are valid targets).
- `ARGUMENT` or `FOR_IN_LIST` where `command_name` either has no spec
  registered with `lle_builtin_get_spec` or has a spec whose
  `default_arg_type` is `FILE` / `DIRECTORY`. If the spec exists *and*
  has subcommands, files are *not* offered (the builtin_args source
  handles subcommand argument types).
- `COMMAND_POSITION` when the typed prefix is path-shaped (the test is
  `prefix_indicates_path` at `source_manager.c:47-63`: leading `/`, `~`,
  `$VAR/`, `./`, or `../`). This is what makes `./local-script` and
  `/usr/bin/specific` complete sensibly at command position.

The `cd` and `rmdir` builtins additionally route through
`lle_completion_source_directories` instead of `_files` so only
directories are offered (`source_manager.c:182-198`).

### 5.4 SSH host source

`lle_completion_source_ssh_hosts` (`completion_sources.c:327`). Reads from
the global `ssh_host_cache_t` (`ssh_hosts.c`), which lazily parses
`~/.ssh/config`, `/etc/ssh/ssh_config`, and `~/.ssh/known_hosts` on first
access (`ssh_hosts.c:356-385`). Cache TTL is 5 minutes
(`SSH_CONFIG_CACHE_TIMEOUT = 300`, `ssh_hosts.c:35`). Cache capacity is
`MAX_SSH_HOSTS = 1000` (`ssh_hosts.c:36`).

The source recognises three prefix forms in
`context->dequoted_filename_prefix`:

| Form | Behavior |
|---|---|
| **bare**: `git` | Match against `host->hostname` and `host->alias`. If a `User` directive is configured for the matching Host stanza, emit `<config-user>@<host>`; otherwise emit just `<host>`. |
| **`user@prefix`**: `alice@git` | Split on `@`; match the post-`@` segment against `host->hostname`. Preserve the user-typed `alice@` literal in the emitted candidate (do *not* override with the Host-stanza `User`, even if one is configured). |
| **`host:`**: `host:`, `host:p/q` | Remote-path syntax for `scp`/`sftp`/`rsync`. Return zero candidates — defer to a future remote-path source. |

The same logic applies for both `host->hostname` and the `host->alias`
field (an alias being the `Host foo` name when `HostName` is different).

**Tracked gap (#92)**: `/etc/hosts` and `/etc/ssh/ssh_known_hosts` are
documented in `docs/COMPLETION_SYSTEM.md` as sources but
`ssh_hosts_refresh` does not currently parse them. See the issue for
scope.

### 5.5 Custom source registration (programmatic API)

`include/lle/completion/custom_source.h` exposes the API for code-defined
custom sources. The struct (header line 79):

```c
typedef struct lle_custom_completion_source {
    const char *name;
    const char *description;
    int priority;             /* 0-1000, higher queried earlier */
    lle_result_t (*generate)(void *user_data,
                             const lle_word_context_t *context,
                             lle_completion_result_t *result);
    bool (*is_applicable)(void *user_data,
                          const lle_word_context_t *context);
    void (*cleanup)(void *user_data);
    void *user_data;
} lle_custom_completion_source_t;
```

Lifecycle:

```c
lle_completion_register_source(&my_source);  /* copies struct + dups strings */
/* ... runtime ... */
lle_completion_unregister_source("my-source");  /* calls cleanup() */
```

The registry holds up to `MAX_CUSTOM_SOURCES = 32` entries
(`custom_source.c:29`); active registrations are bounded by the source
manager's 16 slots minus the eight defaults.

A worked example of writing one is in
[§15.1 Programmatic API](#151-programmatic-api).

### 5.6 Custom sources via TOML config

`~/.config/lush/completions.toml` (or `$XDG_CONFIG_HOME/lush/completions.toml`)
defines completion sources declaratively. A source executes a shell
command and uses its stdout lines as candidates. Schema:

```toml
[sources.NAME]
description = "Human-readable description"
applies_to = ["command", "command subcommand"]  # When this source fires
argument = N                                     # Argument position; 0 = any
command = "shell command"                        # stdout lines = candidates
suffix = " "                                     # Appended after each candidate
cache_seconds = 5                                # Cache TTL; 0 = no cache
```

The reference example file is `examples/completions.toml` (covers git,
docker, ssh, npm, brew, kubectl, systemctl, make, meson — useful as a
real-world template). The TOML parser is
`src/lle/completion/completion_config.c` (922 lines; calls into
`custom_source.c`'s registration API for each loaded source).

**Hardcoded limits** in `completion_config.c:42-46`:

| Constant | Value | Meaning |
|---|---|---|
| `MAX_CONFIG_SOURCES` | 64 | Max sources from a single TOML file |
| `MAX_APPLIES_TO` | 16 | Max patterns per source |
| `MAX_COMMAND_OUTPUT` | 4096 | Max bytes captured from command stdout |
| `COMMAND_TIMEOUT_SECONDS` | 2 | Per-invocation timeout |
| `DEFAULT_CACHE_SECONDS` | 0 | Default TTL when `cache_seconds` is omitted |

Reload via `display lle completion sources reload` (the shell builtin).
List active sources via `display lle completion sources list`.

A worked example of adding a TOML source is in
[§15.2 TOML config](#152-toml-config).

---

## 6. The splicer

`src/lle/completion/splicer.c` (361 lines). Lives in
`include/lle/completion/splicer.h` (222 lines). Pure layer.

The splicer answers two questions: *what bytes do we insert?* and *where
in the buffer do we put them?*

### 6.1 Render rules per quote state

Implemented in `splicer.c`'s `render_none` / `render_double` /
`render_backtick` / `render_single` helpers, dispatched by
`lle_splicer_render_for_context` (`splicer.h:128`).

| `quote_state` | Bytes that get backslash-escaped |
|---|---|
| `NONE` | Whitespace (space/tab/newline); statement terminators `;|&`; redirect chars `<>`; group chars `(){}`; expansion starters `$\``; the escape itself `\`; both quote chars `"'`; glob chars `*?[`; *position-sensitive*: `~` and `#` only at position 0 of the rendered output; `!` everywhere |
| `DOUBLE` | Only the four POSIX double-quote escape characters: `$ \` `\` `\\` `"` |
| `BACKTICK` | POSIX rule: `\ \` `$` |
| `SINGLE` | Backslash is impossible inside `'…'` per POSIX. A literal single-quote inside is rendered by closing the open quote, emitting `\'`, and re-opening: byte `'` becomes `'\''`. All other bytes pass through |
| `ESCAPE_PENDING` | Same as `NONE` for now (a future revision may prepend a placeholder to neutralise the pending escape) |

### 6.2 Suffix rules

Applied in `lle_splicer_compute` (`splicer.c`) when `accept_phase ==
true`. On `false` (preview phase, multi-match cycling) no suffix is
appended at all.

| Item type | Suffix |
|---|---|
| `LLE_COMPLETION_TYPE_DIRECTORY` | `"/"` only. Do *not* close any open quote. Do *not* append a trailing space. The cursor stays inside the open quoted span if any, so the next TAB can continue into the directory |
| All others (`FILE`, `COMMAND`, `BUILTIN`, `ALIAS`, `VARIABLE`, `HISTORY`, `CUSTOM`) | If `quote_state` is `SINGLE` / `DOUBLE` / `BACKTICK`, append the matching close character (via `lle_splicer_close_char`); then append a trailing space |

`lle_splicer_close_char` (`splicer.h:142`) returns `'\''`, `'"'`, or
`` '`' `` for `SINGLE` / `DOUBLE` / `BACKTICK` respectively, and `'\0'`
for `NONE` and `ESCAPE_PENDING`.

### 6.3 Splice computation

`lle_splicer_compute(context, item, accept_phase, pool, &out)`
(`splicer.h:174`).

```
delete_start  = context->filename_portion_start
delete_length = context->word_end - context->filename_portion_start
insert_text   = render_for_context(item->text, context->quote_state)
                + (accept_phase ? suffix_for_type(item->type, quote_state) : "")
insert_length = strlen(insert_text)
cursor_after  = delete_start + insert_length
```

The deletion range covers the user's typed filename-prefix portion only
— the path operators and any open quote/escape that the analyzer placed
*before* `filename_portion_start` are preserved in the buffer.

### 6.4 Worked examples

**Example 1 — directory completion at unquoted path.**

Buffer: `cd /h` (cursor after `h`). The user has `~/home` on the
filesystem.

Analyzer produces:
- `quote_state = NONE`
- `word_start = 3` (the `/`)
- `filename_portion_start = 4` (after the `/`)
- `word_end = 5`
- `dequoted_filename_prefix = "h"`
- `expanded_directory = "/"`

File source matches `h` against entries of `/`, finds `home/`. Item:
`{text: "home", type: DIRECTORY, score: 700}`.

Splicer:
- Render: `render_none("home")` → `"home"` (no shell-special bytes).
- Accept phase: `type == DIRECTORY` → append `"/"` → `"home/"`. No close-
  quote (none open), no trailing space.
- Delete `[4, 5)` (the `h`).
- Insert `"home/"` at byte 4.

Buffer becomes `cd /home/` (cursor at byte 9, ready for next TAB).

**Example 2 — single-quoted file with space.**

Buffer: `git 'f` (cursor right after the open quote and `f`). Cwd
contains `file with spaces.txt`.

Analyzer:
- `quote_state = SINGLE`
- `word_start = 4` (the `'`)
- `filename_portion_start = 5` (after the quote)
- `word_end = 6`
- `dequoted_filename_prefix = "f"`
- `expanded_directory = "."`

File source emits `{text: "file with spaces.txt", type: FILE,
score: 600}`.

Splicer:
- Render: `render_single("file with spaces.txt")` → `"file with
  spaces.txt"` (no `'` bytes to escape; spaces are literal in single
  quotes).
- Accept phase: `type != DIRECTORY` → close char is `'`, append `"'"`,
  then ` ` → `"file with spaces.txt' "`.
- Delete `[5, 6)` (the `f`).
- Insert.

Buffer becomes `git 'file with spaces.txt' ` (cursor after the trailing
space).

**Example 3 — preview phase during cycling.**

Same as Example 2 but the user pressed TAB on a multi-match: there are
also `foo.txt` and `friend.md` in cwd. Menu opens, first item previewed.
For preview, `accept_phase = false` so *no* close-quote or trailing
space is appended:

Buffer becomes `git 'file with spaces.txt` (cursor after the `t`,
*inside* the still-open single quote). Next TAB cycles to `foo.txt` and
the previous candidate's bytes are wiped via the same delete-range
calculation.

ENTER finalises with `accept_phase = true`, appending the close-quote
and space.

### 6.5 Apply layer

The pure compute layer is paired with two apply functions that perform
the buffer mutation:

- `lle_splicer_apply_accept(buffer, cursor_mgr, context, item, pool)` —
  computes accept-phase splice, calls `lle_buffer_replace_text` to
  delete + insert, calls `lle_cursor_manager_move_to_byte_offset`.
- `lle_splicer_apply_preview(buffer, cursor_mgr, context, item, pool)`
  — same but with `accept_phase = false`.

Both are at `splicer.h:197-215`.

---

## 7. Built-in command completions

`src/lle/completion/builtin_completions.c` (909 lines). Header
`builtin_completions.h` (165 lines).

### 7.1 Spec table model

Each builtin has an `lle_builtin_completion_spec_t` (header line 91) with:

- `name` — the builtin name (e.g. `"cd"`).
- `options[]` — top-level options as `(name, description)` pairs.
- `subcommands[]` — recursive `lle_builtin_subcommand_t` array for
  hierarchical builtins (e.g. `display lle theme list`).
- `default_arg_type` — when no subcommand matches, what kind of dynamic
  argument to complete (`FILE`, `DIRECTORY`, `VARIABLE`, `ALIAS`,
  `COMMAND`, `SIGNAL`, `JOB`, `THEME`, `FEATURE`, or `NONE`).

Subcommands recurse: a subcommand can have its own `options[]`,
`subcommands[]`, and `arg_type`. This is how
`display lle theme set <theme-name>` is expressed.

Static specs are registered in `builtin_completions.c`'s spec table and
looked up by name via `lle_builtin_get_spec(name)` (header line 116).
There is no dynamic registration — adding a new builtin spec means
editing `builtin_completions.c`.

### 7.2 Dispatch

`lle_builtin_completions_applicable(context)` returns true when
`context_type == ARGUMENT` and `lle_builtin_get_spec(context->command_name)`
returns non-NULL (header line 128).

`lle_builtin_completions_generate(pool, context, result)` (header line 142):

1. Walks `context->arguments[]` to find the position in the subcommand
   tree (e.g. `["lle", "theme"]` → navigate from `display` spec into the
   `lle` subcommand into the `theme` subcommand).
2. At the resolved level: if the user is mid-flag (prefix starts with
   `-`), emit options. Otherwise, if there are subcommands at this
   level, emit subcommand names. Otherwise, dispatch by `arg_type` to
   the appropriate dynamic source (files for `FILE`, aliases for
   `ALIAS`, etc.).

### 7.3 Signal names

`lle_builtin_get_signal_names()` (header line 151) returns a
NULL-terminated `const char**` of POSIX signal names for `trap`/`kill`
completion.

---

## 8. The menu system

Three files, separated by concern:

| File | Concern | Lines |
|---|---|---|
| `completion_menu_state.{c,h}` | State, layout, queries — no rendering | 414 / 234 |
| `completion_menu_logic.{c,h}` | Navigation (move_next/down/up/right/left, page up/down, category jump) | 778 / 201 |
| `completion_menu_renderer.{c,h}` | Multi-column ANSI-styled text formatting | 593 / 220 |

### 8.1 Engagement

The menu engages when `result->count >= min_items_for_menu` (default 2)
at the end of `lle_completion_system_generate`. The system creates the
menu via `lle_completion_menu_state_create(pool, result, NULL, &menu)`
and stores it on the system; `lle_complete()` hands it to the display
controller via `display_controller_set_completion_menu(dc, menu)`.

### 8.2 State

`lle_completion_menu_state_t` (`completion_menu_state.h:63`) holds:

- `result` — pointer to the completion result (not owned).
- `selected_index`, `first_visible`, `visible_count` — viewport state.
- `target_column` — sticky column for UP/DOWN navigation in
  multi-column layouts (preserves "I'm in column 2" through rows of
  varying width).
- `terminal_width`, `column_width`, `num_columns` — layout state,
  recomputed by `lle_completion_menu_update_layout(state,
  terminal_width)`.
- `category_positions[]`, `category_count` — start indices of each
  category in the sorted result (for category-jump navigation).
- `menu_active` — set false on cancel.
- `config` — copy of `lle_completion_menu_config_t`.

### 8.3 Navigation primitives

All of these are in `completion_menu_logic.c`:

| Function | Trigger | Effect |
|---|---|---|
| `lle_completion_menu_move_next` | TAB | Sequential cycle: row-major next item, wraps to first |
| `lle_completion_menu_move_prev` | Shift+TAB | Sequential cycle backward |
| `lle_completion_menu_move_down` | Down arrow | Down within column, respecting `target_column` |
| `lle_completion_menu_move_up` | Up arrow | Up within column |
| `lle_completion_menu_move_right` / `_left` | Right/Left arrows | Column-by-column |
| `lle_completion_menu_page_down` / `_up` | Page Down / Up | By `visible_count` items |
| `lle_completion_menu_next_category` / `_prev_category` | (binding-dependent) | Jump to the first item of next/prev category |
| `lle_completion_menu_select_first` / `_last` | Home / End | First / last item in result |

Cancel: `lle_completion_menu_cancel(state)` sets `menu_active = false`.

Accept: `lle_completion_menu_accept(state) -> selected_item` returns a
pointer to the chosen item; the *splicer* performs the buffer mutation,
not the menu.

### 8.4 Renderer

`lle_completion_menu_render(state, options) -> output_buffer` produces a
formatted text block (no I/O — the display layer writes it). ANSI
codes used:

- `\033[7m` … `\033[0m` — reverse video for the selected item.
- `\033[1;36m` … `\033[0m` — bold cyan for category headers ("BUILTINS",
  "COMMANDS", etc.).

Multi-column layout is computed via
`lle_menu_renderer_calculate_column_width` and
`lle_menu_renderer_calculate_columns` (header lines in
`completion_menu_renderer.h`). Sizes come from
`lle_menu_renderer_default_options(terminal_width)`.

---

## 9. Configuration

The completion subsystem currently exposes configuration in two places:
the menu config (programmatic) and the TOML config (for sources).
There is no central-config integration for arbitrary completion knobs
yet.

### 9.1 Menu config

`lle_completion_menu_config_t` (`completion_menu_state.h:47`):

```c
typedef struct {
    size_t max_visible_items;   /* default 20 */
    bool show_category_headers; /* default true */
    bool show_type_indicators;  /* default false */
    bool show_descriptions;     /* default false */
    bool enable_scrolling;      /* default true */
    size_t min_items_for_menu;  /* default 2 */
} lle_completion_menu_config_t;
```

Defaults from `lle_completion_menu_default_config()`. The system uses
defaults at session start; programmatic callers can pass a custom config
to `lle_completion_menu_state_create`.

### 9.2 TOML config (custom sources)

See [§5.6](#56-custom-sources-via-toml-config) for the schema. File
location is resolved by `completion_config.c:get_config_path()`:
`$XDG_CONFIG_HOME/lush/completions.toml` if `XDG_CONFIG_HOME` is set,
else `~/.config/lush/completions.toml`. Loaded by `lle_editor_create()`
at startup (`src/lle/lle_editor.c:150`). Reload at runtime via the
shell builtin `display lle completion sources reload` (which calls
`lle_completion_reload_config()`).

### 9.3 What is *not* yet configurable

- No `completion.fuzzy_matching` (the flag exists at
  `completion_system.h:50` but the feature is not implemented).
- No `completion.max_completions` enforcement (`completion_system.h:51`
  exists but the limit is never applied).
- No `completion.eval_command_subst` config wiring — the analyzer
  references it, but there is no central-config registry entry. Today
  it behaves as if always-true.
- Menu defaults (`max_visible_items = 20` etc.) are hardcoded.

These are all candidate central-config additions. See
[§14 Known gaps](#14-known-gaps-and-open-work).

---

## 10. Caching

Three caches, with very different shapes:

### 10.1 SSH host cache

`src/lle/completion/ssh_hosts.c`. Global, singleton, lazily initialised
on first call to `get_ssh_host_cache()` (line 391). Refreshes itself on
TTL expiry (`SSH_CONFIG_CACHE_TIMEOUT = 300` seconds, line 35) or when
the `needs_refresh` flag is set. Capacity `MAX_SSH_HOSTS = 1000` (line
36).

Refresh walks: `~/.ssh/config`, `/etc/ssh/ssh_config`,
`~/.ssh/known_hosts`. (Doesn't walk `/etc/hosts` or
`/etc/ssh/ssh_known_hosts` despite docs claiming it does — tracked as
issue #92.)

### 10.2 Per-source TOML cache

`completion_config.c`. Each TOML source can specify `cache_seconds = N`;
results of the source's shell command are cached for that duration in
`lle_command_source_config_t::cached_results` (`custom_source.h:285`).
Default TTL is `DEFAULT_CACHE_SECONDS = 0` (no caching) per source unless
explicitly set.

`lle_command_source_clear_cache(config)` clears one source's cache;
`lle_completion_clear_all_caches()` clears all of them.

### 10.3 What is not cached

- File / directory listings (every TAB re-runs `opendir`/`readdir`).
- PATH search results (every TAB re-walks `$PATH`).
- Word-context analysis (re-walks the buffer for every TAB; analysis is
  O(buffer length) so this is fast in practice).
- Completion results between TAB presses (each press is a fresh
  generation — though the menu state and completion state persist
  during cycling).

If any of these become hot, adding them is a matter of mirroring the
SSH cache pattern (global TTL'd cache with `clear` API).

---

## 11. Integration points

Every callsite from outside `src/lle/completion/`:

### 11.1 LLE editor lifecycle

`src/lle/lle_editor.c:131-150` — completion subsystem creation:

```c
result = lle_completion_system_create(ed->lle_pool, &ed->completion_system);
/* error handling on failure */

result = lle_custom_source_init(ed->completion_system->source_manager,
                                ed->lle_pool);
/* non-fatal: custom sources unavailable but editor continues */

lle_completion_load_config();  /* non-fatal if file missing */
```

Destruction is symmetric, in `lle_editor_destroy`.

### 11.2 TAB binding

`src/lle/keybinding/keybinding.c:1026` and
`src/lle/keybinding/keybinding_actions.c:3132`:

```c
lle_keybinding_manager_bind(manager, "TAB", lle_complete, "complete");
```

`lle_complete` itself is in `keybinding_actions.c:2407-2543` and is the
sole entry point (already detailed in [§2.2](#22-end-to-end-tab-press-trace)).

### 11.3 ESC binding

`keybinding_actions.c:lle_abort_line` calls
`clear_completion_menu(editor)` which calls
`lle_completion_system_clear()` to discard the active session.

### 11.4 ENTER binding

`keybinding_actions.c:lle_accept_line` interacts with the active
completion: if a menu is visible, the selected item is finalised via
`lle_splicer_apply_accept` (with the suffix); otherwise the buffer is
submitted directly.

### 11.5 Display controller

`src/lle/display/display_controller.c` /
`src/lle/display/display_integration.c`. Receives the menu via
`display_controller_set_completion_menu(dc, menu)`. Renders it via
`lle_completion_menu_render(menu, options)`. Routes navigation key
input to the menu logic functions. Reads `menu_state_changed` to decide
whether to re-render.

### 11.6 Shell builtins for inspection / management

`display lle completion sources list` — list registered sources (custom
and default).
`display lle completion sources reload` — `lle_completion_reload_config()`.
Both wired through `src/builtins/builtins.c`.

---

## 12. Spec divergences

The spec at `docs/lle_specification/12_completion_system_complete.md`
describes an ambitious end-state. The current implementation is a
deliberate rewrite that put correct architectural foundations in place
first; several spec components have not been built *yet* on top of
those foundations, and others have been replaced by simpler equivalents
that fit the new architecture better. Nothing in the spec is rejected
in principle — items marked "not yet implemented" are tracked future
work, not dropped scope. Items marked "replaced by …" are deliberate
substitutions where the rewrite found a cleaner shape.

### 12.1 Components from the spec — current status

| Spec component | Current status |
|---|---|
| `lle_completion_engine_t` (top-level engine struct) | **Replaced** by simpler `lle_completion_system_t` — the rewrite split the engine's responsibilities across analyzer / source manager / state / menu, which made the orchestrator small enough that a separate "engine" type wasn't earning its keep |
| `lle_context_analyzer_t` | **Subsumed** into the unified `lle_word_context_analyze` walker. The previous design had two parallel analyzers that disagreed on word boundaries; consolidating into one was the central point of the rewrite |
| `lle_fuzzy_matcher_t` | **Not yet implemented.** `enable_fuzzy_matching` flag exists at `completion_system.h:50` as a placeholder; design and wiring are future work on top of the deduplication / sort layer |
| `lle_completion_cache_t` (general result cache) | **Not yet implemented.** Per-TOML-source caching (§10.2) and SSH cache (§10.1) are the only caches today. A general result cache layer keyed on context fingerprint is candidate future work — see [§14.3](#143-architectural-gaps) |
| `lle_display_integration_t` | **Replaced** by direct menu-state consumption in the display controller. May be re-introduced if/when display-side concerns grow beyond what the controller can handle inline |
| `lle_interactive_menu_t` | **Implemented** as `lle_completion_menu_state_t` + logic + renderer (three files; see [§8](#8-the-menu-system)) |
| `lle_completion_classifier_t` | **Subsumed** into `lle_completion_classify_text` and the type system in `completion_types.c` |
| `lle_plugin_registry_t` | **Partially implemented**, partially future. The programmatic custom-source API in `custom_source.h` and the TOML config (§5.6) cover the user-extension case; a formal plugin loader (matching what spec 18 describes) is separate future work |
| `lle_performance_monitor_t` | **Not yet implemented.** A `generation_time_us` field on the completion state captures per-session timing today; a telemetry pipeline is future work |
| `lle_security_context_t` | **Not yet implemented.** Audit / access-control surfaces are future work; the current implementation has no per-completion permission checks |
| `lle_error_recovery_t` (multi-strategy recovery) | **Replaced** by simple `lle_result_t` returns at the boundary, with recovery at the call site. The rewrite found that the simpler model handled every real failure mode without ceremony; if specific recovery strategies turn out to need a typed home, they can be added |

### 12.2 Behavioral divergences

- **Context-type classification**: spec describes context-aware
  classification with sub-states for "inside conditional", "inside
  function body", etc. Implementation has the eight context types
  listed in [§3.1](#31-lle_word_context_t--analyzer-output) and
  treats function bodies the same as top-level (which is correct —
  function dispatch matches top-level).
- **Brace expansion**: implementation handles multi-value brace
  expansion via the `branches[]` mechanism with fan-out in the source
  manager (`source_manager.c:259-269`). Spec discusses but does not
  detail this.
- **Suffix handling**: spec's source contract doesn't pin down where
  suffix logic lives. Implementation places it strictly in the splicer,
  not the sources — the source/engine boundary in [§5.1](#51-the-contract).

### 12.3 Implementation-only features

- The **inverted-alias** mechanism (used by `xpg_echo`/`bsd_echo`,
  documented in `feedback-lush-is-its-own-shell.md`) — not in the
  completion subsystem itself, but worth noting for context-cross.
- **TOML-based custom sources** are not in the spec; they are a
  pragmatic substitute for the spec's plugin registry.
- The strict literal-only source contract is not in the spec; it
  emerged from the second-attempt rewrite to fix the
  prefix-preservation bugs in the original implementation.

---

## 13. Tests

Three unit-test binaries plus one compliance test, totalling 178+ tests:

| File | Tests | Covers |
|---|---|---|
| `tests/lle/unit/test_word_context.c` | 126 | `lle_word_context_analyze` across quote/escape state, word boundaries respecting quote state, `filename_portion_start` computation, NFC-normalized dequoting, in-progress expansion detection, context-type classification, command-name + arg-index extraction |
| `tests/lle/unit/test_splicer.c` | 38 | `lle_splicer_render_for_context` across all `quote_state` rendering rules, `lle_splicer_close_char` lookup, `lle_splicer_compute` across (item type × `quote_state` × accept/preview phase) combinations |
| `tests/lle/unit/test_completion_types.c` | 14 | `lle_completion_item_t` lifecycle, `lle_completion_result_t` lifecycle, type metadata queries, classification helpers |
| `tests/lle/unit/test_ssh_completion.c` | 3 | SSH host source: configured-host emission, `user@` prefix preservation, remote-path bypass |
| `tests/lle/compliance/spec_12_completion_compliance.c` | (varies) | Type-level compliance with spec-12 enums and APIs |

Areas with **no direct tests** (gaps for future work):

- `source_manager.c` itself (registration, query dispatch). Indirectly
  exercised by the integration path but not unit-tested.
- `completion_menu_logic.c` (navigation primitives).
- `completion_menu_renderer.c` (text formatting).
- `custom_source.c` (programmatic API; lifecycle).
- `completion_config.c` (TOML parsing).
- `builtin_completions.c` (subcommand-tree dispatch).

---

## 14. Known gaps and open work

The rewrite intentionally landed the architecture and the foundational
primitives first; the following items are future work building on that
base, not abandoned features. Each is a candidate for a focused PR
rather than a blocker on the current architecture.

### 14.1 Future work tracked in code

- **Fuzzy matching**: `enable_fuzzy_matching` placeholder at
  `completion_system.h:50`. Future design will likely sit between the
  source-manager query and the deduplication step, scoring candidates
  against the prefix with an edit-distance metric and adjusting
  `relevance_score`.
- **`max_completions` enforcement**: Limit field at
  `completion_system.h:51`. The unenforced-today state is acceptable
  because results are bounded in practice by source applicability;
  enforcement becomes important once fuzzy matching widens the
  candidate pool.
- **Menu char-input filtering**: `lle_completion_menu_handle_char` is
  stubbed; the planned behaviour is "type while menu is open to filter
  the visible items," similar to fish's UI.
- **HEREDOC_BODY / FOR_IN_LIST / CASE_PATTERN specialisation**:
  analyzer detects these context types; sources don't yet specialise.
  Source-side work item.
- **`expanded_directory` resolution under in-progress expansion**:
  when typed-but-unclosed `${`, `$((`, etc. are present, sources fall
  back to cwd. Refining this is analyzer work.
- **`~user`, `~+`, `~-`**: tracked as issue #91 (filed during the path
  highlighter work — same code path applies to completion).
- **Plugin loader**: spec 18 describes a formal plugin system; the
  programmatic custom-source API and TOML config (§5.6) cover the
  user-extension case today, and a plugin loader on top is candidate
  future work.
- **Performance monitor / telemetry pipeline**: `generation_time_us`
  captures per-session timing today; aggregating across sessions and
  exposing metrics is future work.
- **Security context**: per-completion permission / audit surfaces
  are future work.

### 14.2 Documentation gaps

- **`/etc/hosts` and `/etc/ssh/ssh_known_hosts` parsing**: docs
  promise these as SSH host sources; the implementation only reads
  `~/.ssh/config`, `/etc/ssh/ssh_config`, and `~/.ssh/known_hosts`.
  Tracked as issue #92.

### 14.3 Architectural gaps

- **No central-config knobs for completion**: `lush_config_registry`
  has no `completion.*` keys today. Adding them is a candidate for the
  next config pass.
- **No cache invalidation on `cd`**: the file source stats cwd on every
  TAB so this isn't a correctness gap, but adding a result cache would
  benefit from `cd`-driven invalidation.
- **No completion telemetry / performance monitoring** beyond ad-hoc
  `generation_time_us`.

---

## 15. How to add a new completion source

Three options, in increasing order of effort:

1. [TOML-defined source](#152-toml-config) — for "run this shell
   command, treat its stdout as candidates" sources. Zero code.
2. [Programmatic custom source](#151-programmatic-api) — for sources
   with custom logic (caching, state, computed candidates) that
   shouldn't shell out.
3. [Built-in source](#153-built-in-source) — for sources that should
   ship as part of lush itself (like the SSH source). Edits the
   `source_manager.c` registration list.

### 15.1 Programmatic API

Use this when your source needs custom logic, in-process state, or
performance characteristics that don't fit "exec a shell command and
parse stdout".

```c
#include "lle/completion/custom_source.h"
#include <string.h>

/* Generate completions. Read context->dequoted_filename_prefix to
 * decide what to match against. Append candidates via
 * lle_completion_add_typed_item. */
static lle_result_t my_generate(void *user_data,
                                const lle_word_context_t *context,
                                lle_completion_result_t *result) {
    (void)user_data;
    const char *prefix = context->dequoted_filename_prefix;
    if (!prefix) prefix = "";

    static const char *candidates[] = {"alpha", "beta", "gamma", NULL};
    for (int i = 0; candidates[i]; i++) {
        if (strncmp(candidates[i], prefix, strlen(prefix)) == 0) {
            lle_completion_add_typed_item(
                result, candidates[i], NULL, "Greek letter",
                LLE_COMPLETION_TYPE_CUSTOM, 700);
        }
    }
    return LLE_SUCCESS;
}

/* Decide when to fire. The example fires only when the user is
 * completing an argument to the `greek` command. */
static bool my_applicable(void *user_data,
                          const lle_word_context_t *context) {
    (void)user_data;
    return context->context_type == LLE_CONTEXT_ARGUMENT
        && context->command_name
        && strcmp(context->command_name, "greek") == 0;
}

/* Register at startup (e.g. from a plugin or a startup hook). */
void register_my_source(void) {
    lle_custom_completion_source_t src = {
        .name = "greek-letters",
        .description = "Greek letter names for the `greek` command",
        .priority = 700,
        .generate = my_generate,
        .is_applicable = my_applicable,
        .cleanup = NULL,
        .user_data = NULL,
    };
    lle_completion_register_source(&src);
}
```

Notes:

- The struct is *copied* by `lle_completion_register_source` and the
  string fields are duplicated, so the local definition can be on the
  stack.
- `priority` controls registration order; higher priorities are
  queried first, so they show first in the result if ties are
  unbroken by score.
- `cleanup` is called on `lle_completion_unregister_source(name)` and
  on shutdown; use it for `user_data` resource release if your source
  owns state.
- For in-process state, `user_data` carries a pointer the callbacks
  can dereference. Tests on `user_data` belong inside the callbacks
  rather than at registration time.

### 15.2 TOML config

Add a stanza to `~/.config/lush/completions.toml`:

```toml
[sources.git-branches]
description = "Git branch names"
applies_to = ["git checkout", "git branch -d", "git merge"]
argument = 0
command = "git branch --list 2>/dev/null | sed 's/^[* ]*//'"
suffix = " "
cache_seconds = 5
```

| Field | Meaning |
|---|---|
| `description` | Shown by `display lle completion sources list` |
| `applies_to` | List of `"command"` or `"command subcommand"` patterns. Source fires only when the typed command line matches one of these |
| `argument` | Argument position to fire on. `0` = any |
| `command` | Shell command to execute. Each line of stdout becomes a candidate |
| `suffix` | Appended after each candidate text. `" "` is the usual choice |
| `cache_seconds` | Cache TTL for the command's output. `0` disables caching, results are computed fresh every TAB |

The reference example file `examples/completions.toml` covers git,
docker, ssh, npm, brew, kubectl, systemctl, make, meson.

After editing, reload via `display lle completion sources reload` (no
restart needed).

### 15.3 Built-in source

Used when the source is part of the shell itself rather than a user
extension. Three steps:

**Step 1**: Add the generator function in
`src/lle/completion/completion_sources.c`. Follow the standard signature
`(pool, context, result)`. Read `context->dequoted_filename_prefix` and
`context->expanded_directory`. Append items via
`lle_completion_result_add(result, text, NULL, type, score)`.

**Step 2**: Declare it in `include/lle/completion/completion_sources.h`.

**Step 3**: Register in `src/lle/completion/source_manager.c`:

3a. Add an enum value to `LLE_SOURCE_*` in `source_manager.h` (unless an
existing type fits).

3b. Write an applicability gate next to the existing ones at the top of
`source_manager.c`:

```c
static bool my_source_applicable(const lle_word_context_t *context) {
    return context->context_type == LLE_CONTEXT_ARGUMENT
        && context->command_name
        && strcmp(context->command_name, "mycommand") == 0;
}
```

3c. Write a thin generation wrapper with the standard signature:

```c
static lle_result_t my_source_generate(lle_memory_pool_t *pool,
                                       const lle_word_context_t *context,
                                       lle_completion_result_t *result) {
    return lle_completion_source_my(pool, context, result);
}
```

3d. Register inside `lle_source_manager_create`:

```c
res = lle_source_manager_register(manager, LLE_SOURCE_MY, "my_source",
                                  my_source_generate,
                                  my_source_applicable);
if (res != LLE_SUCCESS) return res;
```

**Step 4**: Add tests. Mirror `tests/lle/unit/test_ssh_completion.c` for
behaviour-driven coverage with fixture files where appropriate; mirror
`tests/lle/unit/test_completion_types.c` for pure-API tests. Wire the
test target into `meson.build`.

That's the complete checklist. Existing built-in sources are useful
templates — the SSH source (the most recently added one) is the
shortest end-to-end example to study (commit `a4252437` for a clean
diff).
