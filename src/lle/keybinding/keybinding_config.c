/**
 * @file keybinding_config.c
 * @brief LLE User Keybinding Configuration System Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements user-customizable keybindings via TOML configuration files.
 * Reuses the TOML-subset parser from the theme system.
 *
 * Specification: Spec 13 (User Customization), Spec 22 (User Interface)
 * Version: 1.0.0
 */

#include "lle/keybinding_config.h"
#include "lle/keybinding.h"
#include "lle/keybinding_actions.h"
#include "lle/prompt/theme_parser.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ============================================================================
 * ACTION REGISTRY
 * ============================================================================
 *
 * Static table mapping GNU Readline action names to LLE function pointers.
 * All 42+ actions from keybinding_actions.h are included.
 */

static const lle_action_registry_entry_t ACTION_REGISTRY[] = {
    /* ========================================================================
     * MOVEMENT ACTIONS
     * ========================================================================
     */
    {      .name = "beginning-of-line",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_beginning_of_line,
     .description = "Move cursor to beginning of line"                       },
    {            .name = "end-of-line",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_end_of_line,
     .description = "Move cursor to end of line"                             },
    {           .name = "forward-char",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_forward_char,
     .description = "Move cursor forward one character"                      },
    {          .name = "backward-char",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_backward_char,
     .description = "Move cursor backward one character"                     },
    {           .name = "forward-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_forward_word,
     .description = "Move cursor forward one word"                           },
    {          .name = "backward-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_backward_word,
     .description = "Move cursor backward one word"                          },

    /* ========================================================================
     * BUFFER NAVIGATION
     * ========================================================================
     */
    {    .name = "beginning-of-buffer",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_beginning_of_buffer,
     .description = "Move cursor to beginning of buffer"                     },
    {          .name = "end-of-buffer",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_end_of_buffer,
     .description = "Move cursor to end of buffer"                           },
    {          .name = "previous-line",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_previous_line,
     .description = "Move cursor to previous line (multiline)"               },
    {              .name = "next-line",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_next_line,
     .description = "Move cursor to next line (multiline)"                   },
    {         .name = "smart-up-arrow",
     .type = LLE_ACTION_TYPE_CONTEXT,
     .func.context = lle_smart_up_arrow_context,
     .description = "Context-aware up: history or previous line (with hints)"},
    {       .name = "smart-down-arrow",
     .type = LLE_ACTION_TYPE_CONTEXT,
     .func.context = lle_smart_down_arrow_context,
     .description = "Context-aware down: history or next line (with hints)"  },

    /* ========================================================================
     * DELETION AND KILLING
     * ========================================================================
     */
    {            .name = "delete-char",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_delete_char,
     .description = "Delete character at cursor"                             },
    {   .name = "backward-delete-char",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_backward_delete_char,
     .description = "Delete character before cursor"                         },
    {              .name = "kill-line",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_kill_line,
     .description = "Kill from cursor to end of line"                        },
    {     .name = "backward-kill-line",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_backward_kill_line,
     .description = "Kill from beginning of line to cursor"                  },
    {              .name = "kill-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_kill_word,
     .description = "Kill from cursor to end of word"                        },
    {     .name = "backward-kill-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_backward_kill_word,
     .description = "Kill from beginning of word to cursor"                  },
    {      .name = "unix-line-discard",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_unix_line_discard,
     .description = "Kill entire line (Unix style)"                          },
    {       .name = "unix-word-rubout",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_unix_word_rubout,
     .description = "Kill word backward (Unix style)"                        },
    {.name = "delete-horizontal-space",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_delete_horizontal_space,
     .description = "Delete whitespace around cursor"                        },

    /* ========================================================================
     * YANK AND TRANSPOSE
     * ========================================================================
     */
    {                   .name = "yank",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_yank,
     .description = "Yank (paste) from kill ring"                            },
    {               .name = "yank-pop",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_yank_pop,
     .description = "Cycle through kill ring"                                },
    {        .name = "transpose-chars",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_transpose_chars,
     .description = "Swap character at cursor with previous"                 },
    {        .name = "transpose-words",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_transpose_words,
     .description = "Swap current word with previous"                        },

    /* ========================================================================
     * CASE CHANGES
     * ========================================================================
     */
    {            .name = "upcase-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_upcase_word,
     .description = "Convert word to uppercase"                              },
    {          .name = "downcase-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_downcase_word,
     .description = "Convert word to lowercase"                              },
    {        .name = "capitalize-word",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_capitalize_word,
     .description = "Capitalize word"                                        },

    /* ========================================================================
     * HISTORY NAVIGATION
     * ========================================================================
     */
    {       .name = "history-previous",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_history_previous,
     .description = "Previous history entry"                                 },
    {           .name = "history-next",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_history_next,
     .description = "Next history entry"                                     },
    { .name = "reverse-search-history",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_reverse_search_history,
     .description = "Incremental reverse history search"                     },
    { .name = "forward-search-history",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_forward_search_history,
     .description = "Incremental forward history search"                     },
    {.name = "history-search-backward",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_history_search_backward,
     .description = "Search history backward for prefix"                     },
    { .name = "history-search-forward",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_history_search_forward,
     .description = "Search history forward for prefix"                      },

    /* ========================================================================
     * COMPLETION
     * ========================================================================
     */
    {               .name = "complete",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_complete,
     .description = "Complete current word"                                  },
    {   .name = "possible-completions",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_possible_completions,
     .description = "List possible completions"                              },
    {     .name = "insert-completions",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_insert_completions,
     .description = "Insert all completions"                                 },

    /* ========================================================================
     * CONTEXT-AWARE ACTIONS
     * ========================================================================
     */
    {            .name = "accept-line",
     .type = LLE_ACTION_TYPE_CONTEXT,
     .func.context = lle_accept_line_context,
     .description = "Accept line for execution"                              },
    {             .name = "abort-line",
     .type = LLE_ACTION_TYPE_CONTEXT,
     .func.context = lle_abort_line_context,
     .description = "Abort current line"                                     },

    /* ========================================================================
     * SHELL OPERATIONS
     * ========================================================================
     */
    {               .name = "send-eof",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_send_eof,
     .description = "Send EOF (Ctrl-D on empty line)"                        },
    {              .name = "interrupt",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_interrupt,
     .description = "Send interrupt signal"                                  },
    {                .name = "suspend",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_suspend,
     .description = "Suspend shell"                                          },
    {           .name = "clear-screen",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_clear_screen,
     .description = "Clear screen and redraw"                                },

    /* ========================================================================
     * UTILITY ACTIONS
     * ========================================================================
     */
    {          .name = "quoted-insert",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_quoted_insert,
     .description = "Insert next character literally"                        },
    {             .name = "tab-insert",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_tab_insert,
     .description = "Insert tab character"                                   },
    {                .name = "newline",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_newline,
     .description = "Insert newline character"                               },
    { .name = "insert-newline-literal",
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = lle_insert_newline_literal,
     .description = "Insert literal newline"                                 },

    /// Sentinel - marks end of registry
    {                     .name = NULL,
     .type = LLE_ACTION_TYPE_SIMPLE,
     .func.simple = NULL,
     .description = NULL                                                     }
};

