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

#include "errors.h"
#include "executor.h"
#include "lle/adaptive_terminal_integration.h"
#include "lush.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Global trap list head */
trap_entry_t *trap_list = NULL;

/** @brief PID of currently running child process (for signal forwarding) */
static pid_t current_child_pid = 0;

/**
 * @brief Bitmask of signals with pending trap execution
 *
 * Set by the signal handler (async-signal-safe), cleared by
 * execute_pending_traps() in the main loop. Only signals that
 * have traps registered will set bits here.
 *
 * Bit positions correspond to signal numbers (e.g., bit 2 = SIGINT).
 */
static volatile sig_atomic_t pending_trap_signals = 0;

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
    (void)signo; // Suppress unused parameter warning

    if (current_child_pid > 0) {
        // We have an active child process - send SIGINT to it
        kill(current_child_pid, SIGINT);
    } else if (lle_readline_active) {
        // LLE readline is active - set flag for LLE to handle
        // LLE will check this flag in its input loop and abort the current line
        sigint_received_during_readline = 1;
        // Don't print newline here - LLE will handle display cleanup
    } else {
        // No active child process and not in LLE readline (GNU readline mode)
        // Set the flag so the main loop knows this was SIGINT, not EOF
        sigint_received_during_readline = 1;
        // Print newline to move past current input
        // NOTE: Using write() instead of printf/fflush for async-signal-safety
        write(STDOUT_FILENO, "\n", 1);
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
 * @brief Check if SIGHUP was received
 *
 * @return true if SIGHUP was received, false otherwise
 */
bool sighup_was_received(void) {
    return sighup_received != 0;
}

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
        // Skip jobs marked to not receive SIGHUP (disown -h)
        if (job->no_sighup) {
            job = job->next;
            continue;
        }

        if (job->pgid > 0) {
            // Send SIGHUP to the process group
            if (kill(-job->pgid, SIGHUP) == 0) {
                count++;
                // Also send SIGCONT so stopped jobs can handle SIGHUP
                kill(-job->pgid, SIGCONT);
            }
        }

        job = job->next;
    }

    return count;
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

    // Ignore SIGQUIT (Ctrl+\) like bash/zsh do
    // This prevents accidental core dumps from Ctrl+\ keypresses
    set_signal_handler(SIGQUIT, SIG_IGN);

    // Set up SIGHUP handler for login shell hangup
    set_signal_handler(SIGHUP, sighup_handler);
}

/**
 * @brief Set current child PID for signal forwarding
 *
 * Called when forking a child process so SIGINT can be forwarded.
 *
 * @param pid PID of the child process
 */
void set_current_child_pid(pid_t pid) { current_child_pid = pid; }

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
 * Sets a bit in pending_trap_signals so execute_pending_traps() can
 * run the trap command safely from the main loop context. This is
 * async-signal-safe (only sets a volatile sig_atomic_t).
 *
 * Previously called system() directly, which is NOT async-signal-safe
 * and could deadlock or corrupt state.
 *
 * @param signo Signal number received
 */
static void trap_signal_handler(int signo) {
    if (signo > 0 && signo < 32) {
        pending_trap_signals |= (1 << signo);
    }
}

/**
 * @brief Set a trap for a signal
 *
 * Associates a command string with a signal. When the signal is
 * received, the command will be executed.
 *
 * @param signal Signal number (0 for EXIT trap)
 * @param command Command string to execute (NULL or empty to remove)
 * @return 0 on success, -1 on error
 */
int set_trap(int signal, const char *command) {
    // Remove existing trap for this signal
    remove_trap(signal);

    if (!command || strlen(command) == 0) {
        // Empty command means remove trap (already done above)
        return 0;
    }

    // Create new trap entry
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

    // Add to list
    new_trap->next = trap_list;
    trap_list = new_trap;

    // Set the signal handler
    if (signal == SIGINT || signal == SIGTERM || signal == SIGQUIT ||
        signal == SIGHUP || signal == SIGUSR1 || signal == SIGUSR2) {
        set_signal_handler(signal, trap_signal_handler);
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
            // Remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                trap_list = current->next;
            }

            // Free memory
            free(current->command);
            free(current);

            // Reset signal handler to default (but not for EXIT trap which is
            // special)
            if (signal != 0) {
                set_signal_handler(signal, SIG_DFL);
            }

            return 0;
        }
        prev = current;
        current = current->next;
    }

    return -1; // Trap not found
}

/**
 * @brief List all active traps
 *
 * Prints all currently set traps in a format suitable for
 * re-input to the shell.
 */
void list_traps(void) {
    trap_entry_t *current = trap_list;
    while (current) {
        printf("trap -- '%s' %d\n", current->command, current->signal);
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
    if (!signame) {
        return -1;
    }

    // Handle numeric signals
    if (signame[0] >= '0' && signame[0] <= '9') {
        return atoi(signame);
    }

    // Handle signal names (with or without SIG prefix)
    if (strcmp(signame, "INT") == 0 || strcmp(signame, "SIGINT") == 0) {
        return SIGINT;
    }
    if (strcmp(signame, "TERM") == 0 || strcmp(signame, "SIGTERM") == 0) {
        return SIGTERM;
    }
    if (strcmp(signame, "QUIT") == 0 || strcmp(signame, "SIGQUIT") == 0) {
        return SIGQUIT;
    }
    if (strcmp(signame, "HUP") == 0 || strcmp(signame, "SIGHUP") == 0) {
        return SIGHUP;
    }
    if (strcmp(signame, "USR1") == 0 || strcmp(signame, "SIGUSR1") == 0) {
        return SIGUSR1;
    }
    if (strcmp(signame, "USR2") == 0 || strcmp(signame, "SIGUSR2") == 0) {
        return SIGUSR2;
    }
    if (strcmp(signame, "EXIT") == 0) {
        return 0; // Special case for EXIT trap
    }

    return -1; // Unknown signal
}

/**
 * @brief Execute any pending trap commands deferred from signal handlers
 *
 * Checks the pending_trap_signals bitmask and runs the corresponding
 * trap commands. Must be called from the main loop (not signal context)
 * so that system() and other non-async-signal-safe functions are safe.
 *
 * Clears each bit after executing the trap.
 */
void execute_pending_traps(void) {
    sig_atomic_t pending = pending_trap_signals;
    if (pending == 0) {
        return;
    }

    /* Clear all pending bits before executing (if a new signal arrives
     * during execution, it will set the bit again for next iteration) */
    pending_trap_signals = 0;

    for (int signo = 1; signo < 32; signo++) {
        if (pending & (1 << signo)) {
            trap_entry_t *trap = find_trap(signo);
            if (trap && trap->command) {
                system(trap->command);
            }
        }
    }
}

/**
 * @brief Execute EXIT traps and cleanup
 *
 * Executes any trap set for signal 0 (EXIT) and resets the
 * terminal to a clean state.
 */
void execute_exit_traps(void) {
    trap_entry_t *trap = find_trap(0); // EXIT is signal 0
    if (trap && trap->command) {
        // Execute the EXIT trap command
        // For now, we'll use system() - this could be improved to use the
        // shell's executor
        system(trap->command);
    }

    // Reset terminal to clean state on exit
    lle_terminal_reset();
}
