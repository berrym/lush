/**
 * @file lush_plugin.c
 * @brief Lush Plugin System Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implementation of the plugin system for lush shell.
 */

#include "lush_plugin.h"
#include "shell_mode.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Get current time in nanoseconds
 */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Duplicate a string safely
 */
static char *safe_strdup(const char *s) {
    if (!s)
        return NULL;
    return strdup(s);
}

/**
 * @brief Set plugin error message
 */
static void set_plugin_error(lush_plugin_t *plugin, const char *fmt, ...) {
    if (!plugin)
        return;

    free(plugin->error_message);
    plugin->error_message = NULL;

    if (!fmt)
        return;

    va_list args;
    va_start(args, fmt);

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    plugin->error_message = safe_strdup(buf);

    va_end(args);
}

/* ============================================================================
 * RESULT AND STATE STRINGS
 * ============================================================================
 */

const char *lush_plugin_result_string(lush_plugin_result_t result) {
    switch (result) {
    case LUSH_PLUGIN_OK:
        return "Success";
    case LUSH_PLUGIN_ERROR:
        return "Generic error";
    case LUSH_PLUGIN_ERROR_NOT_FOUND:
        return "Plugin not found";
    case LUSH_PLUGIN_ERROR_LOAD_FAILED:
        return "Failed to load plugin";
    case LUSH_PLUGIN_ERROR_SYMBOL_NOT_FOUND:
        return "Required symbol not found";
    case LUSH_PLUGIN_ERROR_VERSION_MISMATCH:
        return "API version mismatch";
    case LUSH_PLUGIN_ERROR_INIT_FAILED:
        return "Plugin initialization failed";
    case LUSH_PLUGIN_ERROR_PERMISSION_DENIED:
        return "Permission denied";
    case LUSH_PLUGIN_ERROR_ALREADY_LOADED:
        return "Plugin already loaded";
    case LUSH_PLUGIN_ERROR_INVALID_PLUGIN:
        return "Invalid plugin definition";
    case LUSH_PLUGIN_ERROR_OUT_OF_MEMORY:
        return "Out of memory";
    default:
        return "Unknown error";
    }
}

const char *lush_plugin_state_string(lush_plugin_state_t state) {
    switch (state) {
    case LUSH_PLUGIN_STATE_UNLOADED:
        return "unloaded";
    case LUSH_PLUGIN_STATE_LOADING:
        return "loading";
    case LUSH_PLUGIN_STATE_LOADED:
        return "loaded";
    case LUSH_PLUGIN_STATE_INITIALIZING:
        return "initializing";
    case LUSH_PLUGIN_STATE_ACTIVE:
        return "active";
    case LUSH_PLUGIN_STATE_SUSPENDED:
        return "suspended";
    case LUSH_PLUGIN_STATE_ERROR:
        return "error";
    case LUSH_PLUGIN_STATE_UNLOADING:
        return "unloading";
    default:
        return "unknown";
    }
}

/* ============================================================================
 * PLUGIN MANAGER IMPLEMENTATION
 * ============================================================================
 */

lush_plugin_result_t
lush_plugin_manager_create(lush_plugin_manager_t **manager,
                           const lush_plugin_manager_config_t *config) {
    if (!manager) {
        return LUSH_PLUGIN_ERROR;
    }

    lush_plugin_manager_t *mgr = calloc(1, sizeof(lush_plugin_manager_t));
    if (!mgr) {
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Apply configuration */
    if (config) {
        mgr->config = *config;
    } else {
        /* Default configuration */
        mgr->config.auto_load = false;
        mgr->config.default_permissions = LUSH_PLUGIN_PERM_REGISTER_BUILTIN |
                                          LUSH_PLUGIN_PERM_REGISTER_HOOK |
                                          LUSH_PLUGIN_PERM_READ_VARS;
        mgr->config.enable_sandbox = true;
        mgr->config.max_plugins = 0; /* unlimited */
    }

    mgr->plugins = NULL;
    mgr->plugin_count = 0;
    mgr->executor = NULL;
    mgr->symtable = NULL;
    mgr->active = true;

    *manager = mgr;
    return LUSH_PLUGIN_OK;
}

void lush_plugin_manager_destroy(lush_plugin_manager_t *manager) {
    if (!manager)
        return;

    manager->active = false;

    /* Unload all plugins */
    lush_plugin_t *plugin = manager->plugins;
    while (plugin) {
        lush_plugin_t *next = plugin->next;

        /* Call cleanup if active */
        if (plugin->state == LUSH_PLUGIN_STATE_ACTIVE && plugin->def &&
            plugin->def->cleanup) {
            plugin->def->cleanup(plugin->ctx);
        }

        /* Free registered builtins list */
        if (plugin->registered_builtins) {
            for (size_t i = 0; i < plugin->registered_builtin_count; i++) {
                free(plugin->registered_builtins[i]);
            }
            free(plugin->registered_builtins);
        }

        /* Free context */
        free(plugin->ctx);

        /* Close shared object */
        if (plugin->handle) {
            dlclose(plugin->handle);
        }

        /* Free plugin data */
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);

        plugin = next;
    }

    free(manager);
}

