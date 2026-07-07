/**
 * @file signals.c
 * @brief Signal handling and trap management
 *
 * Implements shell signal handling including:
 * - SIGINT (Ctrl+C) handling for interactive mode
 * - SIGSEGV handler for debugging
 * - Trap command management (trap builtin)
 * - Child process signal forwarding
 * - LLE readline integration for signal handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "signals.h"

#include "executor.h"
#include "lle/adaptive_terminal_integration.h"
#include "lush.h"
#include "symtable.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/// backtrace()/backtrace_symbols_fd() live in <execinfo.h> on glibc and macOS
/// -- the two supported targets. They are not POSIX, so the crash handler's
/// backtrace is compiled only where the header exists and degrades to a plain
/// message-and-reraise elsewhere.
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define LUSH_HAVE_BACKTRACE 1
#endif

/** @brief Global trap list head */
trap_entry_t *trap_list = NULL;

/** @brief PID of currently running child process (for signal forwarding) */
static pid_t current_child_pid = 0;

/**
 * @brief Per-signal flags marking a pending trap dispatch
 *
 * pending_trap_flags[signo] is set by the signal handler (async-signal-safe: a
 * plain sig_atomic_t store) and cleared by execute_pending_traps() in the main
 * loop. One flag per signal number spans the whole platform range (NSIG),
 * including the real-time signals a fixed 32-bit bitmask could not reach.
 */
static volatile sig_atomic_t pending_trap_flags[NSIG];

/**
 * @brief Flag set when SIGINT received during readline
 *
 * Volatile because it's modified by signal handler and read by main code.
 */
static volatile sig_atomic_t sigint_received_during_readline = 0;

/**
 * @brief Check and clear the SIGINT flag
 *
 * Called by LLE to check if SIGINT was received during readline
 * and atomically clear the flag.
 *
 * @return 1 if SIGINT was received, 0 otherwise
 */
int check_and_clear_sigint_flag(void) {
    if (sigint_received_during_readline) {
        sigint_received_during_readline = 0;
        return 1;
    }
    return 0;
}

/** @brief Flag indicating LLE readline is currently active */
static volatile sig_atomic_t lle_readline_active = 0;

/**
 * @brief Set LLE readline active state
 *
 * Called by LLE to indicate when readline is active, so SIGINT
 * handler knows how to behave.
 *
 * @param active 1 if readline is active, 0 otherwise
 */
void set_lle_readline_active(int active) { lle_readline_active = active; }

/**
 * @brief SIGINT handler for interactive shell
 *
 * Properly manages shell vs child process behavior:
 * - If child process running: forward SIGINT to child
 * - If LLE readline active: set flag for LLE to handle
 * - Otherwise: print newline and set flag for main loop
 *
 * @param signo Signal number (SIGINT)
 */
static void sigint_handler(int signo) {
    (void)signo; /// Suppress unused parameter warning

    if (current_child_pid > 0) {
        /// We have an active child process - send SIGINT to it
        kill(current_child_pid, SIGINT);
    } else if (lle_readline_active) {
        /// LLE readline is active - set flag for LLE to handle
        /// LLE will check this flag in its input loop and abort the current
        /// line
        sigint_received_during_readline = 1;
        /// Don't print newline here - LLE will handle display cleanup
    } else {
        /// No active child process and not in LLE readline (GNU readline mode)
        /// Set the flag so the main loop knows this was SIGINT, not EOF
        sigint_received_during_readline = 1;
        /// Print newline to move past current input
        /// NOTE: Using write() instead of printf/fflush for
        /// async-signal-safety. No recovery path inside a signal handler -- a
        /// short/EINTR write here cannot be retried without violating
        /// async-signal-safety, so the return value is intentionally discarded.
        (void)!write(STDOUT_FILENO, "\n", 1);
    }
}

/** @brief Flag indicating shell should exit due to SIGHUP */
static volatile sig_atomic_t sighup_received = 0;

/**
 * @brief SIGHUP handler
 *
 * Sets a flag indicating the shell should exit. The actual cleanup
 * (sending SIGHUP to background jobs) is done by send_sighup_to_jobs()
 * which is called from the main exit path.
 *
 * @param signo Signal number (SIGHUP)
 */
static void sighup_handler(int signo) {
    (void)signo;
    sighup_received = 1;
}

