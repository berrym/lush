# LLE Error System: Architecture and Integration Design

Status: Proposed
Scope: Issue #312 — implement and wire the Spec 16 error-handling subsystem
Related: `docs/lle_specification/16_error_handling_complete.md` (Spec 16),
`include/shell_error.h` / `src/shell_error.c` (shell structured errors),
`include/debug.h` (developer trace channel)

This document is the engineered plan for #312. It records the verified
current state, the architectural decisions that resolve the open forks,
the adopt-versus-reject ruling against Spec 16, and a staged implementation
broken into independently reviewable changes. No integration code is written
until this design is approved.

---

## 1. Verified current state

All facts below were confirmed by reading the tree, not inferred from the spec.

- **The reporting pipeline is built but unreachable.** `lle_report_error`
  (`src/lle/core/error_handling.c:949`), the console/log-file/system-log
  reporters, the atomic counters (`g_error_atomic_counters`), and the
  100-slot pre-allocated context pool are all implemented. The owning
  singleton `g_error_reporting_system` (`error_handling.c:64`) is declared
  `static` and **assigned nowhere in the tree** — there is no init function.
- **There are exactly two callers of `lle_report_error`**, both inside
  `error_handling.c` itself (lines 1729, 1835). No LLE subsystem reports
  through it. Error surfacing across all 13 subsystems is pure
  `lle_result_t` return-code propagation.
- **`lle_report_error` always writes to the console** (`error_handling.c:973`,
  `lle_report_error_to_console`) before it ever consults
  `g_error_reporting_system`. The console format is LLE-native and
  developer-oriented (`[LLE CRITICAL] msg (CODE) … Thread: 0x… Time: …ns`).
  The global only gates the log-file/syslog/callback/statistics tail.
- **Forensics is non-functional.** `lle_create_forensic_log_entry`
  (`error_handling.c:1276`) depends on `lle_capture_system_snapshot`
  (a stub that zeroes fields) and `lle_dump_component_states` (declared by
  the spec, defined nowhere).
- **The recovery/degradation apparatus is declared, not wired.** Recovery
  strategy `execute_strategy` callbacks are NULL; `lle_apply_degradation`
  is never called; the per-component handlers
  (`lle_handle_buffer_error`, `lle_handle_event_system_error`) and the error
  state machine are declared-only.
- **There are 1,437 `return LLE_ERROR_*` sites** across `src/lle/`.
- **The shell already has a complete structured-error subsystem.**
  `shell_error_create` / `shell_error_createv` / `shell_error_display` /
  `shell_error_display_all` (`include/shell_error.h:275-421`), four
  severities (`SHELL_SEVERITY_NOTE/WARNING/ERROR/FATAL`), source locations,
  suggestions, causality chains, and a Rust-style renderer. The executor
  reports through `executor_error_report`. No path connects an LLE error to
  this renderer today.
- **A developer trace channel exists.** `debug_trace_printf`
  (`include/debug.h`), gated on `DEBUG_TRACE`, with a global
  `g_debug_context`.
- **LLE is genuinely multi-threaded.** The prompt git segment runs an async
  worker thread (`pthread_create` at `src/lle/core/async_worker.c:103`),
  which can produce errors off the main REPL thread. Thread-safe counters and
  a reporting mutex are therefore load-bearing, not theoretical.

---

## 2. Design principles

1. **One renderer for the user, one sink for the developer.** User-visible
   faults are rendered by the shell's existing `shell_error_display`; they
   must not introduce a second on-screen error format. Developer diagnostics
   go to `debug_trace_printf`, silent unless `DEBUG_TRACE` is on. Spec 16's
   LLE-native console reporter is retired as a user-facing path.
2. **Report genuine faults, propagate normal control flow.** Most error
   returns are defensive guards and expected misses. Central reporting is
   opt-in at the minority of sites that represent real faults (see §3).
3. **Zero cost when nothing is wrong.** The success path adds nothing. The
   fault path is bounded and, on the editing-critical path, allocation-free
   using the existing pre-allocated context pool.
4. **Harmonize, do not duplicate.** LLE owns detection and classification;
   the shell owns user presentation. The bridge maps between them; it does
   not reimplement either side.
