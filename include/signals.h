/**
 * @file signals.h
 * @brief Signal handling and trap management
 *
 * Provides signal handler setup, trap command management, and coordination
 * between the shell and LLE for proper interrupt handling.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdbool.h>
#include <sys/types.h>

/**
 * @brief Trap entry for signal handling
 *
 * Links a signal number to a command string that should be
 * executed when the signal is received.
 */
typedef struct trap_entry {
    int signal;              /**< Signal number */
    char *command;           /**< Command to execute on signal */
    struct trap_entry *next; /**< Next trap in linked list */
} trap_entry_t;

/**
 * @brief Global list of registered traps
 */
extern trap_entry_t *trap_list;

/**
 * @brief Set a signal handler function
 *
 * @param signum Signal number to handle
 * @param handler Signal handler function
 * @return 0 on success, -1 on error
 */
int set_signal_handler(int signum, void (*handler)(int));

/**
 * @brief Initialize all signal handlers
 *
 * Sets up default signal handlers for the shell including
 * SIGINT, SIGTERM, SIGCHLD, and others.
 */
void init_signal_handlers(void);

/**
 * @brief Set the SIGINT (Ctrl+C) handler
 *
 * Configures handling of interrupt signals for interactive use.
 */
void set_sigint_handler(void);

/**
 * @brief Set the SIGSEGV handler
 *
 * Configures segmentation fault handling for debugging.
 */
void set_sigsegv_handler(void);

/**
 * @brief Set the current child process PID
 *
 * Records the PID of a foreground child process for signal forwarding.
 *
 * @param pid Process ID of the child
 */
void set_current_child_pid(pid_t pid);

/**
 * @brief Clear the current child process PID
 *
 * Clears the recorded child PID after the child terminates.
 */
void clear_current_child_pid(void);

/**
 * @brief Set LLE readline active state
 *
 * Coordinates SIGINT handling between the shell and LLE.
 * When LLE readline is active, SIGINT should interrupt editing
 * rather than the shell.
 *
 * @param active 1 when entering lle_readline, 0 when exiting
 */
void set_lle_readline_active(int active);

/**
 * @brief Check and clear the SIGINT received flag
 *
 * Checks if a SIGINT was received and clears the flag.
 * Used by LLE to detect interrupts during line editing.
 *
 * @return 1 if SIGINT was received, 0 otherwise
 */
int check_and_clear_sigint_flag(void);

/**
 * @brief Set a trap for a signal
 *
 * Associates a command with a signal number. When the signal
 * is received, the command will be executed.
 *
 * @param signal Signal number
 * @param command Command string to execute (NULL to reset to default)
 * @return 0 on success, -1 on error
 */
int set_trap(int signal, const char *command);

/**
 * @brief Remove a trap for a signal
 *
 * Removes any trap associated with the specified signal,
 * restoring default handling.
 *
 * @param signal Signal number
 * @return 0 on success, -1 on error
 */
int remove_trap(int signal);

/**
 * @brief List all defined traps
 *
 * Prints all currently defined signal traps to stdout.
 */
void list_traps(void);

/**
 * @brief Convert a signal name to its number
 *
 * Converts signal names like "INT", "TERM", "HUP" to their
 * corresponding numeric values.
 *
 * @param signame Signal name (with or without "SIG" prefix)
 * @return Signal number, or -1 if not found
 */
int get_signal_number(const char *signame);

/**
 * @brief Execute pending trap commands deferred from signal handlers
 *
 * Signal handlers set a bitmask instead of calling system() directly.
 * This function runs the deferred trap commands safely from main loop
 * context. Should be called at the top of each REPL iteration.
 */
void execute_pending_traps(void);

/**
 * @brief Execute all EXIT traps
 *
 * Runs any commands registered for the EXIT (0) pseudo-signal.
 * Called during shell shutdown.
 */
void execute_exit_traps(void);

/**
 * @brief Check if SIGHUP was received
 *
 * Used by the main loop to detect when the shell should exit
 * due to a hangup signal.
 *
 * @return true if SIGHUP was received, false otherwise
 */
bool sighup_was_received(void);

/**
 * @brief Send SIGHUP to all background jobs
 *
 * Called when a login shell exits. Sends SIGHUP followed by SIGCONT
 * to all background jobs (so stopped jobs can handle SIGHUP).
 * Jobs marked with no_sighup flag (via disown -h) are skipped.
 *
 * @return Number of jobs that received SIGHUP
 */
int send_sighup_to_jobs(void);

#endif