/**
 * @brief Async-signal-safe crash handler for SIGSEGV.
 *
 * A segmentation fault leaves the process in an undefined state -- the fault
 * may have struck mid-malloc or mid-stdio, holding an allocator or FILE lock --
 * so the handler touches only async-signal-safe primitives. It writes a fixed
 * message with write() (never stdio: the old handler formatted through
 * vsprintf/fputs, which can deadlock or corrupt further), emits a best-effort
 * backtrace where available, then restores the default disposition and
 * re-raises the original signal. Re-raising, rather than the old abort(),
 * terminates the shell from SIGSEGV itself -- yielding the correct 128 +
 * SIGSEGV wait status and a core dump honoring the user's `ulimit -c`, instead
 * of masking the fault behind SIGABRT. A one-shot guard forces straight to
 * re-raise if the handler itself faults (e.g. walking a corrupted stack).
 *
 * Not handled here: a stack-overflow SIGSEGV leaves no stack to run the handler
 * on, so the kernel takes the default action directly; catching that would need
 * a sigaltstack, a separate hardening step.
 *
 * @param signo Signal number (SIGSEGV)
 */
static void sigsegv_handler(int signo) {
    static volatile sig_atomic_t handling = 0;

    if (handling) {
        /// The handler itself faulted; do no further work, just terminate.
        signal(signo, SIG_DFL);
        raise(signo);
        _exit(128 + signo);
    }
    handling = 1;

    static const char msg[] =
        "\nlush: fatal signal (segmentation fault) -- this is a bug in lush.\n"
        "Terminating; a core dump follows if enabled (see: ulimit -c).\n";
    (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);

#ifdef LUSH_HAVE_BACKTRACE
    /// backtrace_symbols_fd writes straight to the fd and is async-signal-safe;
    /// backtrace_symbols (which allocates) is not, so it is never used here.
    {
        static const char label[] = "backtrace (innermost frame first):\n";
        void *frames[64];
        int n = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
        (void)!write(STDERR_FILENO, label, sizeof(label) - 1);
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }
#endif

    /// Terminate from the original signal. The delivering signal is blocked for
    /// the duration of its own handler (set_signal_handler uses no SA_NODEFER),
    /// so the re-raised signal stays pending until this handler returns, then
    /// is delivered to SIG_DFL -- the correct wait status and a core dump.
    signal(signo, SIG_DFL);
    raise(signo);
}

/**
 * @brief Check if SIGHUP was received
 *
 * @return true if SIGHUP was received, false otherwise
 */
bool sighup_was_received(void) { return sighup_received != 0; }

/**
 * @brief Send SIGHUP to all background jobs
 *
 * Called when a login shell exits. Sends SIGHUP followed by SIGCONT
 * to all background jobs (so stopped jobs can handle SIGHUP).
 * Jobs marked with no_sighup flag (via disown -h) are skipped.
 *
 * @return Number of jobs that received SIGHUP
 */
int send_sighup_to_jobs(void) {
    executor_t *executor = get_global_executor();
    if (!executor) {
        return 0;
    }

    int count = 0;
    job_t *job = executor->jobs;

    while (job) {
        /// Skip jobs marked to not receive SIGHUP (disown -h)
        if (job->no_sighup) {
            job = job->next;
            continue;
        }

        /// Skip already-completed jobs: their child is reaped, so the leader
        /// pid may have been recycled to an unrelated process.
        if (job->state == JOB_DONE) {
            job = job->next;
            continue;
        }

        if (job->pid > 0) {
            /// Target the job's own group when it has one, else its leader pid
            /// so the hangup reaches only the job and not the shell's own
            /// group.
            pid_t target = job_target(job);
            if (kill(target, SIGHUP) == 0) {
                count++;
                /// Also send SIGCONT so stopped jobs can handle SIGHUP
                kill(target, SIGCONT);
            }
        }

        job = job->next;
    }

    return count;
}

