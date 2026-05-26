/**
 * @file symtable.c
 * @brief Optimized Symbol Table Implementation for Lush Shell
 *
 * This module provides a high-performance symbol table system that leverages
 * libhashtable's ht_strstr_t interface while maintaining full POSIX shell
 * scoping semantics and variable metadata.
 *
 * Key Features:
 * - Uses libhashtable ht_strstr_t interface for maximum performance
 * - FNV1A hash algorithm for superior distribution vs djb2
 * - Serialized variable metadata for optimal performance
 * - Maintains existing scope chain logic for POSIX compliance
 * - Full API compatibility with legacy implementation
 * - Automated memory management via libhashtable
 *
 * The symbol table supports multiple scope levels (global, function, loop,
 * subshell, conditional) with proper variable shadowing and lookup semantics.
 * Variables can have flags for export, readonly, local, and special handling.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "symtable.h"

#include "ht.h"
#include "init.h"
#include "lush.h"
#include "shell_mode.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/// ============================================================================
/// GLOBAL STATE
/// ============================================================================

/// Global manager
static symtable_manager_t *global_manager = NULL;

/// Forward declaration so scope-pop / manager-free paths can reach
/// the helper before its definition further down.
static void free_arrays_in_scope(symtable_scope_t *scope);

/// Legacy compatibility structures
static symtable_t dummy_symtable = {0, NULL, NULL};

/// Special variable tracking
static time_t shell_start_time = 0;  /// For $SECONDS
static unsigned int random_seed = 0; /// For $RANDOM
static int current_lineno = 0;       /// For $LINENO

/// Constants
#define DEFAULT_HT_FLAGS (HT_STR_NONE | HT_SEED_RANDOM)
#define MAX_SCOPE_DEPTH 256
#define METADATA_SEPARATOR "|"
#define METADATA_BUFFER_SIZE 64

/// Forward declarations

/// ============================================================================
/// STRUCTURES
/// ============================================================================

/// Enhanced manager structure
struct symtable_manager {
    symtable_scope_t *current_scope; /// Current active scope
    symtable_scope_t *global_scope;  /// Global scope reference
    size_t max_scope_level;          /// Maximum nesting depth
    bool debug_mode;                 /// Debug output enabled
};

/// ============================================================================
/// METADATA SERIALIZATION UTILITIES
/// ============================================================================

/**
 * @brief Serialize variable metadata into a string format
 *
 * Creates a serialized representation of variable data in the format:
 * "value|type|flags|scope_level" for storage in the hash table.
 *
 * @param value Variable value (NULL treated as empty string)
 * @param type Variable type (string, integer, array, etc.)
 * @param flags Variable flags (exported, readonly, local, etc.)
 * @param scope_level Scope level where variable is defined
 * @return Allocated serialized string, or NULL on allocation failure
 */
static char *serialize_variable(const char *value, symvar_type_t type,
                                symvar_flags_t flags, size_t scope_level) {
    if (!value) {
        value = "";
    }

    /// Calculate needed size
    size_t value_len = strlen(value);
    size_t total_size = value_len + METADATA_BUFFER_SIZE;

    char *serialized = malloc(total_size);
    if (!serialized) {
        return NULL;
    }

    snprintf(serialized, total_size, "%s%s%d%s%d%s%zu", value,
             METADATA_SEPARATOR, (int)type, METADATA_SEPARATOR, (int)flags,
             METADATA_SEPARATOR, scope_level);

    return serialized;
}

/**
 * @brief Deserialize variable metadata from string format
 *
 * Parses a serialized variable string and creates a symvar_t structure.
 * Handles empty fields correctly by parsing separators manually.
 *
 * @param name Variable name to associate with the result
 * @param serialized Serialized string in "value|type|flags|scope_level" format
 * @return Allocated symvar_t structure, or NULL on failure
 */
static symvar_t *deserialize_variable(const char *name,
                                      const char *serialized) {
    if (!serialized || !name) {
        return NULL;
    }

    symvar_t *var = malloc(sizeof(symvar_t));
    if (!var) {
        return NULL;
    }

    /// Initialize defaults
    var->name = strdup(name);
    var->value = NULL;
    var->type = SYMVAR_STRING;
    var->flags = SYMVAR_NONE;
    var->scope_level = 0;
    var->array = NULL;
    var->next = NULL;

    if (!var->name) {
        free(var);
        return NULL;
    }

    /// Parse serialized string manually to handle empty values correctly
    char *serialized_copy = strdup(serialized);
    if (!serialized_copy) {
        free(var->name);
        free(var);
        return NULL;
    }

    /// Find the separators manually to handle empty fields
    char *pos = serialized_copy;
    char *sep1 = strstr(pos, METADATA_SEPARATOR);
    if (sep1) {
        *sep1 = '\0';
        var->value = strdup(pos);

        pos = sep1 + strlen(METADATA_SEPARATOR);
        char *sep2 = strstr(pos, METADATA_SEPARATOR);
        if (sep2) {
            *sep2 = '\0';
            var->type = (symvar_type_t)atoi(pos);

            pos = sep2 + strlen(METADATA_SEPARATOR);
            char *sep3 = strstr(pos, METADATA_SEPARATOR);
            if (sep3) {
                *sep3 = '\0';
                var->flags = (symvar_flags_t)atoi(pos);

                pos = sep3 + strlen(METADATA_SEPARATOR);
                var->scope_level = (size_t)atoi(pos);
            }
        }
    }

    free(serialized_copy);

    /// Ensure we have a value
    if (!var->value) {
        var->value = strdup("");
    }

    /// For SYMVAR_ARRAY entries, value is the array_value_t pointer as
    /// a hex string; parse it back into the typed array field so
    /// callers reading symvar.array directly see the live pointer
    /// without re-scanning value on every access.
    if (var->type == SYMVAR_ARRAY && var->value && var->value[0]) {
        void *ptr = NULL;
        if (sscanf(var->value, "%p", &ptr) == 1) {
            var->array = (array_value_t *)ptr;
        }
    }

    return var;
}

/**
 * @brief Free a deserialized variable structure
 *
 * Frees all memory associated with a symvar_t structure including
 * the name and value strings.
 *
 * @param var Variable structure to free (NULL is safe)
 */
static void free_symvar(symvar_t *var) {
    if (!var) {
        return;
    }
    free(var->name);
    free(var->value);
    free(var);
}

/// ============================================================================
/// CORE IMPLEMENTATION
/// ============================================================================

/**
 * @brief Create a new symbol table manager
 *
 * Allocates and initializes a symbol table manager with a global scope.
 * The global scope uses libhashtable with FNV1A hashing for performance.
 *
 * @return Newly allocated manager, or NULL on allocation failure
 */
symtable_manager_t *symtable_manager_new(void) {
    symtable_manager_t *manager = calloc(1, sizeof(symtable_manager_t));
    if (!manager) {
        return NULL;
    }

    /// Create global scope
    symtable_scope_t *global = calloc(1, sizeof(symtable_scope_t));
    if (!global) {
        free(manager);
        return NULL;
    }

    global->scope_type = SCOPE_GLOBAL;
    global->level = 0;
    global->vars_ht = ht_strstr_create(DEFAULT_HT_FLAGS);
    global->parent = NULL;
    global->scope_name = strdup("global");

    if (!global->vars_ht || !global->scope_name) {
        if (global->vars_ht) {
            ht_strstr_destroy(global->vars_ht);
        }
        free(global->scope_name);
        free(global);
        free(manager);
        return NULL;
    }

    manager->current_scope = global;
    manager->global_scope = global;
    manager->max_scope_level = 0;
    manager->debug_mode = false;

    return manager;
}

/**
 * @brief Free a symbol table manager and all its scopes
 *
 * Pops all scopes from the scope chain and frees all associated memory
 * including hash tables and scope names.
 *
 * @param manager Manager to free (NULL is safe)
 */
void symtable_manager_free(symtable_manager_t *manager) {
    if (!manager) {
        return;
    }

    /// Pop all scopes to free memory. Each scope first surrenders
    /// any array_value_t backing memory bound at that level
    /// (free_arrays_in_scope) before its vars_ht is destroyed --
    /// otherwise the serialized hex pointers vanish and we leak the
    /// arrays.
    while (manager->current_scope &&
           manager->current_scope != manager->global_scope) {
        symtable_scope_t *old_scope = manager->current_scope;
        manager->current_scope = old_scope->parent;

        free_arrays_in_scope(old_scope);
        if (old_scope->vars_ht) {
            ht_strstr_destroy(old_scope->vars_ht);
        }
        free(old_scope->scope_name);
        free(old_scope);
    }

    /// Free global scope (same array-cleanup-then-destroy sequence).
    if (manager->global_scope) {
        free_arrays_in_scope(manager->global_scope);
        if (manager->global_scope->vars_ht) {
            ht_strstr_destroy(manager->global_scope->vars_ht);
        }
        free(manager->global_scope->scope_name);
        free(manager->global_scope);
    }

    free(manager);
}

/**
 * @brief Enable or disable debug mode for the manager
 *
 * When debug mode is enabled, scope push/pop and variable set operations
 * print diagnostic messages to stdout/stderr.
 *
 * @param manager Manager to configure
 * @param debug True to enable debug output, false to disable
 */
void symtable_manager_set_debug(symtable_manager_t *manager, bool debug) {
    if (manager) {
        manager->debug_mode = debug;
    }
}

/**
 * @brief Find a variable in the scope chain
 *
 * Searches from the given scope up through parent scopes until a variable
 * with the given name is found. Skips variables marked as unset.
 *
 * @param scope Starting scope for the search
 * @param name Variable name to find
 * @return Allocated symvar_t if found, NULL otherwise
 */
static symvar_t *find_var(symtable_scope_t *scope, const char *name) {
    if (!scope || !name) {
        return NULL;
    }

    while (scope) {
        const char *serialized = ht_strstr_get(scope->vars_ht, name);
        if (serialized) {
            symvar_t *var = deserialize_variable(name, serialized);
            if (var && !(var->flags & SYMVAR_UNSET)) {
                return var;
            }
            free_symvar(var);
        }
        scope = scope->parent;
    }

    return NULL;
}

/**
 * @brief Push a new scope onto the scope stack
 *
 * Creates a new scope with its own hash table and links it as the
 * current scope. The new scope inherits from parent scopes for variable
 * lookups but has its own storage for local variables.
 *
 * @param manager Symbol table manager
 * @param type Type of scope (global, function, loop, subshell, conditional)
 * @param name Human-readable name for debugging
 * @return 0 on success, -1 on failure or max depth exceeded
 */
