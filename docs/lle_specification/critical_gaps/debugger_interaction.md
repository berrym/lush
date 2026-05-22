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
  already handles dynamic, context-aware re-prompting. Note this is
  *intra-session* re-prompting within one `lle_readline` call, not
  the nested call a debug prompt needs -- see §4.

## 4. The gap to close: re-entrancy

`lle_readline()` is explicitly documented **not reentrant**
(`include/lle/lle_readline.h:51`: "This function is NOT reentrant.
Only one readline operation should be active at a time."). The note
states an assumption; it cites no hazard. It reflects that LLE was
never *designed or validated* for a nested call.

A debug prompt is, structurally, a nested `lle_readline()`: the break
fires deep in the executor while the shell's primary prompt is
logically suspended on the stack. Closing the gap means genuinely
**validating and hardening** `lle_readline` for re-entrancy -- not
assuming the per-call state "looks isolated enough." The
non-reentrancy contract is a real contract; lifting it is the work.

## 5. Open questions the implementation must resolve

A risk register, not a list of knowns:

- **Shared editor state.** `global_lle_editor` (and the Spec-26
  `g_lle_integration` editor) persist across calls as a config holder
  -- history, keybindings, completion config. A nested call must not
  corrupt the parked outer call's view of it. To be proven.
- **Display layer.** LLE renders through the layered display
  controller. A nested readline drawing the debug prompt while the
  outer session's display state is parked must not corrupt the outer
  prompt's layer -- the debug prompt may need a separate layer, or
  the outer display must be suspended for the duration.
- **Terminal state.** The plausible LIFO composition in §3 must be
  verified precisely under a real nested session, including the
  `TCSAFLUSH` discard-unread-input behavior on enter/exit.
- **Event system.** A nested event loop while an outer one is parked
  -- any shared queue or global handler state.
- **Completion source lifetime.** Registering and scoping a
  debug-only completion source so it applies to the debug prompt and
  not the primary prompt, and is cleaned up after.

## 6. Effort

The foundation is genuinely present: a parameterized prompt,
substantially per-call session state, pluggable completion, and a
terminal model that plausibly composes. The architecture permits a
nested session. This is a focused re-entrancy validation-and-hardening
effort, not a rewrite.

Its size is determined by what the validation in §5 finds; it is not
responsibly pre-estimated before that validation is done. An early
exploratory "~30 lines" estimate is explicitly **not** relied upon
here -- it predated verification and assumed coexisting terminal
interfaces that the per-call lifecycle may not produce.

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
