/**
 * @file lush_plugin.h
 * @brief Lush Plugin System Foundation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Plugin system for extending lush shell functionality. Plugins can:
 * - Register new builtins
 * - Add hook functions
 * - Provide custom completions
 * - Extend the editor (via LLE widgets)
 * - Subscribe to shell events
 *
 * Design Principles:
 * - Simple, focused API following lush patterns
 * - Dynamic loading via dlopen/dlsym
 * - Sandboxed execution with permission system
 * - Memory-safe with proper lifecycle management
 * - Version compatibility checking
 *
 * Example Plugin:
 *
 *     // my_plugin.c
 *     #include <lush_plugin.h>
 *
 *     static int my_builtin(int argc, char **argv) {
 *         printf("Hello from plugin!\n");
 *         return 0;
 *     }
 *
 *     LUSH_PLUGIN_DEFINE(
 *         .name = "my-plugin",
 *         .version = "1.0.0",
 *         .description = "Example plugin",
 *         .init = my_plugin_init,
 *         .cleanup = my_plugin_cleanup,
 *     );
 *
 *     int my_plugin_init(lush_plugin_context_t *ctx) {
 *         lush_plugin_register_builtin(ctx, "my-cmd", my_builtin);
 *         return 0;
 *     }
 */

#ifndef LUSH_PLUGIN_H
#define LUSH_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION AND COMPATIBILITY
 * ============================================================================
 */

/** Plugin API version - increment on breaking changes */
#define LUSH_PLUGIN_API_VERSION 1

/** Minimum API version for backwards compatibility */
#define LUSH_PLUGIN_API_VERSION_MIN 1

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

typedef struct lush_plugin lush_plugin_t;
typedef struct lush_plugin_manager lush_plugin_manager_t;
typedef struct lush_plugin_context lush_plugin_context_t;

/* Forward declarations from other lush components */
struct executor;
struct symtable;

/* ============================================================================
 * PLUGIN PERMISSIONS
 * ============================================================================
 */

/**
 * @brief Plugin permission flags
 *
 * Controls what a plugin is allowed to do. Plugins declare required
 * permissions, and users can grant or deny them.
 */
typedef enum {
    LUSH_PLUGIN_PERM_NONE = 0,

    /* Builtin registration */
    LUSH_PLUGIN_PERM_REGISTER_BUILTIN = 1 << 0,

    /* Hook registration */
    LUSH_PLUGIN_PERM_REGISTER_HOOK = 1 << 1,

    /* Variable access */
    LUSH_PLUGIN_PERM_READ_VARS = 1 << 2,
    LUSH_PLUGIN_PERM_WRITE_VARS = 1 << 3,

    /* File system access */
    LUSH_PLUGIN_PERM_FILE_READ = 1 << 4,
    LUSH_PLUGIN_PERM_FILE_WRITE = 1 << 5,

    /* Network access */
    LUSH_PLUGIN_PERM_NETWORK = 1 << 6,

    /* Event subscription */
    LUSH_PLUGIN_PERM_EVENTS = 1 << 7,

    /* Completion provider */
    LUSH_PLUGIN_PERM_COMPLETIONS = 1 << 8,

    /* Widget registration (LLE) */
    LUSH_PLUGIN_PERM_WIDGETS = 1 << 9,

    /* Execute external commands */
    LUSH_PLUGIN_PERM_EXEC = 1 << 10,

    /* All permissions (for trusted plugins) */
    LUSH_PLUGIN_PERM_ALL = 0x7FF,
} lush_plugin_permission_t;

/* ============================================================================
 * PLUGIN STATE
 * ============================================================================
 */

/**
 * @brief Plugin lifecycle states
 */
typedef enum {
    LUSH_PLUGIN_STATE_UNLOADED,     /**< Not loaded */
    LUSH_PLUGIN_STATE_LOADING,      /**< Being loaded */
    LUSH_PLUGIN_STATE_LOADED,       /**< Loaded but not initialized */
    LUSH_PLUGIN_STATE_INITIALIZING, /**< Being initialized */
    LUSH_PLUGIN_STATE_ACTIVE,       /**< Fully active */
    LUSH_PLUGIN_STATE_SUSPENDED,    /**< Temporarily suspended */
    LUSH_PLUGIN_STATE_ERROR,        /**< Error state */
    LUSH_PLUGIN_STATE_UNLOADING,    /**< Being unloaded */
} lush_plugin_state_t;

/* ============================================================================
 * PLUGIN RESULT CODES
 * ============================================================================
 */

/**
 * @brief Plugin operation result codes
 */