int symtable_push_scope(symtable_manager_t *manager, scope_type_t type,
                        const char *name) {
    if (!manager || !name) {
        return -1;
    }

    if (manager->current_scope->level >= MAX_SCOPE_DEPTH) {
        if (manager->debug_mode) {
            fprintf(stderr, "ERROR: Maximum scope depth exceeded\n");
        }
        return -1;
    }

    symtable_scope_t *new_scope = calloc(1, sizeof(symtable_scope_t));
    if (!new_scope) {
        return -1;
    }

    new_scope->scope_type = type;
    new_scope->level = manager->current_scope->level + 1;
    new_scope->vars_ht = ht_strstr_create(DEFAULT_HT_FLAGS);
    new_scope->parent = manager->current_scope;
    new_scope->scope_name = strdup(name);

    if (!new_scope->vars_ht || !new_scope->scope_name) {
        if (new_scope->vars_ht) {
            ht_strstr_destroy(new_scope->vars_ht);
        }
        free(new_scope->scope_name);
        free(new_scope);
        return -1;
    }

    manager->current_scope = new_scope;
    if (new_scope->level > manager->max_scope_level) {
        manager->max_scope_level = new_scope->level;
    }

    if (manager->debug_mode) {
        printf("DEBUG: Pushed scope '%s' (level %zu)\n", name,
               new_scope->level);
    }

    return 0;
}

/**
 * @brief Capture the current scope for a future lexical-scope push.
 *
 * Returns an opaque borrowed pointer to the scope that was current at
 * call time. Caller stores it in a `typed_fn` record (or equivalent)
 * and feeds it to symtable_push_lexical_scope when invoking the
 * closure. The pointer must NOT be freed by the caller; it is owned
 * by the symtable's scope stack.
 *
 * @param manager Symbol table manager
 * @return Opaque pointer to the current scope, or NULL on error
 */
void *symtable_capture_scope_for_lexical(symtable_manager_t *manager) {
    if (!manager) {
        return NULL;
    }
    return manager->current_scope;
}

/**
 * @brief Push a SCOPE_LEXICAL frame with a captured parent.
 *
 * Identical to symtable_push_scope(SCOPE_LEXICAL, name) except that
 * the new frame's `parent` is the supplied captured pointer, not the
 * dynamic current_scope. This is what gives typed-function (`fn`)
 * bodies lexical (closure) semantics per SEMANTICS §5.3: free names
 * inside the body resolve through the declaration-site scope chain,
 * not the call-site chain.
 *
 * `level` is one more than the captured parent's level (preserving
 * the depth-limit check), and the scope's lifecycle on pop is the
 * same as for any other scope -- only the parent linkage differs.
 *
 * @param manager Symbol table manager
 * @param name Scope name for diagnostics (typically the fn name)
 * @param captured_parent Opaque pointer from
 *                        symtable_capture_scope_for_lexical
 * @return 0 on success, -1 on error
 */
int symtable_push_lexical_scope(symtable_manager_t *manager, const char *name,
                                void *captured_parent) {
    if (!manager || !name || !captured_parent) {
        return -1;
    }
    symtable_scope_t *parent = (symtable_scope_t *)captured_parent;

    if (manager->current_scope->level >= MAX_SCOPE_DEPTH) {
        if (manager->debug_mode) {
            fprintf(stderr, "ERROR: Maximum scope depth exceeded\n");
        }
        return -1;
    }

    symtable_scope_t *new_scope = calloc(1, sizeof(symtable_scope_t));
    if (!new_scope) {
        return -1;
    }

    new_scope->scope_type = SCOPE_LEXICAL;
    new_scope->level = manager->current_scope->level + 1;
    new_scope->vars_ht = ht_strstr_create(DEFAULT_HT_FLAGS);
    new_scope->parent = parent;
    new_scope->scope_name = strdup(name);

    if (!new_scope->vars_ht || !new_scope->scope_name) {
        if (new_scope->vars_ht) {
            ht_strstr_destroy(new_scope->vars_ht);
        }
        free(new_scope->scope_name);
        free(new_scope);
        return -1;
    }

    /// Stash the dynamic caller so pop can restore it. The lexical
    /// frame's `parent` field points at the captured scope (lookup
    /// direction), but the SCOPE STACK is still LIFO -- pop must
    /// return to whoever pushed us, not to the captured site. We
    /// piggyback the link through a side field on the manager so
    /// existing pop machinery can find it without growing the public
    /// struct shape.
    new_scope->dynamic_caller = manager->current_scope;

    manager->current_scope = new_scope;
    if (new_scope->level > manager->max_scope_level) {
        manager->max_scope_level = new_scope->level;
    }

    if (manager->debug_mode) {
        printf("DEBUG: Pushed lexical scope '%s' (level %zu, captured "
               "parent level %zu)\n",
               name, new_scope->level, parent->level);
    }

    return 0;
}

/**
 * @brief Pop the current scope from the scope stack
 *
 * Removes the current scope, freeing its hash table and memory,
 * and restores the parent scope as current. Cannot pop the global scope.
 *
 * @param manager Symbol table manager
 * @return 0 on success, -1 if at global scope or invalid manager
 */
/// Walk a scope's vars_ht and free the array_value_t backing every
/// SYMVAR_ARRAY entry. Must run before ht_strstr_destroy on the scope,
/// otherwise the serialized hex pointers go away and we leak the
/// arrays. The scope's vars_ht itself is freed by the caller.
static void free_arrays_in_scope(symtable_scope_t *scope) {
    if (!scope || !scope->vars_ht) {
        return;
    }
    ht_enum_t *e = ht_strstr_enum_create(scope->vars_ht);
    if (!e) {
        return;
    }
    const char *key;
    const char *serialized;
    while (ht_strstr_enum_next(e, &key, &serialized)) {
        symvar_t *var = deserialize_variable(key, serialized);
        if (var && var->type == SYMVAR_ARRAY && var->array) {
            symtable_array_free(var->array);
        }
        free_symvar(var);
    }
    ht_strstr_enum_destroy(e);
}

int symtable_pop_scope(symtable_manager_t *manager) {
    if (!manager || !manager->current_scope ||
        manager->current_scope == manager->global_scope) {
        return -1; /// Can't pop global scope
    }

    symtable_scope_t *old_scope = manager->current_scope;
    /// Lexical (typed-fn) frames keep their `parent` pointing at the
    /// captured declaration site for lookup; the LIFO restore goes to
    /// the dynamic_caller stashed at push time.
    if (old_scope->scope_type == SCOPE_LEXICAL && old_scope->dynamic_caller) {
        manager->current_scope = old_scope->dynamic_caller;
    } else {
        manager->current_scope = old_scope->parent;
    }

    if (manager->debug_mode) {
        printf("DEBUG: Popped scope '%s' (level %zu)\n", old_scope->scope_name,
               old_scope->level);
    }

    /// Free any array_value_t backing memory bound at this scope
    /// before destroying the hashtable.
    free_arrays_in_scope(old_scope);
    ht_strstr_destroy(old_scope->vars_ht);
    free(old_scope->scope_name);
    free(old_scope);

    return 0;
}

/**
 * @brief Get the current scope nesting level
 *
 * Returns the depth of the current scope in the scope chain.
 * Global scope is level 0, first nested scope is level 1, etc.
 *
 * @param manager Symbol table manager
 * @return Current scope level, or 0 if manager is invalid
 */
size_t symtable_current_level(symtable_manager_t *manager) {
    if (!manager || !manager->current_scope) {
        return 0;
    }
    return manager->current_scope->level;
}

/**
 * @brief Get the name of the current scope
 *
 * Returns the human-readable name assigned when the scope was created.
 *
 * @param manager Symbol table manager
 * @return Scope name string, or "unknown" if manager is invalid
 */
const char *symtable_current_scope_name(symtable_manager_t *manager) {
    if (!manager || !manager->current_scope) {
        return "unknown";
    }
    return manager->current_scope->scope_name;
}

/**
 * @brief Check if currently executing within a function scope
 *
 * Walks the scope stack to find any SCOPE_FUNCTION scope.
 * This is used by the return builtin to validate that return
 * is being called from within a function or sourced script.
 *
 * @param manager Symbol table manager
 * @return true if in a function scope, false otherwise
 */
bool symtable_in_function_scope(symtable_manager_t *manager) {
    if (!manager) {
        return false;
    }

    /// Walk the scope stack looking for a function scope
    symtable_scope_t *scope = manager->current_scope;
    while (scope) {
        if (scope->scope_type == SCOPE_FUNCTION) {
            return true;
        }
        scope = scope->parent;
    }
    return false;
}

scope_type_t symtable_current_scope_type(symtable_manager_t *manager) {
    if (!manager || !manager->current_scope) {
        return SCOPE_GLOBAL;
    }
    return manager->current_scope->scope_type;
}

/**
 * @brief Set a variable in the current scope
 *
 * Serializes the variable data and stores it in the current scope's
 * hash table. Overwrites any existing variable with the same name.
 *
 * @param manager Symbol table manager
 * @param name Variable name
 * @param value Variable value (NULL is allowed)
 * @param flags Variable flags (exported, readonly, local, etc.)
 * @return 0 on success, -1 on failure
 */
int symtable_set_var(symtable_manager_t *manager, const char *name,
                     const char *value, symvar_flags_t flags) {
    if (!manager || !name) {
        return -1;
    }

    /// Serialize variable data
    char *serialized = serialize_variable(value, SYMVAR_STRING, flags,
                                          manager->current_scope->level);
    if (!serialized) {
        return -1;
    }

    /// Insert into current scope's hash table
    ht_strstr_insert(manager->current_scope->vars_ht, name, serialized);

    free(serialized);

    if (manager->debug_mode) {
        printf("DEBUG: Set variable '%s'='%s'\n", name, value ? value : "");
    }

    return 0;
}

/**
 * @brief Set a local variable in the current scope
 *
 * Convenience wrapper that sets a variable with the SYMVAR_LOCAL flag.
 *
 * @param manager Symbol table manager
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on failure
 */
int symtable_set_local_var(symtable_manager_t *manager, const char *name,
                           const char *value) {
    return symtable_set_var(manager, name, value, SYMVAR_LOCAL);
}

/**
 * @brief Set a variable in the global scope
 *
 * Temporarily switches to global scope to set the variable, then
 * restores the original current scope.
 *
 * @param manager Symbol table manager
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on failure
 */
int symtable_set_global_var(symtable_manager_t *manager, const char *name,
                            const char *value) {
    if (!manager || !name) {
        return -1;
    }

    /// Temporarily switch to global scope
    symtable_scope_t *old_scope = manager->current_scope;
    manager->current_scope = manager->global_scope;

    int result = symtable_set_var(manager, name, value, SYMVAR_NONE);

    /// Restore original scope
    manager->current_scope = old_scope;

    return result;
}