/// Count of actions (excluding sentinel)
static const size_t ACTION_REGISTRY_COUNT =
    (sizeof(ACTION_REGISTRY) / sizeof(ACTION_REGISTRY[0])) - 1;

/* ============================================================================
 * ACTION REGISTRY API IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Look up an action by name in the registry
 * @param name Action name to look up (e.g., "forward-char")
 * @return Pointer to registry entry if found, NULL if not found or name is NULL
 */
const lle_action_registry_entry_t *
lle_action_registry_lookup(const char *name) {
    if (!name) {
        return NULL;
    }

    for (size_t i = 0; i < ACTION_REGISTRY_COUNT; i++) {
        if (strcmp(ACTION_REGISTRY[i].name, name) == 0) {
            return &ACTION_REGISTRY[i];
        }
    }

    return NULL;
}

/**
 * @brief Get all entries in the action registry
 * @param count_out Pointer to store the number of entries (may be NULL)
 * @return Pointer to the first entry in the registry array
 */
const lle_action_registry_entry_t *
lle_action_registry_get_all(size_t *count_out) {
    if (count_out) {
        *count_out = ACTION_REGISTRY_COUNT;
    }
    return ACTION_REGISTRY;
}

/**
 * @brief Get the number of actions in the registry
 * @return Total count of registered actions
 */
size_t lle_action_registry_count(void) { return ACTION_REGISTRY_COUNT; }

/**
 * @brief Get an action registry entry by index
 * @param index Zero-based index into the registry
 * @return Pointer to registry entry if index is valid, NULL if out of bounds
 */
