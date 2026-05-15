/**
 * @file lush.h
 * @brief Main shell header with common definitions and utilities
 *
 * Core header file providing common macros, utility functions, and
 * definitions used throughout the Lush shell.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 */

#ifndef LUSH_H
#define LUSH_H

/* Portable macro for marking intentionally unused
 * functions/variables/parameters. Use for code that is implemented but not yet
 * wired up. */
#ifndef MAYBE_UNUSED
#ifdef __GNUC__
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif
#endif

/** @brief Convenience macro for unused parameters in function bodies */
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#include "node.h"

#include <fcntl.h>
#include <glob.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

/** @brief Shell name identifier */
#define SHELL_NAME "lush"

#ifdef MAXLINE
#undef MAXLINE
#endif
/** @brief Maximum line length */
#define MAXLINE 4096

/** @brief Flag indicating shell should exit */
extern bool exit_flag;

/** @brief Last command exit status ($?) */
extern int last_exit_status;

/** @brief Shell process ID ($$) */
extern pid_t shell_pid;

/** @brief Shell argument vector */
extern char **shell_argv;

/** @brief Shell argument count */
extern int shell_argc;

/** @brief Last background process ID ($!) */
extern pid_t last_background_pid;

/** @brief Whether shell_argv was dynamically allocated */
extern bool shell_argv_is_dynamic;

/** @brief Get the shell source name, defaults to SHELL_NAME */
#define SOURCE_NAME get_shell_varp("0", SHELL_NAME)

/** @brief File mode for write operations (truncate) */
#define MODE_WRITE (O_RDWR | O_CREAT | O_TRUNC)

/** @brief File mode for append operations */
#define MODE_APPEND (O_RDWR | O_CREAT | O_APPEND)

/** @brief File mode for read operations */
#define MODE_READ (O_RDONLY)

/**
 * @brief Expansion context structure
 *
 * Tracks state during word expansion to handle quotes, assignments,
 * and expansion mode flags.
 */
typedef struct expansion_context {
    bool in_double_quotes;   /**< Inside double quotes */
    bool in_single_quotes;   /**< Inside single quotes */
    bool in_var_assign;      /**< Processing variable assignment */
    int var_assign_eq_count; /**< Number of = signs seen */
    bool no_field_split;     /**< Disable field splitting */
    bool no_pathname_expand; /**< Disable pathname expansion */
    bool no_tilde_expand;    /**< Disable tilde expansion */
} exp_ctx_t;

/**
 * @brief Expansion result codes
 */
typedef enum {
    EXP_OK,           /**< Expansion succeeded */
    EXP_ERROR,        /**< Expansion failed */
    EXP_NO_EXPANSION, /**< No expansion needed */
    EXP_INVALID_VAR   /**< Invalid variable reference */
} exp_result_t;

/**
 * @brief Expansion result structure
 *
 * Contains the result of an individual expansion operation.
 */
typedef struct {
    exp_result_t result; /**< Result code */
    char *expanded;      /**< Expanded string (caller must free) */
    size_t len;          /**< Length of expanded string */
} expansion_t;

/**
 * @brief String builder for efficient string manipulation
 */
typedef struct string_builder {
    char *data;      /**< String data buffer */
    size_t len;      /**< Current string length */
    size_t capacity; /**< Buffer capacity */
} str_builder_t;

/* ============================================================================
 * Parser Functions
 * ============================================================================
 */

/**
 * @brief Parse and execute a command string
 *
 * The starting_line parameter seeds source-location tracking so errors
 * carry file-relative line numbers when `command` is a slice of a
 * larger script (e.g. one logical statement read from a multi-line
 * file). Pass 1 for fresh contexts (interactive REPL turn, -c command,
 * top of file).
 *
 * @param command       Shell command string to parse and execute
 * @param starting_line Line number of `command`'s first character within
 *                      the original source file (1-based)
 * @return Exit status of the executed command
 */
int parse_and_execute(const char *command, size_t starting_line);

/* ============================================================================
 * Executor Access Functions
 * ============================================================================
 */

/** @brief Forward declaration for executor */
struct executor;

/**
 * @brief Get the global executor instance
 *
 * @return Pointer to the global executor
 */
struct executor *get_global_executor(void);

/* ============================================================================
 * Symbol Table Variable Functions
 * ============================================================================
 */

/**
 * @brief Get a shell variable as a string pointer
 *
 * @param name Variable name
 * @param default_val Default value if not found
 * @return Variable value or default
 */
char *get_shell_varp(char *name, char *default_val);

/**
 * @brief Get a shell variable as an integer
 *
 * @param name Variable name
 * @param default_val Default value if not found or not numeric
 * @return Variable value or default
 */
