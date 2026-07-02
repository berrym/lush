# Signal Handling Audit and Lockdown Plan

Status: **audit + proposed model — no code commitment.** §1–§8 are the
read-only current-state audit; §9 is the proposed lush signal model for
decision. The deliverable is this document. Every remediation named here is a
proposal to be scheduled separately after review.

Scope: the complete signal-handling surface of lush on master
(`16c17a5fb`) — dispositions, masks, fork/exec resets, wait/reap, the trap
subsystem, job control, and terminal-mode restores — mapped against POSIX
XCU and the bash/zsh reference models, with a keep-or-change verdict for
every disposition and a phased execution plan.

Method: a read-only inventory sweep of the whole `src/` tree (seven
independent passes plus a tree-wide completeness index). All `file:line`
references below are from that inventory; they are anchors for review, not
guaranteed stable across future edits.

---

## 1. Executive summary

lush has the signal *primitives* (a `sigaction` wrapper, `pthread_sigmask`,
a deferred SIGHUP flag) but not a unifying *model*. Two regimes coexist and
were built at different times by different reasoning:

- **The shell regime** (`src/signals.c`): SIGINT / SIGSEGV / SIGQUIT / SIGHUP
  plus the trap machinery, all installed through one `set_signal_handler`
  funnel with `sa_flags = 0` (no `SA_RESTART`), relying on `EINTR` to break
  blocking reads.
- **The LLE terminal regime** (`src/lle/terminal/terminal_unix_interface.c`):
  SIGWINCH / SIGTSTP / SIGCONT, installed with `SA_RESTART`, plus a private
  raw-mode/termios layer.

