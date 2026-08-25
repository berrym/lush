/**
 * @file builtins.h
 * @brief Shell builtin command declarations
 *
 * Declares all shell builtin commands and the builtin dispatch table
 * for command lookup and execution.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef BUILTINS_H
#define BUILTINS_H

/* Universal builtin contract.
 *
 * Every src/builtins/bin_<name>.c includes this header. The headers
 * pulled in here are the ones every builtin needs: structured-error
 * reporting, the executor + current_executor + executor_error_report
 * surface, and the C stdlib basics that ~every builtin uses. Per-file
 * includes in bin_<name>.c list only their own non-universal
 * dependencies (lush.h, symtable.h, <errno.h>, etc.).
 *
 * Threshold for inclusion here was ~50/56 usage across the bin_*.c
 * files at split time. Headers used by smaller subsets stay per-file. */
#include "executor.h"
#include "ht.h"
#include "shell_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Atomically replace the stashed builtin call-site source location
 *
 * Called by the executor's builtin dispatch path on entry (with the new
 * call site's loc) and again on exit (with the previous loc returned
 * here on entry) so re-entrant builtin dispatch -- e.g. `eval` invoking
 * another builtin -- preserves the outer caller's loc when the inner
 * call returns. The stashed location is read by the builtin error
 * helpers so structured-error output gets a real `--> file:line:col`
 * line and source-snippet caret span.
 *
 * Usage idiom in the dispatcher:
 *   source_location_t saved = builtin_swap_source_location(command_loc);
 *   int result = <dispatch builtin>;
 *   (void)builtin_swap_source_location(saved);
 *   return result;
 *
 * @param loc New source location to stash
 * @return Previous stashed source location
 */
/**
 * @brief Write a value to an assignable target that addresses an array element.
 *
 * Returns false when @p target is a plain variable name, leaving the caller to
 * handle it; returns true when it addressed `name[subscript]`, with the
 * builtin status in @p out_rc. Shared so `printf -v`, `declare` and `read`
 * cannot drift from the assignment surfaces on subscript arithmetic, the index
 * base, negative offsets, or associative key canonicalization (#643, #798).
 */
/**
 * @brief Does this target address an array element rather than name a variable?
 */
bool builtin_is_element_target(const char *target);

bool builtin_assign_element_target(const char *target, const char *value,
                                   int *out_rc);

source_location_t builtin_swap_source_location(source_location_t loc);

/**
 * @brief Read the current builtin call-site source location
 *
 * Returns the location stashed by builtin_swap_source_location (the
 * dispatcher's entry path). When no stash is active, falls back to the
 * topmost frame on the executor's context-location stack, then to
 * SOURCE_LOC_UNKNOWN. Use this from a builtin's error sites to obtain
 * the loc to pass into shell_error_create / executor_error_report.
 *
 * @return Current source_location_t for the active builtin invocation
 */
source_location_t builtin_get_source_location(void);

/**
 * @brief Print the directory stack after a pushd/popd unless silenced
 *
 * pushd and popd echo the resulting stack after each operation. The zsh
 * `pushd_silent` option suppresses that automatic report; the explicit
 * `dirs` command is unaffected. Shared by bin_pushd and bin_popd so the
 * gate lives in one place.
 */
void builtin_report_dirstack(void);

/**
 * @brief Create an array from a parenthesized array-literal and bind it
 *
 * Parses `(elem1 elem2 [k]=v ...)` (as carried by the parser's `\x1F`
 * `name=(...)` sentinel), creates an indexed or associative array, binds it
 * to @p name, and applies @p flags (SYMVAR_EXPORTED / SYMVAR_READONLY). When
 * @p literal does not begin with '(', an empty array is bound. Shared by
 * `declare`/`local`, and by `export`/`readonly` in the section 3.9 relaxed
 * modes, so array-literal creation has a single definition.
 *
 * @param name    Variable name to bind
 * @param literal Value string; parsed as a literal when it starts with '('
 * @param assoc   true for an associative array, false for indexed
 * @param flags   Attribute flags to add (SYMVAR_NONE for none)
 * @param append  true for the `name+=(...)` form: extend the existing array
 *                (creating it if absent) instead of replacing it
 * @param global  true for `declare -g`/`typeset -g`: force a newly created
 *                array into the global scope; false keeps it in the current
 *                scope (function-local inside a function, matching bash)
 * @return 0 on success, 1 on error (diagnostic already reported)
 */