int symtable_assign_var(symtable_manager_t *manager, const char *name,
                        const char *value) {
    if (!manager || !name) {
        return -1;
    }

    /// Walk scope chain from current scope upward looking for the variable.
    /// If found, update that scope (preserves locality). If not, fall through
    /// to global write (POSIX default for unprefixed assignments).
    symtable_scope_t *scope = manager->current_scope;
    while (scope) {
        const char *serialized = ht_strstr_get(scope->vars_ht, name);
        if (serialized) {
            /// Preserve the existing flags so locality and other attributes
            /// (export, readonly bookkeeping, etc.) survive the assignment.
            /// Mask out the unset sentinel since we are setting a value.
            symvar_flags_t flags = SYMVAR_NONE;
            symvar_t *existing = deserialize_variable(name, serialized);
            if (existing) {
                flags = existing->flags & ~SYMVAR_UNSET;
                free_symvar(existing);
            }

            /// Temporarily switch current_scope so symtable_set_var writes
            /// to the scope where the variable actually lives. Restore on
            /// the way out.
            symtable_scope_t *old_scope = manager->current_scope;
            manager->current_scope = scope;
            int result = symtable_set_var(manager, name, value, flags);
            manager->current_scope = old_scope;
            return result;
        }
        scope = scope->parent;
    }

    /// Variable does not exist in any scope — create it globally per POSIX.
    return symtable_set_global_var(manager, name, value);
}

/**
 * @brief Get a variable's value from the scope chain
 *
 * Searches from the current scope up through parent scopes to find
 * the variable. Returns a copy of the value that must be freed.
 *
 * @param manager Symbol table manager
 * @param name Variable name to look up
 * @return Allocated copy of value, or NULL if not found
 */
char *symtable_get_var(symtable_manager_t *manager, const char *name) {
    if (!manager || !name) {
        return NULL;
    }

    /// Handle special dynamic variables
    if (strcmp(name, "RANDOM") == 0) {
        /// Initialize random seed on first use
        if (random_seed == 0) {
            random_seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        }
        /// Simple LCG random number generator (same as bash uses)
        random_seed = random_seed * 1103515245 + 12345;
        int value = (random_seed / 65536) % 32768;
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%d", value);
        return strdup(buffer);
    }

    if (strcmp(name, "SECONDS") == 0) {
        /// Initialize start time on first use
        if (shell_start_time == 0) {
            shell_start_time = time(NULL);
        }
        time_t elapsed = time(NULL) - shell_start_time;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", (long)elapsed);
        return strdup(buffer);
    }

    if (strcmp(name, "LINENO") == 0) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%d", current_lineno);
        return strdup(buffer);
    }

    /// Handle $- (current shell option flags)
    if (strcmp(name, "-") == 0) {
        char flags[32];
        int pos = 0;
        if (is_interactive_shell())
            flags[pos++] = 'i';
        if (shell_opts.job_control)
            flags[pos++] = 'm';
        if (shell_opts.exit_on_error)
            flags[pos++] = 'e';
        if (shell_opts.unset_error)
            flags[pos++] = 'u';
        if (shell_opts.trace_execution)
            flags[pos++] = 'x';
        if (shell_opts.verbose)
            flags[pos++] = 'v';
        if (shell_opts.noclobber)
            flags[pos++] = 'C';
        if (shell_opts.no_globbing)
            flags[pos++] = 'f';
        if (shell_opts.syntax_check)
            flags[pos++] = 'n';
        if (shell_opts.allexport)
            flags[pos++] = 'a';
        if (shell_opts.notify)
            flags[pos++] = 'b';
        if (shell_opts.physical_mode)
            flags[pos++] = 'P';
        if (shell_opts.privileged_mode)
            flags[pos++] = 'p';
        if (shell_opts.history_mode)
            flags[pos++] = 'H';
        if (shell_opts.histexpand_mode)
            flags[pos++] = 'B';
        flags[pos] = '\0';
        return strdup(flags);
    }

    /// Resolve nameref if applicable
    const char *resolved_name = name;
    if (symtable_is_nameref(manager, name)) {
        const char *target = symtable_resolve_nameref(manager, name, 10);
        if (target && target != name) {
            resolved_name = target;
        }
    }

    symvar_t *var = find_var(manager->current_scope, resolved_name);
    if (!var) {
        return NULL;
    }

    /// Array bindings are not scalars; symtable_get_var returns NULL
    /// so the array path (symtable_get_array / symtable_lookup) is the
    /// only way to read them. Without this filter, get_var would
    /// return the hex pointer string stored in var->value -- harmless
    /// for the common bare-${arr} call sites already migrated to
    /// symtable_lookup, but a leak for paths like ${!ref} indirection
    /// that resolve the name and then call get_var.
    if (var->type == SYMVAR_ARRAY) {
        free_symvar(var);
        return NULL;
    }

    char *result = var->value ? strdup(var->value) : NULL;
    free_symvar(var);

    return result;
}

/**
 * @brief Unset a variable
 *
 * Marks the variable as unset rather than removing it from the hash table.
 * This allows proper scoping behavior where an unset shadows parent values.
 *
 * @param manager Symbol table manager
 * @param name Variable name to unset
 * @return 0 on success, -1 on failure
 */
int symtable_unset_var(symtable_manager_t *manager, const char *name) {
    if (!manager || !name) {
        return -1;
    }

    /// For array bindings: free the underlying array_value_t. Read
    /// the binding via find_var so we honor the scope chain (locals
    /// shadow globals) and so we can drop the array's backing memory
    /// before the scope entry gets overwritten with the UNSET marker.
    if (manager->current_scope) {
        symvar_t *var = find_var(manager->current_scope, name);
        if (var && var->type == SYMVAR_ARRAY && var->array) {
            symtable_array_free(var->array);
        }
        free_symvar(var);
    }

    /// Mark as unset rather than removing
    return symtable_set_var(manager, name, "", SYMVAR_UNSET);
}

/* ============================================================================
 * NAMEREF SUPPORT (Phase 6: Function Enhancements)
 * ============================================================================
 */

/**
 * @brief Create a nameref variable
 *
 * Creates a variable that references another variable by name.
 * The nameref's value stores the name of the target variable.
 *
 * @param manager Manager instance
 * @param name Nameref variable name
 * @param target Name of the variable to reference
 * @param flags Additional flags (SYMVAR_LOCAL, etc.)
 * @return 0 on success, -1 on error
 */
int symtable_set_nameref(symtable_manager_t *manager, const char *name,
                         const char *target, symvar_flags_t flags) {
    if (!manager || !name || !target) {
        return -1;
    }

    /// Cannot create a nameref to itself
    if (strcmp(name, target) == 0) {
        return -1;
    }

    /// Set the variable with the target name as value and nameref flag
    return symtable_set_var(manager, name, target, flags | SYMVAR_NAMEREF_FLAG);
}

/**
 * @brief Resolve a nameref to its target variable name
 *
 * Follows the nameref chain to find the ultimate target variable.
 * Detects circular references and returns NULL in that case.
 *
 * @param manager Manager instance
 * @param name Variable name (may be a nameref)
 * @param max_depth Maximum chain depth to follow (prevents infinite loops)
 * @return Resolved variable name or NULL if circular/not found
 */
const char *symtable_resolve_nameref(symtable_manager_t *manager,
                                     const char *name, int max_depth) {
    if (!manager || !name || max_depth <= 0) {
        return NULL;
    }

    symvar_t *var = find_var(manager->current_scope, name);
    if (!var) {
        return name; /// Not found, return original name
    }

    /// Check if this is a nameref
    if (!(var->flags & SYMVAR_NAMEREF_FLAG)) {
        free_symvar(var);
        return name; /// Not a nameref, return original name
    }

    /// Get the target name
    const char *target = var->value;
    if (!target || !*target) {
        free_symvar(var);
        return name; /// No target, return original
    }

    /// Make a copy since we need to free var
    char *target_copy = strdup(target);
    free_symvar(var);

    if (!target_copy) {
        return name;
    }

    /// Recursively resolve (with depth limit)
    const char *resolved =
        symtable_resolve_nameref(manager, target_copy, max_depth - 1);

    /// If resolution returns the copy, we need to keep it
    /// Otherwise, free the copy
    if (resolved != target_copy) {
        free(target_copy);
        return resolved;
    }

    /// The resolved name is our copy - caller shouldn't free this
    /// This is a limitation - we should use a static buffer or allocate
    /// For now, return the copy (small memory leak potential)
    return target_copy;
}

/**
 * @brief Check if a variable is a nameref
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return True if variable is a nameref
 */
bool symtable_is_nameref(symtable_manager_t *manager, const char *name) {
    if (!manager || !name) {
        return false;
    }

    symvar_t *var = find_var(manager->current_scope, name);
    if (!var) {
        return false;
    }

    bool is_nameref = (var->flags & SYMVAR_NAMEREF_FLAG) != 0;
    free_symvar(var);
    return is_nameref;
}

/**
 * @brief Get variable flags
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return Variable flags or SYMVAR_NONE if not found
 */
symvar_flags_t symtable_get_flags(symtable_manager_t *manager,
                                  const char *name) {
    if (!manager || !name) {
        return SYMVAR_NONE;
    }

    symvar_t *var = find_var(manager->current_scope, name);
    if (!var) {
        return SYMVAR_NONE;
    }

    symvar_flags_t flags = var->flags;
    free_symvar(var);
    return flags;
}

/**
 * @brief Set variable flags
 *
 * Updates the flags of an existing variable.
 *
 * @param manager Manager instance
 * @param name Variable name
 * @param flags Flags to set
 * @return 0 on success, -1 on error
 */
int symtable_set_flags(symtable_manager_t *manager, const char *name,
                       symvar_flags_t flags) {
    if (!manager || !name) {
        return -1;
    }

    /// Get current value
    char *value = symtable_get_var(manager, name);
    if (!value) {
        /// Variable doesn't exist, create it with empty value
        value = strdup("");
    }

    /// Set with new flags
    int result = symtable_set_var(manager, name, value, flags);
    free(value);
    return result;
}

/**
 * @brief Check if a variable exists in the scope chain
 *
 * Searches from the current scope through parent scopes. Variables
 * marked as unset are not considered to exist.
 *
 * @param manager Symbol table manager
 * @param name Variable name to check
 * @return True if variable exists and is not unset, false otherwise
 */
bool symtable_var_exists(symtable_manager_t *manager, const char *name) {
    if (!manager || !name) {
        return false;
    }

    symvar_t *var = find_var(manager->current_scope, name);
    if (var) {
        free_symvar(var);
        return true;
    }
    return false;
}