int get_shell_vari(char *name, int default_val);

/**
 * @brief Get a shell variable as a long integer
 *
 * @param name Variable name
 * @param default_val Default value if not found or not numeric
 * @return Variable value or default
 */
ssize_t get_shell_varl(char *name, int default_val);

/**
 * @brief Set a shell variable to a string value
 *
 * @param name Variable name
 * @param val String value
 */
void set_shell_varp(char *name, char *val);

/**
 * @brief Set a shell variable to an integer value
 *
 * @param name Variable name
 * @param val Integer value
 */
void set_shell_vari(char *name, int val);

/* ============================================================================
 * Shell Options
 * ============================================================================
 */

/**
 * @brief POSIX shell options structure
 *
 * Contains all shell option flags for behavior control.
 */
typedef struct shell_options {
    /* Command-line invocation modes */
    bool command_mode;    /**< -c flag: execute command string */
    char *command_string; /**< -c argument: command to execute */
    bool stdin_mode;      /**< -s flag: read from stdin */
    bool interactive;     /**< -i flag: force interactive mode */
    bool login_shell;     /**< -l flag: login shell behavior */
    bool analyze_mode;    /**< --analyze: full script analysis */
    bool lint_mode;       /**< --lint: actionable linting with fix support */
    bool fix_mode;        /**< --fix: apply safe automatic fixes */
    bool unsafe_fixes;    /**< --unsafe-fixes: also apply unsafe fixes */
    bool dry_run;         /**< --dry-run: preview fixes without applying */
    char *analyze_file;   /**< --analyze/--lint argument: file to analyze */
    char *output_format;  /**< --format: output format (text, json, gcc) */

    /* Shell behavior flags */
    bool exit_on_error;   /**< -e flag: exit on command failure */
    bool trace_execution; /**< -x flag: trace command execution */
    bool syntax_check;    /**< -n flag: syntax check only */
    bool unset_error;     /**< -u flag: error on unset variables */
    bool verbose;         /**< -v flag: print input lines */
    bool no_globbing;     /**< -f flag: disable pathname expansion */
    bool hash_commands;   /**< -h flag: command hashing */
    bool job_control;     /**< -m flag: enable job control */
    bool allexport;       /**< -a flag: automatically export all variables */
    bool noclobber;       /**< -C flag: prevent overwriting with > */
    bool onecmd;          /**< -t flag: exit after one command */
    bool notify;          /**< -b flag: asynchronous job notification */
    bool ignoreeof;       /**< ignoreeof: prevent exit on EOF (Ctrl+D) */
    bool nolog;           /**< nolog: prevent function defs in history */
    bool emacs_mode;      /**< emacs: emacs-style line editing (default) */
    bool vi_mode;         /**< vi: vi-style line editing */
    bool posix_mode;      /**< posix: strict POSIX compliance mode */
    bool pipefail_mode;   /**< pipefail: pipeline fails if any command fails */
    bool histexpand_mode; /**< histexpand: enable history expansion */
    bool history_mode;    /**< history: enable command history recording */
    bool interactive_comments_mode; /**< interactive-comments: enable # comments
                                     */
    bool physical_mode;             /**< physical: resolve symlinks in paths */
    bool privileged_mode; /**< privileged: restricted shell security mode */

    /* Early-init CLI mode override. Populated by --posix/--bash/--zsh/--lush
     * during parse_opts; consumed by detect_initial_mode() before
     * config_init so the user's lushrc layers on top of the right preset.
     * Stored as int rather than shell_mode_t to avoid pulling shell_mode.h
     * into lush.h. */
    int cli_mode_override;
    bool cli_mode_override_set;
} shell_options_t;

/** @brief Global shell options */
extern shell_options_t shell_opts;

/**
 * @brief Initialize shell options to defaults
 */
void init_shell_opts(void);

/**
 * @brief Check if enhanced completion is enabled
 *
 * @return true if enhanced completion is enabled
 */
bool get_enhanced_completion(void);

/**
 * @brief Check if word expansion is disabled
 *
 * @return true if word expansion is disabled
 */
bool get_no_word_expand(void);

/* ============================================================================
 * POSIX Option Management Functions
 * ============================================================================
 */

/**
 * @brief Initialize POSIX options
 */
void init_posix_options(void);

/**
 * @brief Check if a POSIX option is set
 *
 * @param option Option character (e.g., 'e', 'x')
 * @return true if option is set
 */
bool is_posix_option_set(char option);

/**
 * @brief Check if exit-on-error mode is enabled (-e)
 *
 * @return true if should exit on error
 */