int builtin_bind_array_literal(const char *name, const char *literal,
                               bool assoc, symvar_flags_t flags, bool append,
                               bool global);

/**
 * @brief Detect and strip the `+=` array-append marker from a sentinel name
 *
 * The parser's `\x1F name+=(...)` array-append sentinel leaves the name
 * segment ending in `+` (the value/name split lands on the `=` of `+=`).
 * If @p name ends with `+`, remove it in place and return true; otherwise
 * return false. Shared by declare/local/export/readonly so the append form
 * is recognized identically, before identifier validation. Only call on a
 * name that came from the array-literal sentinel.
 *
 * @param name Mutable name buffer (modified in place when it ends with '+')
 * @return true if the append marker was present and stripped
 */
bool builtin_array_name_is_append(char *name);

/**
 * @brief Shared implementation for `break` and `continue`
 *
 * Both builtins parse an optional positive-integer level, validate that
 * we are inside a loop, that the level does not exceed the current
 * nesting depth, and then set the executor's loop_control to the
 * requested value. The only differences between them are the verb used
 * in error messages and the loop_control value -- bin_break and
 * bin_continue are thin wrappers around this.
 *
 * @param argc argc as received by the builtin
 * @param argv argv as received by the builtin (argv[1] is the optional level)
 * @param verb "break" or "continue", used in error / suggestion text
 * @param ctl  LOOP_BREAK or LOOP_CONTINUE, the value to set on success
 * @return 0 on success, 1 on any error (already reported via shell_error_*)
 */
int builtin_loop_control(int argc, char **argv, const char *verb,
                         loop_control_t ctl);

/** Builtin command entry */
typedef struct builtin_s {
    const char *name;                   ///< Command name
    const char *doc;                    ///< Help documentation
    int (*func)(int argc, char **argv); ///< Handler function
} builtin;

/** Array of all builtin commands */
extern builtin builtins[];

/** Number of builtin commands */
extern const size_t builtins_count;

/* ============================================================================
 * Builtin Command Handlers
 * ============================================================================
 */

/**
 * @brief Exit the shell
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status (does not return on success)
 */
int bin_exit(int argc, char **argv);

/**
 * @brief Terminate a login shell
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status, or 1 if invoked outside a login shell
 */
int bin_logout(int argc, char **argv);

/**
 * @brief Display help information
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_help(int argc, char **argv);

/**
 * @brief Change current working directory
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_cd(int argc, char **argv);

/**
 * @brief Print current working directory
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_pwd(int argc, char **argv);

/**
 * @brief Display or manipulate command history
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_history(int argc, char **argv);

/**
 * @brief Define or display aliases
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_alias(int argc, char **argv);

/**
 * @brief Remove alias definitions
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_unalias(int argc, char **argv);

/**
 * @brief Clear the terminal screen
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_clear(int argc, char **argv);

/**
 * @brief Display terminal information
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_terminal(int argc, char **argv);

/**
 * @brief Show command type (builtin, alias, function, or external)
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_type(int argc, char **argv);

/**
 * @brief Unset shell variables or functions
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_unset(int argc, char **argv);

/**
 * @brief Echo arguments to standard output
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_echo(int argc, char **argv);

/**
 * @brief Formatted output to standard output
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_printf(int argc, char **argv);

/**
 * @brief Zsh-style print: -l/-n/-r/-u/-f/-P
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_print(int argc, char **argv);

/**
 * @brief Export variables to environment
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_export(int argc, char **argv);

/**
 * @brief Source and execute a script file
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status of sourced script
 */
int bin_source(int argc, char **argv);

/**
 * @brief Test file types and compare values
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 if condition is true, 1 if false, 2 on error
 */
int bin_test(int argc, char **argv);

/**
 * @brief Read a line from standard input
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on EOF or error
 */
int bin_read(int argc, char **argv);

/**
 * @brief Evaluate arguments as a shell command
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status of evaluated command
 */