void reset_subshell_signals(void) {
    /// The interactive shell installs caught handlers for SIGSEGV and SIGHUP
    /// (init_signal_handlers); a forked subshell that does not exec inherits
    /// them and would run interactive logic -- print a crash report, or record
    /// a hangup flag instead of terminating -- rather than behaving as a normal
    /// process (e.g. a login shell's exit-time SIGHUP would be swallowed, the
    /// bug fixed for background jobs and generalized here to every subshell).
    /// Restore the defaults so the child is terminated by a fault or a hangup.
    ///
    /// SIGINT is deliberately left as inherited (tracked in issue #375).
    /// Resetting it correctly would need foreground/background context the
    /// fork sites do not have. Job control is on by default for interactive
    /// shells (init.c), so a backgrounded subshell normally gets its own
    /// process group and is isolated from terminal SIGINT -- but when job
    /// control is off (non-interactive, or `set +m`) a backgrounded `(...) &`
    /// shares the shell's process group and a naive SIG_DFL would wrongly kill
    /// it on Ctrl-C (POSIX requires SIG_IGN there). The foreground benefit is
    /// also moot: Ctrl-C already reaches a foreground subshell's running
    /// command, and whether a builtin-only body can be interrupted is governed
    /// by terminal mode (ISIG), not signal disposition.
    set_signal_handler(SIGHUP, SIG_DFL);
    set_signal_handler(SIGSEGV, SIG_DFL);

    /// The shell blocks SIGHUP during its own startup (init_signal_handlers); a
    /// subshell forked before init reaches enable_sighup_delivery() would
    /// inherit that block. Restore normal delivery so the child is hung up like
    /// any process, not left with SIGHUP masked. (pthread_sigmask for
    /// consistency; the post-fork child is single-threaded.)
    sigset_t hup;
    sigemptyset(&hup);
    sigaddset(&hup, SIGHUP);
    pthread_sigmask(SIG_UNBLOCK, &hup, NULL);
}

/**
 * @brief Initialize default signal handlers
 *
 * Sets up signal handlers for SIGINT, SIGSEGV, SIGQUIT, and SIGHUP.
 * Called during shell initialization.
 */
void init_signal_handlers(void) {
    set_signal_handler(SIGINT, sigint_handler);
    set_signal_handler(SIGSEGV, sigsegv_handler);

    /// Ignore SIGQUIT so a stray Ctrl+\ cannot dump core on the interactive
    /// shell -- the conventional interactive-shell disposition (bash and zsh
    /// both ignore it).
    set_signal_handler(SIGQUIT, SIG_IGN);

    /// Set up SIGHUP handler for login shell hangup, then block SIGHUP for the
    /// remainder of startup. The handler only records the signal; nothing
    /// consumes that record until the main read loop. A hangup delivered while
    /// the shell is mid-init -- sourcing rc/login scripts, bringing up the line
    /// editor -- would otherwise interrupt a startup read and either leave the
    /// shell blocked in the loop's read forever or tear down a half-built
    /// shell. Holding SIGHUP pending until enable_sighup_delivery() (called at
    /// the end of init, once every dispatch path is ready to consume it) makes
    /// a hangup at any point during startup resolve to one clean exit.
    set_signal_handler(SIGHUP, sighup_handler);
    sigset_t hup;
    sigemptyset(&hup);
    sigaddset(&hup, SIGHUP);
    /// pthread_sigmask, not sigprocmask: sigprocmask's behavior is unspecified
    /// once the process is multithreaded (the LLE async worker starts later in
    /// init), and pthread_sigmask is the defined per-thread API. Blocking on
    /// the main thread here, before the worker is created, also means the
    /// worker inherits the block and SIGHUP is only ever delivered to the main
    /// thread.
    pthread_sigmask(SIG_BLOCK, &hup, NULL);
}

void enable_sighup_delivery(void) {
    /// Unblock SIGHUP at the end of startup, once the dispatch that follows
    /// (the main read loop, -c, a script) can act on it (see
    /// init_signal_handlers). Any hangup that arrived during startup is pending
    /// and is delivered synchronously here. pthread_sigmask (per-thread)
    /// because this can run after the async worker thread exists.
    sigset_t hup;
    sigemptyset(&hup);
    sigaddset(&hup, SIGHUP);
    pthread_sigmask(SIG_UNBLOCK, &hup, NULL);
}

void reset_signal_mask_for_exec(void) {
    /// A child about to exec must not inherit the SIGHUP block the shell holds
    /// during startup (init_signal_handlers). execve preserves the signal mask
    /// (only handler dispositions reset to SIG_DFL), so a program exec'd while
    /// the block is in effect -- e.g. `exec tmux` from a login rc, sourced
    /// before enable_sighup_delivery() runs -- would run with SIGHUP masked and
    /// never die on a controlling-terminal hangup. Restore the conventional
    /// empty child mask. The exec child (or, for the exec builtin, the shell
    /// process itself just before it is replaced) is single-threaded here.
    sigset_t empty;
    sigemptyset(&empty);
    pthread_sigmask(SIG_SETMASK, &empty, NULL);
}

