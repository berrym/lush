/// @file LLE_PAGER_DESIGN.md
/// @brief Design doc -- LLE pagination layer
/// @status Resolved (all 8 open questions answered; implementation gated on user sign-off)
/// @date 2026-05-25

# LLE Pagination Layer -- Design Discussion

**Status**: 8 open questions resolved (see section "Outstanding decisions
before code" at the bottom). Implementation-ready pending user sign-off
on the resolved positions.

This document does NOT propose an implementation timeline or
roadmap-style sequencing. It describes the architectural placement
of a pager inside lush's existing display-controller + LLE world,
the public API that consumers would call, the internal state model,
and the explicit decisions that need user input before a single
function is written.

---

## 1. Existing architecture (what we must respect)

The LLE + display rules (SCREEN_BUFFER_SPECIFICATION,
LLE_DISPLAY_ARCHITECTURE_RESEARCH, README in src/display/) collapse
to the following non-negotiables:

1. **LLE is the single source of truth** for the interactive
   buffer contents and cursor position. Display queries LLE;
   display never modifies LLE state.
2. **`display_controller.c` is the sole stdout writer** for the
   interactive REPL surface. Nothing else writes bytes to the tty
   for the active prompt+command line.
3. **Prompt-once + clear-and-redraw** is the actual current
   implementation. Differential update language in older docs is
   historical; the spec referenced in SCREEN_BUFFER_SPECIFICATION
   *Reading note (2026-05-23)* is authoritative.
4. **Layer composition via events**. Layers don't call each other.
   The composition_engine combines L3 layer renders into one
   display state.
5. **screen_buffer is the cursor-math source of truth**. It owns
   line wrapping, UTF-8 width, ANSI skipping, line-prefix accounting.
6. **Continuation-prompt pattern is the working example** of how
   non-command content reaches the screen -- set as line prefixes
   on screen_buffer *before* render, NOT rendered into command text.
7. **LLE supports reentrant modal prompts**. The debugger's
   `(lush-debug)` prompt is implemented via `lle_readline_no_history`
   with `lle_in_debug_prompt()` as the mode probe. Pager-mode key
   handling can use the same reentrancy pattern.

If the design below violates any of these silently, it is wrong.

---

## 2. Goal

Provide a paginated view for any long output produced by lush
(builtins, debugger inspect / vars / bt, completion previews, help
text, history listings, alias listings, dirs, jobs, set -o output,
type for complex commands, etc.) so the user never has to scroll
the terminal scrollback to read content the shell itself produced.

**Non-goals:**
- We are not replacing `less` for paging external program output
  (`cat foo.txt | less`). User runs `less` for that.
- We are not paginating arbitrary subprocess stdout. The pager is
  for lush-produced content.
- We are not building a full text editor or scrollback viewer.

---

## 3. Architectural placement

The pager is a **new L3 sibling layer** in the layered display
architecture (alongside prompt_layer, command_layer,
autosuggestions_layer). When active, it dominates the screen
through the existing composition_engine; when inactive, it
contributes nothing and the prompt+command rendering is unchanged.

```
L1 base_terminal
L2 terminal_control
L3 prompt_layer | command_layer | autosuggestions_layer | pager_layer  <- new
L4 composition_engine
L5 display_controller
```

The display_controller gains one piece of state (`pager_active`)
and one render branch:

- `pager_active = false` -> existing composition (prompt + command
  + suggestions). Pager contributes nothing.
- `pager_active = true` -> composition uses pager content as the
  primary layer; prompt+command are suppressed for the duration.

The pager_layer follows the standard L3 contract:
`layer_create`, `layer_init`, `layer_update`, `layer_render`,
`layer_cleanup`, `layer_destroy`. It publishes
`LAYER_EVENT_CONTENT_CHANGED` and `LAYER_EVENT_SIZE_CHANGED` like
the others.

**Why this placement and not "a separate subsystem"?**

A separate-subsystem pager that takes over stdout / stdin
directly would violate rule 2 (`display_controller.c` is the sole
stdout writer). The composition_engine + display_controller path
is the only legal way to put bytes on the user's terminal during
an interactive session. The pager must route through it.

**Why not "a mode of the command_layer"?**

The command_layer's contract is "the command being edited". A
pager view is not a command; it's content being read. Overloading
command_layer would break its semantics and the contract test the
layer model rests on.

---

## 4. State model

### 4.1 Pager layer state

```c
typedef struct lle_pager_layer {
    /// Source content (owned; freed on deactivation)
    char *content;
    size_t content_byte_length;

    /// Logical line index for fast scroll
    struct {
        size_t start_offset;    ///< Byte offset into content
        size_t byte_length;     ///< Byte length of the line
        size_t visual_height;   ///< Wrapped lines on current terminal
    } *lines;
    size_t line_count;
    size_t lines_capacity;

    /// View state
    size_t top_line;            ///< Index into lines[] -- first visible
    size_t view_rows;           ///< terminal_rows - 1 (status reserves a row)
    int terminal_width;         ///< Cached on activate; recomputed on RESIZE

    /// Search state (forward, less-style)
    char *search_pattern;       ///< NULL when not searching
    size_t current_match_line;  ///< Index into lines[]; SIZE_MAX = no match
    bool wrap_searches;         ///< Wrap to top on no-match (config)

    /// Mode
    enum {
        LLE_PAGER_VIEW,         ///< Default -- navigation keys active
        LLE_PAGER_SEARCH,       ///< Search prompt active at status line
        LLE_PAGER_HELP,         ///< Help text overlaid
    } mode;

    /// Lifecycle
    bool active;
} lle_pager_layer_t;
```

**OPEN QUESTION 1**: should the pager support styled content
(syntax highlighting / colored spans from the source content)?
For the debugger's variable inspector this matters a lot -- typed
values should retain their kind color. If yes, `content` becomes
"raw text + spans" similar to command_layer's highlighted_text.
If no, the pager is plain-text-only and styled producers (debugger,
syntax-highlighted help) strip ANSI before paging.

### 4.2 Display controller additions

```c
typedef struct display_controller {
    // ... existing fields ...

    /// Pager layer presence. When non-NULL and active, the render
    /// cycle uses pager content as the primary surface and
    /// suppresses prompt + command rendering for the duration.
    lle_pager_layer_t *pager_layer;
} display_controller_t;
```

The render cycle gains one branch:

```c
if (controller->pager_layer && controller->pager_layer->active) {
    render_pager_cycle(controller);
} else {
    render_normal_cycle(controller);  // existing path
}
```

Both branches end with `display_controller` writing the result to
stdout -- single-writer rule preserved.

---

## 5. Public API

The API is intentionally narrow. One entry point that consumers
call; everything else is internal.

```c
/// @brief Present content, paginating if interactive and overflowing.
///
/// Decides at call time whether pagination applies:
///   - stdout is not a tty -> write content directly, return
///   - content fits in (terminal_rows - 1) rows -> write directly, return
///   - otherwise -> activate pager layer, run pager input loop until
///     user quits, deactivate layer, return
///
/// The function blocks until the user exits the pager (q / Esc /
/// EOF on stdin). On return, the display controller has restored
/// the prompt + command rendering that was on screen before entry.
///
/// @param executor Executor context (for shell options & config)
/// @param content  Text to display; pager owns a copy
/// @return 0 on success, non-zero if pager couldn't initialize
int lle_pager_present(struct executor *executor, const char *content);
```

**OPEN QUESTION 2**: should there be a streaming variant for
content that is expensive to materialise all at once (very long
history, large completion candidate lists)? Possible signature:

```c
typedef ssize_t (*lle_pager_chunk_fn)(void *userdata, char *out, size_t cap);
int lle_pager_present_streaming(executor_t *e, lle_pager_chunk_fn, void *ud);
```

For an MVP I'd argue the non-streaming form is enough; the streams
the corpus actually produces (help text, alias list, etc.) fit in
tens-of-KB. But debugger memory inspection and large arrays could
benefit. Decision: defer the streaming variant until a concrete
caller needs it.

---

## 6. Rendering

### 6.1 Pager render cycle

When `pager_active = true`, the display controller's render cycle
becomes:

1. **Clear from prompt origin** (same `\033[J` + cursor-move that
   the normal cycle uses to clear stale wrapped content)
2. **Compose pager view** into a screen_buffer:
   - Lines `[top_line, top_line + view_rows)` from
     `pager_layer->lines[]`, rendered into screen_buffer cells
     via screen_buffer_render (we feed pager content as the
     "command" text and the empty string as the prompt -- the
     existing render path handles UTF-8 width and wrapping)
   - One status line at the bottom: position indicator
     (`Lines X-Y of Z (NN%)`), mode indicator, hint string
3. **Write the screen_buffer to stdout** (same code path as the
   normal cycle; no new terminal-write primitives)
4. **Position cursor** at the status line column where the
   next-key prompt is (default: trailing column of status text)

screen_buffer remains the cursor-math source of truth. We do not
write to the terminal outside screen_buffer / display_controller.

### 6.2 Line-prefix reuse

The status line is rendered using the existing screen_buffer
**line-prefix** facility (the same mechanism continuation prompts
use). Line `view_rows` gets prefixed with the status string;
screen_buffer_render handles the column-accounting.

**OPEN QUESTION 3**: status line behavior during fast scroll --
flush on every scroll, or rate-limit at, say, 60Hz? screen_buffer
+ clear-and-redraw is fast enough on modern terminals that flushing
every keystroke shouldn't visibly tear, but on slow tmux-over-ssh
sessions rate-limiting might be wanted. Defer to "flush every
scroll" for the MVP; revisit if measurements show problems.

---

## 7. Input routing

### 7.1 Pager-mode keybindings

Following the `(lush-debug)` precedent: pager activates a
mode-specific keybinding map that overrides the default editing
bindings for the duration.

Default bindings (`display lle pager bindings` to inspect / customize):

| Key | Action |
|---|---|
| Space, Ctrl-F, PgDn | Page down |
| b, Ctrl-B, PgUp | Page up |
| Down, j | Line down |
| Up, k | Line up |
| g, Home | Top |
| G, End | Bottom |
| / | Begin search (forward) |
| ? | Begin search (backward) |
| n | Next match |
| N | Previous match |
| Esc | Cancel sub-mode (search -> view) OR light quit (view -> exit) |
| q | Quit pager (alias for Esc-from-view) |
| Ctrl-C | Quit pager (standard) |
| Ctrl-G x3 (rapid) | Force-exit pager (nuclear) |
| h | Help overlay |

Navigation keys mirror `less(1)` so muscle memory transfers. The
quit hierarchy (Esc/q -> Ctrl-C -> Ctrl-G x3) follows lush's
project-wide modal-exit convention, not less's. A future config
surface (`display lle pager bind KEY ACTION`) can override per
key.

### 7.2 Input loop

Pager-mode input is a synchronous loop, modelled on
`lle_readline_no_history`:

```c
static int run_pager_loop(lle_pager_layer_t *pager) {
    lle_set_pager_active(true);
    while (pager->active) {
        // Display controller renders pager view (handled by event
        // subscribers when content/view state changes).
        // Block waiting for next key event.
        lle_key_event_t key = lle_input_read_key();
        // Dispatch to pager-mode binding for this key.
        dispatch_pager_key(pager, &key);
    }
    lle_set_pager_active(false);
    return 0;
}
```

`lle_set_pager_active(bool)` parallels `lle_set_debug_prompt_active(bool)`.
Code that checks "is the user editing a command right now?" gains
a corresponding `lle_in_pager()` probe.

**OPEN QUESTION 4**: how does the pager interact with completion?
At the `/` search prompt, should tab-completion offer search
history? Default to no; the search prompt is intentionally
minimal.

**OPEN QUESTION 5**: signal handling -- Ctrl-C inside pager. Less
treats it as "interrupt search, return to view"; more treats it
as "quit". I'd default to less's behavior (interrupt-then-quit
on second Ctrl-C).

---

## 8. Builtin integration

Pagination is **opt-in per builtin**. Producers of long output call
`lle_pager_present` instead of writing line-by-line. The pattern:

```c
// Before:
int bin_alias(int argc, char **argv) {
    // ... iterate aliases ...
    for (each entry) {
        printf("alias %s='%s'\n", name, value);
    }
    return 0;
}

// After:
int bin_alias(int argc, char **argv) {
    string_buffer_t sb;
    sbuf_init(&sb);
    // ... iterate aliases ...
    for (each entry) {
        sbuf_appendf(&sb, "alias %s='%s'\n", name, value);
    }
    int rc = lle_pager_present(current_executor, sbuf_str(&sb));
    sbuf_free(&sb);
    return rc;
}
```

Initial candidate builtins (the ones where output regularly
overflows the screen):

- `help` / `help <cmd>` -- already long
- `alias` (no args; list all)
- `history` (no args; list all)
- `set -o` / `setopt` (list)
- `dirs -v`
- `jobs -l`
- `type` for complex multi-line types
- `unset` -- no (output is empty or trivial)
- Debugger `bt`, `vars`, `inspect`, `breakpoints`

**OPEN QUESTION 6**: should `lle_pager_present` honor
`set -o pager off` / a `display lle pager off` config flag that
disables pagination project-wide? Yes -- pagination is a behavior
the user must be able to turn off, especially for scripts that
were designed against the old printf path. Default: on when
stdout is a tty.

**OPEN QUESTION 7**: when pager is disabled (config OFF) but
content overflows, do we still write everything to stdout (let
the user scroll the terminal)? Yes -- that's the bash/zsh
behavior and breaking it would be a worse surprise than missing
pagination.

---

## 9. Debugger integration

The debugger is the highest-leverage caller. Currently `bt`,
`vars`, `inspect`, `breakpoints` emit free-form text that scrolls
off-screen on any non-trivial frame depth or variable count.

Refactor each debugger command to:
1. Build a string buffer of the output
2. Call `lle_pager_present` instead of printf

The debugger's existing `lle_in_debug_prompt` mode and the new
pager activation are **independently active modes** that can
overlap: the user types `inspect huge_array` at `(lush-debug)`,
the inspector builds output, calls `pager_present`, pager runs
its own input loop until quit, returns to debugger which renders
`(lush-debug)` again. Both modes use the LLE reentrant-readline
pattern; no conflict.

**OPEN QUESTION 8**: should the debugger's `inspect` output retain
kind coloring inside the pager? See OPEN QUESTION 1 -- answering
yes there answers yes here. (My recommendation: yes; the kind
colors are exactly the information density a paginated inspect
view needs to keep.)

---

## 10. Configuration surfaces

Following the standard CONFIGURATION doc rules -- pager
configuration lives in the central config registry, exposed via
the `display lle pager` builtin path that already exists for
LLE-facing settings:

| Config key | Type | Default | Effect |
|---|---|---|---|
| `display.lle.pager.enabled` | bool | `true` | Master switch |
| `display.lle.pager.min_lines` | int | `terminal_rows` | Below this row count, just print |
| `display.lle.pager.wrap_search` | bool | `true` | Wrap to top on no-match |
| `display.lle.pager.show_lineno` | bool | `false` | Render line numbers |
| `display.lle.pager.help_key` | string | `"h"` | Override help key |

User-facing commands:

```
display lle pager on|off
display lle pager status
display lle pager bind KEY ACTION
display lle pager unbind KEY
```

All routed through the central config registry per the established
pattern (memory: project-central-config-architecture).

---

## 11. Testing strategy

- **Unit tests** in `tests/unit/test_pager.c`:
  - Line index construction from raw text (correct line offsets,
    correct visual heights with wrapping)
  - Scroll math (top_line bounded by [0, line_count - view_rows])
  - Search forward/backward, wraparound
  - Mode transitions
  - Resize handling -- terminal_width changes invalidate visual
    heights, rebuild line index
  - Empty content (defensive)
  - Single-line content (no pagination)
- **Integration tests** in `tests/unit/test_pager_integration.c`:
  - `lle_pager_present` with content that fits -> direct write,
    no mode entry
  - With overflow content -> mode entry, simulated keys, mode exit
  - Non-tty stdout -> bypass entirely
  - Re-entrancy -- pager invoked while another pager is active
    (refuse; return error)
- **Functional tests** under `tests/lle-functional/`:
  - `help` produces paginated output
  - Pager quit restores prompt + command exactly
  - Multi-line command with pager invocation mid-edit preserves
    LLE buffer
- **Manual smoke tests** before any commit lands:
  - All keys behave per the table above
  - Resize while paging redraws correctly
  - Ctrl-C behaves per OPEN QUESTION 5's decision

---

## 12. Implementation order (once design is signed off)

This is the order I'd propose for landing the work. Each step
ships independently, behind config-gating where appropriate, with
its own tests and commit.

1. **screen_buffer line-index helper** -- extract the
   "split content into logical lines + compute visual heights"
   utility from what will become pager_layer. Lives in screen_buffer
   because it's pure cursor math. *Implemented; see
   `screen_line_index_*` in `include/display/screen_buffer.h`.*
2. **pager_layer skeleton** -- struct, lifecycle, no rendering.
   Tests for line index and scroll math only.
3. **Display controller `pager_active` branch** -- render cycle
   chooses between normal and pager rendering. Pager render
   produces the visible view + status line via screen_buffer.
4. **Pager input loop** -- keybinding table, navigation, mode
   transitions, search. Reentrant-readline pattern; `lle_in_pager()`
   probe.
5. **`lle_pager_present` public API** -- the consumer-facing
   entry; wires steps 1-4 together.
6. **Config wiring** -- `display.lle.pager.*` keys, builtin
   surface (`display lle pager ...`).
7. **Builtin refactors** -- convert producers (alias, history,
   help, etc.) to use `lle_pager_present`. One builtin per commit;
   each commit is reviewable.
8. **Debugger integration** -- convert bt / vars / inspect /
   breakpoints. Independent commits.
9. **Documentation** -- DEBUGGER_GUIDE.md addition, user-facing
   pager docs, update SCREEN_BUFFER_SPECIFICATION with the
   line-index helper's contract.

---

## 13. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Pager state interferes with LLE buffer on activate/deactivate | LLE buffer is single source of truth and is not touched during pager mode. Pager has its own state struct; display controller has a render-mode flag only. |
| screen_buffer assumes prompt+command shape | We feed pager content as the "command text" argument and "" as prompt; screen_buffer_render is shape-agnostic. Line prefixes handle the status line. |
| Builtins forget to call pager and bypass it | Catalog the producer list (table in section 8); add a one-off lint / grep test that catches new builtins emitting >N lines without pager. |
| User wants to disable pager globally | `display.lle.pager.enabled = false`; falls back to direct print. |
| Tests can't simulate tty | `lle_pager_present` non-tty path is the bypass; tests cover both branches. |
| Pager + debugger reentrancy | Both modes use the LLE-reentrant pattern (the debug prompt has this working); document the layering. |

---

## Resolved decisions

Numbered to match the OPEN QUESTION markers above. Resolved
2026-05-25; see the design discussion that produced these answers
for fuller reasoning.

### Q1 -- Styled content (ANSI in pager content): RESOLVED YES.

The pager preserves ANSI escape sequences in `content`. No
separate spans data structure; `content` is a `const char *` that
may contain color/style escapes, and screen_buffer's existing
ANSI-skip pass keeps cursor math correct (the command_layer's
`highlighted_text` already exercises this path daily).

**Implementation consequence:** search must be ANSI-aware. For
each line, derive a stripped-text view for matching; remember
byte offsets in the original content for hit-position highlight
rendering. Reuse `screen_buffer_visual_width`'s ANSI-skip helper
or factor it out so search and rendering share one stripper.

### Q2 -- Streaming variant: RESOLVED DEFER.

Non-streaming `lle_pager_present(executor, const char *)` only
for the MVP. The corpus producers (help, alias, history, dirs,
jobs, inspect, debugger output) all fit comfortably in tens of
KB; a 100K-entry history page is hypothetical. Adding
`lle_pager_present_streaming(executor, chunk_fn, ud)` later is a
pure addition with no ABI lock-in.

### Q3 -- Status-line flush cadence: RESOLVED FLUSH EVERY SCROLL.

The clear-and-redraw model is already proven fast enough at
keystroke rate (the editing loop). Scroll fires less often than
typing. No rate limiting at MVP. Revisit only if tmux-over-ssh
measurements show observable tearing.

### Q4 -- Tab in search prompt: RESOLVED NO COMPLETION.

Tab inserts a literal tab character into the search pattern
(matches `less(1)`). The search prompt is one-shot per session
with no meaningful candidate universe to offer.

### Q5 -- Quit-key hierarchy: RESOLVED to match lush convention.

Lush has a canonical three-tier modal-quit hierarchy used
consistently across LLE modes:

| Key | Severity | Semantic |
|---|---|---|
| `Esc` | Lightest, most convenient | Cancel current sub-mode (e.g. abandon in-progress search), return to outer mode |
| `Ctrl-C` | Standard quit | Exit current modal cleanly |
| `Ctrl-G x3` (rapid) | Nuclear | Force-exit with state cleanup; used when normal quit isn't responsive |

Pager-mode bindings update to follow this hierarchy:

| Key | Pager-mode action |
|---|---|
| `Esc` | Cancel current sub-mode: in `LLE_PAGER_SEARCH` -> return to `LLE_PAGER_VIEW`; in `LLE_PAGER_VIEW` -> quit pager (light exit) |
| `Ctrl-C` | Quit pager (standard exit) |
| `Ctrl-G x3` (rapid) | Force-exit pager with state reset (nuclear; matches the project-wide pattern) |
| `q` | Alias for `Esc` from VIEW (quit pager via the conventional reader key) |

The three-tier model is internally consistent: light cancellation
nests inside standard quit nests inside the nuclear option. Code
in the input loop checks bindings in that order so the lighter
semantic always wins where it applies.

### Q6 -- Master config switch: RESOLVED YES.

`display.lle.pager.enabled = true` by default for interactive
sessions. Non-interactive (`!isatty(STDOUT_FILENO)`) automatically
bypasses regardless of the config setting.

### Q7 -- Pager disabled + overflow: RESOLVED WRITE THROUGH.

When `display.lle.pager.enabled = false` and content exceeds the
viewport, write everything directly to stdout. Matches bash/zsh
behavior; breaking it would be a worse surprise than missing
pagination.

### Q8 -- Debugger inspect kind colors through pager: RESOLVED YES.

Follows from Q1. Kind colors are exactly the information density
a paginated inspect view needs to keep; stripping them defeats
the feature precisely when output is large enough to need pagination.

---

## Additional design constraints (added 2026-05-25)

Three behaviors not surfaced explicitly in the first draft:

### Re-entrancy: refuse with error.

`lle_pager_present` called while `lle_in_pager()` is already true
returns a non-zero error code immediately. Caller decides whether
to write content directly, queue, or ignore. Auto-merging
contents would create surprising interleaved output; refusal is
cleaner.

### SIGWINCH during pager: rebuild line index, redraw.

`pager_layer` subscribes to the existing terminal-resize event
channel. On resize:
1. Re-read `terminal_width` from the terminal abstraction.
2. Recompute `visual_height` for each entry in `lines[]`.
3. Clamp `top_line` to the new valid range.
4. Publish `LAYER_EVENT_SIZE_CHANGED` so the display controller
   triggers a render cycle.

### Empty / NULL content: defensive bypass.

`lle_pager_present(executor, NULL)` and
`lle_pager_present(executor, "")` return `0` immediately without
entering pager mode. No state mutation, no terminal output.

---

**Implementation gate:** all resolutions above stand pending user
sign-off. If any answer is wrong for lush's standards, push back
before code lands. Implementation order is in section 12.