int bin_eval(int argc, char **argv);

/**
 * @brief Return successful exit status
 * @param argc Argument count
 * @param argv Argument vector
 * @return Always returns 0
 */
int bin_true(int argc, char **argv);

/**
 * @brief Return failure exit status
 * @param argc Argument count
 * @param argv Argument vector
 * @return Always returns 1
 */
int bin_false(int argc, char **argv);

/**
 * @brief Bind a completion function to one or more command names.
 *
 * `compdef FN CMD [CMD2 ...]` records that CMD's completion is
 * generated by calling FN. `compdef -d CMD` removes a binding.
 * `compdef` with no arguments lists every binding.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_compdef(int argc, char **argv);

/**
 * @brief Emit completion candidates from inside a compdef-bound function.
 *
 * Errors with rc=1 when called outside a completion-function call.
 * See src/builtins/bin_compadd.c for flag details (-d, -a, -k, --).
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_compadd(int argc, char **argv);

/**
 * @brief Set or display shell options and positional parameters
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_set(int argc, char **argv);

/**
 * @brief Shift positional parameters
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_shift(int argc, char **argv);

/**
 * @brief Break out of a loop
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_break(int argc, char **argv);

/**
 * @brief Continue to next iteration of a loop
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_continue(int argc, char **argv);

/**
 * @brief Return from a function
 * @param argc Argument count
 * @param argv Argument vector
 * @return Specified return value or last command status
 */
int bin_return(int argc, char **argv);

/**
 * @brief Set signal traps
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_trap(int argc, char **argv);

/**
 * @brief Replace shell with specified command
 * @param argc Argument count
 * @param argv Argument vector
 * @return Does not return on success, non-zero on error
 */
int bin_exec(int argc, char **argv);

/**
 * @brief Wait for background jobs to complete
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status of waited job
 */
int bin_wait(int argc, char **argv);

/**
 * @brief `kill` builtin -- send a signal to jobs and processes
 *
 * @param argc Argument count
 * @param argv Arguments: [-s SIGNAL | -SIGNAL] target... or -l [name|number]...
 * @return 0 on success, non-zero if any target or option was invalid
 */
int bin_kill(int argc, char **argv);

/**
 * @brief Set or display file creation mask
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_umask(int argc, char **argv);

/**
 * @brief Set or display resource limits
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_ulimit(int argc, char **argv);

/**
 * @brief Display process times
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success
 */
int bin_times(int argc, char **argv);

/**
 * @brief Parse command options
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero when no more options
 */
int bin_getopts(int argc, char **argv);

/**
 * @brief Declare local variables in function scope
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_local(int argc, char **argv);

/**
 * @brief Declare variables with attributes
 *
 * Declare variables and give them attributes like arrays (-a, -A),
 * integers (-i), or readonly (-r). Also aliased as 'typeset'.
 *
 * Options:
 *   -a  Declare indexed array
 *   -A  Declare associative array
 *   -i  Declare integer variable
 *   -r  Declare readonly variable
 *   -x  Export variable
 *   -p  Display attributes and values
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_declare(int argc, char **argv);

/**
 * @brief Manage command hash table
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_hash(int argc, char **argv);

/**
 * @brief Fix command - edit and re-execute history commands
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status of executed command
 */
int bin_fc(int argc, char **argv);

/**
 * @brief Debug commands and settings
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_debug(int argc, char **argv);

/**
 * @brief Execute command bypassing shell functions and builtins
 *
 * POSIX command builtin. With no options, executes the specified command
 * bypassing any shell functions, aliases, or builtins of the same name.
 *
 * Options:
 *   -v  Print command path (like 'which')
 *   -V  Print verbose command description (like 'type')
 *   -p  Use default PATH for command search
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Command exit status, or 127 if command not found
 */
int bin_command(int argc, char **argv);