typedef enum {
    LUSH_PLUGIN_OK = 0,                       /**< Success */
    LUSH_PLUGIN_ERROR = -1,                   /**< Generic error */
    LUSH_PLUGIN_ERROR_NOT_FOUND = -2,         /**< Plugin not found */
    LUSH_PLUGIN_ERROR_LOAD_FAILED = -3,       /**< dlopen failed */
    LUSH_PLUGIN_ERROR_SYMBOL_NOT_FOUND = -4,  /**< Missing required symbol */
    LUSH_PLUGIN_ERROR_VERSION_MISMATCH = -5,  /**< API version incompatible */
    LUSH_PLUGIN_ERROR_INIT_FAILED = -6,       /**< Plugin init() failed */
    LUSH_PLUGIN_ERROR_PERMISSION_DENIED = -7, /**< Permission not granted */
    LUSH_PLUGIN_ERROR_ALREADY_LOADED = -8,    /**< Plugin already loaded */
    LUSH_PLUGIN_ERROR_INVALID_PLUGIN = -9,    /**< Invalid plugin definition */
    LUSH_PLUGIN_ERROR_OUT_OF_MEMORY = -10,    /**< Memory allocation failed */
} lush_plugin_result_t;

/* ============================================================================
 * CALLBACK SIGNATURES
 * ============================================================================
 */

/**
 * @brief Builtin command callback
 *
 * Same signature as internal builtins for consistency.
 */
typedef int (*lush_plugin_builtin_fn)(int argc, char **argv);

/**
 * @brief Hook function callback
 *
 * Called for shell lifecycle events (precmd, preexec, chpwd).
 */
typedef void (*lush_plugin_hook_fn)(lush_plugin_context_t *ctx,
                                    const char *event_data);

/**
 * @brief Event handler callback
 *
 * Called when subscribed events occur.
 */
typedef void (*lush_plugin_event_fn)(lush_plugin_context_t *ctx, int event_type,
                                     void *event_data);

/**
 * @brief Completion provider callback
 *
 * Returns completions for a given input prefix.
 *
 * @param ctx Plugin context
 * @param line Current input line
 * @param cursor Cursor position
 * @param completions Output array (plugin allocates, shell frees)
 * @param count Output count of completions
 * @return 0 on success
 */
typedef int (*lush_plugin_completion_fn)(lush_plugin_context_t *ctx,
                                         const char *line, size_t cursor,
                                         char ***completions, size_t *count);

/* ============================================================================
 * PLUGIN DEFINITION
 * ============================================================================
 */

/**
 * @brief Plugin definition structure
 *
 * Every plugin must export a `lush_plugin_definition` symbol of this type.
 * Use the LUSH_PLUGIN_DEFINE macro for convenience.
 */
typedef struct lush_plugin_def {
    /** API version - must match LUSH_PLUGIN_API_VERSION */
    uint32_t api_version;

    /** Plugin name (unique identifier) */
    const char *name;

    /** Human-readable version string (semver recommended) */
    const char *version;

    /** Short description of the plugin */
    const char *description;

    /** Author/maintainer */
    const char *author;

    /** License (e.g., "MIT", "GPL-3.0") */
    const char *license;

    /** Required permissions (bitfield) */
    lush_plugin_permission_t required_permissions;

    /** Optional dependencies (NULL-terminated array of plugin names) */
    const char **dependencies;

    /**
     * @brief Initialize the plugin
     *
     * Called after loading. Plugin should register builtins, hooks, etc.
     *
     * @param ctx Plugin context for registration APIs
     * @return 0 on success, non-zero on failure
     */
    int (*init)(lush_plugin_context_t *ctx);

    /**
     * @brief Clean up the plugin
     *
     * Called before unloading. Plugin should free resources.
     *
     * @param ctx Plugin context
     */
    void (*cleanup)(lush_plugin_context_t *ctx);

    /**
     * @brief Suspend the plugin (optional)
     *
     * Called when plugin is temporarily suspended.
     *
     * @param ctx Plugin context
     */
    void (*suspend)(lush_plugin_context_t *ctx);

    /**
     * @brief Resume the plugin (optional)
     *
     * Called when plugin is resumed after suspension.
     *
     * @param ctx Plugin context
     */
    void (*resume)(lush_plugin_context_t *ctx);

} lush_plugin_def_t;

/**
 * @brief Convenience macro for defining a plugin
 *
 * Usage:
 *     LUSH_PLUGIN_DEFINE(
 *         .name = "my-plugin",
 *         .version = "1.0.0",
 *         .init = my_init,
 *         .cleanup = my_cleanup,
 *     );
 */
