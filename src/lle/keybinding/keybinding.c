/**
 * @file keybinding.c
 * @brief Keybinding Engine Implementation
 *
 * Implements fast key sequence lookup and binding management using libhashtable
 * through LLE's hashtable wrapper for O(1) lookup performance. Supports both
 * simple and context-aware keybinding actions with Emacs and Vi mode presets.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md
 * Implementation Plan:
 * docs/lle_specification/critical_gaps/25_IMPLEMENTATION_PLAN.md
 */

#include "lle/keybinding.h"
#include "libhashtable/ht.h"
#include "lle/hashtable.h"
#include "lle/keybinding_actions.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================
 */

/**
 * Keybinding entry - stores action and metadata
 */
typedef struct {
    lle_keybinding_action_t action;
    char *function_name;
    lle_keymap_mode_t mode;
} lle_keybinding_entry_t;

/**
 * Multi-key sequence buffer for chords
 */
typedef struct {
    lle_key_event_t keys[LLE_MAX_SEQUENCE_KEYS];
    size_t count;
    struct timeval last_key_time;
} lle_key_sequence_buffer_t;

/**
 * Keybinding manager structure
 */
struct lle_keybinding_manager {
    lle_strstr_hashtable_t *bindings;     ///< Key sequence -> entry mapping
    lle_keymap_mode_t current_mode;       ///< Active keymap mode
    lle_key_sequence_buffer_t seq_buffer; ///< Multi-key sequence buffer
    lush_memory_pool_t *pool;             ///< Memory pool for allocations

    /// Performance tracking
    uint64_t total_lookups;
    uint64_t total_lookup_time_us;
    uint64_t max_lookup_time_us;
};

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get current time in microseconds
 * @return Current time as microseconds since epoch
 */
static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/**
 * @brief Allocate string using memory pool or malloc
 * @param pool Memory pool to use (NULL for malloc)
 * @param str Source string to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
static char *keybinding_strdup(lush_memory_pool_t *pool, const char *str) {
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char *copy;

    if (pool != NULL) {
        copy = (char *)lush_pool_alloc(len + 1);
    } else {
        copy = (char *)malloc(len + 1);
    }

    if (copy != NULL) {
        memcpy(copy, str, len + 1);
    }

    return copy;
}

/**
 * @brief Free string from memory pool or malloc
 * @param pool Memory pool used for allocation (NULL if malloc was used)
 * @param str String to free (may be NULL)
 */
static void keybinding_free_string(lush_memory_pool_t *pool, char *str) {
    if (str == NULL) {
        return;
    }

    if (pool != NULL) {
        lush_pool_free(str);
    } else {
        free(str);
    }
}

/**
 * @brief Free keybinding entry and its allocated strings
 * @param pool Memory pool used for allocation
 * @param entry Keybinding entry to free (may be NULL)
 */
static void free_keybinding_entry(lush_memory_pool_t *pool,
                                  lle_keybinding_entry_t *entry) {
    if (entry == NULL) {
        return;
    }

    if (entry->function_name != NULL) {
        keybinding_free_string(pool, entry->function_name);
    }

    if (pool != NULL) {
        lush_pool_free(entry);
    } else {
        free(entry);
    }
}

/**
 * @brief Parse special key name to key code
 * @param name Key name string (e.g., "RET", "TAB", "UP", "F1")
 * @param key_out Pointer to store the parsed special key code
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_FORMAT if unknown key
 */