They overlap (three independent SIGWINCH installers; two SIGINT delivery
routes into `lle_readline`), diverge (`pthread_sigmask` in `signals.c` vs
`sigprocmask` in `display_controller.c`, in a process that is multithreaded
after init), and leave gaps (SIGCHLD has no reaper; SIGPIPE is never set;
trap actions never dispatch outside the REPL). The recently-closed cluster
(#374, #376, #404, #407) fixed the worst SIGHUP defects one at a time; this
audit exists so the *model* is locked down and the next defect is designed
out rather than discovered.

The headline findings:

1. **No deferred-dispatch model.** Signal handling relies on "a signal
   interrupts a blocking read with `EINTR`." That is exactly the fragility
   that produced #405. bash polls `run_pending_traps` at safe points; zsh
   brackets critical regions with `queue_signals`/`unqueue_signals`. lush
   does neither systematically.
2. **Trap dispatch is wired into one loop.** `execute_pending_traps()` has a
   single caller (`src/lush.c:300`), so signal-trap bodies never run under
   `-c`, in the middle of a batch, or during a foreground wait (#409).
3. **`trap '' SIG` does not ignore** — it resets to `SIG_DFL` (#408).
4. **Multithread hazards.** The LLE async worker makes the process
   multithreaded during init. SIGHUP is correctly confined to the main
   thread, but the five other trappable signals are not, so their handlers
   can mutate `pending_trap_signals` from the worker; and
   `display_controller.c` uses `sigprocmask` (unspecified when
   multithreaded).
5. **Async-signal-safety is inconsistent.** The SIGSEGV handler formats
   output with stdio before `abort()`; the SIGINT handler reads a
   non-atomic `pid_t`; two handlers write plain `bool`.
6. **The TCSAFLUSH wedge that caused the 23-minute hang (#404) still lives**
   in the SIGTSTP/SIGCONT handlers, the atexit cleanup, and the
   adaptive/base terminal layers — only `exit_raw_mode` was converted to
   `TCSANOW`.
7. **Missing dispositions.** SIGPIPE is never set (left `SIG_DFL`; no
   observable divergence found in testing — see G7); SIGTERM has no
   interactive-shell disposition (interactive-only, unverified — G8); SIGCHLD
   has no reaper.

None of these are being fixed in this document. They are the map.

---

## 2. Constraints that shape any remediation

These are lush-specific and non-negotiable; they rule several textbook
approaches out.

- **Multithreaded process.** The LLE async worker thread is created *during*
  init (`init.c:850` → git segment registration →
  `async_worker.c:106 pthread_create`), before `enable_sighup_delivery()`.
  Therefore: mask operations must use `pthread_sigmask`, not `sigprocmask`
  (POSIX leaves `sigprocmask` unspecified once multithreaded); and any
  per-signal flag touched from more than one thread needs real atomics, not
  a bare `sig_atomic_t` (which only guarantees single-thread
  interrupt-safety).
- **Linux and macOS.** `signalfd` is Linux-only. The portable race-free
  primitive for "wake a blocking read on signal delivery" is the **self-pipe**
  (or `pselect`/`ppoll` with a saved mask). Any move away from bare-`EINTR`
  reliance must use one of these, not `signalfd`.
- **The terminal-drain wedge class.** `tcsetattr(TCSAFLUSH|TCSADRAIN)` blocks
  until output drains and is *uninterruptible* when nothing is reading the
  master — this is the mechanism of the #404 hang. Any tcsetattr on a
  teardown/signal path must be `TCSANOW`.
- **The LLE is the single source of truth for the line buffer.** Signal-
  driven redraw and abort must route through LLE state, not poke the terminal
  directly (consistent with the display architecture docs).
- **Few, centralized blocking boundaries.** Unlike bash's sprawling
  reader/executor, lush blocks in a countable set of places (§5.5). This is
  what makes a poll-at-boundaries model tractable here.

---

## 3. Current-state inventory

### 3.1 Per-signal disposition table

Interactive shell, master. "Reference" is the interactive bash/zsh behavior
that POSIX permits or requires.

| Signal | lush default disposition | Where set | Reference (interactive) | Verdict |
|---|---|---|---|---|
| SIGINT | `sigint_handler` (forward to child / abort line) | `signals.c:225` | caught; re-prompt | **keep**, harden (§3.7) |
| SIGSEGV | `sigsegv_handler` (crash report → abort) | `signals.c:226` → `errors.c:75` | usually `SIG_DFL` | keep, but handler not async-safe (§3.7) |
| SIGQUIT | `SIG_IGN` | `signals.c:230` | `SIG_IGN` | **keep** (matches bash/zsh) |
| SIGHUP | `sighup_handler` (flag) + blocked in init, unblocked at end | `signals.c:241,251,263` | caught; hang up + cascade | **keep** (fixed by #404/#407) |
| SIGTERM | none (default terminate) | — | interactive: `SIG_IGN` | **change (unverified)** — interactive-only; `-c` parity confirmed |
| SIGCHLD | none (synchronous reap) | — | handler + job-table reap | **research** (P4, §7) |
| SIGPIPE | none (default `SIG_DFL`) | — | shell-specific; children `SIG_DFL` | **verify** — no divergence found (G7) |
| SIGTSTP/SIGTTIN/SIGTTOU | `SIG_IGN` via raw `signal()` | `executor.c:16432-16434` | `SIG_IGN` in job-control shell | keep, but use `sigaction` (§3.2) |
| SIGWINCH | `handle_sigwinch` (flag) — three installers | `terminal_unix_interface.c:203`, `base_terminal.c:562`, masked in `display_controller.c` | caught; redraw | **change** — one owner (§3.6) |
| SIGCONT | `handle_sigcont` (re-arm termios) | `terminal_unix_interface.c` | caught / default | keep, harden termios (§3.6) |
| SIGALRM | `lle_watchdog` handler via `alarm()` | `lle_watchdog.c:128` | (shell-specific) | keep; note single-timer contention |

### 3.2 The disposition funnel and restart policy

- `set_signal_handler(signo, handler)` (`signals.c:303-309`) is the single
  funnel: `sigemptyset(sa_mask)`, `sa_flags = 0`, `sigaction`. Portable
  (`sigaction`, not `signal()`), but **`SA_RESTART` is never set** in the
  shell regime, so every blocking syscall interrupted by an installed signal
  returns `EINTR`. This is deliberate for SIGHUP (documented at
  `terminal_unix_interface.c:239`) and is the load-bearing mechanism of the
  whole shell regime.
- The LLE regime installs SIGWINCH/SIGTSTP/SIGCONT **with** `SA_RESTART`
  (`terminal_unix_interface.c` `install_signal_handlers`) — intentional
  asymmetry, but it means the two regimes have opposite restart semantics.
- Two sites use raw `signal()` instead of the funnel: the SIGTSTP/TTIN/TTOU
  ignores (`executor.c:16432-16434`) and the re-arm inside the LLE
  TSTP/CONT handlers. `signal()` has SysV/BSD-variant semantics and does not
  set `sa_mask`, so the re-armed TSTP handler no longer blocks SIGWINCH as
  the original install did.

### 3.3 Mask lifecycle (SIGHUP)

The one signal with a real lifecycle:

1. **Block** at init: `pthread_sigmask(SIG_BLOCK, {SIGHUP})` on the main
   thread (`signals.c:251`, called from `init.c:384`), *before* the async
   worker is created, so the worker inherits the block and SIGHUP is
   main-thread-only.
2. Everything between there and `init.c:1033` runs with SIGHUP masked:
   `config_init`, login-env, **system-profile / login-script / interactive-
   startup / `$ENV` sourcing**, LLE bring-up incl. the worker thread, first
   prompt.
3. **Unblock** at end of init: `enable_sighup_delivery()` →
   `pthread_sigmask(SIG_UNBLOCK, {SIGHUP})` (`signals.c:263`, `init.c:1033`).
   A hangup that arrived during init is pending and delivered here.
4. **Fork children**: `reset_subshell_signals()` unblocks SIGHUP
   (`signals.c:215`).
5. **Before exec**: `reset_signal_mask_for_exec()` clears the *entire* mask
   (`SIG_SETMASK`, empty — `signals.c:266-278`); mitigates the "program
   exec'd during the init block window inherits a masked SIGHUP" regression
   caught in #404.

No other signal is masked deliberately. `display_controller.c` masks
SIGWINCH transiently (see §3.6).

### 3.4 Fork / exec reset discipline

`reset_subshell_signals()` (`signals.c:184`) resets **only SIGHUP+SIGSEGV to
`SIG_DFL` and unblocks SIGHUP**. It deliberately leaves SIGINT inherited
(#375) and does not touch SIGTSTP/TTIN/TTOU (stay `SIG_IGN`), SIGQUIT (stays
`SIG_IGN`), SIGPIPE/SIGCHLD/SIGTERM, **or the trap list / trap handlers**.

Called at 9 non-exec fork sites: pipeline stage (`executor.c:2587`), coproc
(`4470`), subshell (`6936`), command-sub (`15739`), background ×2
(`16664`,`16699`), captured-builtin (`17014`), procsub (`18526`). The lone
fork-with-exec external-command child (`9038`) additionally calls
`reset_signal_mask_for_exec()` (`9056`) right before `execvp` (`9057`).

Reset **gaps** (fork sites that do *not* go through the shared helper):

- `bin_command.c` child uses a *different* hand-rolled `signal(..., SIG_DFL)`
  list (`bin_command.c:203-208`) that disagrees with `reset_subshell_signals`.
- `bin_env.c` child resets only the mask (`:233`), not dispositions;
  `bin_exec.c` resets the mask (`:211`) and relies on exec's auto-reset
  (which does **not** clear `SIG_IGN`). Three exec builtins, three different
  reset behaviors.
- `redirection.c` heredoc-writer children (fork at `1027`, `1140`) reset
  **neither** dispositions nor mask.
- LLE helper children `git_command.c:129` and `completion_config.c:248`
  reset neither before `execl(/bin/sh)`, and run from the **worker thread**.

### 3.5 Wait / reap model

No SIGCHLD handler exists; all reaping is synchronous. Three distinct wait
implementations:

1. `executor_wait_foreground` (`executor.c:2468`) — blocking, EINTR-retrying,
   SIGHUP-forwarding (`kill` SIGHUP `2472` + SIGCONT `2473`, records
   `128+SIGHUP` `2479`). The canonical foreground reap after #407.
2. `executor_update_job_status` (`executor.c:16587`) — `WNOHANG|WUNTRACED`
   poll, no retry.
3. `executor_builtin_fg` (`executor.c:16841`) — blocking `WUNTRACED`, **no
   EINTR retry and no SIGHUP forwarding** — diverges from (1).

`current_child_pid` (for SIGINT forwarding) is maintained only for lone
external commands (`set 9074` / `clear 9105,9108`); pipelines, subshells,
command substitution, etc. do not set it, so Ctrl-C forwarding covers only
that one case.

### 3.6 Job control and terminal

- Ownership entirely in `executor.c`: `initialize_job_control` (`setpgid
  16423`, `tcsetpgrp 16429`, `SIGTTIN 16418`); fg transfer/reclaim
  (`tcsetpgrp 16828/16845`, `SIGCONT 16833`); bg resume (`SIGCONT 16898`);
  bg-child pgroup (`setpgid(0,0) 16694`, parent `setpgid 16709`). No
  `setsid()` anywhere.
- `LUSH_FUZZ_SANDBOX` (`16400-16412`, #75) returns before installing the
  TSTP/TTIN/TTOU ignores and tcsetpgrp, so under fuzz those dispositions are
  whatever the process started with.
- **Three independent SIGWINCH installers**, last-installer-wins:
  `terminal_unix_interface.c:203` (functional flag handler),
  `base_terminal.c:562` (no-op), and transient masking in
  `display_controller.c`. `display_controller.c` uses **`sigprocmask`**
  (not `pthread_sigmask`) at three sites (`228/236`, `255/343`, `453/978`),
  and two early-return paths (`457`, `487`) skip the restore at `978`,
  leaking a blocked SIGWINCH.
- **TCSAFLUSH wedge class** (§2) — `TCSANOW` was applied only at
  `exit_raw_mode` (`terminal_unix_interface.c:447/458`). Still `TCSAFLUSH`
  on signal/teardown paths: SIGTSTP handler (`:91`), SIGCONT handler
  (`:116`), atexit cleanup (`:144`), `adaptive_native_controller.c:277`,
  `base_terminal.c:473`.

### 3.7 Async-signal-safety

- `sighup_handler` (`signals.c:132`), `trap_signal_handler` (flag only), and
  `lle_watchdog` (atomics) — **safe**.
- `sigint_handler` (`signals.c:95`) reads `current_child_pid`, declared plain
  `static pid_t` (`signals.c:36`), not `volatile sig_atomic_t` — a non-atomic
  read in handler context.
- `sigsegv_handler` (`errors.c:75`) calls `error_abort`/`do_error`, which do
  `vfprintf`-style formatting **before** `abort()` — **not async-signal-safe**
  (the classic crash-handler footgun; can deadlock in `malloc`/stdio locks
  taken at fault time).
- `handle_sigwinch` writes a plain `bool sigwinch_received`
  (`terminal_unix_interface.c:72`); `raw_mode_active` is a plain `bool` from
  handler and main thread. The watchdog, by contrast, uses `atomic_bool`.

### 3.8 Trap subsystem

- **Dispatch**: `execute_pending_traps()` (`signals.c:626`) has exactly one
  caller — `src/lush.c:300`, top of the REPL loop. The `-c` path
  (`lush.c:110-155`) is `parse_and_execute` + `execute_exit_traps` + exit and
  never enters that loop; a mid-batch statement list does not call it either.
  Result: signal-trap bodies fire only between REPL iterations (#409).
- **Ignore semantics**: `trap '' SIG` → `bin_trap.c:182 set_trap(sig, "")` →
  `set_trap` treats empty as removal (`signals.c:360-363`) → `SIG_DFL`,
  identical to `trap - SIG`. `SIG_IGN` is never installed by the builtin
  (#408).
- **Signal coverage**: only 6 signals get a handler
  (INT/TERM/QUIT/HUP/USR1/USR2, `signals.c:383-386`). A numeric operand for
  any other signal is stored via `atoi` but never gets a handler, so its
  body cannot fire. `trap -l` hardcodes **Linux** signal numbers (TERM 15,
  USR1 10, USR2 12) — wrong on the macOS target.
- **Worker-thread race**: only SIGHUP is blocked on the worker;
  INT/TERM/QUIT/USR1/USR2 are not, so `trap_signal_handler` can run on the
  worker and mutate `pending_trap_signals` concurrently with the main
  thread's read/clear — outside the single-thread guarantee of
  `sig_atomic_t`.
- **Fork inheritance**: `reset_subshell_signals` does not reset trapped
  INT/TERM/QUIT/USR1/USR2 handlers and does not clear `trap_list`, so fork
  children inherit the full trap list and 5 of 6 handlers but (except
  `execute_subshell` at `6996`) never call `execute_exit_traps`, and never
  call `execute_pending_traps`. bash resets non-ignored traps to default in
  subshells; lush does not.
- **EXIT/DEBUG/ERR/RETURN** are executor-dispatched pseudo-traps, not kernel
  signals. EXIT is hand-placed at 4 sites (`lush.c:153`, `lush.c:454`,
  `executor.c:6996`, `bin_exec.c:201`) with no `atexit()`; non-subshell fork
  children exit without firing it. DEBUG fires in `execute_command_list`,
  brace groups, function bodies — but not in `execute_command_chain`,
  subshell bodies, or pipelines. ERR similar. These are coverage gaps in the
  pseudo-trap machinery, adjacent to but distinct from the signal work.

---

## 4. Reference model

### 4.1 The two industry models

- **bash — poll (`run_pending_traps`)**: async handlers set a flag and
  return; the reader/executor calls `QUIT`/`run_pending_traps` at safe
  points. Simple to reason about, safe by construction; cost is *coverage* —
  every blocking boundary and long loop must poll or the shell feels
  unresponsive.
- **zsh — queue (`queue_signals`/`unqueue_signals`)**: signals arrive by
  default; critical regions (job-table mutation, allocation, parsing) are
  bracketed by mask block/unblock, and pending signals fire on unqueue. Robust
  for a huge feature surface; cost is constant mask churn and the footgun of
  an early return that skips the matching `unqueue`.

### 4.2 The model for lush

**A poll-dominant hybrid**, weighted opposite to a generic recommendation,
for reasons specific to this codebase:

- lush's blocking boundaries are **few and now partly centralized** — the
  REPL input read (`lush.c:320`), the LLE terminal read
  (`lle_readline`/`select` at `terminal_unix_interface.c:699`), and the
  foreground waits (all through `executor_wait_foreground`), plus the
  between-statement (`executor.c:1575`) and between-batch (`1318`) checks
  #407 already added. The "hundreds of poll sites" cost of the bash model
  does not apply at this scale.
- lush is **multithreaded**, which makes zsh-style constant `sigprocmask`
  churn both more expensive (per-critical-section `pthread_sigmask`) and more
  dangerous (an early return without the matching unqueue permanently blocks
  a signal, in a thread the developer may not be tracking).

So: **deferred flags + poll at the enumerated boundaries + a self-pipe to
make a blocking read wake deterministically**, and **tactical blocking only
around genuine critical mutations** (the job table and, if a SIGCHLD reaper
lands, its data structures). Zsh's `queue_signals` idea is adopted *narrowly*
(bracket the few real critical regions), not as the global model.

### 4.3 Invariants the model must hold

1. Async handlers touch only `volatile sig_atomic_t` (single-thread) or a
   real atomic (cross-thread); never allocate, format, or take locks.
2. Signals that can be delivered to the worker thread are either blocked on
   the worker or dispatched through a cross-thread-safe channel.
3. Every blocking boundary either polls pending signals before/after or is
   woken by the self-pipe.
4. Every fork child scrubs signal state through **one** path (unify
   `reset_subshell_signals`, the `bin_command`/`env`/`exec` resets, the
   heredoc and LLE-helper forks) — dispositions and trap list, before running
   shell code or exec.
5. Every teardown/signal-path `tcsetattr` is `TCSANOW`.
6. One owner per signal disposition (kill the three-way SIGWINCH split).

---

## 5. Gap catalogue and verdicts

Severity: **H** breaks correctness or can hang/crash; **M** observable
divergence from bash/zsh; **L** latent / hygiene.

| # | Finding | Locations | Sev | Verdict |
|---|---|---|---|---|
| G1 | `trap '' SIG` resets to `SIG_DFL` instead of `SIG_IGN` | `signals.c:360-363`, `bin_trap.c:182` | H | change — **issue #408** |
| G2 | Signal-trap bodies never dispatch outside the REPL (`-c`, mid-batch, foreground wait) | `execute_pending_traps` sole caller `lush.c:300` | H | change — **issue #409** |
| G3 | `sigsegv_handler` does stdio formatting before `abort()` | `errors.c:75` | H | change — new issue |
| G4 | `current_child_pid` non-atomic read in SIGINT handler | `signals.c:36,98` | M | change — new issue |
| G5 | INT/TERM/QUIT/USR1/USR2 handlers run on the worker thread; race on `pending_trap_signals` | `signals.c:383`, worker unblocked | H | change — new issue |
| G6 | TCSAFLUSH wedge remains on SIGTSTP/SIGCONT/atexit/adaptive/base paths | `terminal_unix_interface.c:91,116,144`; `adaptive_native_controller.c:277`; `base_terminal.c:473` | H | change — new issue (same class as #404) |
| G7 | SIGPIPE never set (left `SIG_DFL` process-wide) | none in `src/` | L | **verify** — no divergence found: `yes\|head`, finite and infinite builtin writer\|head all stop correctly in both shells (`rc=0`). Disposition-hygiene note; needs a shell-process-direct-write-to-broken-pipe repro to qualify as a bug |
| G8 | SIGTERM has no interactive disposition | none | M | change (**unverified**) — `-c` is parity (both terminate, rc 143); the interactive-ignore divergence needs a PTY to confirm |
| G9 | Fork children inherit full trap list + 5/6 handlers; not reset in subshells | `reset_subshell_signals` `signals.c:184` | M | change — new issue (bash resets) |
| G10 | Three exec builtins reset signals three different ways | `bin_command.c:203-211`, `bin_env.c:233`, `bin_exec.c:211` | M | change — unify |
| G11 | Heredoc-writer + LLE-helper fork children reset nothing before exec | `redirection.c:1027,1140`; `git_command.c:129`; `completion_config.c:248` | M | change — route through unified reset |
| G12 | `executor_builtin_fg` wait lacks EINTR-retry + SIGHUP-forward | `executor.c:16841` | M | change — route through `executor_wait_foreground` semantics |
| G13 | `display_controller.c` uses `sigprocmask` (multithread-unspecified); 2 early returns leak blocked SIGWINCH | `display_controller.c:228,255,453,457,487,978` | M | change — `pthread_sigmask` + restore on all exits |
| G14 | Three SIGWINCH installers, last wins; TSTP/CONT re-arm via `signal()` drops `sa_mask` | `terminal_unix_interface.c:203`, `base_terminal.c:562` | L | change — one owner, `sigaction` everywhere |
| G15 | Trap limited to 6 signals; numeric operands for others silently no-op; `trap -l` Linux-only numbers | `signals.c:383`, `get_signal_number`, `trap -l` | M | change — table-driven, portable numbers |
| G16 | `sigwinch_received` / `raw_mode_active` plain `bool` from handler+main | `terminal_unix_interface.c:72` | L | change — atomics |
| G17 | Subshell SIGINT/SIGQUIT disposition inherited without fg/bg context | `reset_subshell_signals` (SIGINT left inherited) | M | **issue #375** |
| G18 | `set_sigsegv_handler` declared, never defined/called | `include/signals.h:128` | L | change — remove dead decl |
| G19 | No SIGCHLD reaper; `$!`/`wait -n`/job-status timing all synchronous | tree-wide | — | **research — P4, §7** |
| G20 | DEBUG/ERR/RETURN pseudo-traps not fired in subshell bodies / pipelines / chains | `executor.c` fire sites | M | separate — pseudo-trap coverage, adjacent |

Verdicts confirmed **correct — keep as-is**: SIGQUIT `SIG_IGN` (`signals.c:230`);
the SIGHUP block/unblock lifecycle and `pthread_sigmask` choice; the
`reset_signal_mask_for_exec` full-mask clear before exec; the `SA_RESTART`-off
choice for SIGHUP specifically (it *must* interrupt reads); `send_sighup_to_jobs`
group-cascade with SIGCONT.

---

## 6. Phased execution plan

Each phase is gated: do not advance until its integration tests pass 100%
deterministically under CI load. No phase here is committed by this document.

### P1 — Deferred-dispatch framework (infrastructure)

Goal: eliminate reliance on bare `EINTR`, make dispatch explicit, and make
the multithread story correct.

- Handlers touch only flags (already true except G3/G4/G5/G16 — fix those).
- Block the trappable signals on the worker thread the same way SIGHUP is,
  so all shell signals are main-thread-only (closes G5); or route them
  through the self-pipe.
- Introduce a **self-pipe** (portable; not `signalfd`) so the REPL/LLE read
  and the foreground wait can be woken deterministically instead of relying
  on `EINTR`.
- Define the canonical **safe points** and a single `run_pending_signals()`
  that dispatches both signal traps and the SIGHUP cascade: REPL top
  (exists), between statements/batches (exist for SIGHUP — generalize), and
  the `-c`/script path (missing).
- Unify fork reset (G9/G10/G11) into one `reset_subshell_signals` that all
  fork/exec sites call; add trap-list reset.
- Convert raw `signal()` sites to `sigaction` (G14); `pthread_sigmask` in the
  display layer (G13); atomics for cross-thread flags (G16).

### P2 — Trap correctness (user layer)

Goal: make `trap` conform to POSIX/bash on top of P1.

- `trap '' SIG` → `SIG_IGN`, distinct from `trap - SIG` → default; preserve a
  startup-ignored disposition across reset (G1/#408).
- Wire `execute_pending_signals` into every execution path so signal-trap
  bodies run at the next safe point on `-c`/script/mid-batch/after
  foreground wait (G2/#409).
- Table-driven signal set + portable `trap -l` numbers (G15).
- Reset non-ignored traps to default in subshells (G9).

### P3 — Subshell SIGINT/SIGQUIT context (#375)

Goal: thread foreground/background + job-control context to the fork sites so
SIGINT/SIGQUIT get the POSIX-correct disposition (fg → `SIG_DFL`, async →
`SIG_IGN` when job control is off), and address the terminal-ISIG gap for
builtin-only foreground subshells.

### P4 — SIGCHLD job model (research / recommendation only — deferred)

**Not scheduled for implementation by this plan.** The riskiest change: an
async SIGCHLD reaper rewires every wait path, including
`executor_wait_foreground`. It is explicitly **not** the fix for the PTY
hangs (those were TCSAFLUSH + the write-only flag, §1). Its real payoff is
correct `$!`, `wait -n`, and job-status *timing*, and async job-done
notification.

Design sketch to evaluate, not build: SIGCHLD handler drains with `waitpid
WNOHANG` into a lock-free/atomic ring buffer; the main loop consumes it at
safe points; `executor_wait_foreground` and the WNOHANG poller are reconciled
against that buffer so a synchronously-awaited child is not double-reaped.
Decision to schedule P4 is deferred until P1–P3 land and the interaction with
`executor_wait_foreground` is prototyped in isolation.

---

## 7. Issues this audit surfaces

Already open and folded in: **#408** (G1), **#409** (G2), **#375** (G17).

Candidate **new** issues (drafts to be reviewed before filing, per project
issue-filing practice):

- G3 — SIGSEGV handler is not async-signal-safe (stdio before `abort`).
- G4 — SIGINT handler reads a non-atomic `current_child_pid`.
- G5 — trappable signals race `pending_trap_signals` from the LLE worker
  thread.
- G6 — TCSAFLUSH wedge class remains on SIGTSTP/SIGCONT/atexit/adaptive/base
  terminal paths (same class as #404).
- G7 — SIGPIPE left `SIG_DFL` process-wide. **Not a confirmed bug** — no
  divergence found in testing; carry as a disposition-hygiene item pending a
  concrete repro, do not file yet.
- G8 — SIGTERM has no interactive disposition. **Interactive-only, unverified**
  — confirm with a PTY (`-c` is parity) before filing.
- G9/G10/G11 — fork/exec signal-reset is not unified (three exec builtins,
  heredoc and LLE-helper forks, trap-list inheritance).
- G12 — `executor_builtin_fg` wait diverges from the canonical foreground
  reap.
- G13/G14/G16 — SIGWINCH ownership, `sigprocmask`-in-multithread, and
  cross-thread `bool` flags in the terminal/display layer.
- G15 — trap signal-set coverage and non-portable `trap -l`.
- G18 — dead `set_sigsegv_handler` declaration.
- G20 — DEBUG/ERR/RETURN pseudo-trap coverage gaps (adjacent subsystem).

Several of these may consolidate (e.g. G9–G11 into one "unify fork signal
reset"; G13/G14/G16 into one "terminal-layer signal hygiene").

---

## 8. Open questions for review

1. **SIGWINCH ownership** — collapse to the LLE terminal handler and delete
   the `base_terminal.c` no-op and the `display_controller.c` masking, or
   keep masking around output and just fix `sigprocmask`→`pthread_sigmask`?
2. **Worker-thread signals** — block all trappable signals on the worker
   (simplest, main-thread-only), or deliver via the self-pipe (enables future
   worker-side handling)?
3. **SIGTERM interactive disposition** — ignore (bash-interactive) or leave
   default? Affects scripts that `kill` the shell.
4. **P4 scope** — confirm SIGCHLD reaping stays deferred until P1–P3 land, as
   recommended.
5. **`command`/`env`/`exec` reset unification** — one helper, or is a
   builtin-specific reset justified anywhere?

---

## 9. The lush signal model (proposed — for decision)

The model this audit recommends, derived from what makes lush *lush* rather
than from what bash or zsh do. The governing principle is the one lush already
applies to language (polyglot syntax → one feature engine) and to
configuration (CREG → one reactive store): **one authoritative model per
concept, legible surfaces over it, and a hard data-vs-behavior line.** Applied
to signals, it separates cleanly into policy and mechanism.

### 9.1 Policy → CREG (the lush-distinctive part)

By the CNS config-vs-code boundary (`CONFIG_NERVOUS_SYSTEM.md` §6):

- **Disposition is data.** Each signal's disposition is an enum cell
  `signal.<name>.disposition ∈ {default, ignore, handle}`, layered
  (default < mode < user < session) with provenance. A tri-state disposition
  makes `trap '' SIG` a first-class `ignore` distinct from `trap - SIG`
  (→ default), closing G1/#408; interactive SIGTERM-ignore (G8) becomes a
  mode-layer default answerable by `config explain`.
- **The trap table is data.** The signal→handler mapping is a keyed relation,
  the same category as keybindings and aliases (which §6 places in CREG).
- **The trap body is code.** The command string is a *declarative parameter*
  (the alias-value analog, §6 data) stored in the table so `trap -p` can print
  it; *executing* it at a safe point is the executor's job, never stored as
  behavior.

This yields what no shell offers: `config explain signal.sigterm.disposition`
→ *"ignore (from mode: interactive)"* — the "lush knows why" guarantee applied
to signal state. The layer is **additive and reversible**: a binding is a
cache over the real disposition, so removing the CREG surface leaves
dispositions enacting correctly — the introspection is what is lost, not
correctness. This bounds the risk of adopting it.

Polyglot surface: POSIX `trap`, a fish-style `--on-signal`, and
`display lle hook` are three syntaxes over the one binding table — consistent
with lush's polyglot identity and the existing hooks/widgets surface.

### 9.2 Mechanism → self-pipe + poll (conservative, borrowed)

- A single async-safe **front door**: the handler sets a per-signal pending
  flag and writes one byte to a **self-pipe** (write-end `O_NONBLOCK`; a
  dropped wakeup byte is harmless because the pending flag is the source of
  truth). No `signalfd` (Linux-only); the self-pipe is the portable primitive
  for both targets.
- **All trappable signals blocked on the LLE worker thread** before
  `pthread_create`, exactly as SIGHUP already is — handlers run main-thread
  only, closing G5.
- One **`run_pending_signals()`** drained at enumerated safe points; it reads
  the pending-flag set (coalesced per signal, POSIX semantics), consults the
  CREG policy, and enacts/dispatches on the main thread. Safe points: REPL top
  (exists), between statements/batches (generalize the #407 SIGHUP checks),
  after every foreground wait, and the `-c`/script path (the missing site,
  G2/#409).

### 9.3 The event system → finished in its designed scope, not amputated

Per `04_event_system_complete.md`, the LLE event system was designed as the
*line editor's* async dispatch spine (sources: terminal, timer, display,
buffer, history, completion, plugin, internal — there is **no** signal
source). It is **unfinished core, not vestigial**. It is *a consumer* of
signals within its domain (SIGWINCH → redraw, SIGCONT → re-arm, SIGINT →
abort line, surfaced via the self-pipe folded into the reader's `pselect`),
**not** the process-wide signal bus — because signals matter most where the
editor loop is not running (executor waits, `-c`, scripts). The executor is
the other consumer, draining the same front door at its safe points.

### 9.4 Invariants

1. Settings are data (CREG); behavior is code (executor); **enacting a
   disposition (`sigaction`) and fork-child reset happen on the main thread at
   safe points only.** The CREG reactive-apply path must never run post-fork —
   it allocates, which is async-unsafe and recreates the hang class. Fork
   reset stays the dumb, direct `SIG_DFL` path (unified per G9–G11).
2. One front door; two temporally-disjoint consumers (editor loop, executor) —
   never concurrent, because the main thread runs one or the other, and the
   self-pipe + flag set is the durable pending-state across the handoff.
3. Every teardown/signal-path `tcsetattr` is `TCSANOW` (G6).

### 9.5 Open decisions (resolve here before P1)

- **D1 — drain ownership across the line→execute handoff.** *Recommended:*
  one dispatcher, safe points in both contexts; no concurrency because the
  editor and executor never run simultaneously on the single main thread, and
  the pipe+flag set is durable pending-state, so "drain-all-then-dispatch at
  each safe point" is exactly-once and in-order. Confirm there is no path where
  both loops are live at once (the worker is signal-blocked, so it is not one).
  *Drain-on-transition invariant:* the reader drains immediately before
  returning the parsed line, and the executor drains immediately before running
  the first statement, so the handoff vacuum between the two loops is always
  covered by an adjacent safe point (this reuses the existing safe-point set,
  not a new mechanism).
- **D2 — coalescing / ordering.** *Recommended:* the self-pipe is a **wakeup
  only**; the authoritative pending state is a per-signal flag set read at each
  safe point, so each trapped signal fires once per drain (POSIX coalescing),
  in a fixed priority order (hangup/terminate class first). No total ordering
  with keystrokes is attempted (POSIX does not require it).
- **D3 — the trap command string.** *Recommended:* store it as a declarative
  parameter in the CREG trap table (the alias-value analog), so `trap -p`
  reads CREG; the executor parses and runs it at dispatch. Confirm the string
  is treated as data, its execution as behavior. *Execution-context guarantee:*
  the executor runs the trap body in the **primary shell execution context**,
  never in a subshell or async block, so a trap's side effects — variable
  assignments, `shift`/positional-parameter changes, `cd` — register in the
  shell as POSIX requires (a signal trap runs in the current environment). This
  complements invariant 9.4.1 (apply on the main thread) and is the correct
  behavior #409 must deliver.

### 9.6 Mapping to the phases

P1 (§6) builds the front door + `run_pending_signals` + worker-block + unified
fork reset. P2 adds the CREG policy layer (§9.1) and wires trap dispatch to the
safe points, closing G1/G2/G8. P3 (#375) and P4 (SIGCHLD, deferred) are
unchanged. The event-system completion (§9.3) is sequenced with P1 (the reader
`pselect` + front door) but its full async-dispatch build is its own LLE-scope
workstream.

---

*End of audit. No source changed. §1–§8 are the current-state audit; §9 is the
proposed model for decision. Remediation is proposed in phases (§6) and
scheduled separately after the §9.5 decisions are settled.*
