/**
 * @file fc.c
 * @brief POSIX fc Command Builtin Implementation using LLE History System
 *
 * This module implements the POSIX fc (fix command) builtin for Lush shell.
 * The fc command provides POSIX-compliant history editing, listing, and
 * re-execution capabilities using LLE's history system.
 *
 * POSIX fc command syntax:
 * - fc [-r] [-e editor] [first [last]]    # Edit and re-execute
 * - fc -l [-nr] [first [last]]            # List commands
 * - fc -s [old=new] [first]               # Substitute and re-execute
 *
 * @author Michael Berry
 * @version 2.0 (LLE-based implementation)
 */

#include "builtins.h"
#include "executor.h"
#include "lle/history.h"
#include "lle/lle_editor.h"
#include "lle/lle_pager.h"
#include "lle/lle_shell_integration.h"
#include "shell_error.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// Maximum editor command length
#define FC_MAX_EDITOR_COMMAND 4096

/// fc command options
typedef struct fc_options {
    bool list_mode;
    bool reverse_order;
    bool suppress_numbers;
    bool substitute_mode;
    char *editor;
    char *old_pattern;
    char *new_pattern;
    int first;
    int last;
    bool range_valid;
} fc_options_t;

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

/**
 * @brief Get the LLE history core from the global editor
 *
 * Retrieves the history system from the global LLE editor instance.
 *
 * @return Pointer to the history core, or NULL if editor not initialized
 */
static lle_history_core_t *get_lle_history(void) {
    lle_editor_t *editor = lle_get_global_editor();
    if (!editor) {
        return NULL;
    }
    return editor->history_system;
}

/**
 * @brief Get total history entry count
 *
 * Queries the history system for the total number of stored entries.
 *
 * @param history The history core to query
 * @return Number of history entries, or 0 on error
 */
static size_t get_history_count(lle_history_core_t *history) {
    size_t count = 0;
    if (lle_history_get_entry_count(history, &count) != LLE_SUCCESS) {
        return 0;
    }
    return count;
}

/**
 * @brief Parse old=new substitution pattern
 *
 * Parses a substitution pattern for fc -s command. The pattern has
 * the form "old=new" where old is replaced with new in the command.
 *
 * @param pattern The substitution pattern string
 * @param old Output pointer for the old string (newly allocated, caller must
 * free)
 * @param new_str Output pointer for the new string (newly allocated, caller
 * must free)
 * @return true on success, false on allocation failure
 */
static bool parse_substitution_pattern(const char *pattern, char **old,
                                       char **new_str) {
    if (!pattern || !old || !new_str) {
        return false;
    }

    const char *equals = strchr(pattern, '=');
    if (!equals) {
        /// No equals sign, treat entire pattern as old with empty new
        *old = strdup(pattern);
        *new_str = strdup("");
        return (*old && *new_str);
    }

    size_t old_len = (size_t)(equals - pattern);
    size_t new_len = strlen(equals + 1);

    *old = malloc(old_len + 1);
    *new_str = malloc(new_len + 1);

    if (!*old || !*new_str) {
        free(*old);
        free(*new_str);
        *old = NULL;
        *new_str = NULL;
        return false;
    }

    strncpy(*old, pattern, old_len);
    (*old)[old_len] = '\0';
    strcpy(*new_str, equals + 1);

    return true;
}

/**
 * @brief Get default editor from environment
 *
 * Determines the editor to use for fc command by checking environment
 * variables in order: FCEDIT, EDITOR, VISUAL. Falls back to "ed" as
 * the POSIX default if none are set.
 *
 * @return Newly allocated string with editor command (caller must free)
 */
static char *get_default_editor(void) {
    const char *fcedit = getenv("FCEDIT");
    if (fcedit && *fcedit) {
        return strdup(fcedit);
    }

    const char *editor = getenv("EDITOR");
    if (editor && *editor) {
        return strdup(editor);
    }

    const char *visual = getenv("VISUAL");
    if (visual && *visual) {
        return strdup(visual);
    }

    /// POSIX default
    return strdup("ed");
}

/**
 * @brief Create temporary file with content
 *
 * Creates a temporary file in /tmp with the specified content for
 * use by the fc edit command.
 *
 * @param content The content to write to the file (may be NULL for empty file)
 * @return Newly allocated string with temporary file path (caller must free),
 *         or NULL on error
 */