#define LUSH_PLUGIN_DEFINE(...)                                                \
    const lush_plugin_def_t lush_plugin_definition = {                         \
        .api_version = LUSH_PLUGIN_API_VERSION, __VA_ARGS__}

/** Symbol name for plugin definition (for dlsym) */
#define LUSH_PLUGIN_SYMBOL "lush_plugin_definition"

/* ============================================================================
 * PLUGIN CONTEXT
 * ============================================================================
 */

/**
 * @brief Plugin execution context
 *
 * Passed to plugin callbacks, provides access to shell internals
 * (subject to permissions) and registration APIs.
 */
struct lush_plugin_context {
    /** The plugin this context belongs to */
    lush_plugin_t *plugin;

    /** Plugin manager for inter-plugin communication */
    lush_plugin_manager_t *manager;

    /** Executor access (if permitted) */
    struct executor *executor;

    /** Symbol table access (if permitted) */
    struct symtable *symtable;

    /** Plugin-private data (set by plugin, freed by plugin) */
    void *user_data;

    /** Granted permissions (may be less than requested) */
    lush_plugin_permission_t granted_permissions;
};

/* ============================================================================
 * PLUGIN INSTANCE
 * ============================================================================
 */

/**
 * @brief Plugin instance structure
 *
 * Represents a loaded plugin. Managed by the plugin manager.
 */
struct lush_plugin {
    /** Plugin definition (from shared object) */
    const lush_plugin_def_t *def;

    /** Current state */
    lush_plugin_state_t state;

    /** Path to the shared object */
    char *path;

    /** dlopen handle */
    void *handle;

    /** Plugin context */
    lush_plugin_context_t *ctx;

    /** Registered builtins (for cleanup) */
    char **registered_builtins;
    size_t registered_builtin_count;

    /** Error message (if state == ERROR) */
    char *error_message;

    /** Load timestamp */
    uint64_t load_time;

    /** Next plugin in manager's list */
    struct lush_plugin *next;
};

/* ============================================================================
 * PLUGIN MANAGER
 * ============================================================================
 */

/**
 * @brief Plugin manager configuration
 */
typedef struct {
    /** Plugin search paths (NULL-terminated array) */
    const char **search_paths;

    /** Auto-load plugins from paths on init */
    bool auto_load;

    /** Default permissions for new plugins */
    lush_plugin_permission_t default_permissions;

    /** Enable plugin sandboxing */
    bool enable_sandbox;

    /** Maximum loaded plugins (0 = unlimited) */
    size_t max_plugins;
} lush_plugin_manager_config_t;

/**
 * @brief Plugin manager structure
 *
 * Central registry for all plugins. Handles loading, unloading,
 * and lifecycle management.
 */
struct lush_plugin_manager {
    /** Loaded plugins (linked list) */
    lush_plugin_t *plugins;

    /** Plugin count */
    size_t plugin_count;

    /** Configuration */
    lush_plugin_manager_config_t config;

    /** Executor reference */
    struct executor *executor;

    /** Symbol table reference */
    struct symtable *symtable;

    /** Manager active flag */
    bool active;
};

/* ============================================================================
 * PLUGIN MANAGER API
 * ============================================================================
 */

/**
 * @brief Create plugin manager
 *
 * @param manager Output pointer for created manager
 * @param config Configuration (NULL for defaults)
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t
lush_plugin_manager_create(lush_plugin_manager_t **manager,
                           const lush_plugin_manager_config_t *config);

/**
 * @brief Destroy plugin manager
 *
 * Unloads all plugins and frees resources.
 *
 * @param manager Manager to destroy
 */
void lush_plugin_manager_destroy(lush_plugin_manager_t *manager);

/**
 * @brief Set executor reference
 *
 * @param manager Plugin manager
 * @param executor Executor to use
 */
void lush_plugin_manager_set_executor(lush_plugin_manager_t *manager,
                                      struct executor *executor);

/**
 * @brief Set symbol table reference
 *
 * @param manager Plugin manager
 * @param symtable Symbol table to use
 */
void lush_plugin_manager_set_symtable(lush_plugin_manager_t *manager,
                                      struct symtable *symtable);