/**
 * @brief Export a variable to the environment
 *
 * Marks the variable as exported and calls setenv() to add it to
 * the system environment for child processes.
 *
 * @param manager Symbol table manager
 * @param name Variable name to export
 * @return 0 on success, -1 if variable doesn't exist or on failure
 */
int symtable_export_var(symtable_manager_t *manager, const char *name) {
    if (!manager || !name) {
        return -1;
    }

    /// Get current value
    char *value = symtable_get_var(manager, name);
    if (!value) {
        return -1;
    }

    /// Reset with export flag
    int result = symtable_set_var(manager, name, value, SYMVAR_EXPORTED);

    /// Actually export to system environment
    if (result == 0) {
        setenv(name, value, 1);
    }

    free(value);

    return result;
}

/**
 * @brief Get the environment as a NULL-terminated string array
 *
 * Creates an array of "name=value" strings for exported variables.
 * Iterates through the global scope and collects all variables with
 * the SYMVAR_EXPORTED flag set.
 *
 * @param manager Symbol table manager
 * @return Allocated environment array, must be freed with symtable_free_environ
 */
char **symtable_get_environ(symtable_manager_t *manager) {
    if (!manager || !manager->global_scope || !manager->global_scope->vars_ht) {
        /// Return empty environment
        char **env = malloc(sizeof(char *));
        if (env) {
            env[0] = NULL;
        }
        return env;
    }

    /// Initial capacity for environment array
    size_t capacity = 64;
    size_t count = 0;
    char **env = malloc(capacity * sizeof(char *));
    if (!env) {
        return NULL;
    }

    /// Iterate through all variables in the global scope
    ht_enum_t *e = ht_strstr_enum_create(manager->global_scope->vars_ht);
    if (!e) {
        free(env);
        return NULL;
    }

    const char *name;
    const char *serialized;
    while (ht_strstr_enum_next(e, &name, &serialized)) {
        /// Deserialize to check flags
        symvar_t *var = deserialize_variable(name, serialized);
        if (!var) {
            continue;
        }

        /// Check if variable is exported
        if (var->flags & SYMVAR_EXPORTED) {
            /// Build "name=value" string
            size_t name_len = strlen(name);
            size_t value_len = var->value ? strlen(var->value) : 0;
            char *entry = malloc(name_len + 1 + value_len + 1); /// name=value\0
            if (entry) {
                snprintf(entry, name_len + 1 + value_len + 1, "%s=%s", name,
                         var->value ? var->value : "");

                /// Grow array if needed
                if (count + 1 >= capacity) {
                    capacity *= 2;
                    char **new_env = realloc(env, capacity * sizeof(char *));
                    if (!new_env) {
                        free(entry);
                        /// Free what we've allocated so far
                        for (size_t i = 0; i < count; i++) {
                            free(env[i]);
                        }
                        free(env);
                        free(var->name);
                        free(var->value);
                        free(var);
                        ht_strstr_enum_destroy(e);
                        return NULL;
                    }
                    env = new_env;
                }

                env[count++] = entry;
            }
        }

        /// Free the deserialized variable
        free(var->name);
        free(var->value);
        free(var);
    }

    ht_strstr_enum_destroy(e);

    /// NULL-terminate the array
    env[count] = NULL;

    return env;
}

/**
 * @brief Free an environment array
 *
 * Frees each string in the array and the array itself.
 *
 * @param environ Environment array to free (NULL is safe)
 */
void symtable_free_environ(char **environ) {
    if (!environ) {
        return;
    }

    for (int i = 0; environ[i]; i++) {
        free(environ[i]);
    }
    free(environ);
}

/**
 * @brief Dump variables in a specific scope type for debugging
 *
 * Finds the first scope of the given type in the scope chain and
 * prints all variables in that scope to stdout.
 *
 * @param manager Symbol table manager
 * @param scope Scope type to dump (global, function, loop, etc.)
 */
void symtable_dump_scope(symtable_manager_t *manager, scope_type_t scope) {
    if (!manager) {
        printf("DEBUG: No manager provided\n");
        return;
    }

    /// Find the requested scope in the scope chain
    symtable_scope_t *target_scope = NULL;
    symtable_scope_t *current = manager->current_scope;

    while (current) {
        if (current->scope_type == scope) {
            target_scope = current;
            break;
        }
        current = current->parent;
    }

    if (!target_scope) {
        printf("DEBUG: Scope type %d not found\n", scope);
        return;
    }

    const char *scope_name = "unknown";
    switch (scope) {
    case SCOPE_GLOBAL:
        scope_name = "global";
        break;
    case SCOPE_FUNCTION:
        scope_name = "function";
        break;
    case SCOPE_LOOP:
        scope_name = "loop";
        break;
    case SCOPE_SUBSHELL:
        scope_name = "subshell";
        break;
    case SCOPE_CONDITIONAL:
        scope_name = "conditional";
        break;
    case SCOPE_LEXICAL:
        scope_name = "lexical (fn)";
        break;
    }

    printf("=== %s scope (level %zu) ===\n", scope_name, target_scope->level);

    if (!target_scope->vars_ht) {
        printf("  (no variables)\n");
        return;
    }

    /// Enumerate all variables in this scope's hashtable
    ht_enum_t *enum_iter = ht_strstr_enum_create(target_scope->vars_ht);
    if (!enum_iter) {
        printf("  (enumeration failed)\n");
        return;
    }

    const char *key, *value;
    bool has_vars = false;

    while (ht_strstr_enum_next(enum_iter, &key, &value)) {
        has_vars = true;
        printf("  %-12s = '%s'\n", key ? key : "(null)",
               value ? value : "(null)");
    }

    if (!has_vars) {
        printf("  (no variables in scope)\n");
    }

    ht_strstr_enum_destroy(enum_iter);
}

/**
 * @brief Dump all scopes in the scope chain for debugging
 *
 * Walks the scope chain from current to global, printing each scope's
 * name, type, level, and all variables it contains.
 *
 * @param manager Symbol table manager
 */
void symtable_dump_all_scopes(symtable_manager_t *manager) {
    if (!manager) {
        printf("DEBUG: No manager provided\n");
        return;
    }

    printf("=== Symbol Table Scope Dump ===\n");
    printf("Current scope level: %zu\n",
           manager->current_scope ? manager->current_scope->level : 0);
    printf("Max scope level: %zu\n", manager->max_scope_level);
    printf("\n");

    /// Walk through the scope chain from current to global
    symtable_scope_t *current = manager->current_scope;
    int scope_count = 0;

    while (current) {
        const char *scope_name =
            current->scope_name ? current->scope_name : "unnamed";
        const char *type_name = "unknown";

        switch (current->scope_type) {
        case SCOPE_GLOBAL:
            type_name = "global";
            break;
        case SCOPE_FUNCTION:
            type_name = "function";
            break;
        case SCOPE_LOOP:
            type_name = "loop";
            break;
        case SCOPE_SUBSHELL:
            type_name = "subshell";
            break;
        case SCOPE_CONDITIONAL:
            type_name = "conditional";
            break;
        case SCOPE_LEXICAL:
            type_name = "lexical";
            break;
        }

        printf("--- Scope #%d: %s (%s, level %zu) ---\n", scope_count++,
               scope_name, type_name, current->level);

        if (!current->vars_ht) {
            printf("  (no hashtable)\n");
        } else {
            /// Enumerate variables in this scope
            ht_enum_t *enum_iter = ht_strstr_enum_create(current->vars_ht);
            if (!enum_iter) {
                printf("  (enumeration failed)\n");
            } else {
                const char *key, *value;
                bool has_vars = false;

                while (ht_strstr_enum_next(enum_iter, &key, &value)) {
                    has_vars = true;
                    printf("  %-12s = '%s'\n", key ? key : "(null)",
                           value ? value : "(null)");
                }

                if (!has_vars) {
                    printf("  (no variables in this scope)\n");
                }

                ht_strstr_enum_destroy(enum_iter);
            }
        }

        printf("\n");
        current = current->parent;
    }

    printf("=== End Scope Dump ===\n");
}

/// ============================================================================
/// CONVENIENCE API (High-level functions for common operations)
/// ============================================================================

/**
 * @brief Get access to the global symbol table manager
 *
 * Returns the singleton global manager used throughout the shell.
 *
 * @return Global manager instance, or NULL if not initialized
 */
symtable_manager_t *symtable_get_global_manager(void) { return global_manager; }

/**
 * @brief Get a variable from the global scope
 *
 * Convenience function that uses the global manager.
 *
 * @param name Variable name
 * @return Allocated copy of value, or NULL if not found
 */
char *symtable_get_global(const char *name) {
    if (!global_manager) {
        return NULL;
    }
    return symtable_get_var(global_manager, name);
}

/**
 * @brief Get a variable with a default fallback value
 *
 * Returns the variable value if it exists, otherwise returns a copy
 * of the default value.
 *
 * @param name Variable name
 * @param default_value Value to return if variable is not set
 * @return Allocated string (value or default), must be freed
 */
char *symtable_get_global_default(const char *name, const char *default_value) {
    char *value = symtable_get_global(name);
    if (!value && default_value) {
        value = strdup(default_value);
    }
    return value;
}

/**
 * @brief Set a variable in the global scope
 *
 * Convenience function that uses the global manager.
 *
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on failure
 */
int symtable_set_global(const char *name, const char *value) {
    if (!global_manager) {
        return -1;
    }
    return symtable_set_global_var(global_manager, name, value);
}

/**
 * @brief Check if a variable exists in global scope
 *
 * @param name Variable name to check
 * @return True if variable exists, false otherwise
 */
bool symtable_exists_global(const char *name) {
    if (!global_manager) {
        return false;
    }
    return symtable_var_exists(global_manager, name);
}

/**
 * @brief Unset a variable in global scope
 *
 * @param name Variable name to unset
 * @return 0 on success, -1 on failure
 */
int symtable_unset_global(const char *name) {
    if (!global_manager) {
        return -1;
    }
    return symtable_unset_var(global_manager, name);
}

/**
 * @brief Get a variable as an integer
 *
 * Parses the variable value as an integer using atoi().
 *
 * @param name Variable name
 * @param default_value Value to return if variable is not set
 * @return Integer value, or default_value if not found
 */
int symtable_get_global_int(const char *name, int default_value) {
    char *value = symtable_get_global(name);
    if (!value) {
        return default_value;
    }

    int result = atoi(value);
    free(value);
    return result;
}

/**
 * @brief Set a variable to an integer value
 *
 * Converts the integer to a string and stores it.
 *
 * @param name Variable name
 * @param value Integer value to set
 * @return 0 on success, -1 on failure
 */
int symtable_set_global_int(const char *name, int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return symtable_set_global(name, buffer);
}

