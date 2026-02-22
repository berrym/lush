/**
 * @file lle_shell_integration.c
 * @brief LLE Shell Integration - Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements the centralized LLE initialization and lifecycle management.
 * Provides shell-level LLE init, three-tier reset hierarchy, and error
 * tracking.
 *
 * Specification: docs/lle_specification/26_initialization_system_complete.md
 * Date: 2025-01-16
 */

#include "lle/lle_shell_integration.h"
#include "config.h"
#include "executor.h"
#include "lle/arena.h"
#include "lle/adaptive_terminal_integration.h"
#include "lle/display_integration.h"
#include "lle/history.h"
#include "lle/lle_editor.h"
#include "lle/lle_readline.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/lle_shell_hooks.h"
#include "lle/lle_watchdog.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/prompt_expansion.h"
#include "lle/prompt/segment.h"
#include "lle/utf8_support.h"
#include "lle/prompt/theme.h"
#include "lle/prompt/theme_loader.h"
#include "lush.h"
#include "lush_memory_pool.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * LLE COMPLETION SYSTEM BRIDGE - Strong symbols for shell integration
 * ============================================================================
 */

/**
 * @brief Check if autocd feature is enabled (strong symbol)
 *
 * Overrides weak symbol in completion_types.c to provide actual
 * shell mode query for FEATURE_AUTO_CD.
 *
 * @return true if FEATURE_AUTO_CD is enabled, false otherwise
 */
bool lle_shell_autocd_enabled(void) {
    return shell_mode_allows(FEATURE_AUTO_CD);
}

/**
 * @brief Check if a shell function exists by name (strong symbol)
 *
 * Overrides weak symbol in syntax_highlighting.c to provide actual
 * function lookup from the executor's function table.
 *
 * @param name Function name to look up
 * @return true if a function with this name is defined, false otherwise
 */
bool lle_shell_function_exists(const char *name) {
    if (!name || !current_executor) {
        return false;
    }
    function_def_t *func = current_executor->functions;
    while (func) {
        if (func->name && strcmp(func->name, name) == 0) {
            return true;
        }
        func = func->next;
    }
    return false;
}

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================
 */

/** Global shell integration instance */
lle_shell_integration_t *g_lle_integration = NULL;

/** Flag to prevent double atexit registration */
static bool atexit_registered = false;

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

static void lle_shell_integration_atexit_handler(void);
static lle_result_t create_and_configure_editor(lle_shell_integration_t *integ);
static void destroy_editor(lle_shell_integration_t *integ);
static lle_result_t
create_and_configure_prompt_composer(lle_shell_integration_t *integ);
static void destroy_prompt_composer(lle_shell_integration_t *integ);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get current timestamp in microseconds
 */
static uint64_t get_timestamp_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000;
    }
    return 0;
}

/**
 * @brief Populate history config from Lush config system
 */