const lle_action_registry_entry_t *
lle_action_registry_get_by_index(size_t index) {
    if (index >= ACTION_REGISTRY_COUNT) {
        return NULL;
    }
    return &ACTION_REGISTRY[index];
}

/* ============================================================================
 * FILE PATH UTILITIES
 * ============================================================================
 */

/**
 * @brief Get the user's home directory path
 * @return Home directory path string, or NULL if not available
 */
static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home && home[0]) {
        return home;
    }

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return pw->pw_dir;
    }

    return NULL;
}

/**
 * @brief Get the path to the user's keybinding configuration file
 * @param buffer Buffer to store the path
 * @param buffer_size Size of the buffer in bytes
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_get_user_config_path(char *buffer,
                                                 size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Try XDG_CONFIG_HOME first
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0]) {
        int written = snprintf(buffer, buffer_size, "%s/lush/%s", xdg_config,
                               LLE_KEYBINDING_CONFIG_FILENAME);
        if (written > 0 && (size_t)written < buffer_size) {
            return LLE_SUCCESS;
        }
    }

    /// Fall back to ~/.config/lush/
    const char *home = get_home_dir();
    if (!home) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    int written = snprintf(buffer, buffer_size, "%s/.config/lush/%s", home,
                           LLE_KEYBINDING_CONFIG_FILENAME);
    if (written < 0 || (size_t)written >= buffer_size) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * CONFIG FILE READING
 * ============================================================================
 */

/**
 * @brief Read entire file contents into a heap-allocated buffer
 * @param filepath Path to the file to read
 * @param size_out Pointer to store the number of bytes read (may be NULL)
 * @return Heap-allocated buffer with file contents, or NULL on failure
 */
static char *read_file_contents(const char *filepath, size_t *size_out) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        return NULL;
    }

    /// Get file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0 || (size_t)size > LLE_KEYBINDING_CONFIG_FILE_MAX) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    /// Allocate buffer
    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    /// Read file
    size_t read_size = fread(content, 1, (size_t)size, fp);
    fclose(fp);

    content[read_size] = '\0';
    if (size_out) {
        *size_out = read_size;
    }

    return content;
}

/* ============================================================================
 * CONFIG PARSER CALLBACK
 * ============================================================================
 */

/**
 * Parser context for keybinding config
 */
typedef struct {
    lle_keybinding_manager_t *manager;
    lle_keybinding_load_result_t *result;
} keybinding_parser_ctx_t;

/**
 * @brief Parser callback for keybinding configuration file entries
 * @param section Current TOML section name
 * @param key Configuration key within the section
 * @param value Parsed value for the key
 * @param user_data User context (keybinding_parser_ctx_t pointer)
 * @return LLE_SUCCESS to continue parsing, error code to abort
 */