bool should_exit_on_error(void);

/**
 * @brief Check if trace execution mode is enabled (-x)
 *
 * @return true if should trace execution
 */
bool should_trace_execution(void);

/**
 * @brief Check if syntax check only mode is enabled (-n)
 *
 * @return true if syntax check mode
 */
bool is_syntax_check_mode(void);

/**
 * @brief Check if unset variable error mode is enabled (-u)
 *
 * @return true if should error on unset vars
 */
bool should_error_unset_vars(void);

/**
 * @brief Check if verbose mode is enabled (-v)
 *
 * @return true if verbose mode
 */
bool is_verbose_mode(void);

/**
 * @brief Check if globbing is disabled (-f)
 *
 * @return true if globbing is disabled
 */
bool is_globbing_disabled(void);

/**
 * @brief Check if auto-export mode is enabled (-a)
 *
 * @return true if auto-export enabled
 */
bool should_auto_export(void);

/**
 * @brief Check if noclobber mode is enabled (-C)
 *
 * @return true if noclobber enabled
 */
bool is_noclobber_enabled(void);

/**
 * @brief Print command trace output
 *
 * @param command Command being traced
 */
void print_command_trace(const char *command);

/**
 * @brief Implement the set builtin command
 *
 * @param args Command arguments
 * @return Exit status
 */
int builtin_set(char **args);

/* ============================================================================
 * Word Expansion Types and Functions
 * ============================================================================
 */

/**
 * @brief Word structure for expansion results
 *
 * Represents a single word resulting from word expansion,
 * forming a linked list of words.
 */
typedef struct word {
    char *data;        /**< Word text */
    size_t len;        /**< Word length */
    struct word *next; /**< Next word in list */
} word_t;

/**
 * @brief Create a new word structure
 *
 * @param str String to create word from
 * @return New word or NULL on failure
 */
word_t *make_word(char *str);

/**
 * @brief Free a list of words
 *
 * @param first First word in list
 */
void free_all_words(word_t *first);

/**
 * @brief Check if a string is a valid identifier name
 *
 * @param str String to check
 * @return true if valid name
 */
bool is_name(const char *str);

/**
 * @brief Substitute a portion of a string
 *
 * @param s1 Original string
 * @param s2 Replacement string
 * @param start Start index
 * @param end End index
 * @return New string with substitution (caller must free)
 */
char *substitute_str(char *s1, char *s2, size_t start, size_t end);

/**
 * @brief Convert a word list to a single string
 *
 * @param word First word in list
 * @return Concatenated string (caller must free)
 */
char *wordlist_to_str(word_t *word);

/* ============================================================================
 * Core Expansion Functions
 * ============================================================================
 */

/**
 * @brief Perform tilde expansion
 *
 * @param str String to expand
 * @param ctx Expansion context
 * @return Expansion result
 */
expansion_t lush_tilde_expand(const char *str, const exp_ctx_t *ctx);

/**
 * @brief Perform variable expansion
 *
 * @param str String to expand
 * @param ctx Expansion context
 * @return Expansion result
 */
expansion_t var_expand(const char *str, const exp_ctx_t *ctx);

/**
 * @brief Perform command substitution expansion
 *
 * @param str String containing command substitution
 * @param ctx Expansion context
 * @return Expansion result
 */
expansion_t command_substitute_exp(const char *str, const exp_ctx_t *ctx);

/**
 * @brief Perform arithmetic expansion
 *
 * @param str String containing arithmetic expression
 * @param ctx Expansion context
 * @return Expansion result
 */
expansion_t arithm_expand_exp(const char *str, const exp_ctx_t *ctx);

/* ============================================================================
 * Main Word Expansion Pipeline
 * ============================================================================
 */

/**
 * @brief Perform full word expansion
 *
 * @param orig_word Original word to expand
 * @return List of resulting words
 */
word_t *word_expand(const char *orig_word);

/**
 * @brief Expand a word to a single string
 *
 * @param word Word to expand
 * @return Expanded string (caller must free)
 */
char *word_expand_to_str(const char *word);

/**
 * @brief Perform field splitting on a string
 *
 * @param str String to split
 * @return List of words
 */
word_t *field_split(const char *str);

/**
 * @brief Perform pathname expansion on word list
 *
 * @param words Word list to expand
 * @return Expanded word list
 */
word_t *pathnames_expand(word_t *words);

/**
 * @brief Remove quotes from a word list
 *
 * @param wordlist Word list to process
 */
void remove_quotes(word_t *wordlist);

/* ============================================================================
 * Context Management
 * ============================================================================
 */

/**
 * @brief Create a new expansion context
 *
 * @return New context (caller must free)
 */