static char *create_temp_file(const char *content) {
    char template[] = "/tmp/fc.XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        return NULL;
    }

    if (content && *content) {
        size_t len = strlen(content);
        ssize_t written = write(fd, content, len);
        if (written < 0 || (size_t)written != len) {
            close(fd);
            unlink(template);
            return NULL;
        }
    }

    close(fd);
    return strdup(template);
}

/**
 * @brief Read file content into a string
 *
 * Reads the entire contents of a file into a newly allocated string.
 * Used to read back edited commands from the temporary file.
 *
 * @param filename The path to the file to read
 * @return Newly allocated string with file contents (caller must free),
 *         or NULL on error
 */
static char *read_file_content(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return strdup("");
    }

    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(content, 1, (size_t)size, fp);
    content[read_size] = '\0';
    fclose(fp);

    return content;
}

/**
 * @brief Execute a command and return its exit status
 *
 * Creates an executor and runs the specified command string.
 * Used to execute commands after fc editing or substitution.
 *
 * @param command The command string to execute
 * @return Exit status of the command, or 1 on error
 */
static int execute_command(const char *command) {
    if (!command || !*command) {
        return 1;
    }

    executor_t *executor = executor_new();
    if (!executor) {
        executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                              builtin_get_source_location(),
                              "failed to create executor");
        return 1;
    }

    /// fc re-executes a history entry; the joined command has no
    /// surrounding source, so line 1.
    int exit_status = executor_execute_command_line(executor, command, 1);

    /// Forward only legacy-text errors from the sub-executor. Structured
    /// errors set executor->error_message to NULL after displaying, so
    /// this branch fires solely for the diminishing pool of bare-string
    /// errors that haven't been migrated yet. Wrapping that opaque text
    /// in a fresh shell_error layer here would obscure the real origin,
    /// so keep a plain forwarding print.
    const char *err_msg = executor_error(executor);
    if (executor_has_error(executor) && err_msg) {
        fprintf(stderr, "lush: fc: %s\n", err_msg);
    }

    executor_free(executor);
    return exit_status;
}

/**
 * @brief Resolve a range specifier to an index
 *
 * @param history History core
 * @param spec Range specifier (number, -offset, or string prefix)
 * @param count Total history count
 * @param result Output index (0-based)
 * @return true on success, false on error
 */
static bool resolve_range_spec(lle_history_core_t *history, const char *spec,
                               size_t count, size_t *result) {
    if (!spec || !*spec || count == 0) {
        return false;
    }

    /// Check for negative offset
    if (spec[0] == '-' && isdigit((unsigned char)spec[1])) {
        long offset = strtol(spec, NULL, 10);
        if (offset < 0 && (size_t)(-offset) <= count) {
            *result = count + (size_t)offset;
            return true;
        }
        return false;
    }

    /// Check for positive number (1-based history number)
    if (isdigit((unsigned char)spec[0])) {
        long num = strtol(spec, NULL, 10);
        if (num > 0 && (size_t)num <= count) {
            *result = (size_t)(num - 1);
            return true;
        }
        return false;
    }

    /// String prefix search - find most recent match
    size_t prefix_len = strlen(spec);
    for (size_t i = count; i > 0; i--) {
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(history, i - 1, &entry) ==
                LLE_SUCCESS &&
            entry && entry->command) {
            if (strncmp(entry->command, spec, prefix_len) == 0) {
                *result = i - 1;
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Parse range arguments for fc command
 *
 * Parses the first and last range specifiers for the fc command and
 * resolves them to history indices. Handles defaults based on the
 * current operation mode (list vs edit).
 *
 * @param history The history core to query
 * @param first_str First range specifier (number, -offset, or string prefix)
 * @param last_str Last range specifier (may be NULL)
 * @param opts Options structure to populate with resolved range
 * @return true on success, false if range is invalid or out of bounds
 */
static bool parse_range(lle_history_core_t *history, const char *first_str,
                        const char *last_str, fc_options_t *opts) {
    size_t count = get_history_count(history);
    if (count == 0) {
        executor_error_report(current_executor, SHELL_ERR_HISTORY_ERROR,
                              builtin_get_source_location(),
                              "no history available");
        return false;
    }

    size_t first_idx, last_idx;

    if (!first_str) {
        /// Default: last command for edit, last 16 for list
        if (opts->list_mode) {
            first_idx = (count > 16) ? count - 16 : 0;
            last_idx = count - 1;
        } else {
            first_idx = last_idx = count - 1;
        }
    } else {
        if (!resolve_range_spec(history, first_str, count, &first_idx)) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "%s: history specification out of range",
                                  first_str);
            return false;
        }

        if (last_str) {
            if (!resolve_range_spec(history, last_str, count, &last_idx)) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "%s: history specification out of range", last_str);
                return false;
            }
        } else {
            /// Single spec: for list use to end, for edit use single command
            if (opts->list_mode) {
                last_idx = count - 1;
            } else {
                last_idx = first_idx;
            }
        }
    }

    /// Ensure first <= last
    if (first_idx > last_idx) {
        size_t tmp = first_idx;
        first_idx = last_idx;
        last_idx = tmp;
    }

    opts->first = (int)first_idx;
    opts->last = (int)last_idx;
    opts->range_valid = true;

    return true;
}