/**
 * @brief Get a variable as a boolean
 *
 * Returns true if the value is "1" or "true", false otherwise.
 *
 * @param name Variable name
 * @param default_value Value to return if variable is not set
 * @return Boolean value, or default_value if not found
 */
bool symtable_get_global_bool(const char *name, bool default_value) {
    char *value = symtable_get_global(name);
    if (!value) {
        return default_value;
    }

    bool result = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    free(value);
    return result;
}

/**
 * @brief Set a variable to a boolean value
 *
 * Stores "1" for true, "0" for false.
 *
 * @param name Variable name
 * @param value Boolean value to set
 * @return 0 on success, -1 on failure
 */
int symtable_set_global_bool(const char *name, bool value) {
    return symtable_set_global(name, value ? "1" : "0");
}

/**
 * @brief Export a global variable to the environment
 *
 * @param name Variable name to export
 * @return 0 on success, -1 on failure
 */
int symtable_export_global(const char *name) {
    if (!global_manager) {
        return -1;
    }
    return symtable_export_var(global_manager, name);
}

/**
 * @brief Remove export flag from a global variable
 *
 * Re-sets the variable without the export flag.
 *
 * @param name Variable name to unexport
 * @return 0 on success, -1 on failure
 */
int symtable_unexport_global(const char *name) {
    /// Get current value and reset without export flag
    char *value = symtable_get_global(name);
    if (!value) {
        return -1;
    }

    int result = symtable_set_global(name, value);
    free(value);
    return result;
}

/**
 * @brief Set a special shell variable
 *
 * Special variables like $?, $!, $$ have the SYMVAR_SPECIAL_VAR flag.
 *
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on failure
 */
int symtable_set_special_global(const char *name, const char *value) {
    if (!global_manager) {
        return -1;
    }
    return symtable_set_var(global_manager, name, value, SYMVAR_SPECIAL_VAR);
}

/**
 * @brief Get a special shell variable
 *
 * @param name Variable name
 * @return Allocated copy of value, or NULL if not found
 */
char *symtable_get_special_global(const char *name) {
    return symtable_get_global(name);
}

/**
 * @brief Set a read-only variable
 *
 * Read-only variables cannot be modified or unset after being set.
 *
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on failure
 */
int symtable_set_readonly_global(const char *name, const char *value) {
    if (!global_manager) {
        return -1;
    }
    return symtable_set_var(global_manager, name, value, SYMVAR_READONLY);
}

/**
 * @brief Dump global scope variables for debugging
 *
 * Convenience wrapper that calls symtable_dump_scope with SCOPE_GLOBAL.
 */
void symtable_debug_dump_global_scope(void) {
    if (global_manager) {
        symtable_dump_scope(global_manager, SCOPE_GLOBAL);
    }
}

/**
 * @brief Dump all scopes for debugging
 *
 * Convenience wrapper that calls symtable_dump_all_scopes.
 */
void symtable_debug_dump_all_scopes(void) {
    if (global_manager) {
        symtable_dump_all_scopes(global_manager);
    }
}

/**
 * @brief Enumerate all global variables with a callback
 *
 * Iterates over all variables in the global scope and calls the
 * provided callback function for each one.
 *
 * @param callback Function to call for each variable
 * @param userdata User data to pass to the callback
 */
void symtable_debug_enumerate_global_vars(void (*callback)(const char *key,
                                                           const char *value,
                                                           void *userdata),
                                          void *userdata) {
    if (!global_manager || !global_manager->global_scope ||
        !global_manager->global_scope->vars_ht || !callback) {
        return;
    }

    ht_enum_t *enum_iter =
        ht_strstr_enum_create(global_manager->global_scope->vars_ht);
    if (!enum_iter) {
        return;
    }

    const char *key, *value;
    while (ht_strstr_enum_next(enum_iter, &key, &value)) {
        callback(key, value, userdata);
    }

    ht_strstr_enum_destroy(enum_iter);
}

/**
 * @brief Enumerate global variables with clean values
 *
 * Enumerates all global shell variables and calls the callback for each one.
 * Unlike symtable_debug_enumerate_global_vars, this function deserializes
 * the stored values and returns only the actual variable value without
 * internal metadata (flags, scope level, type).
 *
 * @param callback Function to call for each variable (key, value, userdata)
 * @param userdata User data to pass to the callback
 */
void symtable_enumerate_global_vars(void (*callback)(const char *key,
                                                     const char *value,
                                                     void *userdata),
                                    void *userdata) {
    if (!global_manager || !global_manager->global_scope ||
        !global_manager->global_scope->vars_ht || !callback) {
        return;
    }

    ht_enum_t *enum_iter =
        ht_strstr_enum_create(global_manager->global_scope->vars_ht);
    if (!enum_iter) {
        return;
    }

    const char *key, *serialized;
    while (ht_strstr_enum_next(enum_iter, &key, &serialized)) {
        /// Deserialize to get clean value
        symvar_t *var = deserialize_variable(key, serialized);
        if (var && !(var->flags & SYMVAR_UNSET)) {
            callback(key, var->value, userdata);
        }
        free_symvar(var);
    }

    ht_strstr_enum_destroy(enum_iter);
}

void symtable_enumerate_current_scope_vars(symtable_manager_t *manager,
                                           void (*callback)(const char *name,
                                                            const char *value,
                                                            symvar_type_t type,
                                                            void *userdata),
                                           void *userdata) {
    if (!manager || !manager->current_scope ||
        !manager->current_scope->vars_ht || !callback) {
        return;
    }

    ht_enum_t *enum_iter =
        ht_strstr_enum_create(manager->current_scope->vars_ht);
    if (!enum_iter) {
        return;
    }

    const char *key, *serialized;
    while (ht_strstr_enum_next(enum_iter, &key, &serialized)) {
        symvar_t *var = deserialize_variable(key, serialized);
        if (var && !(var->flags & SYMVAR_UNSET)) {
            callback(key, var->value, var->type, userdata);
        }
        free_symvar(var);
    }

    ht_strstr_enum_destroy(enum_iter);
}

/**
 * @brief Count the number of global variables
 *
 * @return Number of variables in global scope (currently unimplemented)
 */
size_t symtable_count_global_vars(void) {
    /// TODO: Implement variable counting
    return 0;
}

/**
 * @brief Get the environment as an array
 *
 * Returns a NULL-terminated array of "name=value" strings.
 *
 * @return Allocated environment array, must be freed with
 * symtable_free_environment_array
 */
char **symtable_get_environment_array(void) {
    if (!global_manager) {
        return NULL;
    }
    return symtable_get_environ(global_manager);
}

/**
 * @brief Free an environment array
 *
 * @param env Environment array to free
 */
void symtable_free_environment_array(char **env) { symtable_free_environ(env); }

/// ============================================================================
/// SYSTEM INTERFACE (Essential functions for shell operation)
/// ============================================================================

/**
 * @brief Initialize the global symbol table
 *
 * Creates the global symbol table manager. Should be called once
 * during shell startup. Safe to call multiple times.
 */
void init_symtable(void) {
    if (global_manager) {
        return; /// Already initialized
    }

    global_manager = symtable_manager_new();
    if (!global_manager) {
        fprintf(stderr, "ERROR: Failed to initialize symbol table\n");
        return;
    }
}

/**
 * @brief Free the global symbol table
 *
 * Releases all resources associated with the global symbol table manager.
 * Should be called during shell shutdown.
 */
void free_global_symtable(void) {
    /// Storage unification: arrays live in scope hashtables (their
    /// backing memory is reclaimed via free_arrays_in_scope as each
    /// scope is destroyed in symtable_manager_free), so there is no
    /// longer a separate side-table to clean up.
    if (global_manager) {
        symtable_manager_free(global_manager);
        global_manager = NULL;
    }
}

/**
 * @brief Set the exit status special variable
 *
 * Updates both the $? special variable and the global last_exit_status.
 *
 * @param status Exit status value (0-255)
 */
void set_exit_status(int status) {
    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", status);
    symtable_set_special_global("?", status_str);

    /// Also update the global variable for consistency
    last_exit_status = status;
}

/**
 * @brief Get environment array for child processes
 *
 * @return Allocated environment array
 */
char **get_environ_array(void) { return symtable_get_environment_array(); }

/**
 * @brief Free environment array
 *
 * @param env Environment array to free
 */
void free_environ_array(char **env) { symtable_free_environment_array(env); }

/// ============================================================================
/// LEGACY COMPATIBILITY (For string management and other subsystems)
/// ============================================================================

/**
 * @brief Create a new symbol table (legacy compatibility)
 *
 * Returns a dummy symbol table for legacy API compatibility.
 *
 * @param level Scope level (ignored)
 * @return Pointer to dummy symbol table
 */
symtable_t *new_symtable(size_t level) {
    (void)level;
    return &dummy_symtable;
}

/**
 * @brief Push a scope onto the stack (legacy compatibility)
 *
 * Creates a new function scope using the modern API.
 *
 * @return Pointer to dummy symbol table
 */
symtable_t *symtable_stack_push(void) {
    if (global_manager) {
        symtable_push_scope(global_manager, SCOPE_FUNCTION, "legacy-scope");
    }
    return &dummy_symtable;
}

/**
 * @brief Pop a scope from the stack (legacy compatibility)
 *
 * Pops the current scope using the modern API.
 *
 * @return Pointer to dummy symbol table
 */
symtable_t *symtable_stack_pop(void) {
    if (global_manager) {
        symtable_pop_scope(global_manager);
    }
    return &dummy_symtable;
}

/**
 * @brief Remove entry from symbol table (legacy compatibility)
 *
 * No-op for legacy API compatibility.
 *
 * @param symtable Symbol table (ignored)
 * @param entry Entry to remove (ignored)
 * @return Always returns 0
 */
int remove_from_symtable(symtable_t *symtable, symtable_entry_t *entry) {
    (void)symtable;
    (void)entry;
    return 0;
}

/**
 * @brief Add entry to symbol table (legacy compatibility)
 *
 * Sets an empty variable if it doesn't exist.
 *
 * @param name Variable name
 * @return Non-NULL dummy pointer on success, NULL on failure
 */
symtable_entry_t *add_to_symtable(char *name) {
    if (!global_manager || !name) {
        return NULL;
    }

    /// Set variable with empty value if it doesn't exist
    if (!symtable_var_exists(global_manager, name)) {
        symtable_set_var(global_manager, name, "", SYMVAR_NONE);
    }

    /// Return a dummy pointer for compatibility
    return (symtable_entry_t *)1;
}

/**
 * @brief Look up a symbol in the table (legacy compatibility)
 *
 * Checks if a variable exists using the modern API.
 *
 * @param symtable Symbol table (ignored)
 * @param name Variable name to look up
 * @return Non-NULL dummy pointer if found, NULL otherwise
 */