exp_ctx_t *create_expansion_context(void);

/**
 * @brief Free an expansion context
 *
 * @param ctx Context to free
 */
void free_expansion_context(exp_ctx_t *ctx);

/**
 * @brief Reset an expansion context to defaults
 *
 * @param ctx Context to reset
 */
void reset_expansion_context(exp_ctx_t *ctx);

/* ============================================================================
 * String Builder Functions
 * ============================================================================
 */

/**
 * @brief Create a new string builder
 *
 * @param initial_capacity Initial buffer capacity
 * @return New string builder (caller must free)
 */
str_builder_t *sb_create(size_t initial_capacity);

/**
 * @brief Free a string builder
 *
 * @param sb String builder to free
 */
void sb_free(str_builder_t *sb);

/**
 * @brief Append a string to the builder
 *
 * @param sb String builder
 * @param str String to append
 * @return true on success
 */
bool sb_append(str_builder_t *sb, const char *str);

/**
 * @brief Append a character to the builder
 *
 * @param sb String builder
 * @param c Character to append
 * @return true on success
 */
bool sb_append_char(str_builder_t *sb, char c);

/**
 * @brief Append a string with specified length
 *
 * @param sb String builder
 * @param str String to append
 * @param len Number of characters to append
 * @return true on success
 */
bool sb_append_len(str_builder_t *sb, const char *str, size_t len);

/**
 * @brief Finalize string builder and get result
 *
 * @param sb String builder (freed after call)
 * @return Resulting string (caller must free)
 */
char *sb_finalize(str_builder_t *sb);

/**
 * @brief Expand positional parameters
 *
 * @param tmp Parameter reference string
 * @param in_double_quotes Whether inside double quotes
 * @return Expanded string (caller must free)
 */
char *pos_params_expand(char *tmp, bool in_double_quotes);

/**
 * @brief Evaluate an arithmetic expression
 *
 * @param orig_expr Expression to evaluate
 * @return Result string (caller must free)
 */
char *arithm_expand(const char *orig_expr);

/* ============================================================================
 * Command Execution Functions
 * ============================================================================
 */

/**
 * @brief Search PATH for an executable
 *
 * @param fn Filename to search for
 * @return Full path (caller must free) or NULL
 */
char *search_path(char *fn);

/**
 * @brief Execute a command with arguments
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status
 */
int do_exec_cmd(int argc, char **argv);

/**
 * @brief Execute a basic command node
 *
 * @param n Command node
 * @return Exit status
 */
int do_basic_command(node_t *n);

/**
 * @brief Execute a simple pipeline from a line
 *
 * @param line Pipeline command line
 * @return Exit status
 */
int execute_pipeline_simple(char *line);

/**
 * @brief Execute a simple pipeline with command array
 *
 * @param commands Array of command strings
 * @param cmd_count Number of commands
 * @return Exit status
 */
int execute_simple_pipeline(char **commands, int cmd_count);

/**
 * @brief Execute a single command
 *
 * @param command Command string
 * @return Exit status
 */
int execute_single_command(char *command);

/**
 * @brief Execute multiple pipeline commands
 *
 * @param cmd_args Array of argument arrays
 * @param cmd_argc Array of argument counts
 * @param cmd_count Number of commands
 * @return Exit status
 */
int execute_pipeline_commands(char ***cmd_args, int *cmd_argc, int cmd_count);

/**
 * @brief Set up a redirection from a node
 *
 * @param redir Redirection node
 * @return 0 on success, -1 on error
 */
int setup_redirection(node_t *redir);

/**
 * @brief Execute a command from the new parser
 *
 * @param cmd Command node
 * @return Exit status
 */
int execute_new_parser_command(node_t *cmd);

/**
 * @brief Execute a pipeline from the new parser
 *
 * @param pipe_node Pipeline node
 * @return Exit status
 */
int execute_new_parser_pipeline(node_t *pipe_node);

/**
 * @brief Execute a control structure from the new parser
 *
 * @param control_node Control structure node
 * @return Exit status
 */
int execute_new_parser_control_structure(node_t *control_node);

/**
 * @brief Execute an if statement from the new parser
 *
 * @param if_node If statement node
 * @return Exit status
 */
int execute_new_parser_if(node_t *if_node);

/**
 * @brief Execute a while loop from the new parser
 *
 * @param while_node While loop node
 * @return Exit status
 */
int execute_new_parser_while(node_t *while_node);

/**
 * @brief Execute a for loop from the new parser
 *
 * @param for_node For loop node
 * @return Exit status
 */
int execute_new_parser_for(node_t *for_node);

#endif /* LUSH_H */