void lush_plugin_manager_set_executor(lush_plugin_manager_t *manager,
                                      struct executor *executor) {
    if (manager) {
        manager->executor = executor;
    }
}

void lush_plugin_manager_set_symtable(lush_plugin_manager_t *manager,
                                      struct symtable *symtable) {
    if (manager) {
        manager->symtable = symtable;
    }
}

lush_plugin_result_t lush_plugin_manager_load(lush_plugin_manager_t *manager,
                                              const char *path,
                                              lush_plugin_t **out_plugin) {
    if (!manager || !path) {
        return LUSH_PLUGIN_ERROR;
    }

    if (!manager->active) {
        return LUSH_PLUGIN_ERROR;
    }

    /* Check plugin limit */
    if (manager->config.max_plugins > 0 &&
        manager->plugin_count >= manager->config.max_plugins) {
        return LUSH_PLUGIN_ERROR;
    }

    /* Allocate plugin structure */
    lush_plugin_t *plugin = calloc(1, sizeof(lush_plugin_t));
    if (!plugin) {
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    plugin->state = LUSH_PLUGIN_STATE_LOADING;
    plugin->path = safe_strdup(path);
    if (!plugin->path) {
        free(plugin);
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    /* Open shared object */
    plugin->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!plugin->handle) {
        set_plugin_error(plugin, "dlopen failed: %s", dlerror());
        plugin->state = LUSH_PLUGIN_STATE_ERROR;
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_LOAD_FAILED;
    }

    /* Find plugin definition symbol */
    dlerror(); /* Clear any existing error */
    const lush_plugin_def_t *def =
        (const lush_plugin_def_t *)dlsym(plugin->handle, LUSH_PLUGIN_SYMBOL);
    char *error = dlerror();
    if (error || !def) {
        set_plugin_error(plugin, "Symbol '%s' not found: %s",
                         LUSH_PLUGIN_SYMBOL, error ? error : "NULL definition");
        plugin->state = LUSH_PLUGIN_STATE_ERROR;
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_SYMBOL_NOT_FOUND;
    }

    /* Validate plugin definition */
    if (!def->name || !def->version || !def->init) {
        set_plugin_error(plugin, "Invalid plugin: missing required fields");
        plugin->state = LUSH_PLUGIN_STATE_ERROR;
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_INVALID_PLUGIN;
    }

    /* Check API version */
    if (def->api_version < LUSH_PLUGIN_API_VERSION_MIN ||
        def->api_version > LUSH_PLUGIN_API_VERSION) {
        set_plugin_error(plugin, "API version %u not supported (need %u-%u)",
                         def->api_version, LUSH_PLUGIN_API_VERSION_MIN,
                         LUSH_PLUGIN_API_VERSION);
        plugin->state = LUSH_PLUGIN_STATE_ERROR;
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_VERSION_MISMATCH;
    }

    /* Check if already loaded */
    if (lush_plugin_manager_find(manager, def->name)) {
        set_plugin_error(plugin, "Plugin '%s' already loaded", def->name);
        plugin->state = LUSH_PLUGIN_STATE_ERROR;
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_ALREADY_LOADED;
    }

    plugin->def = def;
    plugin->state = LUSH_PLUGIN_STATE_LOADED;
    plugin->load_time = get_time_ns();

    /* Create plugin context */
    plugin->ctx = calloc(1, sizeof(lush_plugin_context_t));
    if (!plugin->ctx) {
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin);
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    plugin->ctx->plugin = plugin;
    plugin->ctx->manager = manager;
    plugin->ctx->executor = manager->executor;
    plugin->ctx->symtable = manager->symtable;
    plugin->ctx->user_data = NULL;

    /* Grant permissions (intersection of required and default) */
    plugin->ctx->granted_permissions =
        def->required_permissions & manager->config.default_permissions;

    /* Check if all required permissions are granted */
    if ((plugin->ctx->granted_permissions & def->required_permissions) !=
        def->required_permissions) {
        set_plugin_error(plugin, "Required permissions not granted");
        plugin->state = LUSH_PLUGIN_STATE_ERROR;
        free(plugin->ctx);
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_PERMISSION_DENIED;
    }

    /* Initialize plugin */
    plugin->state = LUSH_PLUGIN_STATE_INITIALIZING;
    int init_result = def->init(plugin->ctx);
    if (init_result != 0) {
        set_plugin_error(plugin, "init() returned %d", init_result);
        plugin->state = LUSH_PLUGIN_STATE_ERROR;

        /* Free registered builtins */
        if (plugin->registered_builtins) {
            for (size_t i = 0; i < plugin->registered_builtin_count; i++) {
                free(plugin->registered_builtins[i]);
            }
            free(plugin->registered_builtins);
        }

        free(plugin->ctx);
        dlclose(plugin->handle);
        free(plugin->path);
        free(plugin->error_message);
        free(plugin);
        return LUSH_PLUGIN_ERROR_INIT_FAILED;
    }

    plugin->state = LUSH_PLUGIN_STATE_ACTIVE;

    /* Add to manager's list */
    plugin->next = manager->plugins;
    manager->plugins = plugin;
    manager->plugin_count++;

    if (out_plugin) {
        *out_plugin = plugin;
    }

    return LUSH_PLUGIN_OK;
}

lush_plugin_result_t
lush_plugin_manager_load_by_name(lush_plugin_manager_t *manager,
                                 const char *name, lush_plugin_t **plugin) {
    if (!manager || !name) {
        return LUSH_PLUGIN_ERROR;
    }

    /* Check if already loaded */
    if (lush_plugin_manager_find(manager, name)) {
        return LUSH_PLUGIN_ERROR_ALREADY_LOADED;
    }

    /* Try search paths */
    if (manager->config.search_paths) {
        for (const char **path = manager->config.search_paths; *path; path++) {
            char full_path[4096];
            snprintf(full_path, sizeof(full_path), "%s/%s.so", *path, name);

            /* Check if file exists */
            FILE *f = fopen(full_path, "r");
            if (f) {
                fclose(f);
                return lush_plugin_manager_load(manager, full_path, plugin);
            }

            /* Try with lib prefix */
            snprintf(full_path, sizeof(full_path), "%s/lib%s.so", *path, name);
            f = fopen(full_path, "r");
            if (f) {
                fclose(f);
                return lush_plugin_manager_load(manager, full_path, plugin);
            }
        }
    }

    /* Try current directory */
    char local_path[4096];
    snprintf(local_path, sizeof(local_path), "./%s.so", name);
    FILE *f = fopen(local_path, "r");
    if (f) {
        fclose(f);
        return lush_plugin_manager_load(manager, local_path, plugin);
    }

    return LUSH_PLUGIN_ERROR_NOT_FOUND;
}

lush_plugin_result_t lush_plugin_manager_unload(lush_plugin_manager_t *manager,
                                                const char *name) {
    if (!manager || !name) {
        return LUSH_PLUGIN_ERROR;
    }

    lush_plugin_t *prev = NULL;
    lush_plugin_t *plugin = manager->plugins;

    while (plugin) {
        if (plugin->def && plugin->def->name &&
            strcmp(plugin->def->name, name) == 0) {
            break;
        }
        prev = plugin;
        plugin = plugin->next;
    }

    if (!plugin) {
        return LUSH_PLUGIN_ERROR_NOT_FOUND;
    }

    plugin->state = LUSH_PLUGIN_STATE_UNLOADING;

    /* Call cleanup */
    if (plugin->def && plugin->def->cleanup) {
        plugin->def->cleanup(plugin->ctx);
    }

    /* Remove from list */
    if (prev) {
        prev->next = plugin->next;
    } else {
        manager->plugins = plugin->next;
    }
    manager->plugin_count--;

    /* Free registered builtins */
    if (plugin->registered_builtins) {
        for (size_t i = 0; i < plugin->registered_builtin_count; i++) {
            free(plugin->registered_builtins[i]);
        }
        free(plugin->registered_builtins);
    }

    /* Free context */
    free(plugin->ctx);

    /* Close shared object */
    if (plugin->handle) {
        dlclose(plugin->handle);
    }

    /* Free plugin data */
    free(plugin->path);
    free(plugin->error_message);
    free(plugin);

    return LUSH_PLUGIN_OK;
}

lush_plugin_t *lush_plugin_manager_find(lush_plugin_manager_t *manager,
                                        const char *name) {
    if (!manager || !name) {
        return NULL;
    }

    lush_plugin_t *plugin = manager->plugins;
    while (plugin) {
        if (plugin->def && plugin->def->name &&
            strcmp(plugin->def->name, name) == 0) {
            return plugin;
        }
        plugin = plugin->next;
    }

    return NULL;
}

lush_plugin_result_t lush_plugin_manager_list(lush_plugin_manager_t *manager,
                                              lush_plugin_t **plugins,
                                              size_t *count) {
    if (!manager || !count) {
        return LUSH_PLUGIN_ERROR;
    }

    size_t max_count = *count;
    *count = manager->plugin_count;

    if (!plugins) {
        return LUSH_PLUGIN_OK;
    }

    size_t i = 0;
    lush_plugin_t *plugin = manager->plugins;
    while (plugin && i < max_count) {
        plugins[i++] = plugin;
        plugin = plugin->next;
    }

    return LUSH_PLUGIN_OK;
}

lush_plugin_result_t lush_plugin_manager_reload(lush_plugin_manager_t *manager,
                                                const char *name) {
    if (!manager || !name) {
        return LUSH_PLUGIN_ERROR;
    }

    lush_plugin_t *plugin = lush_plugin_manager_find(manager, name);
    if (!plugin) {
        return LUSH_PLUGIN_ERROR_NOT_FOUND;
    }

    char *path = safe_strdup(plugin->path);
    if (!path) {
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    lush_plugin_result_t result = lush_plugin_manager_unload(manager, name);
    if (result != LUSH_PLUGIN_OK) {
        free(path);
        return result;
    }

    result = lush_plugin_manager_load(manager, path, NULL);
    free(path);

    return result;
}

/* ============================================================================
 * PLUGIN REGISTRATION API
 * ============================================================================
 */

lush_plugin_result_t lush_plugin_register_builtin(lush_plugin_context_t *ctx,
                                                  const char *name,
                                                  lush_plugin_builtin_fn fn) {
    if (!ctx || !name || !fn) {
        return LUSH_PLUGIN_ERROR;
    }

    if (!lush_plugin_has_permission(ctx, LUSH_PLUGIN_PERM_REGISTER_BUILTIN)) {
        return LUSH_PLUGIN_ERROR_PERMISSION_DENIED;
    }

    lush_plugin_t *plugin = ctx->plugin;
    if (!plugin) {
        return LUSH_PLUGIN_ERROR;
    }

    /* Track registered builtin for cleanup */
    size_t new_count = plugin->registered_builtin_count + 1;
    char **new_builtins =
        realloc(plugin->registered_builtins, new_count * sizeof(char *));
    if (!new_builtins) {
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    new_builtins[plugin->registered_builtin_count] = safe_strdup(name);
    if (!new_builtins[plugin->registered_builtin_count]) {
        return LUSH_PLUGIN_ERROR_OUT_OF_MEMORY;
    }

    plugin->registered_builtins = new_builtins;
    plugin->registered_builtin_count = new_count;

    /* NOTE: Actual registration with the shell's builtin system
     * would be implemented here. For the foundation, we just track
     * what builtins the plugin wants to register.
     *
     * Future integration:
     * return register_plugin_builtin(ctx->executor, name, fn);
     */

    return LUSH_PLUGIN_OK;
}

lush_plugin_result_t lush_plugin_unregister_builtin(lush_plugin_context_t *ctx,
                                                    const char *name) {
    if (!ctx || !name) {
        return LUSH_PLUGIN_ERROR;
    }

    lush_plugin_t *plugin = ctx->plugin;
    if (!plugin) {
        return LUSH_PLUGIN_ERROR;
    }

    /* Remove from tracked list */
    for (size_t i = 0; i < plugin->registered_builtin_count; i++) {
        if (plugin->registered_builtins[i] &&
            strcmp(plugin->registered_builtins[i], name) == 0) {
            free(plugin->registered_builtins[i]);
            /* Move last element to this position */
            plugin->registered_builtins[i] =
                plugin
                    ->registered_builtins[plugin->registered_builtin_count - 1];
            plugin->registered_builtin_count--;
            break;
        }
    }

    /* NOTE: Actual unregistration would happen here */

    return LUSH_PLUGIN_OK;
}

lush_plugin_result_t lush_plugin_register_hook(lush_plugin_context_t *ctx,
                                               const char *hook_name,
                                               lush_plugin_hook_fn fn) {
    if (!ctx || !hook_name || !fn) {
        return LUSH_PLUGIN_ERROR;
    }

    if (!lush_plugin_has_permission(ctx, LUSH_PLUGIN_PERM_REGISTER_HOOK)) {
        return LUSH_PLUGIN_ERROR_PERMISSION_DENIED;
    }

    /* NOTE: Integration with lle_shell_hooks would happen here.
     * For foundation, we validate the hook name.
     */
    if (strcmp(hook_name, "precmd") != 0 && strcmp(hook_name, "preexec") != 0 &&
        strcmp(hook_name, "chpwd") != 0) {
        return LUSH_PLUGIN_ERROR;
    }

    return LUSH_PLUGIN_OK;
}

lush_plugin_result_t
lush_plugin_register_completion(lush_plugin_context_t *ctx, const char *name,
                                lush_plugin_completion_fn fn) {
    if (!ctx || !name || !fn) {
        return LUSH_PLUGIN_ERROR;
    }

    if (!lush_plugin_has_permission(ctx, LUSH_PLUGIN_PERM_COMPLETIONS)) {
        return LUSH_PLUGIN_ERROR_PERMISSION_DENIED;
    }

    /* NOTE: Integration with LLE completion system would happen here */

    return LUSH_PLUGIN_OK;
}

lush_plugin_result_t lush_plugin_subscribe_event(lush_plugin_context_t *ctx,
                                                 int event_type,
                                                 lush_plugin_event_fn fn) {
    (void)event_type; /* Used when integrating with LLE event system */

    if (!ctx || !fn) {
        return LUSH_PLUGIN_ERROR;
    }

    if (!lush_plugin_has_permission(ctx, LUSH_PLUGIN_PERM_EVENTS)) {
        return LUSH_PLUGIN_ERROR_PERMISSION_DENIED;
    }

    /* NOTE: Integration with LLE event system would happen here */

    return LUSH_PLUGIN_OK;
}

/* ============================================================================
 * VARIABLE ACCESS API
 * ============================================================================
 */

const char *lush_plugin_get_var(lush_plugin_context_t *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }

    if (!lush_plugin_has_permission(ctx, LUSH_PLUGIN_PERM_READ_VARS)) {
        return NULL;
    }

    /* NOTE: Integration with symtable would happen here
     * return symtable_get_value(ctx->symtable, name);
     */

    return NULL;
}

lush_plugin_result_t lush_plugin_set_var(lush_plugin_context_t *ctx,
                                         const char *name, const char *value) {
    (void)value; /* Used when integrating with symtable */

    if (!ctx || !name) {
        return LUSH_PLUGIN_ERROR;
    }

    if (!lush_plugin_has_permission(ctx, LUSH_PLUGIN_PERM_WRITE_VARS)) {
        return LUSH_PLUGIN_ERROR_PERMISSION_DENIED;
    }

    /* NOTE: Integration with symtable would happen here
     * symtable_set_value(ctx->symtable, name, value);
     */

    return LUSH_PLUGIN_OK;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

bool lush_plugin_has_permission(lush_plugin_context_t *ctx,
                                lush_plugin_permission_t perm) {
    if (!ctx)
        return false;
    return (ctx->granted_permissions & perm) == perm;
}

void lush_plugin_log(lush_plugin_context_t *ctx, int level, const char *fmt,
                     ...) {
    if (!ctx || !fmt)
        return;

    const char *level_str;
    switch (level) {
    case 0:
        level_str = "DEBUG";
        break;
    case 1:
        level_str = "INFO";
        break;
    case 2:
        level_str = "WARN";
        break;
    case 3:
        level_str = "ERROR";
        break;
    default:
        level_str = "LOG";
        break;
    }

    const char *plugin_name = "unknown";
    if (ctx->plugin && ctx->plugin->def && ctx->plugin->def->name) {
        plugin_name = ctx->plugin->def->name;
    }

    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "[%s][%s] ", plugin_name, level_str);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);
}