/**
 * @brief Push directory onto stack and change to it
 *
 * Options:
 *   pushd [dir]  - Push current dir, cd to dir
 *   pushd +N     - Rotate stack so Nth entry is at top
 *   pushd -N     - Rotate stack so Nth from bottom is at top
 *   pushd        - Exchange top two stack entries
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_pushd(int argc, char **argv);

/**
 * @brief Pop directory from stack and change to it
 *
 * Options:
 *   popd         - Pop top entry and cd there
 *   popd +N      - Remove Nth entry from top
 *   popd -N      - Remove Nth entry from bottom
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_popd(int argc, char **argv);

/**
 * @brief Display directory stack
 *
 * Options:
 *   dirs         - Display stack on one line
 *   dirs -p      - Print one entry per line
 *   dirs -v      - Print with stack index numbers
 *   dirs -c      - Clear the stack
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_dirs(int argc, char **argv);

/* ============================================================================
 * Additional builtin declarations
 * ============================================================================
 *
 * These declarations are public so each builtin can live in its own
 * `src/builtins/bin_<name>.c` file and the registry table in
 * `src/builtins/builtins.c` can reference them without per-file forward
 * declarations.
 */

int bin_jobs(int argc, char **argv);
int bin_fg(int argc, char **argv);
int bin_bg(int argc, char **argv);
int bin_colon(int argc, char **argv);
int bin_readonly(int argc, char **argv);
int bin_config(int argc, char **argv);
int bin_setopt(int argc, char **argv);
int bin_unsetopt(int argc, char **argv);
int bin_shopt(int argc, char **argv);
int bin_mode(int argc, char **argv);
int bin_display(int argc, char **argv);
int bin_network(int argc, char **argv);
int bin_mapfile(int argc, char **argv);
int bin_env(int argc, char **argv);
int bin_analyze(int argc, char **argv);
int bin_lint(int argc, char **argv);
int bin_disown(int argc, char **argv);
int bin_let(int argc, char **argv);

/// zsh compatibility stubs (silent no-ops). See bin_zsh_stubs.c for the
/// rationale: scripts that call these at top level don't break, the
/// zsh-specific bookkeeping these would perform is invisible from a
/// non-interactive script's signal surface.
int bin_bindkey(int argc, char **argv);
int bin_autoload(int argc, char **argv);
int bin_zmodload(int argc, char **argv);
int bin_emulate(int argc, char **argv);
int bin_colors(int argc, char **argv);
int bin_zle(int argc, char **argv);

/// bash-completion compatibility stubs. See bin_zsh_stubs.c for the
/// silent-no-op rationale; full record-and-query parity is a future
/// implementation parallel to bindkey/zle.
int bin_complete(int argc, char **argv);
int bin_compgen(int argc, char **argv);
int bin_compopt(int argc, char **argv);
int bin_zstyle(int argc, char **argv);
int bin_compinit(int argc, char **argv);
int bin_bashcompinit(int argc, char **argv);
int bin_unfunction(int argc, char **argv);

/**
 * @brief Validate a string as a shell variable identifier.
 *
 * Returns 1 if `name` is non-NULL, non-empty, starts with letter or
 * underscore, and contains only alphanumerics or underscores after.
 * Otherwise 0. Shared by every builtin that accepts identifier
 * arguments (declare, local, export, readonly, unset, ...).
 */
int is_valid_identifier(const char *name);

/* ============================================================================
 * Command Hash Table
 * ============================================================================
 */

/** @brief Initialize the command hash table */
void init_command_hash(void);

/** @brief Free the command hash table */
void free_command_hash(void);

/**
 * @brief Clear the negative PATH-search cache.
 *
 * The negative cache short-circuits repeated find_command_in_path()
 * lookups for missing commands within a TTL window
 * (behavior.path_negative_cache_ttl_ms). `hash -r` clears it alongside
 * the positive command_hash so a freshly-installed binary is picked up
 * immediately instead of waiting for the TTL to expire.
 */
void path_negative_cache_clear(void);

/**
 * @brief Check if a command is a builtin
 *
 * Searches the builtins table for a matching command name.
 *
 * @param name Command name to check
 * @return true if name is a builtin, false otherwise
 */
bool is_builtin(const char *name);

/**
 * @brief Find a command in PATH
 *
 * @param command Command name to search for
 * @return Full path or NULL (caller must free)
 */
char *find_command_in_path(const char *command);

/** Command hash table for PATH caching */
extern ht_strstr_t *command_hash;

#endif