5. **No declared-but-unreachable surface — but one live seam is allowed.**
   Every public symbol this work touches is either wired and exercised by a
   test, or removed with a recorded decision. This is the core remediation
   #312 asks for. The single sanctioned exception is the fault-lifecycle
   dispatch seam (§5): it is *invoked on every fault and asserted by a test*
   from day one, so it is a no-op default with a real call site, not dead
   code. A seam that is actually called is live; a declared type with no
   caller is the disease.
6. **Thread-safe by construction.** Reporting is callable from the async
   worker thread; counters stay atomic and the reporting tail stays mutexed.

---

## 3. Decision 1 — report-versus-propagate taxonomy

A 93-site sample across the seven largest subsystems categorizes every
`return LLE_ERROR_*` into:

| Cat | Kind | Share | Disposition |
|-----|------|-------|-------------|
| A | Validation guard (NULL/bounds/arg) | ~54% | Propagate silently |
| B | Expected not-found / would-block / already-init | ~8% | Propagate silently |
| C | Allocation / pool-exhaustion / OOM | ~26% | **Report** |
| D | State corruption / invariant violation | ~4% | **Report** (forensic) |
| E | Syscall / IO / terminal failure | ~8% | **Report** |

**Rule:** report at C/D/E sites; never at A/B. That is roughly **150 sites,
not 1,437.** Blanket-wrapping every return would manufacture alert fatigue
and would itself be a form of theater — exactly what this epic exists to
remove.

Fault density varies by subsystem and guides where to start:

- **Terminal ~71%** (syscall-heavy: `tcsetattr`, `sigaction`, `select`,
  `read`) — highest value.
- **Display ~54%**, **Completion ~46%**, **Buffer ~42%**, **Event ~39%** —
  mostly allocation and corruption.
- **Keybinding ~30%**, **History ~23%** — lowest; config-load misses are
  normal, not faults.

The taxonomy is encoded as a one-line helper used only at C/D/E sites:

```c
/// Report a genuine fault and return its code unchanged. A no-op for
/// reporting purposes if the subsystem is not initialized; always returns
/// `code` so call sites stay single-expression:
///     return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "index alloc");
#define LLE_FAULT(code, component, detail) \
    lle_fault_report((code), (component), (detail), __func__, __FILE__, __LINE__)
```

Call sites change from `return LLE_ERROR_OUT_OF_MEMORY;` to
`return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "index alloc");`.
Category A/B sites are left exactly as they are.

---

## 4. Decision 2 — two-channel routing

`lle_fault_report` classifies severity (reusing the existing
`lle_determine_error_severity`) and routes:

```
                         lle_fault_report(code, component, detail, loc)
                                          |
                    +---------------------+----------------------+
                    | severity >= ERROR-equivalent               | always
                    v  (user needs to know)                      v
        shell_error bridge                            debug_trace_printf
        (lle_error_to_shell_error -> shell_error_display)   (developer sink,
        Rust-style, on stderr, color-aware                   silent unless
        via isatty)                                          DEBUG_TRACE)
                    |
                    v
        atomic counters + optional log-file/syslog tail
        (only when g_error_reporting_system configured)
```

- **User channel.** Faults a user can act on or must be told about (history
  file unreadable, terminal raw-mode setup failed, OOM aborting an
  operation) are converted to a `shell_error_t` and rendered by
  `shell_error_display`. One on-screen error grammar for the whole shell.
- **Developer channel.** Every reported fault also emits a
  `debug_trace_printf` line carrying component/function/file/line. Invisible
  in normal use; available with `DEBUG_TRACE`.
- **Statistics/forensic tail.** Atomic counters always update (cheap, lock
  free). Log-file/syslog reporting runs only when `g_error_reporting_system`
  is configured, which by default it is not.

The current unconditional `lle_report_error_to_console` user-facing behavior
is removed; that format becomes the body of the developer-channel line.

### Error-code bridge

A focused mapping module (`lle_error_to_shell_error`) translates LLE result
ranges to shell codes and severity. Representative rows:

| LLE range / code | Shell code | Shell severity |
|---|---|---|
| `LLE_ERROR_OUT_OF_MEMORY` (1100s) | `SHELL_ERR_OUT_OF_MEMORY` | FATAL/ERROR |
| `LLE_ERROR_MEMORY_CORRUPTION`, `_STATE_CORRUPTION` | `SHELL_ERR_STATE_CORRUPTION` | FATAL |
| `LLE_ERROR_SYSTEM_CALL`, `_IO_ERROR` (1200s) | `SHELL_ERR_IO_ERROR` | ERROR |
| `LLE_ERROR_HISTORY_SYSTEM` (1300s) | `SHELL_ERR_SUBSYSTEM_INIT_FAILED` | ERROR/WARNING |
| `LLE_ERROR_TERMINAL_ABSTRACTION` | `SHELL_ERR_SUBSYSTEM_INIT_FAILED` | ERROR |

The full table is built incrementally as subsystems are wired; the six
severity levels collapse to the shell's four (MINOR→WARNING, MAJOR/CRITICAL→
ERROR, FATAL→FATAL, INFO/WARNING→NOTE/WARNING).

---

## 5. Decision 3 — adopt versus reject against Spec 16 (Option 1.5: foundation first, seam reserved)

Spec 16 is 1,561 lines that mix a sound reporting core with enterprise
aspiration (ML-based recovery, distributed error correlation, compliance
audit trails, a recovery-strategy scoring engine, a circuit-breaker event
subsystem, a graceful-degradation controller, an error state machine).
Implementing the aspirational layers now would add large
declared-but-unreachable surface — the precise defect #312 exists to remove.
The issue explicitly sanctions documented non-adoption.

The chosen path is **foundation first, scaffolding hooked**: build the tight
reporting + forensics + shell-bridge core, defer the recovery and degradation
logic to a dedicated later milestone, and reserve their insertion point with
a **single live dispatch seam** rather than the speculative type zoo. The
recovery engine is treated as an upcoming milestone, not dead weight, and it
is sequenced *after* forensics deliberately: the reporting and forensic data
this core produces is the instrumentation needed to design recovery
strategies from measurement instead of guesswork.

### The reserved seam

The fault router (`lle_fault_report`, §3/§4) calls exactly one dispatch
function on every fault:

```c
/// Disposition of a fault after the lifecycle dispatch. Today the dispatch
/// only ever surfaces; the recovery/degradation milestone will register
/// strategies that can instead return RECOVERED/DEGRADED, without changing
/// any LLE_FAULT() call site.
typedef enum {
    LLE_FAULT_SURFACED,   /// reported through the channels (the only path today)
    LLE_FAULT_RECOVERED,  /// reserved: a future strategy handled it; caller may continue
    LLE_FAULT_DEGRADED,   /// reserved: a future strategy reduced functionality
} lle_fault_disposition_t;

lle_fault_disposition_t lle_handle_fault_lifecycle(const lle_fault_t *fault);
```

Today `lle_handle_fault_lifecycle` does only: classify severity → route to the
user/developer channels → bump counters → `return LLE_FAULT_SURFACED`. A test
asserts it is invoked on every fault, so it is **live no-op code, not dead
surface** (principle 5). The future milestone inserts a strategy-consultation
step before surfacing; recovery and degradation share this one seam rather
than scattering separate hooks. (Strict-honesty variant: ship with only
`LLE_FAULT_SURFACED` and add the reserved enumerators when recovery lands —
the load-bearing reservation is the function boundary returning a disposition,
not the unused enum values.)

**Adopt (build, wire, test):**

- Error classification: result-code ranges and `lle_error_severity_t`
  (already defined; keep).
- Error context creation and the pre-allocated/critical-path pool
  (already implemented; keep, wire in).
- Severity classification (`lle_determine_error_severity`; keep).
- Atomic statistics counters (keep; they become observable via a debug
  command).
- The reporting pipeline, refit to the two-channel router in §4.
- Forensic log entry, with **real** `lle_capture_system_snapshot` and
  `lle_dump_component_states` implementations (memory stats, active-component
  mask, buffer/event/terminal state strings). Off by default, enabled by a
  config/env switch; writes to a file, never to the user's screen.
- Lifecycle init/teardown and the init-guard idiom (§6).