static lle_result_t parse_special_key(const char *name,
                                      lle_special_key_t *key_out) {
    if (strcmp(name, "RET") == 0 || strcmp(name, "RETURN") == 0) {
        *key_out = LLE_KEY_ENTER;
    } else if (strcmp(name, "TAB") == 0) {
        *key_out = LLE_KEY_TAB;
    } else if (strcmp(name, "DEL") == 0 || strcmp(name, "DELETE") == 0) {
        *key_out = LLE_KEY_DELETE;
    } else if (strcmp(name, "BACKSPACE") == 0 || strcmp(name, "BS") == 0) {
        *key_out = LLE_KEY_BACKSPACE;
    } else if (strcmp(name, "UP") == 0) {
        *key_out = LLE_KEY_UP;
    } else if (strcmp(name, "DOWN") == 0) {
        *key_out = LLE_KEY_DOWN;
    } else if (strcmp(name, "LEFT") == 0) {
        *key_out = LLE_KEY_LEFT;
    } else if (strcmp(name, "RIGHT") == 0) {
        *key_out = LLE_KEY_RIGHT;
    } else if (strcmp(name, "HOME") == 0) {
        *key_out = LLE_KEY_HOME;
    } else if (strcmp(name, "END") == 0) {
        *key_out = LLE_KEY_END;
    } else if (strcmp(name, "PAGEUP") == 0 || strcmp(name, "PGUP") == 0) {
        *key_out = LLE_KEY_PAGE_UP;
    } else if (strcmp(name, "PAGEDOWN") == 0 || strcmp(name, "PGDN") == 0) {
        *key_out = LLE_KEY_PAGE_DOWN;
    } else if (strcmp(name, "INSERT") == 0 || strcmp(name, "INS") == 0) {
        *key_out = LLE_KEY_INSERT;
    } else if (strcmp(name, "ESC") == 0 || strcmp(name, "ESCAPE") == 0) {
        *key_out = LLE_KEY_ESCAPE;
    } else if (name[0] == 'F' && isdigit(name[1])) {
        /// F1-F12
        int num = atoi(name + 1);
        if (num >= 1 && num <= 12) {
            *key_out = LLE_KEY_F1 + (num - 1);
        } else {
            return LLE_ERROR_INVALID_FORMAT;
        }
    } else {
        return LLE_ERROR_INVALID_FORMAT;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Create a new keybinding manager instance
 * @param manager Pointer to store the created manager
 * @param pool Memory pool for allocations (NULL to use malloc/free)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_manager_create(lle_keybinding_manager_t **manager,
                                           lush_memory_pool_t *pool) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Allocate manager structure
    lle_keybinding_manager_t *new_manager;
    if (pool != NULL) {
        new_manager = (lle_keybinding_manager_t *)lush_pool_alloc(
            sizeof(lle_keybinding_manager_t));
    } else {
        new_manager = (lle_keybinding_manager_t *)malloc(
            sizeof(lle_keybinding_manager_t));
    }

    if (new_manager == NULL) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(new_manager, 0, sizeof(lle_keybinding_manager_t));
    new_manager->pool = pool;
    new_manager->current_mode = LLE_KEYMAP_EMACS;

    /// Create hashtable for bindings
    lle_hashtable_config_t config;
    lle_hashtable_config_init_default(&config);
    config.initial_capacity = LLE_KEYBINDING_INITIAL_SIZE;
    config.memory_pool = pool;
    config.use_memory_pool = (pool != NULL);
    config.hashtable_name = "keybindings";

    /// Use factory to create hashtable
    lle_hashtable_factory_t *factory = NULL;
    lle_result_t result = lle_hashtable_factory_init(&factory, pool);
    if (result != LLE_SUCCESS) {
        if (pool != NULL) {
            lush_pool_free(new_manager);
        } else {
            free(new_manager);
        }
        return result;
    }

    result = lle_hashtable_factory_create_strstr(factory, &config,
                                                 &new_manager->bindings);
    lle_hashtable_factory_destroy(factory);

    if (result != LLE_SUCCESS) {
        if (pool != NULL) {
            lush_pool_free(new_manager);
        } else {
            free(new_manager);
        }
        return result;
    }

    *manager = new_manager;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a keybinding manager and free all resources
 * @param manager Keybinding manager to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if manager is NULL
 */
lle_result_t lle_keybinding_manager_destroy(lle_keybinding_manager_t *manager) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Free all keybinding entries before destroying hashtable
    if (manager->bindings != NULL) {
        /// Use libhashtable enumeration to iterate through all entries
        ht_enum_t *enumerator = ht_strstr_enum_create(manager->bindings->ht);
        if (enumerator != NULL) {
            const char *key;
            const char *value_str;

            /// Iterate through all key-value pairs
            while (ht_strstr_enum_next(enumerator, &key, &value_str)) {
                /// Value is a pointer stored as string - convert back to
                /// pointer
                lle_keybinding_entry_t *entry;
                sscanf(value_str, "%p", (void **)&entry);

                /// Free the entry and its allocated strings
                free_keybinding_entry(manager->pool, entry);
            }

            ht_strstr_enum_destroy(enumerator);
        }

        /// Now destroy the hashtable itself
        lle_strstr_hashtable_destroy(manager->bindings);
    }

    /// Free manager structure
    if (manager->pool != NULL) {
        lush_pool_free(manager);
    } else {
        free(manager);
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * KEY SEQUENCE PARSING
 * ============================================================================
 */

/**
 * @brief Parse a key sequence string into a key event structure
 * @param key_sequence Key sequence string (e.g., "C-x", "M-f", "RET")
 * @param key_event_out Pointer to store the parsed key event
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_key_sequence_parse(const char *key_sequence,
                                    lle_key_event_t *key_event_out) {
    if (key_sequence == NULL || key_event_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    memset(key_event_out, 0, sizeof(lle_key_event_t));

    const char *p = key_sequence;

    /// Parse modifiers
    while (*p != '\0') {
        if (p[0] == 'C' && p[1] == '-') {
            key_event_out->ctrl = true;
            p += 2;
        } else if (p[0] == 'M' && p[1] == '-') {
            key_event_out->alt = true;
            p += 2;
        } else if (p[0] == 'S' && p[1] == '-') {
            key_event_out->shift = true;
            p += 2;
        } else {
            break;
        }
    }

    /// Parse key
    if (*p == '\0') {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /// Check for special key name
    if (isupper(*p) || strcmp(p, "RET") == 0) {
        lle_special_key_t special_key;
        lle_result_t result = parse_special_key(p, &special_key);
        if (result == LLE_SUCCESS) {
            key_event_out->is_special = true;
            key_event_out->special_key = special_key;
            return LLE_SUCCESS;
        }
    }

    /// Regular character
    if (key_event_out->ctrl && islower(*p)) {
        /// Ctrl+letter is typically uppercase in ASCII control codes
        key_event_out->codepoint = toupper(*p);
    } else {
        key_event_out->codepoint = (uint32_t)(*p);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Convert a key event structure to a string representation
 * @param key_event Key event to convert
 * @param buffer Buffer to store the string
 * @param buffer_size Size of the buffer
 * @return LLE_SUCCESS on success, LLE_ERROR_BUFFER_OVERFLOW if buffer too small
 */
lle_result_t lle_key_event_to_string(const lle_key_event_t *key_event,
                                     char *buffer, size_t buffer_size) {
    if (key_event == NULL || buffer == NULL || buffer_size == 0) {
        return LLE_ERROR_NULL_POINTER;
    }

    char *p = buffer;
    size_t remaining = buffer_size;

    /// Add modifiers
    if (key_event->ctrl) {
        if (remaining < 3)
            return LLE_ERROR_BUFFER_OVERFLOW;
        *p++ = 'C';
        *p++ = '-';
        remaining -= 2;
    }
    if (key_event->alt) {
        if (remaining < 3)
            return LLE_ERROR_BUFFER_OVERFLOW;
        *p++ = 'M';
        *p++ = '-';
        remaining -= 2;
    }
    if (key_event->shift) {
        if (remaining < 3)
            return LLE_ERROR_BUFFER_OVERFLOW;
        *p++ = 'S';
        *p++ = '-';
        remaining -= 2;
    }

    /// Add key
    if (key_event->is_special) {
        const char *name = NULL;
        switch (key_event->special_key) {
        case LLE_KEY_ENTER:
            name = "RET";
            break;
        case LLE_KEY_TAB:
            name = "TAB";
            break;
        case LLE_KEY_BACKSPACE:
            name = "BACKSPACE";
            break;
        case LLE_KEY_DELETE:
            name = "DEL";
            break;
        case LLE_KEY_UP:
            name = "UP";
            break;
        case LLE_KEY_DOWN:
            name = "DOWN";
            break;
        case LLE_KEY_LEFT:
            name = "LEFT";
            break;
        case LLE_KEY_RIGHT:
            name = "RIGHT";
            break;
        case LLE_KEY_HOME:
            name = "HOME";
            break;
        case LLE_KEY_END:
            name = "END";
            break;
        case LLE_KEY_PAGE_UP:
            name = "PAGEUP";
            break;
        case LLE_KEY_PAGE_DOWN:
            name = "PAGEDOWN";
            break;
        case LLE_KEY_INSERT:
            name = "INSERT";
            break;
        case LLE_KEY_ESCAPE:
            name = "ESC";
            break;
        default:
            if (key_event->special_key >= LLE_KEY_F1 &&
                key_event->special_key <= LLE_KEY_F12) {
                int fn = key_event->special_key - LLE_KEY_F1 + 1;
                if (remaining < 4)
                    return LLE_ERROR_BUFFER_OVERFLOW;
                snprintf(p, remaining, "F%d", fn);
                return LLE_SUCCESS;
            }
            return LLE_ERROR_INVALID_PARAMETER;
        }

        size_t len = strlen(name);
        if (remaining <= len)
            return LLE_ERROR_BUFFER_OVERFLOW;
        strcpy(p, name);
    } else {
        if (remaining < 2)
            return LLE_ERROR_BUFFER_OVERFLOW;
        /// Convert uppercase back to lowercase for ctrl+letter combinations
        char ch = (char)key_event->codepoint;
        if (key_event->ctrl && isupper(ch)) {
            ch = tolower(ch);
        }
        *p++ = ch;
        *p = '\0';
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * KEYBINDING REGISTRATION
 * ============================================================================
 */

/**
 * @brief Bind a simple action to a key sequence
 * @param manager Keybinding manager instance
 * @param key_sequence Key sequence string (e.g., "C-f", "M-x")
 * @param action Simple action function pointer
 * @param function_name Human-readable function name for introspection
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_manager_bind(lle_keybinding_manager_t *manager,
                                         const char *key_sequence,
                                         lle_action_simple_t action,
                                         const char *function_name) {
    if (manager == NULL || key_sequence == NULL || action == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Create keybinding entry
    lle_keybinding_entry_t *entry;
    if (manager->pool != NULL) {
        entry = (lle_keybinding_entry_t *)lush_pool_alloc(
            sizeof(lle_keybinding_entry_t));
    } else {
        entry =
            (lle_keybinding_entry_t *)malloc(sizeof(lle_keybinding_entry_t));
    }

    if (entry == NULL) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize action as simple type
    entry->action.type = LLE_ACTION_TYPE_SIMPLE;
    entry->action.func.simple = action;
    entry->action.name = function_name;
    entry->mode = manager->current_mode;
    entry->function_name =
        function_name ? keybinding_strdup(manager->pool, function_name) : NULL;

    /// Convert entry to string for storage (hackish but works with strstr
    /// hashtable)
    char entry_str[32];
    snprintf(entry_str, sizeof(entry_str), "%p", (void *)entry);

    /// Insert into hashtable
    lle_result_t result =
        lle_strstr_hashtable_insert(manager->bindings, key_sequence, entry_str);

    if (result != LLE_SUCCESS) {
        free_keybinding_entry(manager->pool, entry);
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Bind a context-aware action to a key sequence
 * @param manager Keybinding manager instance
 * @param key_sequence Key sequence string (e.g., "C-f", "M-x")
 * @param action Context-aware action function pointer
 * @param function_name Human-readable function name for introspection
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_manager_bind_context(
    lle_keybinding_manager_t *manager, const char *key_sequence,
    lle_action_context_t action, const char *function_name) {
    if (manager == NULL || key_sequence == NULL || action == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Create keybinding entry
    lle_keybinding_entry_t *entry;
    if (manager->pool != NULL) {
        entry = (lle_keybinding_entry_t *)lush_pool_alloc(
            sizeof(lle_keybinding_entry_t));
    } else {
        entry =
            (lle_keybinding_entry_t *)malloc(sizeof(lle_keybinding_entry_t));
    }

    if (entry == NULL) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize action as context-aware type
    entry->action.type = LLE_ACTION_TYPE_CONTEXT;
    entry->action.func.context = action;
    entry->action.name = function_name;
    entry->mode = manager->current_mode;
    entry->function_name =
        function_name ? keybinding_strdup(manager->pool, function_name) : NULL;

    /// Convert entry to string for storage (hackish but works with strstr
    /// hashtable)
    char entry_str[32];
    snprintf(entry_str, sizeof(entry_str), "%p", (void *)entry);

    /// Insert into hashtable
    lle_result_t result =
        lle_strstr_hashtable_insert(manager->bindings, key_sequence, entry_str);

    if (result != LLE_SUCCESS) {
        free_keybinding_entry(manager->pool, entry);
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Remove a keybinding for a key sequence
 * @param manager Keybinding manager instance
 * @param key_sequence Key sequence string to unbind
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_manager_unbind(lle_keybinding_manager_t *manager,
                                           const char *key_sequence) {
    if (manager == NULL || key_sequence == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Lookup entry first to free it
    const char *entry_str =
        lle_strstr_hashtable_lookup(manager->bindings, key_sequence);
    if (entry_str != NULL) {
        lle_keybinding_entry_t *entry;
        sscanf(entry_str, "%p", (void **)&entry);
        free_keybinding_entry(manager->pool, entry);
    }

    /// Remove from hashtable
    return lle_strstr_hashtable_delete(manager->bindings, key_sequence);
}

/**
 * @brief Clear all keybindings from the manager
 * @param manager Keybinding manager instance
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if manager is NULL
 */
lle_result_t lle_keybinding_manager_clear(lle_keybinding_manager_t *manager) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Note: This leaks entries, but since we're clearing everything,
    /// it's acceptable if the hashtable and manager are about to be destroyed.
    /// For a proper implementation, we'd need to enumerate and free each entry.
    lle_strstr_hashtable_clear(manager->bindings);

    return LLE_SUCCESS;
}

/* ============================================================================
 * KEY PROCESSING
 * ============================================================================
 */

/**
 * @brief Process a key event and execute the bound action
 * @param manager Keybinding manager instance
 * @param editor Editor instance for action execution
 * @param key_event Key event to process
 * @return LLE_SUCCESS if action executed, LLE_ERROR_NOT_FOUND if unbound
 */
lle_result_t
lle_keybinding_manager_process_key(lle_keybinding_manager_t *manager,
                                   lle_editor_t *editor,
                                   const lle_key_event_t *key_event) {
    if (manager == NULL || editor == NULL || key_event == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    uint64_t start_time = get_time_us();

    /// Convert key event to string
    char key_str[LLE_MAX_KEY_SEQUENCE_LENGTH];
    lle_result_t result =
        lle_key_event_to_string(key_event, key_str, sizeof(key_str));
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Lookup binding
    const char *entry_str =
        lle_strstr_hashtable_lookup(manager->bindings, key_str);
    if (entry_str == NULL) {
        /// Update stats
        uint64_t elapsed = get_time_us() - start_time;
        manager->total_lookups++;
        manager->total_lookup_time_us += elapsed;
        if (elapsed > manager->max_lookup_time_us) {
            manager->max_lookup_time_us = elapsed;
        }

        return LLE_ERROR_NOT_FOUND;
    }

    /// Parse entry pointer
    lle_keybinding_entry_t *entry;
    sscanf(entry_str, "%p", (void **)&entry);

    /// Update stats
    uint64_t elapsed = get_time_us() - start_time;
    manager->total_lookups++;
    manager->total_lookup_time_us += elapsed;
    if (elapsed > manager->max_lookup_time_us) {
        manager->max_lookup_time_us = elapsed;
    }

    /// Execute action - only simple actions supported here (no readline
    /// context)
    if (entry->action.type == LLE_ACTION_TYPE_SIMPLE) {
        return entry->action.func.simple(editor);
    } else {
        /// Context-aware actions cannot be executed without readline context
        return LLE_ERROR_INVALID_STATE;
    }
}

/**
 * @brief Reset the multi-key sequence buffer
 * @param manager Keybinding manager instance
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if manager is NULL
 */
lle_result_t
lle_keybinding_manager_reset_sequence(lle_keybinding_manager_t *manager) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    manager->seq_buffer.count = 0;
    return LLE_SUCCESS;
}

/* ============================================================================
 * KEYMAP MODE MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Set the current keymap mode (Emacs, Vi insert, Vi command)
 * @param manager Keybinding manager instance
 * @param mode Keymap mode to set
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if manager is NULL
 */
lle_result_t lle_keybinding_manager_set_mode(lle_keybinding_manager_t *manager,
                                             lle_keymap_mode_t mode) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    manager->current_mode = mode;
    return LLE_SUCCESS;
}

/**
 * @brief Get the current keymap mode
 * @param manager Keybinding manager instance
 * @param mode_out Pointer to store the current mode
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER on NULL argument
 */
lle_result_t lle_keybinding_manager_get_mode(lle_keybinding_manager_t *manager,
                                             lle_keymap_mode_t *mode_out) {
    if (manager == NULL || mode_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    *mode_out = manager->current_mode;
    return LLE_SUCCESS;
}

/* ============================================================================
 * PRESET LOADING
 * ============================================================================
 */

/**
 * @brief Load the Emacs-style keybinding preset
 *
 * Configures all GNU Readline compatible Emacs keybindings including
 * movement, editing, history, completion, and shell operations.
 *
 * @param manager Keybinding manager instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_keybinding_manager_load_emacs_preset(lle_keybinding_manager_t *manager) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Set mode to emacs
    manager->current_mode = LLE_KEYMAP_EMACS;

    /// ========================================================================
    /// GNU Readline Emacs-style Keybindings
    ///
    /// This loads all SIMPLE actions that operate on lle_editor_t only.
    /// Context-aware actions (requiring readline_context_t) must be registered
    /// separately in lle_readline.c after this preset loads.
    ///
    /// Context-aware bindings registered in lle_readline.c will OVERRIDE
    /// simple bindings registered here for: RIGHT, END, C-e, C-f, C-g,
    /// C-RIGHT, ESC, ENTER (these need autosuggestion/completion/readline
    /// access)
    /// ========================================================================

    lle_result_t result;

    /// ------------------------------------------------------------------------
    /// MOVEMENT - Character Level
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-f", lle_forward_char,
                                         "forward-char");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-b", lle_backward_char,
                                         "backward-char");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "RIGHT", lle_forward_char,
                                         "forward-char");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "LEFT", lle_backward_char,
                                         "backward-char");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// MOVEMENT - Word Level
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "M-f", lle_forward_word,
                                         "forward-word");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-b", lle_backward_word,
                                         "backward-word");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// MOVEMENT - Line Level
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-a", lle_beginning_of_line,
                                         "beginning-of-line");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-e", lle_end_of_line,
                                         "end-of-line");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "HOME", lle_beginning_of_line,
                                         "beginning-of-line");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "END", lle_end_of_line,
                                         "end-of-line");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// MOVEMENT - Buffer Level (Multiline)
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(
        manager, "M-<", lle_beginning_of_buffer, "beginning-of-buffer");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M->", lle_end_of_buffer,
                                         "end-of-buffer");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// DELETION - Character Level
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-d", lle_delete_char,
                                         "delete-char");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "DELETE", lle_delete_char,
                                         "delete-char");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(
        manager, "BACKSPACE", lle_backward_delete_char, "backward-delete-char");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// KILL/YANK - Line Operations
    /// ------------------------------------------------------------------------
    result =
        lle_keybinding_manager_bind(manager, "C-k", lle_kill_line, "kill-line");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-u", lle_unix_line_discard,
                                         "unix-line-discard");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// KILL/YANK - Word Operations
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-w", lle_unix_word_rubout,
                                         "unix-word-rubout");
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_keybinding_manager_bind(manager, "M-d", lle_kill_word, "kill-word");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(
        manager, "M-BACKSPACE", lle_backward_kill_word, "backward-kill-word");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// YANK (Paste)
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-y", lle_yank, "yank");
    if (result != LLE_SUCCESS)
        return result;

    result =
        lle_keybinding_manager_bind(manager, "M-y", lle_yank_pop, "yank-pop");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// CASE CHANGES
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "M-u", lle_upcase_word,
                                         "upcase-word");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-l", lle_downcase_word,
                                         "downcase-word");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-c", lle_capitalize_word,
                                         "capitalize-word");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// TRANSPOSE
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-t", lle_transpose_chars,
                                         "transpose-chars");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-t", lle_transpose_words,
                                         "transpose-words");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// HISTORY NAVIGATION
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-p", lle_history_previous,
                                         "history-previous");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-n", lle_history_next,
                                         "history-next");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "UP", lle_smart_up_arrow,
                                         "smart-up-arrow");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "DOWN", lle_smart_down_arrow,
                                         "smart-down-arrow");
    if (result != LLE_SUCCESS)
        return result;

    /// History search - not yet fully implemented but bind for future
    result = lle_keybinding_manager_bind(
        manager, "M-p", lle_history_search_backward, "history-search-backward");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(
        manager, "M-n", lle_history_search_forward, "history-search-forward");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// COMPLETION
    /// ------------------------------------------------------------------------
    result =
        lle_keybinding_manager_bind(manager, "TAB", lle_complete, "complete");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(
        manager, "M-?", lle_possible_completions, "possible-completions");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-*", lle_insert_completions,
                                         "insert-completions");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// SPECIAL FUNCTIONS
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "C-l", lle_clear_screen,
                                         "clear-screen");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-g", lle_abort_line,
                                         "abort-line");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "ENTER", lle_accept_line,
                                         "accept-line");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "RET", lle_accept_line,
                                         "accept-line");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// LITERAL INSERTION
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "S-ENTER",
                                         lle_insert_newline_literal,
                                         "insert-newline-literal");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-ENTER",
                                         lle_insert_newline_literal,
                                         "insert-newline-literal");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-q", lle_quoted_insert,
                                         "quoted-insert");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "C-v", lle_quoted_insert,
                                         "quoted-insert");
    if (result != LLE_SUCCESS)
        return result;

    result = lle_keybinding_manager_bind(manager, "M-TAB", lle_tab_insert,
                                         "tab-insert");
    if (result != LLE_SUCCESS)
        return result;

    /// ------------------------------------------------------------------------
    /// MISCELLANEOUS
    /// ------------------------------------------------------------------------
    result = lle_keybinding_manager_bind(manager, "M-\\",
                                         lle_delete_horizontal_space,
                                         "delete-horizontal-space");
    if (result != LLE_SUCCESS)
        return result;

    return LLE_SUCCESS;
}

/**
 * @brief Load the Vi insert mode keybinding preset
 * @param manager Keybinding manager instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_manager_load_vi_insert_preset(
    lle_keybinding_manager_t *manager) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Set mode to vi insert
    manager->current_mode = LLE_KEYMAP_VI_INSERT;

    /// Note: Vi bindings will be implemented in a future phase

    return LLE_SUCCESS;
}

/* ============================================================================
 * INTROSPECTION
 * ============================================================================
 */

/**
 * @brief List all current keybindings
 * @param manager Keybinding manager instance
 * @param bindings_out Pointer to store the bindings array
 * @param count_out Pointer to store the count of bindings
 * @return LLE_SUCCESS on success, error code on failure
 */
/// Fill context for lle_keybinding_manager_list_bindings()'s enumeration.
typedef struct {
    lle_keybinding_info_t *bindings; ///< Caller-allocated output array
    size_t capacity;                 ///< Number of slots in @c bindings
    size_t index;                    ///< Next slot to write
} list_bindings_fill_t;

/// Visitor: each hashtable value is a "%p" pointer to the entry; copy its
/// key sequence and action metadata into the next output slot.
static void list_bindings_visit(const char *key, const char *value,
                                void *user_data) {
    list_bindings_fill_t *fill = (list_bindings_fill_t *)user_data;
    if (fill->index >= fill->capacity) {
        return;
    }
    lle_keybinding_entry_t *entry = NULL;
    if (sscanf(value, "%p", (void **)&entry) != 1 || entry == NULL) {
        return;
    }
    lle_keybinding_info_t *info = &fill->bindings[fill->index];
    memset(info, 0, sizeof(*info));
    snprintf(info->key_sequence, sizeof(info->key_sequence), "%s", key);
    info->action = entry->action;
    info->function_name = entry->function_name;
    info->mode = entry->mode;
    fill->index++;
}

lle_result_t
lle_keybinding_manager_list_bindings(lle_keybinding_manager_t *manager,
                                     lle_keybinding_info_t **bindings_out,
                                     size_t *count_out) {
    if (manager == NULL || bindings_out == NULL || count_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    /// Get hashtable size
    size_t count = lle_strstr_hashtable_size(manager->bindings);
    if (count == 0) {
        *bindings_out = NULL;
        *count_out = 0;
        return LLE_SUCCESS;
    }

    /// Allocate bindings array
    lle_keybinding_info_t *bindings;
    if (manager->pool != NULL) {
        bindings = (lle_keybinding_info_t *)lush_pool_alloc(
            sizeof(lle_keybinding_info_t) * count);
    } else {
        bindings = (lle_keybinding_info_t *)malloc(
            sizeof(lle_keybinding_info_t) * count);
    }

    if (bindings == NULL) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Enumerate the bindings table and copy each entry into the output
    /// array. ht_strstr_enum (via lle_strstr_hashtable_foreach) walks the
    /// table reliably -- the same API backs the wrapper's clear() and the
    /// executor's associative-array iteration.
    list_bindings_fill_t fill = {
        .bindings = bindings, .capacity = count, .index = 0};
    lle_strstr_hashtable_foreach(manager->bindings, list_bindings_visit, &fill);

    *bindings_out = bindings;
    *count_out = fill.index;

    return LLE_SUCCESS;
}

/**
 * @brief Look up the action bound to a key sequence
 * @param manager Keybinding manager instance
 * @param key_sequence Key sequence string to look up
 * @param action_out Pointer to store the action (not a copy)
 * @return LLE_SUCCESS if found, LLE_ERROR_NOT_FOUND if unbound
 */
lle_result_t
lle_keybinding_manager_lookup(lle_keybinding_manager_t *manager,
                              const char *key_sequence,
                              lle_keybinding_action_t **action_out) {
    if (manager == NULL || key_sequence == NULL || action_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    const char *entry_str =
        lle_strstr_hashtable_lookup(manager->bindings, key_sequence);
    if (entry_str == NULL) {
        return LLE_ERROR_NOT_FOUND;
    }

    lle_keybinding_entry_t *entry;
    sscanf(entry_str, "%p", (void **)&entry);

    /// Return pointer to action structure in entry
    *action_out = &entry->action;
    return LLE_SUCCESS;
}

/**
 * @brief Get the number of registered keybindings
 * @param manager Keybinding manager instance
 * @param count_out Pointer to store the count
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER on NULL argument
 */
lle_result_t lle_keybinding_manager_get_count(lle_keybinding_manager_t *manager,
                                              size_t *count_out) {
    if (manager == NULL || count_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    *count_out = lle_strstr_hashtable_size(manager->bindings);
    return LLE_SUCCESS;
}

/* ============================================================================
 * PERFORMANCE MONITORING
 * ============================================================================
 */

/**
 * @brief Get performance statistics for keybinding lookups
 * @param manager Keybinding manager instance
 * @param avg_lookup_time_us Pointer to store average lookup time (may be NULL)
 * @param max_lookup_time_us Pointer to store max lookup time (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if manager is NULL
 */
lle_result_t lle_keybinding_manager_get_stats(lle_keybinding_manager_t *manager,
                                              uint64_t *avg_lookup_time_us,
                                              uint64_t *max_lookup_time_us) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    if (avg_lookup_time_us != NULL) {
        if (manager->total_lookups > 0) {
            *avg_lookup_time_us =
                manager->total_lookup_time_us / manager->total_lookups;
        } else {
            *avg_lookup_time_us = 0;
        }
    }

    if (max_lookup_time_us != NULL) {
        *max_lookup_time_us = manager->max_lookup_time_us;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Reset performance statistics counters
 * @param manager Keybinding manager instance
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if manager is NULL
 */
lle_result_t
lle_keybinding_manager_reset_stats(lle_keybinding_manager_t *manager) {
    if (manager == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    manager->total_lookups = 0;
    manager->total_lookup_time_us = 0;
    manager->max_lookup_time_us = 0;

    return LLE_SUCCESS;
}