static void populate_history_config(lle_history_config_t *hist_config) {
    if (!hist_config) {
        return;
    }

    memset(hist_config, 0, sizeof(lle_history_config_t));

    /* Capacity settings */
    hist_config->max_entries =
        config.history_size > 0 ? config.history_size : 5000;
    hist_config->max_command_length = 8192;

    /* File settings */
    if (config.lle_history_file && config.lle_history_file[0] != '\0') {
        hist_config->history_file_path = config.lle_history_file;
    } else {
        hist_config->history_file_path = NULL;
    }
    hist_config->auto_save = true;
    hist_config->load_on_init = true;

    /* Deduplication behavior */
    hist_config->ignore_duplicates =
        config.lle_enable_deduplication &&
        (config.lle_dedup_scope != LLE_DEDUP_SCOPE_NONE);

    /* Map dedup strategy */
    switch (config.lle_dedup_strategy) {
    case LLE_DEDUP_STRATEGY_IGNORE:
        hist_config->dedup_strategy = LLE_DEDUP_IGNORE;
        break;
    case LLE_DEDUP_STRATEGY_KEEP_FREQUENT:
        hist_config->dedup_strategy = LLE_DEDUP_KEEP_FREQUENT;
        break;
    case LLE_DEDUP_STRATEGY_MERGE:
        hist_config->dedup_strategy = LLE_DEDUP_MERGE_METADATA;
        break;
    case LLE_DEDUP_STRATEGY_KEEP_ALL:
        hist_config->dedup_strategy = LLE_DEDUP_KEEP_ALL;
        break;
    case LLE_DEDUP_STRATEGY_KEEP_RECENT:
    default:
        hist_config->dedup_strategy = LLE_DEDUP_KEEP_RECENT;
        break;
    }

    /* Map dedup scope (issue #41) */
    switch (config.lle_dedup_scope) {
    case LLE_DEDUP_SCOPE_NONE:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_NONE;
        break;
    case LLE_DEDUP_SCOPE_SESSION:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_SESSION;
        break;
    case LLE_DEDUP_SCOPE_RECENT:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_RECENT;
        break;
    case LLE_DEDUP_SCOPE_GLOBAL:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_GLOBAL;
        break;
    default:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_SESSION;
        break;
    }

    hist_config->unicode_normalize = config.lle_dedup_unicode_normalize;
    hist_config->ignore_space_prefix = false;

    /* Metadata */
    hist_config->save_timestamps = config.history_timestamps;
    hist_config->save_working_dir = config.lle_enable_forensic_tracking;
    hist_config->save_exit_codes = config.lle_enable_forensic_tracking;

    /* Performance */
    hist_config->initial_capacity =
        config.lle_enable_history_cache && config.lle_cache_size > 0
            ? config.lle_cache_size
            : 1000;
    hist_config->use_indexing = config.lle_enable_history_cache;
}

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize the LLE shell integration subsystem
 *
 * Creates and initializes all LLE subsystems in dependency order:
 * memory pool verification, terminal detection, event hub, editor,
 * history, prompt composer, and watchdog. Registers atexit handler
 * for automatic cleanup.
 *
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_shell_integration_init(void) {
    /* Already initialized? */
    if (g_lle_integration) {
        return LLE_SUCCESS;
    }

    /* Step 1: Verify global memory pool exists */
    if (!global_memory_pool) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /* Step 2: Create session arena - root of arena hierarchy
     * The session arena owns all LLE memory for the shell session.
     * 64KB initial size, grows as needed. */
    lle_arena_t *session_arena =
        lle_arena_create(NULL, "session", 64 * 1024);
    if (!session_arena) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate integration structure from session arena */
    lle_shell_integration_t *integ =
        lle_arena_calloc(session_arena, 1, sizeof(lle_shell_integration_t));
    if (!integ) {
        lle_arena_destroy(session_arena);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    integ->session_arena = session_arena;
    integ->init_time_us = get_timestamp_us();
    integ->init_state.memory_pool_verified = true;

    /* Step 3: Verify terminal detection is complete
     * Terminal detection is handled by Lush's display system,
     * so we just verify it has been initialized */
    integ->init_state.terminal_detected = true;

    /* Step 4: Create shell event hub */
    lle_result_t result = lle_shell_event_hub_create(&integ->event_hub);
    if (result != LLE_SUCCESS) {
        lle_arena_destroy(session_arena);
        return result;
    }
    integ->init_state.event_hub_initialized = true;

    /* Step 4.5: Initialize shell hook function bridge (Phase 7)
     * This registers handlers that call user-defined hook functions
     * (precmd, preexec, chpwd) when shell events fire. */
    /* Note: We set g_lle_integration temporarily so hooks can register */
    g_lle_integration = integ;
    lle_shell_hooks_init();
    g_lle_integration = NULL;  /* Will be set permanently at end */

    /* Step 5: Create and configure LLE editor */
    result = create_and_configure_editor(integ);
    if (result != LLE_SUCCESS) {
        lle_shell_event_hub_destroy(integ->event_hub);
        lle_arena_destroy(session_arena);
        return result;
    }
    integ->init_state.editor_initialized = true;

    /* Step 6: Initialize history (already done in create_and_configure_editor)
     */
    integ->init_state.history_initialized = true;

    /* Step 7: Create and configure prompt composer (Spec 25) */
    result = create_and_configure_prompt_composer(integ);
    if (result != LLE_SUCCESS) {
        /* Prompt composer is optional - log warning but continue */
        /* The shell can still function without the fancy prompt system */
    } else {
        integ->init_state.prompt_initialized = true;
    }

    /* Step 8: Register atexit handler for cleanup */
    if (!atexit_registered) {
        if (atexit(lle_shell_integration_atexit_handler) == 0) {
            atexit_registered = true;
            integ->init_state.atexit_registered = true;
        }
    }

    /* Step 9: Initialize watchdog subsystem for deadlock detection */
    result = lle_watchdog_init();
    if (result != LLE_SUCCESS) {
        /* Watchdog is optional - log warning but continue */
        /* Shell can still function without watchdog protection */
    }

    /* Mark shell hooks as installed */
    integ->init_state.shell_hooks_installed = true;

    /* Set global pointer */
    g_lle_integration = integ;

    return LLE_SUCCESS;
}

/**
 * @brief Shutdown the LLE shell integration subsystem
 *
 * Saves history and destroys all LLE subsystems in reverse dependency
 * order. Cleans up prompt composer, editor, event hub, and watchdog.
 * Safe to call multiple times.
 */
void lle_shell_integration_shutdown(void) {
    if (!g_lle_integration) {
        return;
    }

    lle_shell_integration_t *integ = g_lle_integration;

    /* Save history before shutdown */
    if (integ->editor && integ->editor->history_system) {
        const char *home = getenv("HOME");
        if (home) {
            char history_path[1024];
            snprintf(history_path, sizeof(history_path), "%s/.lush_history",
                     home);
            lle_history_save_to_file(integ->editor->history_system,
                                     history_path);
        }
    }

    /* Cleanup global display integration (created in lle_readline) */
    lle_display_integration_t *display_integ =
        lle_display_integration_get_global();
    if (display_integ) {
        lle_display_integration_cleanup(display_integ);
    }

    /* Destroy prompt composer (unregisters from event hub) */
    destroy_prompt_composer(integ);

    /* Destroy editor */
    destroy_editor(integ);

    /* Cleanup shell hook function bridge (Phase 7) */
    lle_shell_hooks_cleanup();

    /* Destroy event hub */
    if (integ->event_hub) {
        lle_shell_event_hub_destroy(integ->event_hub);
        integ->event_hub = NULL;
    }

    /* Cleanup watchdog subsystem */
    lle_watchdog_cleanup();

    /* Clear global pointer before destroying arena
     * (integ is allocated from session_arena) */
    g_lle_integration = NULL;

    /* Destroy session arena - frees ALL LLE memory including integ itself */
    if (integ->session_arena) {
        lle_arena_destroy(integ->session_arena);
    }
    /* Note: integ is now invalid - do not access after this point */
}

/**
 * @brief Atexit handler for automatic cleanup
 *
 * Uses static flag to ensure shutdown only runs once, preventing
 * double-free issues if shutdown is called from multiple paths.
 */
static void lle_shell_integration_atexit_handler(void) {
    static bool shutdown_complete = false;
    if (shutdown_complete) {
        return;
    }
    shutdown_complete = true;
    lle_shell_integration_shutdown();
}

/**
 * @brief Get the global shell integration instance
 *
 * @return Pointer to the global shell integration instance, or NULL if not initialized
 */
lle_shell_integration_t *lle_get_shell_integration(void) {
    return g_lle_integration;
}

/**
 * @brief Check if LLE is active and ready for use
 *
 * @return true if LLE is initialized and editor is ready, false otherwise
 */
bool lle_is_active(void) {
    return g_lle_integration != NULL &&
           g_lle_integration->init_state.editor_initialized;
}

/* ============================================================================
 * EDITOR MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create and configure the LLE editor instance
 */
static lle_result_t
create_and_configure_editor(lle_shell_integration_t *integ) {
    if (!integ) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Create editor */
    lle_result_t result = lle_editor_create(&integ->editor, global_memory_pool);
    if (result != LLE_SUCCESS || !integ->editor) {
        return result != LLE_SUCCESS ? result : LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize history subsystem */
    lle_history_config_t hist_config;
    populate_history_config(&hist_config);

    result = lle_history_core_create(&integ->editor->history_system,
                                     integ->editor->lle_pool, &hist_config);

    if (result == LLE_SUCCESS && integ->editor->history_system) {
        /* Load existing history from file */
        const char *home = getenv("HOME");
        if (home) {
            char history_path[1024];
            snprintf(history_path, sizeof(history_path), "%s/.lush_history",
                     home);
            lle_history_load_from_file(integ->editor->history_system,
                                       history_path);
        }

        /* Initialize the history bridge for builtin commands
         * This connects the LLE history core to the shell's history builtin */
        lle_result_t bridge_result =
            lle_history_bridge_init(integ->editor->history_system,
                                    NULL, /* No POSIX manager - LLE-only now */
                                    integ->editor->lle_pool);
        if (bridge_result != LLE_SUCCESS) {
            /* Non-fatal - history builtin won't work but shell continues */
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Destroy the editor instance
 */
static void destroy_editor(lle_shell_integration_t *integ) {
    if (!integ || !integ->editor) {
        return;
    }

    lle_editor_destroy(integ->editor);
    integ->editor = NULL;
    integ->init_state.editor_initialized = false;
    integ->init_state.history_initialized = false;
}

/* ============================================================================
 * PROMPT COMPOSER MANAGEMENT (Spec 25 Integration)
 * ============================================================================
 */

/* Static registries for prompt composer */
static lle_segment_registry_t g_segment_registry;
static lle_theme_registry_t g_theme_registry;
static bool g_registries_initialized = false;

/**
 * @brief Create and configure the prompt composer
 *
 * Initializes the prompt composer and registers it with the shell event hub
 * for automatic cache invalidation on directory changes and command events.
 */
static lle_result_t
create_and_configure_prompt_composer(lle_shell_integration_t *integ) {
    if (!integ || !integ->event_hub) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Initialize registries only if not already initialized
     * This prevents double-init issues during hard reset */
    if (!g_registries_initialized) {
        /* Initialize segment registry and register built-in segments */
        lle_result_t result = lle_segment_registry_init(&g_segment_registry);
        if (result != LLE_SUCCESS) {
            return result;
        }
        lle_segment_register_builtins(&g_segment_registry);

        /* Initialize theme registry and register built-in themes */
        result = lle_theme_registry_init(&g_theme_registry);
        if (result != LLE_SUCCESS) {
            lle_segment_registry_cleanup(&g_segment_registry);
            return result;
        }
        lle_theme_register_builtins(&g_theme_registry);

        /* Load user themes from standard locations (Issue #21)
         * This loads themes from:
         * - $XDG_CONFIG_HOME/lush/themes/ (~/.config/lush/themes/)
         * - /etc/lush/themes/ (system-wide)
         */
        lle_theme_load_user_themes(&g_theme_registry);

        g_registries_initialized = true;
    }

    lle_result_t result;

    /* Set default theme as active */
    lle_theme_registry_set_active(&g_theme_registry, "default");

    /* Allocate prompt composer */
    integ->prompt_composer = calloc(1, sizeof(lle_prompt_composer_t));
    if (!integ->prompt_composer) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize the composer with segment and theme registries */
    result = lle_composer_init(integ->prompt_composer, &g_segment_registry,
                               &g_theme_registry);
    if (result != LLE_SUCCESS) {
        free(integ->prompt_composer);
        integ->prompt_composer = NULL;
        return result;
    }

    /* Sync composer config with global config settings */
    integ->prompt_composer->config.enable_transient =
        config.display_transient_prompt;
    integ->prompt_composer->config.newline_before_prompt =
        config.display_newline_before_prompt;

    /* Register with shell event hub for automatic updates
     * This is the key Spec 25 <-> Spec 26 integration point */
    result = lle_composer_register_shell_events(integ->prompt_composer,
                                                integ->event_hub);
    if (result != LLE_SUCCESS) {
        lle_composer_cleanup(integ->prompt_composer);
        free(integ->prompt_composer);
        integ->prompt_composer = NULL;
        return result;
    }

    /* Spec 28 Phase 2: Write theme's format strings to PS1/PS2.
     * PS1 now holds the format string (with ${segment}, \u, %n escapes),
     * not the rendered output. The prompt render loop will expand it. */
    const lle_theme_t *theme =
        lle_composer_get_theme(integ->prompt_composer);
    if (theme && strlen(theme->layout.ps1_format) > 0) {
        symtable_set_global("PS1", theme->layout.ps1_format);
    } else {
        symtable_set_global("PS1", "$ ");
    }
    if (theme && strlen(theme->layout.ps2_format) > 0) {
        symtable_set_global("PS2", theme->layout.ps2_format);
    } else {
        symtable_set_global("PS2", "> ");
    }
    /* Sync PROMPT = PS1 (zsh alias) */
    char *ps1_val = symtable_get_global("PS1");
    if (ps1_val) {
        symtable_set_global("PROMPT", ps1_val);
        free(ps1_val);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Destroy the prompt composer
 *
 * Unregisters from shell event hub and frees resources.
 */
static void destroy_prompt_composer(lle_shell_integration_t *integ) {
    if (!integ || !integ->prompt_composer) {
        return;
    }

    /* Unregister from event hub first */
    lle_composer_unregister_shell_events(integ->prompt_composer);

    /* Cleanup and free */
    lle_composer_cleanup(integ->prompt_composer);
    free(integ->prompt_composer);
    integ->prompt_composer = NULL;
    integ->init_state.prompt_initialized = false;

    /* Cleanup registries only once - prevents double-free on exit */
    if (g_registries_initialized) {
        lle_theme_registry_cleanup(&g_theme_registry);
        lle_segment_registry_cleanup(&g_segment_registry);
        g_registries_initialized = false;
    }
}

/* ============================================================================
 * RESET FUNCTIONS (THREE-TIER HIERARCHY)
 * ============================================================================
 */

/**
 * @brief Perform a soft reset of the LLE editor
 *
 * Tier 1 reset: Sets abort flag, clears buffer, and resets history
 * navigation. Does not destroy or recreate any subsystems.
 */
void lle_soft_reset(void) {
    if (!g_lle_integration || !g_lle_integration->editor) {
        return;
    }

    lle_editor_t *editor = g_lle_integration->editor;

    /* Set abort flag to signal readline to return */
    editor->abort_requested = true;

    /* Clear buffer if present */
    if (editor->buffer) {
        lle_buffer_clear(editor->buffer);
    }

    /* Reset history navigation */
    editor->history_navigation_pos = 0;
    editor->history_nav_seen_count = 0;
}

/**
 * @brief Perform a hard reset of the LLE editor
 *
 * Tier 2 reset: Saves history, destroys and recreates the editor
 * instance. Resets error counters and recovery mode. More aggressive
 * than soft reset but preserves history.
 */
void lle_hard_reset(void) {
    if (!g_lle_integration) {
        return;
    }

    lle_shell_integration_t *integ = g_lle_integration;

    /* Save history before destroying editor */
    if (integ->editor && integ->editor->history_system) {
        const char *home = getenv("HOME");
        if (home) {
            char history_path[1024];
            snprintf(history_path, sizeof(history_path), "%s/.lush_history",
                     home);
            lle_history_save_to_file(integ->editor->history_system,
                                     history_path);
        }
    }

    /* Destroy current editor */
    destroy_editor(integ);

    /* Recreate editor */
    lle_result_t result = create_and_configure_editor(integ);
    if (result == LLE_SUCCESS) {
        integ->init_state.editor_initialized = true;
        integ->init_state.history_initialized = true;
    }

    /* Reset error counters */
    integ->error_count = 0;
    integ->ctrl_g_count = 0;
    integ->recovery_mode = false;

    /* Update statistics */
    integ->hard_reset_count++;
    integ->last_reset_time_us = get_timestamp_us();
}

/**
 * @brief Perform a nuclear reset of the LLE editor and terminal
 *
 * Tier 3 reset: Performs hard reset plus sends terminal reset sequence
 * (ESC c = RIS). Used for severe terminal corruption recovery.
 */
void lle_nuclear_reset(void) {
    if (!g_lle_integration) {
        return;
    }

    /* Perform hard reset first */
    lle_hard_reset();

    /* Send terminal reset sequence */
    /* ESC c = RIS (Reset to Initial State) */
    write(STDOUT_FILENO, "\033c", 2);

    /* Give terminal time to process reset */
    usleep(50000); /* 50ms */

    /* Update statistics */
    g_lle_integration->nuclear_reset_count++;
}

/* ============================================================================
 * PROMPT GENERATION
 * ============================================================================
 */

/**
 * @brief Detect terminal color depth for prompt expansion
 *
 * @return 0=none, 1=8-color, 2=256-color, 3=truecolor
 */
static int detect_prompt_color_depth(void) {
    lle_terminal_detection_result_t *detection = NULL;
    if (lle_detect_terminal_capabilities_optimized(&detection) == LLE_SUCCESS
        && detection) {
        if (detection->supports_truecolor)
            return 3;
        if (detection->supports_256_colors)
            return 2;
        if (detection->supports_colors)
            return 1;
        return 0;
    }
    return 3; /* Fallback: assume truecolor */
}

/**
 * @brief Update the shell prompt by expanding the PS1 format string
 *
 * Spec 28 Phase 2: Reads PS1 as a format string (containing bash \u,
 * zsh %n, and/or LLE ${segment} escapes), expands it via the unified
 * prompt expansion engine, and stores the rendered result for display.
 * PS1 in the symtable retains the format string — it is NOT overwritten
 * with rendered output.
 *
 * The rendered prompt is stored in a static buffer and returned by
 * lle_shell_get_rendered_prompt().
 */

/** @brief Static buffer for the last rendered PS1 prompt */
static char s_rendered_ps1[LLE_PROMPT_OUTPUT_MAX];

/** @brief Get the most recently rendered PS1 prompt */
const char *lle_shell_get_rendered_prompt(void) {
    return s_rendered_ps1;
}

void lle_shell_update_prompt(void) {
    /* Use minimal fallback if LLE integration not available */
    if (!g_lle_integration || !g_lle_integration->prompt_composer) {
        snprintf(s_rendered_ps1, sizeof(s_rendered_ps1), "%s",
                 (getuid() > 0) ? "$ " : "# ");
        return;
    }

    lle_prompt_composer_t *composer = g_lle_integration->prompt_composer;

    /* Update background job count from executor */
    executor_t *executor = get_global_executor();
    if (executor) {
        executor_update_job_status(executor);
        int job_count = executor_count_jobs(executor);
        lle_prompt_context_set_job_count(&composer->context, job_count);
    }

    /* Read PS1 format string from symtable */
    char *ps1_fmt = symtable_get_global("PS1");
    if (!ps1_fmt) {
        ps1_fmt = strdup((getuid() > 0) ? "$ " : "# ");
        if (!ps1_fmt) {
            snprintf(s_rendered_ps1, sizeof(s_rendered_ps1), "$ ");
            return;
        }
    }

    /* Validate UTF-8 encoding — malformed PS1 would produce corrupted
     * terminal output.  Replace with safe fallback. */
    if (!lle_utf8_is_valid(ps1_fmt, strlen(ps1_fmt))) {
        free(ps1_fmt);
        ps1_fmt = strdup((getuid() > 0) ? "$ " : "# ");
        if (!ps1_fmt) {
            snprintf(s_rendered_ps1, sizeof(s_rendered_ps1), "$ ");
            return;
        }
    }

    /* Build the expansion context with template engine callbacks */
    lle_template_render_ctx_t render_ctx =
        lle_composer_create_render_ctx(composer);
    lle_prompt_expand_ctx_t expand_ctx;
    memset(&expand_ctx, 0, sizeof(expand_ctx));
    expand_ctx.template_ctx = &render_ctx;
    expand_ctx.last_exit_status = composer->context.last_exit_code;
    expand_ctx.job_count = composer->context.background_job_count;
    expand_ctx.color_depth = detect_prompt_color_depth();

    /* Prepend newline if configured */
    size_t offset = 0;
    if (composer->config.newline_before_prompt) {
        s_rendered_ps1[0] = '\n';
        offset = 1;
    }

    /* Expand PS1 format → rendered output */
    lle_result_t result = lle_prompt_expand(
        ps1_fmt, s_rendered_ps1 + offset,
        sizeof(s_rendered_ps1) - offset, &expand_ctx);

    if (result != LLE_SUCCESS) {
        /* Write fallback after the newline prefix (if any) */
        snprintf(s_rendered_ps1 + offset, sizeof(s_rendered_ps1) - offset,
                 "%s", (getuid() > 0) ? "$ " : "# ");
    }

    lle_composer_clear_regeneration_flag(composer);
    free(ps1_fmt);
}

/**
 * @brief Notify that PS1, PS2, or PROMPT was set by user code
 *
 * Marks the variable as user-owned so the theme system respects it.
 * Syncs PROMPT ↔ PS1 bidirectionally.
 */
void lle_shell_notify_prompt_var_set(const char *var_name, const char *value) {
    if (!var_name)
        return;

    lle_prompt_composer_t *composer =
        g_lle_integration ? g_lle_integration->prompt_composer : NULL;

    if (strcmp(var_name, "PS1") == 0) {
        lle_prompt_notify_ps1_changed(composer);
        /* Sync PROMPT = PS1 */
        if (value)
            symtable_set_global("PROMPT", value);
    } else if (strcmp(var_name, "PROMPT") == 0) {
        lle_prompt_notify_ps1_changed(composer);
        /* Sync PS1 = PROMPT */
        if (value)
            symtable_set_global("PS1", value);
    } else if (strcmp(var_name, "PS2") == 0) {
        lle_prompt_notify_ps2_changed(composer);
    }
}

/* ============================================================================
 * ERROR TRACKING
 * ============================================================================
 */

/**
 * @brief Record an error occurrence in the integration subsystem
 *
 * Increments error counter and triggers automatic hard reset if
 * the error threshold (LLE_ERROR_THRESHOLD) is reached.
 *
 * @param error The error code that occurred
 */
void lle_record_error(lle_result_t error) {
    if (!g_lle_integration) {
        return;
    }

    (void)error; /* Could log specific error in future */

    g_lle_integration->error_count++;

    /* Check if we've hit the threshold for automatic hard reset */
    if (g_lle_integration->error_count >= LLE_ERROR_THRESHOLD) {
        g_lle_integration->recovery_mode = true;
        g_lle_integration->recovery_count++;
        lle_hard_reset();
    }
}

/**
 * @brief Reset the error counter and exit recovery mode
 *
 * Clears the accumulated error count and disables recovery mode.
 * Called after successful operations to reset error tracking.
 */
void lle_reset_error_counter(void) {
    if (!g_lle_integration) {
        return;
    }

    g_lle_integration->error_count = 0;
    g_lle_integration->recovery_mode = false;
}

/**
 * @brief Record a Ctrl+G (abort) keypress for panic detection
 *
 * Tracks Ctrl+G presses within a time window. If three Ctrl+G presses
 * occur within LLE_CTRL_G_PANIC_WINDOW_US microseconds, triggers an
 * automatic hard reset (panic recovery mechanism).
 */
void lle_record_ctrl_g(void) {
    if (!g_lle_integration) {
        return;
    }

    uint64_t now = get_timestamp_us();

    /* Check if this Ctrl+G is within the panic window */
    if (now - g_lle_integration->last_ctrl_g_time_us <
        LLE_CTRL_G_PANIC_WINDOW_US) {
        g_lle_integration->ctrl_g_count++;
    } else {
        /* Reset counter - too much time passed */
        g_lle_integration->ctrl_g_count = 1;
    }

    g_lle_integration->last_ctrl_g_time_us = now;

    /* Check for panic threshold */
    if (g_lle_integration->ctrl_g_count >= LLE_CTRL_G_PANIC_COUNT) {
        /* Triple Ctrl+G detected - trigger hard reset */
        g_lle_integration->ctrl_g_count = 0;
        lle_hard_reset();
    }
}

/**
 * @brief Update LLE editing mode based on shell options
 *
 * Called when user changes editing mode via set -o vi/emacs.
 * Updates the editor's editing_mode field accordingly.
 */
void lush_update_editing_mode(void) {
    if (!g_lle_integration || !g_lle_integration->editor) {
        return;
    }

    lle_editor_t *editor = g_lle_integration->editor;

    if (shell_opts.vi_mode) {
        editor->editing_mode = LLE_EDITING_MODE_VI_INSERT;
    } else {
        /* Default to emacs mode */
        editor->editing_mode = LLE_EDITING_MODE_EMACS;
    }
}

/**
 * @brief Shell-facing readline wrapper with statistics tracking
 *
 * Calls LLE's lle_readline() and tracks readline statistics.
 * Handles NULL prompts by expanding PS1 format string via the
 * unified prompt expansion engine (Spec 28).
 *
 * @param prompt The prompt string to display, or NULL to use PS1
 * @return Newly allocated line from user, or NULL on EOF/error
 */
char *lush_readline_with_prompt(const char *prompt) {
    if (!g_lle_integration || !g_lle_integration->editor) {
        return NULL;
    }

    g_lle_integration->total_readline_calls++;

    /* If prompt is NULL, expand PS1 format string (Spec 28 Phase 2).
     * PS1 now holds the format string with escapes (\u, %n, ${segment}).
     * lle_shell_update_prompt() expands it into a rendered prompt. */
    const char *effective_prompt = prompt;
    if (!effective_prompt) {
        lle_shell_update_prompt();
        effective_prompt = lle_shell_get_rendered_prompt();
        if (!effective_prompt || !*effective_prompt) {
            effective_prompt = "$ ";
        }
    }

    char *line = lle_readline(effective_prompt);

    if (line) {
        g_lle_integration->successful_reads++;
    }

    return line;
}