**Defer behind the seam (remove the speculative types now, build later):**
The recovery and degradation logic is a future milestone reached through
`lle_handle_fault_lifecycle`. Its Spec-16 data structures are removed now —
we keep the door, not the half-built rooms — and redesigned fresh when the
milestone lands, informed by real forensic data:

- Recovery-strategy framework: `lle_recovery_strategy_t`, scoring, selection,
  `execute_strategy` callbacks. Remove the declarations; the seam reserves
  where a future strategy registry plugs in.
- Graceful-degradation controller and feature-degradation map. Remove; same
  seam covers degradation outcomes (`LLE_FAULT_DEGRADED`).
- Error state machine (`lle_error_state_machine_t`). Remove; models a
  recovery workflow that the milestone will design against measured faults.
- Per-component circuit breakers / handlers
  (`lle_handle_event_system_error` et al.). Remove; the two-channel router
  plus the single lifecycle seam covers their intent without a parallel
  dispatch layer.

**Reject outright (not deferred, not built):**

- Error-injection-as-shipped runtime config. Keep a minimal compile-time or
  test-only injection seam for the test suite; remove the always-present
  runtime `g_error_injection_config` surface.
- Spec section 14 (ML / distributed / enterprise compliance) and the async
  reporting thread: out of scope.

**Out of scope for #312 (tracked elsewhere):** the secure-memory operations
(`lle_memory_enable_secure_mode`, `_disable_secure_mode`, `_secure_clear`)
named in the roadmap belong to Spec 15 memory management, not error handling.
They are noted in #312 only as a co-removed orphan; this design does not
implement them.

The net effect: the surviving public surface is small, every symbol of it is
reachable and tested, and the "enterprise" scaffolding that could never be
honestly exercised is gone.

---

## 6. Decision 4 — lifecycle and initialization

- **Ownership.** The reporting system is a process singleton
  (`g_error_reporting_system`), matching the established `g_lle_integration`
  / `global_manager` / config-registry idiom: null-pointer + boolean guard,
  idempotent init, graceful no-op on use-before-init.
- **Init attach point.** Inside `lle_shell_integration_init`
  (`src/lle/lle_shell_integration.c`), after the event hub is created and
  **before** the editor is created — so a failing editor creation can already
  report. Non-fatal: if init fails, faults still reach the developer channel
  and the counters; only the optional log-file tail is unavailable.
- **Teardown attach point.** Inside `lle_shell_integration_shutdown`, after
  the editor is destroyed and before the event hub is destroyed. This runs
  via the existing `lle_shell_integration_atexit_handler` (`init.c:802`),
  which is registered before `lush_pool_shutdown` and therefore tears down
  while pool memory is still valid.
- **Use-before-init safety.** `lle_fault_report` and `lle_report_error`
  consult the singleton through a guard; with the system NULL they still
  update atomic counters and emit the developer-channel line, and skip the
  log tail. No call site needs to know whether init has run.

---

## 7. Decision 5 — thread-safety

- The async git-segment worker (`async_worker.c:103`) is the one real
  off-main-thread error producer today. The design treats reporting as
  concurrent.
- Counters stay `_Atomic` with the existing memory-ordering (relaxed
  increments, acquire reads). The log-file/syslog tail stays under the
  reporting mutex. The pre-allocated context pool keeps its allocation mutex.
- The shell-error user channel is **main-thread only.** A fault raised on the
  worker thread routes to the developer channel and counters directly; it
  does not call `shell_error_display` from the worker (the renderer and
  stderr ownership assume the main thread). Worker-thread faults that must
  reach the user are marshaled back through the existing async result
  callback, which already runs on the main thread.

---

## 8. Decision 6 — forensics, done honestly or not at all

Forensic logging is the one adopted feature with missing implementations.
Rather than ship stubs:

- `lle_capture_system_snapshot` is implemented against real sources:
  memory-usage stats from the LLE pool, the active-component mask, and
  process timing. No fabricated CPU/op-rate fields — fields we cannot source
  truthfully are removed from the struct, not zero-filled.
- `lle_dump_component_states` serializes the buffer, event-queue, and
  terminal-interface states that are actually reachable from the editor.
- Forensic capture is **disabled by default**, enabled by an explicit switch,
  and writes to a file. It is a debugging aid, never user-facing output.