symtable_entry_t *lookup_symbol(symtable_t *symtable, const char *name) {
    (void)symtable;

    if (!global_manager || !name) {
        return NULL;
    }

    if (symtable_var_exists(global_manager, name)) {
        return (symtable_entry_t *)1;
    }

    return NULL;
}

/**
 * @brief Get a symbol table entry by name (legacy compatibility)
 *
 * @param name Variable name
 * @return Non-NULL dummy pointer if found, NULL otherwise
 */
symtable_entry_t *get_symtable_entry(const char *name) {
    if (!global_manager || !name) {
        return NULL;
    }

    if (symtable_var_exists(global_manager, name)) {
        return (symtable_entry_t *)1;
    }

    return NULL;
}

/**
 * @brief Get the local symbol table (legacy compatibility)
 *
 * @return Pointer to dummy symbol table
 */
symtable_t *get_local_symtable(void) { return &dummy_symtable; }

/**
 * @brief Get the global symbol table (legacy compatibility)
 *
 * @return Pointer to dummy symbol table
 */
symtable_t *get_global_symtable(void) { return &dummy_symtable; }

/**
 * @brief Get the symbol table stack (legacy compatibility)
 *
 * @return Pointer to static dummy stack structure
 */
symtable_stack_t *get_symtable_stack(void) {
    static symtable_stack_t dummy_stack = {
        1, {&dummy_symtable}, &dummy_symtable, &dummy_symtable};
    return &dummy_stack;
}

/**
 * @brief Free a symbol table (legacy compatibility)
 *
 * No-op since we use the global manager.
 *
 * @param symtable Symbol table to free (ignored)
 */
void free_symtable(symtable_t *symtable) { (void)symtable; }

/**
 * @brief Set entry value (legacy compatibility)
 *
 * No-op for legacy API compatibility.
 *
 * @param entry Entry to modify (ignored)
 * @param val Value to set (ignored)
 */
void symtable_entry_setval(symtable_entry_t *entry, char *val) {
    (void)entry;
    (void)val;
}

/// ============================================================================
/// ENHANCED API COMPATIBILITY
/// ============================================================================

/**
 * @brief Check if libhashtable implementation is available
 *
 * Always returns true since this is the main implementation.
 *
 * @return Always true
 */
bool symtable_libht_available(void) {
    return true; /// Always available since this is the main implementation
}

/**
 * @brief Get implementation info string
 *
 * @return Description of the symbol table implementation
 */
const char *symtable_implementation_info(void) {
    return "Optimized libhashtable implementation (ht_strstr_t, FNV1A hash)";
}

/**
 * @brief Initialize libhashtable symbol table (alias)
 */
void init_symtable_libht(void) { init_symtable(); }

/**
 * @brief Free libhashtable symbol table (alias)
 */
void free_symtable_libht(void) { free_global_symtable(); }

/**
 * @brief Get the libhashtable manager (opaque pointer)
 *
 * @return Pointer to global manager cast to void*
 */
void *get_libht_manager(void) { return (void *)global_manager; }

/**
 * @brief Set variable with flags (enhanced API)
 *
 * @param name Variable name
 * @param value Variable value
 * @param flags Variable flags
 * @return 0 on success, -1 on failure
 */
int symtable_set_var_enhanced(const char *name, const char *value,
                              symvar_flags_t flags) {
    if (!global_manager) {
        init_symtable();
        if (!global_manager) {
            return -1;
        }
    }
    return symtable_set_var(global_manager, name, value, flags);
}

/**
 * @brief Get variable value (enhanced API)
 *
 * @param name Variable name
 * @return Allocated copy of value, or NULL if not found
 */
char *symtable_get_var_enhanced(const char *name) {
    if (!global_manager) {
        return NULL;
    }
    return symtable_get_var(global_manager, name);
}

/**
 * @brief Push scope (enhanced API)
 *
 * @param type Scope type
 * @param name Scope name
 * @return 0 on success, -1 on failure
 */
int symtable_push_scope_enhanced(scope_type_t type, const char *name) {
    if (!global_manager) {
        init_symtable();
        if (!global_manager) {
            return -1;
        }
    }
    return symtable_push_scope(global_manager, type, name);
}

/**
 * @brief Pop scope (enhanced API)
 *
 * @return 0 on success, -1 on failure
 */
int symtable_pop_scope_enhanced(void) {
    if (!global_manager) {
        return -1;
    }
    return symtable_pop_scope(global_manager);
}

/**
 * @brief Run benchmark comparison (stub)
 *
 * Prints a message since this is the main implementation.
 *
 * @param iterations Number of iterations (ignored)
 */
void symtable_benchmark_comparison(int iterations) {
    (void)iterations;
    printf(
        "Optimized libhashtable implementation is the main implementation\n");
}

/**
 * @brief Run symbol table self-test
 *
 * Tests basic set/get operations and prints results.
 *
 * @return 0 on success, -1 on failure
 */
int symtable_libht_test(void) {
    printf("Testing main symbol table implementation...\n");

    init_symtable();
    if (!global_manager) {
        printf("FAIL: Could not initialize symbol table\n");
        return -1;
    }

    if (symtable_set_global("test_var", "test_value") != 0) {
        printf("FAIL: Could not set variable\n");
        return -1;
    }

    char *value = symtable_get_global("test_var");
    if (!value || strcmp(value, "test_value") != 0) {
        printf("FAIL: Variable value mismatch\n");
        free(value);
        return -1;
    }
    free(value);

    printf("PASS: Main symbol table test completed successfully\n");
    return 0;
}

/**
 * @brief Check if optimized implementation is available
 *
 * @return Always true
 */
bool symtable_opt_available(void) { return true; }

/**
 * @brief Get optimized implementation info (alias)
 *
 * @return Implementation description string
 */
const char *symtable_opt_implementation_info(void) {
    return symtable_implementation_info();
}

/**
 * @brief Initialize optimized symbol table (alias)
 */
void init_symtable_opt(void) { init_symtable(); }

/**
 * @brief Free optimized symbol table (alias)
 */
void free_symtable_opt(void) { free_global_symtable(); }

/**
 * @brief Get optimized manager (alias)
 *
 * @return Pointer to global manager
 */
void *get_opt_manager(void) { return (void *)global_manager; }

/**
 * @brief Set variable (optimized API alias)
 *
 * @param name Variable name
 * @param value Variable value
 * @param flags Variable flags
 * @return 0 on success, -1 on failure
 */
int symtable_set_var_opt_api(const char *name, const char *value,
                             symvar_flags_t flags) {
    return symtable_set_var_enhanced(name, value, flags);
}

/**
 * @brief Get variable (optimized API alias)
 *
 * @param name Variable name
 * @return Allocated copy of value, or NULL if not found
 */
char *symtable_get_var_opt_api(const char *name) {
    return symtable_get_var_enhanced(name);
}

/**
 * @brief Push scope (optimized API alias)
 *
 * @param type Scope type
 * @param name Scope name
 * @return 0 on success, -1 on failure
 */
int symtable_push_scope_opt_api(scope_type_t type, const char *name) {
    return symtable_push_scope_enhanced(type, name);
}

/**
 * @brief Pop scope (optimized API alias)
 *
 * @return 0 on success, -1 on failure
 */
int symtable_pop_scope_opt_api(void) { return symtable_pop_scope_enhanced(); }

/**
 * @brief Run optimized benchmark (stub)
 *
 * @param iterations Number of iterations (ignored)
 */
void symtable_benchmark_opt_comparison(int iterations) {
    (void)iterations;
    printf("This IS the optimized implementation\n");
}

/**
 * @brief Run optimized self-test (alias)
 *
 * @return 0 on success, -1 on failure
 */
int symtable_opt_test(void) { return symtable_libht_test(); }

/// ============================================================================
/// SPECIAL VARIABLE SETTERS
/// ============================================================================

/**
 * @brief Set the current line number for $LINENO
 *
 * @param lineno Current line number
 */
void symtable_set_lineno(int lineno) { current_lineno = lineno; }

/**
 * @brief Get the current line number
 *
 * @return Current line number
 */
int symtable_get_lineno(void) { return current_lineno; }

/**
 * @brief Reset SECONDS counter
 *
 * Resets the shell start time to now, making $SECONDS return 0.
 */
void symtable_reset_seconds(void) { shell_start_time = time(NULL); }

/**
 * @brief Seed the RANDOM generator
 *
 * @param seed New seed value (0 uses time-based seed)
 */
void symtable_seed_random(unsigned int seed) {
    if (seed == 0) {
        random_seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    } else {
        random_seed = seed;
    }
}

/// ============================================================================
/// ARRAY VARIABLE IMPLEMENTATION (Phase 1: Extended Language Support)
/// ============================================================================

/** Initial capacity for indexed arrays */
#define ARRAY_INITIAL_CAPACITY 8

/** Growth factor for array reallocation */
#define ARRAY_GROWTH_FACTOR 2

/**
 * @brief Create a new array value
 */
array_value_t *symtable_array_create(bool is_associative) {
    array_value_t *array = calloc(1, sizeof(array_value_t));
    if (!array) {
        return NULL;
    }

    array->is_associative = is_associative;
    array->count = 0;
    array->max_index = 0;

    if (is_associative) {
        /// Create hash table for associative array
        array->assoc_map = ht_strstr_create(DEFAULT_HT_FLAGS);
        if (!array->assoc_map) {
            free(array);
            return NULL;
        }
        array->elements = NULL;
        array->indices = NULL;
        array->capacity = 0;
    } else {
        /// Allocate initial capacity for indexed array
        array->capacity = ARRAY_INITIAL_CAPACITY;
        array->elements = calloc(array->capacity, sizeof(char *));
        array->indices = calloc(array->capacity, sizeof(int));
        if (!array->elements || !array->indices) {
            free(array->elements);
            free(array->indices);
            free(array);
            return NULL;
        }
        array->assoc_map = NULL;
    }

    return array;
}

/**
 * @brief Free an array value and all its elements
 */
void symtable_array_free(array_value_t *array) {
    if (!array) {
        return;
    }

    if (array->is_associative) {
        /// Free associative array hash table
        if (array->assoc_map) {
            ht_strstr_destroy(array->assoc_map);
        }
        /// Free insertion-order tracking (strdup'd keys + the array)
        if (array->assoc_insertion_order) {
            for (size_t i = 0; i < array->assoc_insertion_count; i++) {
                free(array->assoc_insertion_order[i]);
            }
            free(array->assoc_insertion_order);
        }
    } else {
        /// Free indexed array elements
        if (array->elements) {
            for (size_t i = 0; i < array->count; i++) {
                free(array->elements[i]);
            }
            free(array->elements);
        }
        free(array->indices);
    }

    free(array);
}