void reset_signals_for_exec(void) {
    /// A child about to exec (or the shell process itself, for the `exec`
    /// builtin) must hand the new program a clean slate. execve resets CAUGHT
    /// handlers to SIG_DFL but LEAVES SIG_IGN as SIG_IGN, so the shell's
    /// ignored signals -- SIGQUIT, and the job-control signals
    /// SIGTSTP/SIGTTIN/SIGTTOU when job control is on -- would otherwise be
    /// inherited ignored, giving a program that cannot be quit with Ctrl-\ or
    /// stopped. Reset every disposition the shell or the line editor may have
    /// changed to the default, then clear the mask.
    ///
    /// This is the single reset for every exec path (the external-command
    /// child, the command/env/exec builtins), replacing an assortment of
    /// hand-rolled per-signal lists and mask-only resets that disagreed on
    /// coverage. Non-exec forked subshells use reset_subshell_signals()
    /// instead, which deliberately leaves SIGINT inherited (issue #375).
    static const int sigs[] = {
        SIGINT,  SIGQUIT, SIGTERM,  SIGHUP,  SIGSEGV, SIGTSTP, SIGTTIN,
        SIGTTOU, SIGCONT, SIGWINCH, SIGCHLD, SIGUSR1, SIGUSR2, SIGALRM,
    };
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        set_signal_handler(sigs[i], SIG_DFL);
    }
    reset_signal_mask_for_exec();
}

/**
 * @brief Set current child PID for signal forwarding
 *
 * Called when forking a child process so SIGINT can be forwarded.
 *
 * @param pid PID of the child process
 */
void set_current_child_pid(pid_t pid) { current_child_pid = pid; }

pid_t get_current_child_pid(void) { return current_child_pid; }

bool signal_traps_pending(void) {
    for (int signo = 1; signo < NSIG; signo++) {
        if (pending_trap_flags[signo]) {
            return true;
        }
    }
    return false;
}

int signal_first_pending_trap(void) {
    for (int signo = 1; signo < NSIG; signo++) {
        if (pending_trap_flags[signo]) {
            return signo;
        }
    }
    return 0;
}

int signal_wait_break_check(void) {
    /// A trapped signal breaks the wait; its pending bit is left set so the
    /// trap body dispatches at the next command boundary (the caller returns
    /// 128 + signo, which that boundary preserves as $?).
    int trapped = signal_first_pending_trap();
    if (trapped > 0) {
        return trapped;
    }
    /// A hangup breaks the wait; the flag stays set so the shell exits at the
    /// next boundary.
    if (sighup_was_received()) {
        return SIGHUP;
    }
    /// An interrupt breaks the wait; the flag is consumed here because the wait
    /// is the interrupt's target and there is no foreground child to forward it
    /// to.
    if (check_and_clear_sigint_flag()) {
        return SIGINT;
    }
    /// An incidental signal (a SIGWINCH resize): the caller resumes the wait.
    return 0;
}

/**
 * @brief Clear current child PID
 *
 * Called when child process exits.
 */
void clear_current_child_pid(void) { current_child_pid = 0; }

/**
 * @brief Set a signal handler using sigaction
 *
 * @param signo Signal number to handle
 * @param handler Handler function, or SIG_IGN/SIG_DFL
 * @return 0 on success, -1 on error
 */
int set_signal_handler(int signo, void(handler)(int)) {
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = handler;
    return sigaction(signo, &sigact, NULL);
}

/**
 * @brief Find trap entry for given signal
 *
 * @param signal Signal number to find trap for
 * @return Pointer to trap entry, or NULL if not found
 */
