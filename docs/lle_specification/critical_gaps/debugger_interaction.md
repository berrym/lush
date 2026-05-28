# Critical Gap: LLE Ownership of Debugger Interaction

**Status**: Design / open. Records an architectural gap and the
direction to close it. Not yet implemented.

**Context**: the integrated debugger's interactive break-prompt
(`debug_enter_interactive_mode`, `src/debug/debug_breakpoints.c`)
reads user input with a hand-rolled `fgets` loop and its own terminal
handling. This is pre-LLE-era code -- it predates the Lush Line
Editor and was never integrated with it.

---

## 1. The gap

LLE is the single owner of terminal and user interaction in lush. Any
component that does its own `fgets` / `termios` / `read` is a
second-class interaction path and a latent conflict with LLE -- two
owners of one terminal. The debugger's break-prompt is exactly that:
it hand-rolls input, contends with LLE for terminal state, and (the
observed symptom) can hang a session.

The fix is not to make the debugger *coordinate* with LLE -- a
politeness protocol between two owners is still two owners. It is to
make the debugger a *consumer* of LLE, like every other interactive
surface. The debugger obtains its input from LLE.

This gap is the concrete instance that motivated PHILOSOPHY.md §7
(the debugger keeps pace with the language): the debugger predates a
core subsystem and never integrated with it.

## 2. The direction

The `(lush-debug)` break-prompt becomes an LLE-driven prompt: the
debugger calls `lle_readline()` with the debug prompt string and a
debug-specific completion source, replacing the `fgets` loop.

Consequences, all positive:

- the split-terminal-ownership conflict -- and the hang -- disappear,
  because there is one owner;
- the debug prompt gains line editing, history, and completion with
  no bespoke code;
- completion of in-scope variable names and debug commands at the
  prompt becomes possible -- the foundation for the type-aware
  inspection UX (debugger Tiers 1-2, SEMANTICS.md §3.9).

## 3. What supports feasibility (verified)

- `lle_readline(const char *prompt)` (`src/lle/lle_readline.c`) takes
  the prompt as a parameter -- a custom debug prompt is trivial.
- Its major session state -- input buffer, event system, edit arena,
  terminal abstraction -- is created and destroyed per call rather
  than held in singletons.
- The LLE unix interface captures `original_termios` at creation
  (`terminal_unix_interface.c:303`) and restores exactly that on
  `exit_raw_mode`. Because each interface restores the state it
  found, nested create/enter/exit/destroy plausibly composes (last
  in, first out) without clobbering an outer session -- though this
  must be confirmed under true nesting (§5).
- The completion source is pluggable via the LLE source manager
  (`lle_source_manager_register`) -- a debug-command / variable-name
  source can be registered for the debug prompt.
- LLE's multiline-continuation machinery shows the prompt layer
  already handles dynamic, context-aware re-prompting -- though the
  debug prompt needs nothing that elaborate (§4).

## 4. The gap to close -- and what it is *not*

The fix is bounded, and it is **not a re-entrancy problem**. This is
worth stating precisely, because it is easy to assume otherwise.

A breakpoint fires during *executor execution*. The shell's main loop
(`src/lush.c:301-356`) reads a complete input batch -- via LLE in
interactive mode -- and only *then* calls `parse_and_execute`; the
breakpoint check in `execute_node` is gated on `in_script_execution`,
which is set during execution, not during input reading. So by the
time the debugger reaches its prompt, the primary `lle_readline` has
already returned and is **off the stack**. The debug prompt is a
**sequential** `lle_readline` call invoked from a deep executor stack
frame -- not a nested one. The `lle_readline.h:51` non-reentrancy
`@note` is therefore not violated: sequential calls are the normal,
supported case -- every shell prompt is one.

The gap is narrow: `debug_enter_interactive_mode` must call
`lle_readline` (with the debug prompt string and a debug completion
source) in place of its `fgets` loop. The terminal conflict
disappears because `lle_readline` owns its terminal lifecycle per
call, exactly as it does for every prompt.

## 5. What the implementation must still handle

Not a re-entrancy risk register -- a short list of real, bounded
concerns:

- **LLE availability.** In interactive mode LLE is initialized. Under
  `lush script.sh` (non-interactive) LLE may not be initialized and
  there may be no controlling terminal. The debug prompt must detect
  this and degrade gracefully -- with no terminal, interactive
  debugging is simply unavailable (the current code already probes
  `/dev/tty` for this).
- **Clean hand-back to the executor.** `lle_readline` runs while the
  executor is mid-statement; it must leave terminal and display state
  as it found them so execution resumes cleanly. `lle_readline`
  already brackets its terminal raw-mode per call -- this should
  hold, but verify.
- **Completion source.** Registering a debug-command / variable-name
  completion source, scoped to the debug prompt and cleaned up after.
- **History.** Debug commands must not pollute the shell's command
  history -- the debug prompt's `lle_readline` use must not feed the
  shared history.

## 6. Effort

With the re-entrancy framing corrected, this is genuinely modest. The
debug prompt is a sequential `lle_readline` call -- a supported usage
-- so the work is: replace the `fgets` loop with an `lle_readline`
call, register a debug completion source, and handle the
no-terminal / LLE-not-initialized degraded case. No re-entrancy
hardening of `lle_readline` is required. The size is small; the care
goes into the degraded-mode handling and the completion source, not
into LLE internals.

## 7. Relationship to the debugger workstream

This is the *interactive* sub-stream of making the debugger
first-class. It is independent of the *executor-side* wiring --
breakpoints halting at the correct source line (`node->loc.line`),
frame-local population, step-over / step-out -- which is a separate
parallel effort. Once the LLE-powered prompt lands, the debugger's
variable inspection and the type-aware features (debugger Tiers 1-2)
build on it.

---

## See also

- [../../PHILOSOPHY.md](../../PHILOSOPHY.md) -- §7, debugger coherence.
- `include/lle/lle_readline.h` -- the `lle_readline` contract.
- `src/debug/debug_breakpoints.c` -- the current `fgets` break-prompt.