/**
 * @brief Find the position of an index in sparse array, or insertion point
 *
 * @param array Source array
 * @param index Index to find
 * @param found Output: true if index exists
 * @return Position in indices array
 */
static size_t array_find_index_pos(array_value_t *array, int index,
                                   bool *found) {
    *found = false;

    /// Binary search for the index
    size_t left = 0;
    size_t right = array->count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (array->indices[mid] == index) {
            *found = true;
            return mid;
        } else if (array->indices[mid] < index) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left; /// Insertion point
}

/// @brief Ensure array has capacity for one more element
static int array_ensure_capacity(array_value_t *array) {
    if (array->count >= array->capacity) {
        size_t new_capacity = array->capacity * ARRAY_GROWTH_FACTOR;
        if (new_capacity < ARRAY_INITIAL_CAPACITY) {
            new_capacity = ARRAY_INITIAL_CAPACITY;
        }

        char **new_elements =
            realloc(array->elements, new_capacity * sizeof(char *));
        int *new_indices = realloc(array->indices, new_capacity * sizeof(int));

        if (!new_elements || !new_indices) {
            /// Restore on partial failure
            if (new_elements)
                array->elements = new_elements;
            if (new_indices)
                array->indices = new_indices;
            return -1;
        }

        array->elements = new_elements;
        array->indices = new_indices;
        array->capacity = new_capacity;

        /// Zero new memory
        for (size_t i = array->count; i < new_capacity; i++) {
            array->elements[i] = NULL;
            array->indices[i] = 0;
        }
    }

    return 0;
}

/**
 * @brief Set an element in an indexed array
 *
 * Supports negative indices: -1 is last element, -2 is second-to-last, etc.
 * Negative indices are converted relative to (max_index + 1).
 */
int symtable_array_set_index(array_value_t *array, int index,
                             const char *value) {
    if (!array || array->is_associative) {
        return -1;
    }

    /// Handle negative indices (Bash-style: -1 = last element)
    if (index < 0) {
        index = (int)(array->max_index + 1) + index;
        if (index < 0) {
            return -1; /// Still negative = out of bounds
        }
    }

    bool found;
    size_t pos = array_find_index_pos(array, index, &found);

    if (found) {
        /// Update existing element
        free(array->elements[pos]);
        array->elements[pos] = value ? strdup(value) : NULL;
    } else {
        /// Insert new element
        if (array_ensure_capacity(array) < 0) {
            return -1;
        }

        /// Shift elements to make room
        for (size_t i = array->count; i > pos; i--) {
            array->elements[i] = array->elements[i - 1];
            array->indices[i] = array->indices[i - 1];
        }

        array->elements[pos] = value ? strdup(value) : NULL;
        array->indices[pos] = index;
        array->count++;
    }

    if ((size_t)index > array->max_index) {
        array->max_index = (size_t)index;
    }

    return 0;
}

/**
 * @brief Get an element from an indexed array
 *
 * Supports negative indices: -1 is last element, -2 is second-to-last, etc.
 * Negative indices are converted relative to (max_index + 1).
 */
const char *symtable_array_get_index(array_value_t *array, int index) {
    if (!array || array->is_associative) {
        return NULL;
    }

    /// Handle negative indices (Bash-style: -1 = last element)
    if (index < 0) {
        index = (int)(array->max_index + 1) + index;
        if (index < 0) {
            return NULL; /// Still negative = out of bounds
        }
    }

    bool found;
    size_t pos = array_find_index_pos(array, index, &found);

    if (found) {
        return array->elements[pos];
    }

    return NULL;
}

/**
 * @brief Set an element in an associative array
 */
int symtable_array_set_assoc(array_value_t *array, const char *key,
                             const char *value) {
    if (!array || !array->is_associative || !key) {
        return -1;
    }

    bool is_new = !ht_strstr_get(array->assoc_map, key);

    /// Check if key exists and update count
    if (is_new) {
        array->count++;
        /// Append to insertion-order list. zsh semantic: a key keeps
        /// its original position when overwritten; only first-set
        /// appends. (Issue #69.)
        if (array->assoc_insertion_count >= array->assoc_insertion_capacity) {
            size_t new_cap = array->assoc_insertion_capacity
                                 ? array->assoc_insertion_capacity * 2
                                 : 8;
            char **grown =
                realloc(array->assoc_insertion_order, new_cap * sizeof(char *));
            if (grown) {
                array->assoc_insertion_order = grown;
                array->assoc_insertion_capacity = new_cap;
            }
            /// If realloc fails, we still set the value; iteration just
            /// won't include this key in insertion order. Hashtable
            /// fallback path remains correct.
        }
        if (array->assoc_insertion_count < array->assoc_insertion_capacity) {
            char *key_copy = strdup(key);
            if (key_copy) {
                array->assoc_insertion_order[array->assoc_insertion_count++] =
                    key_copy;
            }
        }
    }

    ht_strstr_insert(array->assoc_map, key, value ? value : "");
    return 0;
}

/**
 * @brief Get an element from an associative array
 */
const char *symtable_array_get_assoc(array_value_t *array, const char *key) {
    if (!array || !array->is_associative || !key) {
        return NULL;
    }

    return ht_strstr_get(array->assoc_map, key);
}

/**
 * @brief Append a value to an indexed array
 */
int symtable_array_append(array_value_t *array, const char *value) {
    if (!array || array->is_associative) {
        return -1;
    }

    int new_index = (int)(array->max_index + 1);
    if (array->count == 0) {
        new_index = 0;
    }

    if (symtable_array_set_index(array, new_index, value) < 0) {
        return -1;
    }

    return new_index;
}

/**
 * @brief Get the number of elements in an array
 */
size_t symtable_array_length(array_value_t *array) {
    if (!array) {
        return 0;
    }
    return array->count;
}

/**
 * @brief Unset an element in an indexed array
 *
 * Supports negative indices: -1 is last element, -2 is second-to-last, etc.
 * Negative indices are converted relative to (max_index + 1).
 */
int symtable_array_unset_index(array_value_t *array, int index) {
    if (!array || array->is_associative) {
        return -1;
    }

    /// Handle negative indices (Bash-style: -1 = last element)
    if (index < 0) {
        index = (int)(array->max_index + 1) + index;
        if (index < 0) {
            return -1; /// Still negative = out of bounds
        }
    }

    bool found;
    size_t pos = array_find_index_pos(array, index, &found);

    if (!found) {
        return 0; /// Not an error to unset nonexistent element
    }

    /// Free the element
    free(array->elements[pos]);

    /// Shift remaining elements
    for (size_t i = pos; i < array->count - 1; i++) {
        array->elements[i] = array->elements[i + 1];
        array->indices[i] = array->indices[i + 1];
    }
    array->count--;

    /// Recalculate max_index if we removed the max
    if ((size_t)index == array->max_index && array->count > 0) {
        array->max_index = (size_t)array->indices[array->count - 1];
    } else if (array->count == 0) {
        array->max_index = 0;
    }

    return 0;
}

/**
 * @brief Unset an element in an associative array
 */
int symtable_array_unset_assoc(array_value_t *array, const char *key) {
    if (!array || !array->is_associative || !key) {
        return -1;
    }

    if (ht_strstr_get(array->assoc_map, key)) {
        ht_strstr_remove(array->assoc_map, key);
        array->count--;

        /// Remove from insertion-order list. Linear scan + memmove down;
        /// acceptable since assoc-array unset is uncommon and the list
        /// is typically small. (Issue #69.)
        for (size_t i = 0; i < array->assoc_insertion_count; i++) {
            if (strcmp(array->assoc_insertion_order[i], key) == 0) {
                free(array->assoc_insertion_order[i]);
                size_t to_move = array->assoc_insertion_count - i - 1;
                if (to_move > 0) {
                    memmove(&array->assoc_insertion_order[i],
                            &array->assoc_insertion_order[i + 1],
                            to_move * sizeof(char *));
                }
                array->assoc_insertion_count--;
                break;
            }
        }
    }

    return 0;
}

/**
 * @brief Get all keys/indices from an array
 */
char **symtable_array_get_keys(array_value_t *array, size_t *count) {
    if (!array || !count) {
        if (count)
            *count = 0;
        return NULL;
    }

    *count = array->count;
    if (array->count == 0) {
        return NULL;
    }

    char **keys = calloc(array->count, sizeof(char *));
    if (!keys) {
        *count = 0;
        return NULL;
    }

    if (array->is_associative) {
        /// Iteration order is mode-aware (Issue #69):
        ///   - zsh / lush mode: insertion order (matches zsh native;
        ///     lush curated default for predictability + determinism)
        ///   - bash / POSIX mode: hashtable bucket order (matches
        ///     bash's documented behavior for compat)
        /// If insertion-order tracking is empty (e.g. realloc failure
        /// during a previous set), fall back to hashtable order so we
        /// never return fewer keys than the caller expects.
        shell_mode_t mode = shell_mode_get();
        bool use_insertion_order =
            (mode == SHELL_MODE_ZSH || mode == SHELL_MODE_LUSH) &&
            array->assoc_insertion_order &&
            array->assoc_insertion_count == array->count;

        if (use_insertion_order) {
            for (size_t i = 0; i < array->count; i++) {
                keys[i] = strdup(array->assoc_insertion_order[i]);
            }
        } else {
            /// Get keys from hash table (bucket order)
            ht_enum_t *enumerator = ht_strstr_enum_create(array->assoc_map);
            if (enumerator) {
                size_t i = 0;
                const char *key;
                const char *val;
                while (ht_strstr_enum_next(enumerator, &key, &val) &&
                       i < array->count) {
                    keys[i++] = strdup(key);
                }
                ht_strstr_enum_destroy(enumerator);
            }
        }
    } else {
        /// Convert indices to strings
        for (size_t i = 0; i < array->count; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", array->indices[i]);
            keys[i] = strdup(buf);
        }
    }

    return keys;
}

/**
 * @brief Get all values from an array
 */