static trap_entry_t *find_trap(int signal) {
    trap_entry_t *current = trap_list;
    while (current) {
        if (current->signal == signal) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief Signal handler that defers trap commands to the main loop
 *
 * Sets pending_trap_flags[signo] so execute_pending_traps() can run the trap
 * command safely from the main loop context. This is async-signal-safe (only
 * a store to a volatile sig_atomic_t).
 *
 * Previously called system() directly, which is NOT async-signal-safe
 * and could deadlock or corrupt state.
 *
 * @param signo Signal number received
 */
static void trap_signal_handler(int signo) {
    if (signo > 0 && signo < NSIG) {
        pending_trap_flags[signo] = 1;
    }
}

/**
 * @brief Report whether a signal carries a real kernel disposition.
 *
 * A genuine, catchable signal number has a default / ignore / caught
 * disposition installed via sigaction. The pseudo-traps (EXIT == 0 and the
 * negative ERR / DEBUG / RETURN sentinels) are dispatched by the executor at
 * command boundaries and have no kernel handler; SIGKILL and SIGSTOP can never
 * be caught or ignored. The upper bound spans the full platform signal range
 * (NSIG), covering the real-time signals as well as the standard set.
 *
 * @param signal Signal or pseudo-signal number
 * @return true if the signal takes an OS-level disposition
 */
static bool signal_has_os_disposition(int signal) {
    return signal > 0 && signal < NSIG && signal != SIGKILL &&
           signal != SIGSTOP;
}

/**
 * @brief Set a trap for a signal
 *
 * Records a disposition for a signal. A non-empty command is deferred and run
 * from the main loop when the signal is next delivered. An empty command is
 * `trap '' SIG`: the signal is ignored (SIG_IGN), distinct from a NULL command,
 * which resets it to the default. Every catchable signal is honored, not a
 * hardcoded subset.
 *
 * @param signal Signal number (0 for the EXIT trap)
 * @param command Command to run; "" to ignore the signal; NULL to reset
 * @return 0 on success, -1 on error
 */
int set_trap(int signal, const char *command) {
    /// Drop any existing trap and reset the kernel disposition to the default.
    remove_trap(signal);

    /// A NULL command resets to the default -- already done by remove_trap.
    if (!command) {
        return 0;
    }

    /// Record the trap so it survives to dispatch time and appears in listings.
    trap_entry_t *new_trap = malloc(sizeof(trap_entry_t));
    if (!new_trap) {
        return -1;
    }

    new_trap->signal = signal;
    new_trap->command = strdup(command);
    if (!new_trap->command) {
        free(new_trap);
        return -1;
    }

    new_trap->next = trap_list;
    trap_list = new_trap;

    /// Install the kernel disposition for every real, catchable signal: an
    /// empty command ignores (SIG_IGN), a non-empty command defers to
    /// trap_signal_handler. Pseudo-traps and the uncatchable signals get none.
    if (signal_has_os_disposition(signal)) {
        set_signal_handler(signal,
                           command[0] == '\0' ? SIG_IGN : trap_signal_handler);
    }

    return 0;
}

/**
 * @brief Remove a trap for a signal
 *
 * Removes the trap command for the specified signal and resets
 * the signal handler to default.
 *
 * @param signal Signal number to remove trap for
 * @return 0 on success, -1 if trap not found
 */
int remove_trap(int signal) {
    trap_entry_t *current = trap_list;
    trap_entry_t *prev = NULL;

    while (current) {
        if (current->signal == signal) {
            /// Remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                trap_list = current->next;
            }

            /// Free memory
            free(current->command);
            free(current);

            /// Reset the kernel disposition to the default. Pseudo-traps and
            /// the uncatchable signals have no disposition to reset.
            if (signal_has_os_disposition(signal)) {
                set_signal_handler(signal, SIG_DFL);
            }

            return 0;
        }
        prev = current;
        current = current->next;
    }

    return -1; /// Trap not found
}

/**
 * @brief List all active traps
 *
 * Prints all currently set traps in a format suitable for
 * re-input to the shell.
 */
/**
 * @brief One entry in the signal name/number table
 */
typedef struct {
    const char *name; ///< Signal name without the SIG prefix ("TERM")
    int number;       ///< Signal number (SIGTERM)
} signal_name_entry_t;

/**
 * @brief The canonical name<->number table for kernel signals
 *
 * Every entry is guarded by `#ifdef` on its signal macro, so the table holds
 * exactly the signals the platform defines -- SIGINFO/SIGEMT on the BSDs and
 * macOS, SIGPWR/SIGSTKFLT/SIGPOLL on Linux, etc. Aliases that share a number
 * (IOT==ABRT, CLD==CHLD, POLL==IO) are listed after their canonical name so a
 * reverse lookup returns the canonical form. Forward lookup accepts either.
 *
 * This is the single source of truth for signal names in the shell:
 * get_signal_number(), signal_number_to_name(), the `trap` builtin, and the
 * `kill` builtin all resolve through it.
 */
static const signal_name_entry_t signal_table[] = {
#ifdef SIGHUP
    {   "HUP",    SIGHUP},
#endif
#ifdef SIGINT
    {   "INT",    SIGINT},
#endif
#ifdef SIGQUIT
    {  "QUIT",   SIGQUIT},
#endif
#ifdef SIGILL
    {   "ILL",    SIGILL},
#endif
#ifdef SIGTRAP
    {  "TRAP",   SIGTRAP},
#endif
#ifdef SIGABRT
    {  "ABRT",   SIGABRT},
#endif
#ifdef SIGIOT
    {   "IOT",    SIGIOT},
#endif
#ifdef SIGBUS
    {   "BUS",    SIGBUS},
#endif
#ifdef SIGFPE
    {   "FPE",    SIGFPE},
#endif
#ifdef SIGKILL
    {  "KILL",   SIGKILL},
#endif
#ifdef SIGUSR1
    {  "USR1",   SIGUSR1},
#endif
#ifdef SIGSEGV
    {  "SEGV",   SIGSEGV},
#endif
#ifdef SIGUSR2
    {  "USR2",   SIGUSR2},
#endif
#ifdef SIGPIPE
    {  "PIPE",   SIGPIPE},
#endif
#ifdef SIGALRM
    {  "ALRM",   SIGALRM},
#endif
#ifdef SIGTERM
    {  "TERM",   SIGTERM},
#endif
#ifdef SIGSTKFLT
    {"STKFLT", SIGSTKFLT},
#endif
#ifdef SIGCHLD
    {  "CHLD",   SIGCHLD},
#endif
#ifdef SIGCLD
    {   "CLD",    SIGCLD},
#endif
#ifdef SIGCONT
    {  "CONT",   SIGCONT},
#endif
#ifdef SIGSTOP
    {  "STOP",   SIGSTOP},
#endif
#ifdef SIGTSTP
    {  "TSTP",   SIGTSTP},
#endif
#ifdef SIGTTIN
    {  "TTIN",   SIGTTIN},
#endif
#ifdef SIGTTOU
    {  "TTOU",   SIGTTOU},
#endif
#ifdef SIGURG
    {   "URG",    SIGURG},
#endif
#ifdef SIGXCPU
    {  "XCPU",   SIGXCPU},
#endif
#ifdef SIGXFSZ
    {  "XFSZ",   SIGXFSZ},
#endif
#ifdef SIGVTALRM
    {"VTALRM", SIGVTALRM},
#endif
#ifdef SIGPROF
    {  "PROF",   SIGPROF},
#endif
#ifdef SIGWINCH
    { "WINCH",  SIGWINCH},
#endif
#ifdef SIGIO
    {    "IO",     SIGIO},
#endif
#ifdef SIGPOLL
    {  "POLL",   SIGPOLL},
#endif
#ifdef SIGPWR
    {   "PWR",    SIGPWR},
#endif
#ifdef SIGINFO
    {  "INFO",   SIGINFO},
#endif
#ifdef SIGEMT
    {   "EMT",    SIGEMT},
#endif
#ifdef SIGSYS
    {   "SYS",    SIGSYS},
#endif
};

#define SIGNAL_TABLE_COUNT (sizeof(signal_table) / sizeof(signal_table[0]))

const char *signal_number_to_name(int signum) {
    for (size_t i = 0; i < SIGNAL_TABLE_COUNT; i++) {
        if (signal_table[i].number == signum) {
            return signal_table[i].name;
        }
    }
    return NULL;
}

/// Number of signal columns per row in the `kill -l` / `trap -l` listing.
#define SIGNAL_LIST_COLUMNS 5

/// Print every signal the platform defines, by number and canonical name, in
/// aligned columns. lush curates one numbered form (bash lists numbered
/// columns, zsh lists bare names); the numbers make the listing self-describing
/// as a number<->name reference, shared verbatim by `kill -l` and `trap -l`.
void print_signal_list(void) {
    int col = 0;
    for (int n = 1; n < NSIG; n++) {
        const char *name = signal_number_to_name(n);
        if (!name) {
            continue; /// A number the platform leaves unnamed (a gap or RT).
        }
        printf("%2d) SIG%-9s", n, name);
        if (++col % SIGNAL_LIST_COLUMNS == 0) {
            printf("\n");
        }
    }
    if (col % SIGNAL_LIST_COLUMNS != 0) {
        printf("\n");
    }
}

/**
 * @brief Render a (pseudo-)signal number to its trap name
 *
 * Returns the trap name for pseudo-signals ("ERR", "DEBUG", "RETURN", "EXIT")
 * and the canonical signal name for real kernel signals ("TERM", "WINCH").
 * An unknown number falls back to its decimal value in a static buffer; that
 * one path aside, the returned pointer is to a string literal.
 */
static const char *trap_signal_name(int signum) {
    if (signum == 0) {
        return "EXIT";
    }
    if (signum == TRAP_PSEUDO_ERR) {
        return "ERR";
    }
    if (signum == TRAP_PSEUDO_DEBUG) {
        return "DEBUG";
    }
    if (signum == TRAP_PSEUDO_RETURN) {
        return "RETURN";
    }
    const char *name = signal_number_to_name(signum);
    if (name) {
        return name;
    }
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d", signum);
    return buf;
}

void list_traps(void) {
    trap_entry_t *current = trap_list;
    while (current) {
        printf("trap -- '%s' %s\n", current->command,
               trap_signal_name(current->signal));
        current = current->next;
    }
}

/**
 * @brief Get signal number from name
 *
 * Converts signal name (with or without SIG prefix) to number.
 * Also accepts numeric strings.
 *
 * @param signame Signal name (e.g., "INT", "SIGINT", "2")
 * @return Signal number, or -1 if not recognized
 */
int get_signal_number(const char *signame) {
    if (!signame || signame[0] == '\0') {
        return -1;
    }

    /// Numeric form ("9", "15"). 0 is the EXIT pseudo-trap; a real signal is
    /// 1..NSIG-1. Reject anything past the platform range (e.g. "9999", or "34"
    /// where the platform has no such signal) rather than recording a trap that
    /// could never be installed or delivered.
    if (signame[0] >= '0' && signame[0] <= '9') {
        int n = atoi(signame);
        if (n < 0 || n >= NSIG) {
            return -1;
        }
        return n;
    }

    /// EXIT and the trap pseudo-signals never carry a SIG prefix. They are not
    /// real kernel signals; the executor dispatches them at well-known points
    /// (after a command for ERR, before a command for DEBUG, on function return
    /// for RETURN, at shell exit for EXIT).
    if (strcmp(signame, "EXIT") == 0) {
        return 0;
    }
    if (strcmp(signame, "ERR") == 0) {
        return TRAP_PSEUDO_ERR;
    }
    if (strcmp(signame, "DEBUG") == 0) {
        return TRAP_PSEUDO_DEBUG;
    }
    if (strcmp(signame, "RETURN") == 0) {
        return TRAP_PSEUDO_RETURN;
    }

    /// Real signal name, with or without the SIG prefix ("TERM" or "SIGTERM").
    const char *name = signame;
    if (strncmp(name, "SIG", 3) == 0) {
        name += 3;
    }
    for (size_t i = 0; i < SIGNAL_TABLE_COUNT; i++) {
        if (strcmp(name, signal_table[i].name) == 0) {
            return signal_table[i].number;
        }
    }

    return -1; /// Unknown signal
}

/**
 * @brief Execute any pending trap commands deferred from signal handlers
 *
 * Checks the pending_trap_flags array and runs the corresponding
 * trap commands. Must be called from the main loop (not signal context)
 * so that system() and other non-async-signal-safe functions are safe.
 *
 * Clears each bit after executing the trap.
 */
/* Execute a trap command in the CURRENT shell, not a /bin/sh subshell.
 * Routing through the global executor preserves the user-defined
 * functions, variables, aliases, and shell options that the trap body
 * almost always references (real_world/posix/106 cleanup function was
 * invisible to `system()`-spawned /bin/sh -> "cleanup: command not
 * found"). Falls back to system() only if the executor is unavailable
 * (very early startup or after teardown). */
static void run_trap_command(const char *command) {
    if (!command || !*command) {
        return;
    }
    executor_t *exec = get_global_executor();
    if (exec) {
        (void)executor_execute_command_line(exec, command, 1);
    } else {
        (void)!system(command);
    }
}

/**
 * @brief Execute the registered ERR trap, if any
 *
 * Looks up the TRAP_PSEUDO_ERR entry in trap_list. Empty command
 * strings (the "trap '' ERR" no-op form) are skipped. Safe to call
 * with no ERR trap registered -- a quick lookup and return.
 */
void fire_err_trap(void) {
    /// errtrace (`set -o errtrace` / `-E`) gates ERR-trap inheritance into
    /// function bodies: when it is OFF and execution is inside a function
    /// scope, the shell's ERR trap is suppressed; when it is ON, the trap
    /// follows execution into nested contexts. errtrace is a bash-originated
    /// trace option with no zsh equivalent, so lush provides it under the
    /// established name and semantics. The check is one-sided: if errtrace is
    /// on, or we are at top-level scope, the trap fires normally.
    if (!shell_opts.errtrace &&
        symtable_in_function_scope(symtable_manager())) {
        return;
    }

    trap_entry_t *trap = find_trap(TRAP_PSEUDO_ERR);
    if (trap && trap->command && trap->command[0] != '\0') {
        run_trap_command(trap->command);
    }
}

/**
 * @brief Execute the registered DEBUG trap, if any
 *
 * functrace (`set -o functrace` / `-T`) gates DEBUG and RETURN trap
 * inheritance into function bodies: without it, DEBUG fires only at
 * top-level scope (not inside functions); with it, DEBUG follows
 * execution into nested contexts. Like errtrace, functrace is a
 * bash-originated trace option with no zsh equivalent that lush provides
 * under the established name. Mirrors fire_err_trap's gating shape.
 */
void fire_debug_trap(void) {
    if (!shell_opts.functrace &&
        symtable_in_function_scope(symtable_manager())) {
        return;
    }

    trap_entry_t *trap = find_trap(TRAP_PSEUDO_DEBUG);
    if (trap && trap->command && trap->command[0] != '\0') {
        run_trap_command(trap->command);
    }
}

/**
 * @brief Execute the registered RETURN trap, if any
 *
 * Fires when a function returns. Gated by functrace the same way
 * DEBUG is: without `set -o functrace`, RETURN is suppressed for
 * function-scope returns. Top-level returns (from sourced files at
 * the top-level shell) fire normally.
 */
void fire_return_trap(void) {
    if (!shell_opts.functrace &&
        symtable_in_function_scope(symtable_manager())) {
        return;
    }

    trap_entry_t *trap = find_trap(TRAP_PSEUDO_RETURN);
    if (trap && trap->command && trap->command[0] != '\0') {
        run_trap_command(trap->command);
    }
}

void execute_pending_traps(void) {
    if (!signal_traps_pending()) {
        return;
    }

    /// A signal trap is transparent to $?: save the status the surrounding
    /// script observes and restore it after the trap body, so a firing trap
    /// (e.g. `trap false USR1`) does not clobber $? for the following commands.
    /// POSIX leaves a trap's effect on $? unspecified; transparency is the
    /// least-surprising choice (bash and zsh both preserve $? across a trap --
    /// the consensus). The trap's own side effects on variables, cwd, etc.
    /// still register because it runs in the primary shell context.
    executor_t *exec = get_global_executor();
    int saved_status = last_exit_status;

    for (int signo = 1; signo < NSIG; signo++) {
        if (pending_trap_flags[signo]) {
            /// Clear before running: a signal re-delivered during its own trap
            /// body re-arms the flag for the next dispatch rather than being
            /// lost.
            pending_trap_flags[signo] = 0;
            trap_entry_t *trap = find_trap(signo);
            if (trap && trap->command && trap->command[0] != '\0') {
                /// Run a copy of the command: a self-modifying trap -- one
                /// whose body runs `trap - SIG` or `trap other SIG` for its own
                /// signal
                /// -- frees this exact string via remove_trap while it is still
                /// in use, so hand run_trap_command an independent copy.
                char *cmd = strdup(trap->command);
                if (cmd) {
                    run_trap_command(cmd);
                    free(cmd);
                }
            }
        }
        /// Stop once a trap has requested shell exit (`exit`, or a POSIX abort
        /// like ${var:?word} / set -u): no further trap bodies should run.
        if (exit_flag || (exec && exec->shell_exit_requested)) {
            break;
        }
    }

    /// Restore $? -- unless a trap requested exit, whose chosen status must
    /// stand rather than be overwritten with the pre-trap value.
    if (!exit_flag && !(exec && exec->shell_exit_requested)) {
        set_exit_status(saved_status);
    }
}

/**
 * @brief Execute EXIT traps and cleanup
 *
 * Executes any trap set for signal 0 (EXIT) and resets the
 * terminal to a clean state.
 */
void execute_exit_traps(void) {
    trap_entry_t *trap = find_trap(0); /// EXIT is signal 0
    if (trap && trap->command && trap->command[0] != '\0') {
        /// Run in the current shell via the global executor so user
        /// functions, variables, and options are in scope. Exit-trap
        /// status is not propagated. An empty command (`trap '' EXIT`)
        /// records an ignore entry that fires nothing, so skip it.
        run_trap_command(trap->command);
    }

    /// Reset terminal to clean state on exit
    lle_terminal_reset();
}