If, during implementation, a snapshot field cannot be sourced honestly, the
field is dropped and the decision noted — consistent with principle 5.

---

## 9. Implementation staging

Each stage is an independently reviewable, independently green change.
Earlier stages deliver value without the later ones.

1. **Lifecycle + guard + observability, purely additive.** Add
   `lle_error_reporting_system_init` / `_shutdown` with the null/boolean
   guard, the init/teardown attach points, and a thread-safe read-only
   counter-snapshot accessor for tests and a future debug command. Zero
   behavior change: the only current callers of `lle_report_error` are the two
   dead recovery handlers (`lle_handle_buffer_error`,
   `lle_handle_event_system_error`, removed in stage 7), so nothing prints
   today. Ships the guarded singleton and restores lifecycle reachability.
2. **The fault helper + the shell bridge + the lifecycle seam.** Add
   `LLE_FAULT` / `lle_fault_report`, the `lle_error_to_shell_error` mapping
   module, the two-channel router, and the `lle_handle_fault_lifecycle` seam
   (§5) that the router calls — today a no-op default returning
   `LLE_FAULT_SURFACED`. Also re-point the LLE-native console reporter into
   the developer channel here, as part of introducing the router (still
   behavior-neutral for users, since real call sites arrive in stage 3). No
   call sites converted yet; unit-test the router, the mapping, and that the
   seam is invoked exactly once per fault.
3. **Wire the high-density subsystem first: terminal.** Convert the ~7 C/E
   sites (`tcsetattr`, `sigaction`, `select`, `read`, OOM). Prove a real
   terminal fault renders one shell-style error to the user.
4. **Wire buffer + display.** Corruption (D) and allocation (C) sites.
5. **Wire event + completion + history + keybinding.** Allocation sites;
   subsystem-gated per §3 density.
6. **Forensics.** Real `lle_capture_system_snapshot` /
   `lle_dump_component_states`, behind the switch, with a behavioral test.
7. **Remove the rejected surface (§5)** and reconcile the spec/guide docs to
   describe what was actually built.
8. **Restore the behavioral test.** Replace the retired
   `test_lle_error_handling.c` orphan with a suite that asserts real routing:
   a fault at a known site produces the mapped shell error and the developer
   line, counters increment, and the user channel stays silent for A/B
   returns. Wire into meson.

Stages 1–2 are the foundation and should land before any call-site
conversion. Stages 3–5 are mechanical once the helper exists. Stage 7's
removals are gated behind the rest so nothing is deleted while still
referenced.

---

## 10. Testing strategy

- **Router/mapping unit tests** (stage 2): every LLE range maps to the
  expected shell code and collapsed severity; the guard makes
  use-before-init a no-op; counters increment exactly once per fault.
- **Per-subsystem behavioral tests** (stages 3–5): inject a real fault
  (e.g. force an allocation failure or a `tcsetattr` failure via a seam) and
  assert the user-channel render and the counter delta — not "the function
  returned non-NULL."
- **Forensic test** (stage 6): a captured snapshot reflects real memory/
  component state; the dump is well-formed and contains the seeded state.
- **Negative tests:** Category A/B returns produce **no** user output and
  **no** counter increment — proving the opt-in boundary holds.
- Each change is gate-proven (mutate the expected literal, rebuild, confirm
  RC=1, restore), run under LeakSanitizer via the Homebrew LLVM clang, and
  verified on macOS and Linux before landing.

---

## 11. Open design questions (for approval)

1. **Adopt/reject scope (§5) — RESOLVED: Option 1.5, foundation first,
   seam reserved.** Build the reporting + forensics + shell-bridge core now;
   remove the speculative recovery/degradation/state-machine/circuit-breaker
   types; reserve their insertion point with the single live
   `lle_handle_fault_lifecycle` seam; design and build the recovery engine as
   a dedicated later milestone informed by the forensic data this core
   produces.
2. **Forensics default and destination.** Off by default, file-only, behind
   an explicit switch (§8) — confirm, or specify a different default.
3. **User-channel threshold.** Route severity ≥ ERROR-equivalent to the user
   channel, everything else developer-only (§4) — confirm the cut line.

Questions 2 and 3 are low-stakes and have sensible defaults; they can be
confirmed in passing or settled during stage 1.