char **symtable_array_get_values(array_value_t *array, size_t *count) {
    if (!array || !count) {
        if (count)
            *count = 0;
        return NULL;
    }

    *count = array->count;
    if (array->count == 0) {
        return NULL;
    }

    char **values = calloc(array->count, sizeof(char *));
    if (!values) {
        *count = 0;
        return NULL;
    }

    if (array->is_associative) {
        /// Same mode-aware iteration as symtable_array_get_keys —
        /// critical that BOTH functions use the same order so that
        /// keys[i] and values[i] pair correctly when callers fetch
        /// both (e.g. ${(kv)assoc_map}). (Issue #69.)
        shell_mode_t mode = shell_mode_get();
        bool use_insertion_order =
            (mode == SHELL_MODE_ZSH || mode == SHELL_MODE_LUSH) &&
            array->assoc_insertion_order &&
            array->assoc_insertion_count == array->count;

        if (use_insertion_order) {
            for (size_t i = 0; i < array->count; i++) {
                const char *v = ht_strstr_get(array->assoc_map,
                                              array->assoc_insertion_order[i]);
                values[i] = strdup(v ? v : "");
            }
        } else {
            /// Get values from hash table (bucket order)
            ht_enum_t *enumerator = ht_strstr_enum_create(array->assoc_map);
            if (enumerator) {
                size_t i = 0;
                const char *key;
                const char *val;
                while (ht_strstr_enum_next(enumerator, &key, &val) &&
                       i < array->count) {
                    values[i++] = val ? strdup(val) : strdup("");
                }
                ht_strstr_enum_destroy(enumerator);
            }
        }
    } else {
        /// Copy indexed array values
        for (size_t i = 0; i < array->count; i++) {
            values[i] =
                array->elements[i] ? strdup(array->elements[i]) : strdup("");
        }
    }

    return values;
}

/**
 * @brief Expand array to string for ${arr[*]} or ${arr[@]}
 */
char *symtable_array_expand(array_value_t *array, const char *sep) {
    if (!array || array->count == 0) {
        return strdup("");
    }

    /// Default separator is space
    if (!sep) {
        sep = " ";
    }
    size_t sep_len = strlen(sep);

    /// Calculate total length needed
    size_t total_len = 0;
    size_t value_count;
    char **values = symtable_array_get_values(array, &value_count);
    if (!values) {
        return strdup("");
    }

    for (size_t i = 0; i < value_count; i++) {
        total_len += strlen(values[i]);
        if (i > 0) {
            total_len += sep_len;
        }
    }

    char *result = malloc(total_len + 1);
    if (!result) {
        for (size_t i = 0; i < value_count; i++) {
            free(values[i]);
        }
        free(values);
        return strdup("");
    }

    /// Build the result string
    result[0] = '\0';
    for (size_t i = 0; i < value_count; i++) {
        if (i > 0) {
            strcat(result, sep);
        }
        strcat(result, values[i]);
        free(values[i]);
    }
    free(values);

    return result;
}

/// ============================================================================
/// ARRAY VARIABLE MANAGEMENT (Global storage integration)
/// ============================================================================

/// Storage unification: the global array_storage side-table has been
/// removed. Arrays live in per-scope vars_ht as SYMVAR_ARRAY-tagged
/// entries; the backing memory is freed by free_arrays_in_scope when
/// each scope is destroyed (see symtable_pop_scope and
/// symtable_manager_free).

/**
 * @brief Set a variable as an array
 *
 * Single storage path: write a SYMVAR_ARRAY-tagged entry into the
 * current scope's vars_ht. The pointer is encoded as a hex string
 * in the serialized value field so the existing ht_strstr storage
 * can carry it verbatim; deserialize parses it back into
 * symvar.array on read.
 */
int symtable_set_array(const char *name, array_value_t *array) {
    if (!name || !array || !global_manager || !global_manager->current_scope) {
        return -1;
    }

    /// Free any existing array bound to this name -- assignment to an
    /// already-array variable replaces the underlying array_value_t.
    /// symtable_get_array walks the scope chain; if it finds an old
    /// array, free its backing memory before we overwrite.
    array_value_t *existing = symtable_get_array(name);
    if (existing && existing != array) {
        symtable_array_free(existing);
    }

    /// Encode the pointer as a hex string in the serialized value;
    /// deserialize will parse it back into symvar.array.
    char ptr_str[32];
    snprintf(ptr_str, sizeof(ptr_str), "%p", (void *)array);

    char *serialized = serialize_variable(ptr_str, SYMVAR_ARRAY, SYMVAR_NONE,
                                          global_manager->current_scope->level);
    if (!serialized) {
        return -1;
    }
    ht_strstr_insert(global_manager->current_scope->vars_ht, name, serialized);
    free(serialized);
    return 0;
}

/**
 * @brief Get an array variable
 */
array_value_t *symtable_get_array(const char *name) {
    if (!name) {
        return NULL;
    }

    /// Single source of truth: the scope chain. Arrays live in
    /// per-scope vars_ht the same way scalars do (storage
    /// unification phase B), so a local array defined in a function
    /// dies when the function scope is popped -- matching bash. The
    /// global array_storage side-table is still allocated (set_array
    /// also writes there for the lle_shell_hooks PROMPT_COMMAND walk
    /// and the shutdown cleanup that frees array_value_t backing
    /// memory) but is no longer consulted as a get fallback: that
    /// fallback was leaking local arrays out of their function scope.
    if (!global_manager || !global_manager->current_scope) {
        return NULL;
    }
    symvar_t *var = find_var(global_manager->current_scope, name);
    if (!var) {
        return NULL;
    }
    array_value_t *result = (var->type == SYMVAR_ARRAY) ? var->array : NULL;
    free_symvar(var);
    return result;
}

/**
 * @brief Check if a variable is an array
 */
bool symtable_is_array(const char *name) {
    return symtable_get_array(name) != NULL;
}

/*
 * Implementation of symtable_lookup. Contract documented in symtable.h.
 * Walks the array side-table first (catches both indexed lists and
 * associative maps, distinguished by the is_associative flag inside
 * array_value_t); on miss falls through to the scalar scope chain.
 * NULL on both paths returns LUSH_VALUE_NONE.
 */
bool symtable_lookup(const char *name, lush_value_view_t *out) {
    if (!name || !out) {
        return false;
    }
    out->kind = LUSH_VALUE_NONE;
    out->scalar_value = NULL;
    out->array = NULL;

    array_value_t *arr = symtable_get_array(name);
    if (arr) {
        out->kind = arr->is_associative ? LUSH_VALUE_MAP : LUSH_VALUE_LIST;
        out->array = arr;
        return true;
    }

    char *scalar = symtable_get_var(symtable_manager(), name);
    if (scalar) {
        out->kind = LUSH_VALUE_SCALAR;
        out->scalar_value = scalar;
        return true;
    }

    return false;
}

/*
 * Implementation of lush_value_view_clear. Contract documented in
 * symtable.h. Idempotent: zeroes the struct after freeing so a
 * follow-up call is safe.
 */
void lush_value_view_clear(lush_value_view_t *view) {
    if (!view) {
        return;
    }
    free(view->scalar_value);
    view->scalar_value = NULL;
    view->array = NULL;
    view->kind = LUSH_VALUE_NONE;
}

/**
 * @brief Set an array element using shell syntax
 *
 * This is the user-facing API for array element assignment.
 * It handles the translation between user indices (which may be 1-indexed
 * in zsh mode) and internal indices (always 0-indexed).
 */
int symtable_set_array_element(const char *name, const char *subscript,
                               const char *value) {
    if (!name || !subscript) {
        return -1;
    }

    array_value_t *array = symtable_get_array(name);
    if (!array) {
        /// Create new indexed array
        array = symtable_array_create(false);
        if (!array) {
            return -1;
        }
        if (symtable_set_array(name, array) < 0) {
            symtable_array_free(array);
            return -1;
        }
    }

    if (array->is_associative) {
        return symtable_array_set_assoc(array, subscript, value);
    } else {
        /// Parse subscript as integer
        char *endptr;
        long index = strtol(subscript, &endptr, 10);
        if (*endptr != '\0') {
            /// Not a valid integer - could be associative key
            /// For now, treat as error for indexed array
            return -1;
        }

        /// Adjust for 1-indexed arrays (zsh mode)
        /// When FEATURE_ARRAY_ZERO_INDEXED is false, user index 1 maps to
        /// internal 0
        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
            if (index <= 0) {
                return -1; /// In 1-indexed mode, index 0 and below are invalid
            }
            index--; /// Convert 1-indexed to 0-indexed internally
        } else if (index < 0) {
            return -1; /// 0-indexed mode doesn't allow negative here
        }

        return symtable_array_set_index(array, (int)index, value);
    }
}

/**
 * @brief Get an array element using shell syntax
 *
 * This is the user-facing API for array element access.
 * It handles the translation between user indices (which may be 1-indexed
 * in zsh mode) and internal indices (always 0-indexed).
 */
char *symtable_get_array_element(const char *name, const char *subscript) {
    if (!name || !subscript) {
        return NULL;
    }

    array_value_t *array = symtable_get_array(name);
    if (!array) {
        return NULL;
    }

    const char *result;
    if (array->is_associative) {
        result = symtable_array_get_assoc(array, subscript);
    } else {
        char *endptr;
        long index = strtol(subscript, &endptr, 10);
        if (*endptr != '\0') {
            return NULL;
        }

        /// Adjust for 1-indexed arrays (zsh mode)
        /// When FEATURE_ARRAY_ZERO_INDEXED is false, user index 1 maps to
        /// internal 0
        if (!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED)) {
            if (index <= 0) {
                return NULL; /// In 1-indexed mode, index 0 and below are
                             /// invalid
            }
            index--; /// Convert 1-indexed to 0-indexed internally
        } else if (index < 0) {
            return NULL; /// 0-indexed mode doesn't allow negative here
        }

        result = symtable_array_get_index(array, (int)index);
    }

    return result ? strdup(result) : NULL;
}

/*
 * Implementation of symtable_enumerate_arrays. Contract documented in
 * symtable.h. With the storage unification, arrays live in the global
 * scope's vars_ht (or in active local scopes); this iteration walks
 * the global scope's hashtable and invokes the callback for every
 * SYMVAR_ARRAY entry that's not currently UNSET. Local-scope arrays
 * are intentionally not surfaced -- they're transient to function
 * invocations and out of scope for shell-level enumeration.
 */
void symtable_enumerate_arrays(void (*callback)(const char *name,
                                                array_value_t *array,
                                                void *userdata),
                               void *userdata) {
    if (!callback || !global_manager || !global_manager->global_scope ||
        !global_manager->global_scope->vars_ht) {
        return;
    }
    ht_enum_t *e = ht_strstr_enum_create(global_manager->global_scope->vars_ht);
    if (!e) {
        return;
    }
    const char *key, *serialized;
    while (ht_strstr_enum_next(e, &key, &serialized)) {
        symvar_t *var = deserialize_variable(key, serialized);
        if (var && var->type == SYMVAR_ARRAY && var->array &&
            !(var->flags & SYMVAR_UNSET)) {
            callback(key, var->array, userdata);
        }
        free_symvar(var);
    }
    ht_strstr_enum_destroy(e);
}