static lle_result_t keybinding_config_callback(const char *section,
                                               const char *key,
                                               const lle_theme_value_t *value,
                                               void *user_data) {
    keybinding_parser_ctx_t *ctx = user_data;

    /// Only process [bindings] section
    if (strcmp(section, "bindings") != 0) {
        return LLE_SUCCESS;
    }

    /// Value must be a string
    if (value->type != LLE_THEME_VALUE_STRING) {
        ctx->result->errors_count++;
        return LLE_SUCCESS;
    }

    const char *key_sequence = key;
    const char *action_name = value->data.string;

    /// Handle "none" to unbind
    if (strcmp(action_name, "none") == 0 || action_name[0] == '\0') {
        lle_result_t unbind_result =
            lle_keybinding_manager_unbind(ctx->manager, key_sequence);
        if (unbind_result == LLE_SUCCESS) {
            ctx->result->bindings_applied++;
        }
        return LLE_SUCCESS;
    }

    /// Look up action in registry
    const lle_action_registry_entry_t *entry =
        lle_action_registry_lookup(action_name);
    if (!entry) {
        /// Unknown action - skip with warning
        ctx->result->errors_count++;
        return LLE_SUCCESS;
    }

    /// Validate key sequence
    lle_key_event_t key_event;
    lle_result_t parse_result =
        lle_key_sequence_parse(key_sequence, &key_event);
    if (parse_result != LLE_SUCCESS) {
        /// Invalid key sequence - skip with warning
        ctx->result->errors_count++;
        return LLE_SUCCESS;
    }

    /// Check if this overrides an existing binding
    lle_keybinding_action_t *existing = NULL;
    if (lle_keybinding_manager_lookup(ctx->manager, key_sequence, &existing) ==
        LLE_SUCCESS) {
        ctx->result->bindings_overridden++;
    }

    /// Bind based on action type
    lle_result_t bind_result;
    if (entry->type == LLE_ACTION_TYPE_SIMPLE) {
        bind_result = lle_keybinding_manager_bind(
            ctx->manager, key_sequence, entry->func.simple, entry->name);
    } else {
        bind_result = lle_keybinding_manager_bind_context(
            ctx->manager, key_sequence, entry->func.context, entry->name);
    }

    if (bind_result == LLE_SUCCESS) {
        ctx->result->bindings_applied++;
    } else {
        ctx->result->errors_count++;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * CONFIG LOADING API IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Load keybinding configuration from a string
 * @param manager Keybinding manager to configure
 * @param content TOML configuration content string
 * @param result Structure to store load results and any errors
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_keybinding_load_from_string(lle_keybinding_manager_t *manager,
                                const char *content,
                                lle_keybinding_load_result_t *result) {
    if (!manager || !content || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Initialize result
    memset(result, 0, sizeof(*result));

    /// Initialize parser
    lle_theme_parser_t parser;
    lle_result_t init_result = lle_theme_parser_init(&parser, content);
    if (init_result != LLE_SUCCESS) {
        result->status = init_result;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Failed to initialize parser");
        return init_result;
    }

    /// Set up parser context
    keybinding_parser_ctx_t ctx = {.manager = manager, .result = result};

    /// Parse and apply bindings
    lle_result_t parse_result =
        lle_theme_parser_parse(&parser, keybinding_config_callback, &ctx);

    if (parse_result != LLE_SUCCESS) {
        result->status = parse_result;
        result->error_line = lle_theme_parser_error_line(&parser);
        result->error_column = lle_theme_parser_error_column(&parser);
        snprintf(result->error_msg, sizeof(result->error_msg), "%s",
                 lle_theme_parser_error(&parser));
        return parse_result;
    }

    result->status = LLE_SUCCESS;
    return LLE_SUCCESS;
}

/**
 * @brief Load keybinding configuration from a file
 * @param manager Keybinding manager to configure
 * @param filepath Path to the TOML configuration file
 * @param result Structure to store load results and any errors
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_keybinding_load_from_file(lle_keybinding_manager_t *manager,
                              const char *filepath,
                              lle_keybinding_load_result_t *result) {
    if (!manager || !filepath || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Initialize result
    memset(result, 0, sizeof(*result));
    snprintf(result->filepath, sizeof(result->filepath), "%s", filepath);

    /// Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        result->status = LLE_ERROR_NOT_FOUND;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "File not found: %s", filepath);
        return LLE_ERROR_NOT_FOUND;
    }

    /// Read file contents
    size_t content_size;
    char *content = read_file_contents(filepath, &content_size);
    if (!content) {
        result->status = LLE_ERROR_SYSTEM_CALL;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Failed to read file: %s", strerror(errno));
        return LLE_ERROR_SYSTEM_CALL;
    }

    /// Parse content
    lle_result_t load_result =
        lle_keybinding_load_from_string(manager, content, result);

    free(content);
    return load_result;
}

/**
 * @brief Load keybinding configuration from the user's config file
 * @param manager Keybinding manager to configure
 * @param result Structure to store load results and any errors
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if file missing
 * (optional)
 */
lle_result_t
lle_keybinding_load_user_config(lle_keybinding_manager_t *manager,
                                lle_keybinding_load_result_t *result) {
    if (!manager || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Get user config path
    char config_path[LLE_KEYBINDING_CONFIG_PATH_MAX];
    lle_result_t path_result =
        lle_keybinding_get_user_config_path(config_path, sizeof(config_path));
    if (path_result != LLE_SUCCESS) {
        memset(result, 0, sizeof(*result));
        result->status = path_result;
        return path_result;
    }

    /// Load from file (LLE_ERROR_NOT_FOUND is acceptable - config is optional)
    return lle_keybinding_load_from_file(manager, config_path, result);
}

/**
 * @brief Reload keybinding configuration from the user's config file
 * @param manager Keybinding manager to reconfigure
 * @param result Structure to store load results and any errors
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_keybinding_reload_user_config(lle_keybinding_manager_t *manager,
                                  lle_keybinding_load_result_t *result) {
    /// Reload is the same as load - just re-apply user config
    return lle_keybinding_load_user_config(manager, result);
}