/**
 * @brief Load a plugin from a path
 *
 * @param manager Plugin manager
 * @param path Path to shared object
 * @param plugin Output pointer for loaded plugin (optional)
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_manager_load(lush_plugin_manager_t *manager,
                                              const char *path,
                                              lush_plugin_t **plugin);

/**
 * @brief Load a plugin by name
 *
 * Searches configured paths for the plugin.
 *
 * @param manager Plugin manager
 * @param name Plugin name
 * @param plugin Output pointer for loaded plugin (optional)
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t
lush_plugin_manager_load_by_name(lush_plugin_manager_t *manager,
                                 const char *name, lush_plugin_t **plugin);

/**
 * @brief Unload a plugin
 *
 * @param manager Plugin manager
 * @param name Plugin name
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_manager_unload(lush_plugin_manager_t *manager,
                                                const char *name);

/**
 * @brief Find a loaded plugin by name
 *
 * @param manager Plugin manager
 * @param name Plugin name
 * @return Plugin if found, NULL otherwise
 */
lush_plugin_t *lush_plugin_manager_find(lush_plugin_manager_t *manager,
                                        const char *name);

/**
 * @brief Get all loaded plugins
 *
 * @param manager Plugin manager
 * @param plugins Output array (caller allocates)
 * @param count Input: array size, Output: actual count
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_manager_list(lush_plugin_manager_t *manager,
                                              lush_plugin_t **plugins,
                                              size_t *count);

/**
 * @brief Reload a plugin
 *
 * Unloads and reloads the plugin (useful for development).
 *
 * @param manager Plugin manager
 * @param name Plugin name
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_manager_reload(lush_plugin_manager_t *manager,
                                                const char *name);

/* ============================================================================
 * PLUGIN REGISTRATION API (for use by plugins)
 * ============================================================================
 */

/**
 * @brief Register a builtin command
 *
 * @param ctx Plugin context
 * @param name Command name
 * @param fn Command function
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_register_builtin(lush_plugin_context_t *ctx,
                                                  const char *name,
                                                  lush_plugin_builtin_fn fn);

/**
 * @brief Unregister a builtin command
 *
 * @param ctx Plugin context
 * @param name Command name
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_unregister_builtin(lush_plugin_context_t *ctx,
                                                    const char *name);

/**
 * @brief Register a hook function
 *
 * @param ctx Plugin context
 * @param hook_name Hook name ("precmd", "preexec", "chpwd")
 * @param fn Hook function
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_register_hook(lush_plugin_context_t *ctx,
                                               const char *hook_name,
                                               lush_plugin_hook_fn fn);

/**
 * @brief Register a completion provider
 *
 * @param ctx Plugin context
 * @param name Provider name
 * @param fn Completion function
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t
lush_plugin_register_completion(lush_plugin_context_t *ctx, const char *name,
                                lush_plugin_completion_fn fn);

/**
 * @brief Subscribe to shell events
 *
 * @param ctx Plugin context
 * @param event_type Event type to subscribe to
 * @param fn Event handler function
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_subscribe_event(lush_plugin_context_t *ctx,
                                                 int event_type,
                                                 lush_plugin_event_fn fn);

/* ============================================================================
 * PLUGIN VARIABLE ACCESS API
 * ============================================================================
 */

/**
 * @brief Get a shell variable value
 *
 * @param ctx Plugin context
 * @param name Variable name
 * @return Variable value (do not free), NULL if not found
 */
const char *lush_plugin_get_var(lush_plugin_context_t *ctx, const char *name);

/**
 * @brief Set a shell variable value
 *
 * @param ctx Plugin context
 * @param name Variable name
 * @param value Variable value
 * @return LUSH_PLUGIN_OK on success
 */
lush_plugin_result_t lush_plugin_set_var(lush_plugin_context_t *ctx,
                                         const char *name, const char *value);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get string description of result code
 *
 * @param result Result code
 * @return Static string describing the result
 */
const char *lush_plugin_result_string(lush_plugin_result_t result);

/**
 * @brief Get string description of plugin state
 *
 * @param state Plugin state
 * @return Static string describing the state
 */
const char *lush_plugin_state_string(lush_plugin_state_t state);

/**
 * @brief Check if a permission is granted
 *
 * @param ctx Plugin context
 * @param perm Permission to check
 * @return true if permission is granted
 */
bool lush_plugin_has_permission(lush_plugin_context_t *ctx,
                                lush_plugin_permission_t perm);

/**
 * @brief Log a message from plugin
 *
 * @param ctx Plugin context
 * @param level Log level (0=debug, 1=info, 2=warn, 3=error)
 * @param fmt Printf-style format string
 */
void lush_plugin_log(lush_plugin_context_t *ctx, int level, const char *fmt,
                     ...);

#ifdef __cplusplus
}
#endif

#endif /* LUSH_PLUGIN_H */