/* ============================================================================
 * fc Command Implementations
 * ============================================================================
 */

/**
 * @brief List history entries with fc formatting
 *
 * Displays history entries in the specified range with optional
 * line numbers. Supports forward and reverse order display.
 *
 * @param history The history core to query
 * @param opts Options containing range and display preferences
 * @return 0 on success, 1 on error
 */
static int fc_list(lle_history_core_t *history, fc_options_t *opts) {
    if (!opts->range_valid) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "invalid range for list operation");
        return 1;
    }

    /// Buffer the listing through open_memstream and hand it to
    /// lle_pager_present so wide ranges of history paginate in
    /// interactive shells. Tight ranges (single entry, recent few)
    /// fall through the pager's fits-in-one-screen branch unchanged.
    /// On memstream allocation failure the per-iteration writes
    /// target stdout directly.
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    FILE *sink = out ? out : stdout;

    if (opts->reverse_order) {
        for (int i = opts->last; i >= opts->first; i--) {
            lle_history_entry_t *entry = NULL;
            if (lle_history_get_entry_by_index(history, (size_t)i, &entry) ==
                    LLE_SUCCESS &&
                entry && entry->command) {
                if (opts->suppress_numbers) {
                    fprintf(sink, "%s\n", entry->command);
                } else {
                    fprintf(sink, "%5d  %s\n", i + 1, entry->command);
                }
            }
        }
    } else {
        for (int i = opts->first; i <= opts->last; i++) {
            lle_history_entry_t *entry = NULL;
            if (lle_history_get_entry_by_index(history, (size_t)i, &entry) ==
                    LLE_SUCCESS &&
                entry && entry->command) {
                if (opts->suppress_numbers) {
                    fprintf(sink, "%s\n", entry->command);
                } else {
                    fprintf(sink, "%5d  %s\n", i + 1, entry->command);
                }
            }
        }
    }

    if (out) {
        fclose(out);
        lle_pager_present(NULL, buf);
        free(buf);
    }

    return 0;
}

/**
 * @brief Edit and re-execute history entries
 *
 * Collects commands in the specified range into a temporary file,
 * opens the file in an editor, then executes the edited commands
 * line by line. Each executed command is added to history.
 *
 * @param history The history core to query and update
 * @param opts Options containing range and editor preferences
 * @return Exit status of the last executed command, or 1 on error
 */
static int fc_edit(lle_history_core_t *history, fc_options_t *opts) {
    if (!opts->range_valid) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "invalid range for edit operation");
        return 1;
    }

    /// Collect commands in range
    size_t content_capacity = 1024;
    size_t content_size = 0;
    char *content = malloc(content_capacity);
    if (!content) {
        executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                              builtin_get_source_location(),
                              "memory allocation failed");
        return 1;
    }
    content[0] = '\0';

    for (int i = opts->first; i <= opts->last; i++) {
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(history, (size_t)i, &entry) ==
                LLE_SUCCESS &&
            entry && entry->command) {
            size_t cmd_len = strlen(entry->command);

            /// Ensure buffer capacity
            if (content_size + cmd_len + 2 > content_capacity) {
                content_capacity = (content_size + cmd_len + 2) * 2;
                char *new_content = realloc(content, content_capacity);
                if (!new_content) {
                    free(content);
                    executor_error_report(current_executor,
                                          SHELL_ERR_OUT_OF_MEMORY,
                                          builtin_get_source_location(),
                                          "memory allocation failed");
                    return 1;
                }
                content = new_content;
            }

            strcat(content, entry->command);
            strcat(content, "\n");
            content_size += cmd_len + 1;
        }
    }

    if (content_size == 0) {
        free(content);
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "no commands in specified range");
        return 1;
    }

    /// Create temporary file
    char *temp_filename = create_temp_file(content);
    free(content);

    if (!temp_filename) {
        executor_error_report(current_executor, SHELL_ERR_IO_ERROR,
                              builtin_get_source_location(),
                              "failed to create temporary file");
        return 1;
    }

    /// Determine editor to use
    char *editor_cmd =
        opts->editor ? strdup(opts->editor) : get_default_editor();
    if (!editor_cmd) {
        unlink(temp_filename);
        free(temp_filename);
        executor_error_report(current_executor, SHELL_ERR_FEATURE_DISABLED,
                              builtin_get_source_location(),
                              "no editor available");
        return 1;
    }

    /// Build and execute editor command
    char editor_command[FC_MAX_EDITOR_COMMAND];
    snprintf(editor_command, sizeof(editor_command), "%s %s", editor_cmd,
             temp_filename);
    free(editor_cmd);

    int editor_status = system(editor_command);
    if (editor_status != 0) {
        unlink(temp_filename);
        free(temp_filename);
        executor_error_report(current_executor, SHELL_ERR_EXEC_FAILED,
                              builtin_get_source_location(),
                              "editor failed with status %d",
                              WEXITSTATUS(editor_status));
        return 1;
    }

    /// Read back edited content
    char *edited_content = read_file_content(temp_filename);
    unlink(temp_filename);
    free(temp_filename);

    if (!edited_content) {
        executor_error_report(current_executor, SHELL_ERR_IO_ERROR,
                              builtin_get_source_location(),
                              "failed to read edited content");
        return 1;
    }

    /// Execute edited commands line by line
    int final_status = 0;
    char *line_start = edited_content;
    char *line_end;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';

        /// Skip empty lines and whitespace-only lines
        char *trimmed = line_start;
        while (*trimmed && isspace((unsigned char)*trimmed))
            trimmed++;

        if (*trimmed) {
            /// Add to history and execute
            lle_history_bridge_add_entry(trimmed, 0, NULL);
            printf("%s\n", trimmed);
            final_status = execute_command(trimmed);
        }

        line_start = line_end + 1;
    }

    /// Handle last line if no trailing newline
    if (*line_start) {
        char *trimmed = line_start;
        while (*trimmed && isspace((unsigned char)*trimmed))
            trimmed++;
        if (*trimmed) {
            lle_history_bridge_add_entry(trimmed, 0, NULL);
            printf("%s\n", trimmed);
            final_status = execute_command(trimmed);
        }
    }

    free(edited_content);
    return final_status;
}

/**
 * @brief Substitute and re-execute a history command
 *
 * Implements fc -s functionality. Retrieves a command from history,
 * performs an optional old=new substitution, and re-executes it.
 * The modified command is added to history before execution.
 *
 * @param history The history core to query and update
 * @param opts Options containing substitution pattern and command index
 * @return Exit status of the executed command, or 1 on error
 */
static int fc_substitute(lle_history_core_t *history, fc_options_t *opts) {
    if (!opts->range_valid) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "invalid range for substitution");
        return 1;
    }

    lle_history_entry_t *entry = NULL;
    if (lle_history_get_entry_by_index(history, (size_t)opts->first, &entry) !=
            LLE_SUCCESS ||
        !entry || !entry->command) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "history entry not found");
        return 1;
    }

    const char *original = entry->command;

    /// If no pattern specified, just re-execute
    if (!opts->old_pattern || !*opts->old_pattern) {
        printf("%s\n", original);
        lle_history_bridge_add_entry(original, 0, NULL);
        return execute_command(original);
    }

    /// Find pattern in command
    const char *match = strstr(original, opts->old_pattern);
    if (!match) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                              builtin_get_source_location(),
                              "pattern '%s' not found in command",
                              opts->old_pattern);
        return 1;
    }

    /// Build new command with substitution
    size_t old_len = strlen(opts->old_pattern);
    size_t new_len = opts->new_pattern ? strlen(opts->new_pattern) : 0;
    size_t original_len = strlen(original);
    size_t prefix_len = (size_t)(match - original);
    size_t suffix_len = original_len - prefix_len - old_len;

    char *new_command = malloc(prefix_len + new_len + suffix_len + 1);
    if (!new_command) {
        executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                              builtin_get_source_location(),
                              "memory allocation failed");
        return 1;
    }

    /// Build the new command
    strncpy(new_command, original, prefix_len);
    new_command[prefix_len] = '\0';
    if (opts->new_pattern) {
        strcat(new_command, opts->new_pattern);
    }
    strcat(new_command, match + old_len);

    /// Print and execute
    printf("%s\n", new_command);
    lle_history_bridge_add_entry(new_command, 0, NULL);
    int status = execute_command(new_command);

    free(new_command);
    return status;
}

/* ============================================================================
 * Main fc Command Entry Point
 * ============================================================================
 */

/**
 * @brief Print fc command usage information
 *
 * Displays usage instructions for the fc builtin command including
 * all options, modes, and range specifier formats.
 */
static void fc_usage(void) {
    fprintf(stderr, "usage: fc [-e editor] [-r] [first [last]]\n");
    fprintf(stderr, "       fc -l [-nr] [first [last]]\n");
    fprintf(stderr, "       fc -s [old=new] [first]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -e editor  Use specified editor\n");
    fprintf(stderr, "  -l         List commands instead of editing\n");
    fprintf(stderr, "  -n         Suppress line numbers in list mode\n");
    fprintf(stderr, "  -r         Reverse order (newest first)\n");
    fprintf(stderr, "  -s         Substitute old with new and re-execute\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "range specifiers:\n");
    fprintf(stderr, "  number     Specific history number (1-based)\n");
    fprintf(stderr, "  -offset    Relative to current (e.g., -1 = last)\n");
    fprintf(stderr, "  string     Most recent command starting with string\n");
}

/**
 * @brief Main fc command implementation using LLE history
 *
 * POSIX-compliant fc (fix command) builtin that provides history
 * editing, listing, and re-execution capabilities. Supports three modes:
 * - Edit mode (default): Edit commands in an editor and re-execute
 * - List mode (-l): Display history entries
 * - Substitute mode (-s): Quick substitution and re-execute
 *
 * @param argc Argument count
 * @param argv Argument vector with fc options and range specifiers
 * @return 0 on success, 1 on error or invalid usage
 */
int bin_fc(int argc, char **argv) {
    lle_history_core_t *history = get_lle_history();
    if (!history) {
        executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                              builtin_get_source_location(),
                              "history system not available");
        return 1;
    }

    /// Initialize options
    fc_options_t opts = {0};

    /// Parse command line options
    int opt;
    optind = 1; /// Reset getopt
    while ((opt = getopt(argc, argv, "e:lnrs")) != -1) {
        switch (opt) {
        case 'e':
            opts.editor = strdup(optarg);
            break;
        case 'l':
            opts.list_mode = true;
            break;
        case 'n':
            opts.suppress_numbers = true;
            break;
        case 'r':
            opts.reverse_order = true;
            break;
        case 's':
            opts.substitute_mode = true;
            break;
        default:
            fc_usage();
            return 1;
        }
    }

    /// Handle substitute mode pattern parsing
    if (opts.substitute_mode) {
        if (optind < argc && strchr(argv[optind], '=')) {
            if (!parse_substitution_pattern(argv[optind], &opts.old_pattern,
                                            &opts.new_pattern)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid substitution pattern");
                free(opts.editor);
                return 1;
            }
            optind++;
        } else {
            /// No pattern - will just re-execute
            opts.old_pattern = strdup("");
            opts.new_pattern = strdup("");
        }
    }

    /// Parse range arguments
    const char *first_str = (optind < argc) ? argv[optind] : NULL;
    const char *last_str = (optind + 1 < argc) ? argv[optind + 1] : NULL;

    if (!parse_range(history, first_str, last_str, &opts)) {
        free(opts.editor);
        free(opts.old_pattern);
        free(opts.new_pattern);
        return 1;
    }

    /// Execute appropriate fc operation
    int status;
    if (opts.list_mode) {
        status = fc_list(history, &opts);
    } else if (opts.substitute_mode) {
        status = fc_substitute(history, &opts);
    } else {
        status = fc_edit(history, &opts);
    }

    /// Cleanup
    free(opts.editor);
    free(opts.old_pattern);
    free(opts.new_pattern);

    return status;
}
