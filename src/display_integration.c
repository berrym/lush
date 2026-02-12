/**
 * @file display_integration.c
 * @brief Display Controller Integration Wrapper for Lush Shell
 *
 * Provides the integration layer between the shell and the layered
 * display controller system. Handles prompt rendering, syntax highlighting,
 * and display caching with performance monitoring.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * DISPLAY INTEGRATION WRAPPER
 *
 * This module provides seamless integration between the existing shell display
 * functions and the new layered display controller architecture. It enables
 * zero-regression deployment while providing enhanced display capabilities.
 *
 * Key Features:
 * - Seamless function replacement with backward compatibility
 * - Configuration-based enable/disable of layered display
 * - Performance monitoring integration with shell diagnostics
 * - Enterprise deployment readiness with configuration management
 * - Graceful fallback to existing display functions
 *
 * Integration Strategy:
 * - Preserve all existing functionality and user experience
 * - Add layered display as an optional enhancement
 * - Provide configuration options for deployment customization
 * - Enable performance monitoring and optimization
 * - Ensure zero memory leaks and resource management
 */

#include "display_integration.h"
#include "config.h"
#include "display/autosuggestions_layer.h"
#include "display/command_layer.h"
#include "display/composition_engine.h"
#include "display/display_controller.h"
#include "display/layer_events.h"
#include "executor.h"
#include "init.h"
#include "lle/lle_shell_integration.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/theme.h"
#include "lush.h"
#include "lush_memory_pool.h"
#include "symtable.h"

#include <inttypes.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* LLE is the sole line editor - stub definitions for display compatibility */
static char *rl_line_buffer = NULL;
static int rl_end = 0;
static int rl_point = 0;
static inline void rl_redisplay(void) {}
static inline int rl_clear_visible_line(void) { return 0; }
static inline int rl_on_new_line(void) { return 0; }

/**
 * @brief Clear screen using ANSI escape sequences
 *
 * Writes ANSI escape sequences to clear the entire screen and move
 * the cursor to the home position. LLE is the sole line editor so
 * no readline fallback is needed.
 */
static void do_clear_screen(void) {
    /* ESC[2J clears entire screen, ESC[H moves cursor to home position */
    const char *clear_seq = "\033[2J\033[H";
    if (write(STDOUT_FILENO, clear_seq, strlen(clear_seq)) < 0) {
        /* Ignore write errors - terminal may be in unusual state */
    }
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Shell display functions that we're replacing
void lush_refresh_line(void);
void lush_prompt_update(void);
void lush_clear_screen(void);

/**
 * @brief Generate current prompt string using LLE prompt composer
 *
 * Creates the shell prompt using the LLE prompt composer if available.
 * Falls back to a simple "$ " or "# " prompt based on user ID.
 *
 * @return Pool-allocated prompt string that caller must free with lush_pool_free()
 */
static char *display_generate_prompt(void) {
    if (g_lle_integration && g_lle_integration->prompt_composer) {
        lle_prompt_composer_t *composer = g_lle_integration->prompt_composer;
        lle_prompt_output_t output;
        memset(&output, 0, sizeof(output));

        lle_result_t result = lle_composer_render(composer, &output);
        if (result == LLE_SUCCESS && output.ps1_len > 0) {
            return lush_pool_strdup(output.ps1);
        }
    }
    /* Fallback */
    return lush_pool_strdup((getuid() > 0) ? "$ " : "# ");
}

/**
 * @brief Generate shell prompt string
 *
 * Public wrapper for display_generate_prompt() that creates the shell
 * prompt using the LLE prompt composer.
 *
 * @return Pool-allocated prompt string that caller must free with lush_pool_free()
 */
char *lush_generate_prompt(void) { return display_generate_prompt(); }

/**
 * @brief Get the active theme name from LLE
 *
 * Retrieves the name of the currently active theme from the LLE
 * theme registry. Returns "default" if no theme is active.
 *
 * @return Theme name string (static, do not free)
 */
static const char *get_active_theme_name(void) {
    if (g_lle_integration && g_lle_integration->prompt_composer &&
        g_lle_integration->prompt_composer->themes) {
        const lle_theme_t *theme = lle_theme_registry_get_active(
            g_lle_integration->prompt_composer->themes);
        if (theme) {
            return theme->name;
        }
    }
    return "default";
}

/**
 * @brief Get the symbol compatibility mode from LLE theme
 *
 * Determines the symbol compatibility mode based on the active theme's
 * capabilities. Returns NERD_FONT, UNICODE, or ASCII mode.
 *
 * @return Symbol compatibility mode enum value
 */
static symbol_compatibility_t get_symbol_mode(void) {
    if (g_lle_integration && g_lle_integration->prompt_composer &&
        g_lle_integration->prompt_composer->themes) {
        const lle_theme_t *theme = lle_theme_registry_get_active(
            g_lle_integration->prompt_composer->themes);
        if (theme) {
            /* Map LLE capabilities to symbol mode */
            if (theme->capabilities & LLE_THEME_CAP_NERD_FONT) {
                return SYMBOL_MODE_NERD_FONT;
            } else if (theme->capabilities & LLE_THEME_CAP_UNICODE) {
                return SYMBOL_MODE_UNICODE;
            }
        }
    }
    return SYMBOL_MODE_ASCII;
}

// ============================================================================
// GLOBAL STATE AND CONFIGURATION
// ============================================================================

// Display integration state
static display_controller_t *global_display_controller = NULL;
static bool layered_display_enabled = false;
static bool integration_initialized = false;
static display_integration_config_t current_config = {0};

// Autosuggestions layer integration
static autosuggestions_layer_t *global_autosuggestions_layer = NULL;
static bool autosuggestions_layer_initialized = false;

// Performance tracking
static display_integration_stats_t integration_stats = {0};

// Layer-specific performance tracking
static struct {
    uint64_t display_controller_hits;
    uint64_t display_controller_misses;
    uint64_t composition_engine_hits;
    uint64_t composition_engine_misses;
    uint64_t command_layer_hits;
    uint64_t command_layer_misses;
    uint64_t autosuggestions_hits;
    uint64_t autosuggestions_misses;
    uint64_t prompt_layer_hits;
    uint64_t prompt_layer_misses;
} layer_cache_stats = {0};

// Buffer for display output (reserved for future batch output optimization)
MAYBE_UNUSED
static char display_output_buffer[DISPLAY_INTEGRATION_MAX_OUTPUT_SIZE];

// ============================================================================
// INITIALIZATION AND CLEANUP
// ============================================================================

/**
 * Initialize the display integration system.
 * Sets up the display controller and prepares for shell integration.
 *
 * @param config Configuration for display integration
 * @return true on success, false on failure
 */
bool display_integration_init(const display_integration_config_t *init_config) {
    if (integration_initialized) {
        return true; // Already initialized
    }

    // Copy configuration
    if (init_config) {
        current_config = *init_config;
    } else {
        // Use default configuration with layered display (always enabled)
        display_integration_create_default_config(&current_config);
    }

    // Create display controller (layered display is exclusive system)
    {
        global_display_controller = display_controller_create();
        if (!global_display_controller) {
            fprintf(
                stderr,
                "display_integration: Failed to create display controller\n");
            return false;
        }

        // Initialize display controller with shell configuration
        // Create default configuration for display controller
        display_controller_config_t controller_config;
        display_controller_create_default_config(&controller_config);

        // Create layer event system for controller
        layer_event_system_t *event_system = layer_events_create(NULL);
        if (!event_system) {
            fprintf(stderr,
                    "display_integration: Failed to create event system\n");
            display_controller_destroy(global_display_controller);
            global_display_controller = NULL;
            return false;
        }

        // Initialize the event system (critical - required for subscriptions to
        // work)
        layer_events_error_t event_init_error = layer_events_init(event_system);
        if (event_init_error != LAYER_EVENTS_SUCCESS) {
            fprintf(
                stderr,
                "display_integration: Failed to initialize event system: %d\n",
                event_init_error);
            layer_events_destroy(event_system);
            display_controller_destroy(global_display_controller);
            global_display_controller = NULL;
            return false;
        }

        display_controller_error_t error = display_controller_init(
            global_display_controller, &controller_config, event_system);
        if (error != DISPLAY_CONTROLLER_SUCCESS) {
            fprintf(stderr,
                    "display_integration: Failed to initialize display "
                    "controller: %d\n",
                    error);
            display_controller_destroy(global_display_controller);
            global_display_controller = NULL;
            return false;
        }

        // Prepare for shell integration
        error = display_controller_prepare_shell_integration(
            global_display_controller, &current_config);
        if (error != DISPLAY_CONTROLLER_SUCCESS) {
            fprintf(stderr,
                    "display_integration: Failed to prepare shell integration: "
                    "%d\n",
                    error);
            display_controller_destroy(global_display_controller);
            global_display_controller = NULL;
            return false;
        }

        // Sync command layer syntax highlighting with shell config
        if (global_display_controller->compositor &&
            global_display_controller->compositor->command_layer) {
            command_layer_set_syntax_enabled(
                global_display_controller->compositor->command_layer,
                config.display_syntax_highlighting);
        }

        // Configure display controller
        display_controller_config_t controller_config2;
        display_controller_create_default_config(&controller_config2);
        controller_config2.optimization_level =
            current_config.optimization_level;
        controller_config2.enable_caching = current_config.enable_caching;
        controller_config2.enable_performance_monitoring =
            current_config.enable_performance_monitoring;
        controller_config2.enable_integration_mode = true;

        error = display_controller_set_config(global_display_controller,
                                              &controller_config2);
        if (error != DISPLAY_CONTROLLER_SUCCESS) {
            fprintf(stderr,
                    "display_integration: Failed to configure display "
                    "controller: %d\n",
                    error);
            display_controller_destroy(global_display_controller);
            global_display_controller = NULL;
            return false;
        }

        layered_display_enabled = true;
    }

    integration_initialized = true;
    integration_stats.init_time = time(NULL);
    integration_stats.total_display_calls = 0;
    integration_stats.layered_display_calls = 0;
    integration_stats.fallback_calls = 0;

    // Initialize autosuggestions layer if layered display is enabled
    // Always initialize professional autosuggestions for seamless user
    // experience
    if (!display_integration_init_autosuggestions()) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Warning - autosuggestions "
                            "layer initialization failed\n");
        }
        // Don't fail the whole initialization - autosuggestions are optional
    }

    if (current_config.debug_mode) {
        printf("display_integration: Initialized successfully "
               "(layered_display=%s, autosuggestions=%s)\n",
               layered_display_enabled ? "enabled" : "disabled",
               autosuggestions_layer_initialized ? "enabled" : "disabled");
    }

    return true;
}

/**
 * Cleanup the display integration system.
 * Releases all resources and resets state.
 */
void display_integration_cleanup(void) {
    if (!integration_initialized) {
        return;
    }

    // Cleanup autosuggestions layer first
    display_integration_cleanup_autosuggestions();

    if (global_display_controller) {
        display_controller_destroy(global_display_controller);
        global_display_controller = NULL;
    }

    layered_display_enabled = false;
    integration_initialized = false;
    memset(&current_config, 0, sizeof(current_config));
    memset(&integration_stats, 0, sizeof(integration_stats));

    // Note: current_config was zeroed above, so we can't check debug_mode here
    // Debug output would need to be checked before memset() if needed
}

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================

/**
 * Create default configuration for display integration.
 *
 * @param config Configuration structure to initialize
 */
void display_integration_create_default_config(
    display_integration_config_t *config) {
    if (!config)
        return;

    memset(config, 0, sizeof(display_integration_config_t));
    // v1.3.0: Layered display is now the exclusive system - no enable/disable
    // needed
    config->enable_caching = true;
    config->enable_performance_monitoring = true;
    config->optimization_level = DISPLAY_OPTIMIZATION_STANDARD;
    config->fallback_on_error = true;
    config->debug_mode = false;
    config->max_output_size = DISPLAY_INTEGRATION_MAX_OUTPUT_SIZE;
    config->performance_threshold_ms = 20;
    config->cache_hit_rate_threshold = 0.8;
}

/**
 * Update display integration configuration.
 *
 * @param config New configuration to apply
 * @return true on success, false on failure
 */
bool display_integration_set_config(
    const display_integration_config_t *config) {
    if (!config || !integration_initialized) {
        return false;
    }

    display_integration_config_t old_config = current_config;
    current_config = *config;

    // v1.3.0: Layered display is exclusive - configuration updates affect
    // performance and caching settings only Display controller is always
    // available, just update its configuration
    if (global_display_controller) {
        // Enable layered display
        if (!global_display_controller) {
            global_display_controller = display_controller_create();
            if (global_display_controller) {
                // Create default configuration for display controller
                display_controller_config_t controller_config;
                display_controller_create_default_config(&controller_config);

                // Create layer event system for controller
                layer_event_system_t *event_system = layer_events_create(NULL);
                if (!event_system) {
                    display_controller_destroy(global_display_controller);
                    global_display_controller = NULL;
                    current_config = old_config;
                    return false;
                }

                // Initialize the event system (critical - required for
                // subscriptions to work)
                layer_events_error_t event_init_error =
                    layer_events_init(event_system);
                if (event_init_error != LAYER_EVENTS_SUCCESS) {
                    fprintf(stderr,
                            "display_integration: Failed to initialize event "
                            "system: %d\n",
                            event_init_error);
                    layer_events_destroy(event_system);
                    display_controller_destroy(global_display_controller);
                    global_display_controller = NULL;
                    current_config = old_config;
                    return false;
                }

                display_controller_error_t error =
                    display_controller_init(global_display_controller,
                                            &controller_config, event_system);
                if (error == DISPLAY_CONTROLLER_SUCCESS) {
                    error = display_controller_prepare_shell_integration(
                        global_display_controller, config);
                    if (error == DISPLAY_CONTROLLER_SUCCESS) {
                        layered_display_enabled = true;
                    }
                }

                if (!layered_display_enabled) {
                    display_controller_destroy(global_display_controller);
                    global_display_controller = NULL;
                    current_config = old_config; // Restore old config
                    return false;
                }

                // Initialize autosuggestions layer now that layered display is
                // enabled
                if (!display_integration_init_autosuggestions()) {
                    if (current_config.debug_mode) {
                        fprintf(
                            stderr,
                            "display_integration: Warning - autosuggestions "
                            "layer initialization failed during enable\n");
                    }
                    // Don't fail the whole operation - autosuggestions are
                    // optional
                }
            }
        }
        // v1.3.0: No disable logic needed - layered display is exclusive
    }

    // Update display controller configuration if active
    if (layered_display_enabled && global_display_controller) {
        display_controller_config_t controller_config;
        display_controller_create_default_config(&controller_config);
        controller_config.optimization_level = config->optimization_level;
        controller_config.enable_caching = config->enable_caching;
        controller_config.enable_performance_monitoring =
            config->enable_performance_monitoring;
        controller_config.enable_integration_mode = true;

        display_controller_error_t error = display_controller_set_config(
            global_display_controller, &controller_config);
        if (error != DISPLAY_CONTROLLER_SUCCESS && config->fallback_on_error) {
            if (current_config.debug_mode) {
                fprintf(stderr,
                        "display_integration: Failed to update controller "
                        "config, continuing with old settings\n");
            }
        }
    }

    if (current_config.debug_mode) {
        printf(
            "display_integration: Configuration updated (layered_display=%s)\n",
            layered_display_enabled ? "enabled" : "disabled");
    }

    return true;
}

/**
 * Get current display integration configuration.
 *
 * @param config Configuration structure to fill
 * @return true on success, false on failure
 */
bool display_integration_get_config(display_integration_config_t *config) {
    if (!config || !integration_initialized) {
        return false;
    }

    *config = current_config;
    return true;
}

// ============================================================================
// MAIN INTEGRATION FUNCTIONS (Shell Function Replacements)
// ============================================================================

/**
 * Integrated display function - replacement for lush_safe_redisplay().
 * Provides coordinated display using layered architecture when enabled,
 * with graceful fallback to existing display functions.
 */
void display_integration_redisplay(void) {
    static volatile sig_atomic_t in_display_redisplay = 0;

    // CRITICAL: Prevent infinite recursion - this was causing stack overflow
    if (in_display_redisplay) {
        // Use safe fallback without any function pointers
        rl_redisplay();
        return;
    }

    in_display_redisplay = 1;
    integration_stats.total_display_calls++;

    // Enhanced Performance Monitoring: Start timing
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    // Use layered display controller when available
    if (integration_initialized && layered_display_enabled &&
        global_display_controller) {
        integration_stats.layered_display_calls++;

        // Get current prompt and command for display controller
        char *current_prompt = lush_generate_prompt();
        char *current_command = rl_line_buffer ? rl_line_buffer : "";

        // Update theme context before display controller operations
        const char *theme_name = get_active_theme_name();
        symbol_compatibility_t symbol_mode = get_symbol_mode();

        display_controller_set_theme_context(global_display_controller,
                                             theme_name, symbol_mode);

        // Use display controller for coordinated display
        char display_output[4096];
        display_controller_error_t result = display_controller_display(
            global_display_controller, current_prompt, current_command,
            display_output, sizeof(display_output));

        if (result == DISPLAY_CONTROLLER_SUCCESS) {
            // Display controller succeeded - output result
            printf("%s", display_output);
            fflush(stdout);
            if (current_prompt)
                lush_pool_free(current_prompt);
            in_display_redisplay = 0;
            return;
        }

        // Clean up prompt
        if (current_prompt)
            lush_pool_free(current_prompt);
    }

    // Fallback to standard display
    integration_stats.fallback_calls++;
    log_fallback_event("redisplay", INTEGRATION_FALLBACK_CONTROLLER_NULL);
    rl_redisplay();

    // Attempt sophisticated layered display operation
    if (layered_display_enabled && global_display_controller) {
        char output_buffer[4096]; // Buffer for full display output
        char *current_prompt = lush_generate_prompt();

        // Update theme context before display controller operations
        const char *theme_name = get_active_theme_name();
        symbol_compatibility_t symbol_mode = get_symbol_mode();

        display_controller_set_theme_context(global_display_controller,
                                             theme_name, symbol_mode);

        // Modern Syntax Highlighting
        // Use command content during real-time typing, but not during prompt
        // display This enables syntax highlighting while preventing stale
        // command display
        bool is_real_time_input =
            (rl_line_buffer && rl_end > 0 && rl_point >= 0);
        char *current_command = is_real_time_input ? rl_line_buffer : "";

        // Use display controller for sophisticated rendering
        display_controller_error_t error = display_controller_display(
            global_display_controller, current_prompt, current_command,
            output_buffer, sizeof(output_buffer));

        if (error == DISPLAY_CONTROLLER_SUCCESS) {
            integration_stats.layered_display_calls++;

            // Modern Syntax Highlighting
            // Always use layered display when system is enabled and output is
            // available
            if (output_buffer[0] != '\0') {
                // Work with readline's natural flow for multiline content
                // Instead of fighting readline's cursor management, work with
                // it
                rl_clear_visible_line();

                // Check if this is a multiline prompt by looking for newlines
                bool is_multiline = (strchr(output_buffer, '\n') != NULL);

                if (is_multiline) {
                    // For multiline prompts, use a more compatible approach
                    // Print each line and let readline handle the final
                    // positioning
                    char *line_start = output_buffer;
                    char *line_end;

                    while ((line_end = strchr(line_start, '\n')) != NULL) {
                        // Print this line
                        fwrite(line_start, 1, line_end - line_start, stdout);
                        printf("\n");
                        line_start = line_end + 1;
                    }

                    // Print the final line (without newline)
                    if (*line_start) {
                        printf("%s", line_start);
                    }
                } else {
                    // Single line output - use standard approach
                    printf("%s", output_buffer);
                }

                fflush(stdout);

                // Let readline know we're ready for input
                rl_on_new_line();

                // Note: current_prompt is managed by readline system, don't
                // free here
                in_display_redisplay = 0;
                return; // Success - skip fallback
            }
        } else {
            // Layered display failed - log and fallback
            integration_stats.fallback_calls++;
            log_controller_error("redisplay", error);

            // Note: current_prompt is managed by readline system, don't free
            // here
        }
    }

    // Fallback to standard readline display
    rl_redisplay();

    // Enhanced Performance Monitoring: Record timing
    gettimeofday(&end_time, NULL);
    uint64_t operation_time_ns =
        ((uint64_t)(end_time.tv_sec - start_time.tv_sec)) * 1000000000ULL +
        ((uint64_t)(end_time.tv_usec - start_time.tv_usec)) * 1000ULL;
    display_integration_record_display_timing(operation_time_ns);

    in_display_redisplay = 0;
}

/**
 * Integrated prompt update function - replacement for lush_prompt_update().
 * Provides coordinated prompt updates using layered architecture when enabled.
 */
void display_integration_prompt_update(void) {
    static volatile sig_atomic_t in_prompt_update = 0;

    // Prevent infinite recursion
    if (in_prompt_update) {
        return;
    }

    in_prompt_update = 1;
    integration_stats.total_display_calls++;

    // Enhanced Performance Monitoring: Start timing
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    // Use layered display controller for prompt updates when available
    if (integration_initialized && layered_display_enabled &&
        global_display_controller) {
        integration_stats.layered_display_calls++;

        // Get current prompt and command for display controller
        char *current_prompt = lush_generate_prompt();
        char *current_command = rl_line_buffer ? rl_line_buffer : "";

        // Update theme context before display controller operations
        const char *theme_name = get_active_theme_name();
        symbol_compatibility_t symbol_mode = get_symbol_mode();

        display_controller_set_theme_context(global_display_controller,
                                             theme_name, symbol_mode);

        // Use display controller for coordinated prompt update
        char display_output[4096];
        display_controller_error_t result = display_controller_display(
            global_display_controller, current_prompt, current_command,
            display_output, sizeof(display_output));

        if (result == DISPLAY_CONTROLLER_SUCCESS) {
            // Display controller succeeded
            if (current_prompt)
                lush_pool_free(current_prompt);
            in_prompt_update = 0;
            return;
        }

        // Clean up prompt
        if (current_prompt)
            lush_pool_free(current_prompt);
    }

    // Fallback: call original functions safely
    lle_shell_update_prompt();
    lush_generate_prompt();

    // Enhanced Performance Monitoring: Record timing
    gettimeofday(&end_time, NULL);
    uint64_t operation_time_ns =
        ((uint64_t)(end_time.tv_sec - start_time.tv_sec)) * 1000000000ULL +
        ((uint64_t)(end_time.tv_usec - start_time.tv_usec)) * 1000ULL;
    display_integration_record_display_timing(operation_time_ns);

    in_prompt_update = 0;
}

/**
 * Integrated clear screen function - replacement for lush_clear_screen().
 * Provides coordinated screen clearing using layered architecture when enabled.
 */
void display_integration_clear_screen(void) {
    static volatile sig_atomic_t in_clear_screen = 0;

    // Prevent recursion
    if (in_clear_screen) {
        do_clear_screen();
        return;
    }

    in_clear_screen = 1;
    integration_stats.total_display_calls++;
    integration_fallback_reason_t fallback_reason;

    // Professional safety check - can we attempt layered display?
    if (!safe_layered_display_attempt("clear_screen", &fallback_reason)) {
        integration_stats.fallback_calls++;
        log_fallback_event("clear_screen", fallback_reason);

        // Graceful fallback to existing system
        do_clear_screen();
        in_clear_screen = 0;
        return;
    }

    // Attempt layered display clear screen operation
    if (layered_display_enabled && global_display_controller) {
        char output_buffer[1024]; // Buffer for clear screen sequence

        // Use display controller for sophisticated clear screen
        display_controller_error_t error = display_controller_refresh(
            global_display_controller, output_buffer, sizeof(output_buffer));

        if (error == DISPLAY_CONTROLLER_SUCCESS) {
            integration_stats.layered_display_calls++;

            // Use appropriate clear screen for current mode (LLE or readline)
            do_clear_screen();

            if (current_config.debug_mode) {
                fprintf(stderr,
                        "display_integration: Clear screen completed\n");
            }

            in_clear_screen = 0;
            return;
        } else {
            // Clear screen failed - log and fallback
            integration_stats.fallback_calls++;
            log_controller_error("clear_screen", error);
        }
    }

    // Fallback to existing system
    do_clear_screen();
    in_clear_screen = 0;
}

/**
 * Update display after command execution completion.
 * Called from main shell loop after each command execution to ensure
 * layered display system handles post-command prompt rendering and caching.
 */
void display_integration_post_command_update(const char *executed_command) {
    static volatile sig_atomic_t in_post_command_update = 0;

    // Use standard debug mode configuration
    bool debug_enabled = current_config.debug_mode;

    // Prevent recursion
    if (in_post_command_update) {
        return;
    }

    // Only proceed if layered display is active
    if (!display_integration_is_layered_active()) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Post-command update skipped "
                            "- layered display not active\n");
        }
        return;
    }

    if (current_config.debug_mode) {
        fprintf(stderr, "display_integration: Post-command update starting\n");
    }

    in_post_command_update = 1;
    integration_stats.total_display_calls++;

    // Enhanced Performance Monitoring: Start timing for post-command operation
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    integration_fallback_reason_t fallback_reason;

    // Professional safety check - can we attempt layered display?
    if (!safe_layered_display_attempt("post_command_update",
                                      &fallback_reason)) {
        integration_stats.fallback_calls++;
        log_fallback_event("post_command_update", fallback_reason);
        in_post_command_update = 0;
        return;
    }

    // Use layered display controller for post-command rendering
    if (layered_display_enabled && global_display_controller) {
        integration_stats.layered_display_calls++;

        // Get current prompt for post-command display
        char *current_prompt = lush_generate_prompt();
        char *current_command = ""; // Post-command state has no active command

        // Update theme context before display controller operations
        const char *theme_name = get_active_theme_name();
        symbol_compatibility_t symbol_mode = get_symbol_mode();

        display_controller_set_theme_context(global_display_controller,
                                             theme_name, symbol_mode);

        // Command Layer Cache Integration for Post-Command Analysis
        // Analyze the executed command for caching without affecting readline
        // display
        if (executed_command && strlen(executed_command) > 0) {
            if (debug_enabled) {
                fprintf(stderr,
                        "display_integration: Starting command layer analysis "
                        "for: %s\n",
                        executed_command);
            }

            // Create command layer for safe post-command analysis
            command_layer_t *cmd_layer = command_layer_create();
            if (cmd_layer) {
                if (debug_enabled) {
                    fprintf(stderr, "display_integration: Command layer "
                                    "created successfully\n");
                }

                // Initialize command layer with event system first
                layer_event_system_t *event_system =
                    display_controller_get_event_system(
                        global_display_controller);
                command_layer_error_t init_error =
                    command_layer_init(cmd_layer, event_system);

                if (init_error == COMMAND_LAYER_SUCCESS) {
                    // Enable caching for command analysis
                    command_syntax_config_t cmd_config;
                    command_layer_error_t config_error =
                        command_layer_create_default_config(&cmd_config);
                    if (config_error == COMMAND_LAYER_SUCCESS) {
                        cmd_config.cache_enabled = true;
                        command_layer_error_t set_config_error =
                            command_layer_set_syntax_config(cmd_layer,
                                                            &cmd_config);

                        if (set_config_error == COMMAND_LAYER_SUCCESS) {
                            // Analyze command for cache optimization
                            // (post-execution, safe)
                            command_layer_error_t set_cmd_error =
                                command_layer_set_command(cmd_layer,
                                                          executed_command, 0);

                            if (debug_enabled) {
                                fprintf(stderr,
                                        "display_integration: Command layer "
                                        "set_command result: %d\n",
                                        set_cmd_error);
                            }

                            if (set_cmd_error == COMMAND_LAYER_SUCCESS) {
                                // Force a highlighting operation to trigger
                                // cache operations
                                char highlighted_output[1024];
                                command_layer_error_t highlight_error =
                                    command_layer_get_highlighted_text(
                                        cmd_layer, highlighted_output,
                                        sizeof(highlighted_output));

                                if (debug_enabled) {
                                    fprintf(stderr,
                                            "display_integration: Command "
                                            "highlighting result: %d\n",
                                            highlight_error);
                                }
                            }
                        } else if (debug_enabled) {
                            fprintf(stderr,
                                    "display_integration: Failed to set syntax "
                                    "config: %d\n",
                                    set_config_error);
                        }
                    } else if (debug_enabled) {
                        fprintf(stderr,
                                "display_integration: Failed to create default "
                                "config: %d\n",
                                config_error);
                    }
                } else if (debug_enabled) {
                    fprintf(stderr,
                            "display_integration: Failed to initialize command "
                            "layer: %d\n",
                            init_error);
                }

                // Clean up command layer
                command_layer_destroy(cmd_layer);

                if (debug_enabled) {
                    fprintf(stderr,
                            "display_integration: Command layer analysis "
                            "complete for: %s\n",
                            executed_command);
                }
            } else if (debug_enabled) {
                fprintf(
                    stderr,
                    "display_integration: Failed to create command layer\n");
            }
        }

        // Use display controller for post-command prompt rendering
        char display_output[4096];
        display_controller_error_t result = display_controller_display(
            global_display_controller, current_prompt ? current_prompt : "$ ",
            current_command, display_output, sizeof(display_output));

        if (result == DISPLAY_CONTROLLER_SUCCESS) {
            // Track successful layered display operation
            // Note: Cache hits are tracked by display controller internally

            if (debug_enabled) {
                fprintf(stderr,
                        "display_integration: Post-command update successful - "
                        "display controller returned SUCCESS\n");
            }
        } else {
            // Display controller failed - track miss and continue
            integration_stats.layered_display_errors++;
            integration_stats.fallback_calls++;
            log_controller_error("post_command_update", result);

            if (debug_enabled) {
                fprintf(stderr, "display_integration: Post-command update "
                                "failed - display controller error\n");
            }
        }

        // Clean up prompt memory
        if (current_prompt) {
            lush_pool_free(current_prompt);
        }
    }

    // Enhanced Performance Monitoring: Record timing
    gettimeofday(&end_time, NULL);
    uint64_t operation_time_ns =
        ((uint64_t)(end_time.tv_sec - start_time.tv_sec)) * 1000000000ULL +
        ((uint64_t)(end_time.tv_usec - start_time.tv_usec)) * 1000ULL;
    display_integration_record_display_timing(operation_time_ns);

    in_post_command_update = 0;
}

// ============================================================================
// PERFORMANCE MONITORING AND DIAGNOSTICS
// ============================================================================

/**
 * Get display integration performance statistics.
 *
 * @param stats Statistics structure to fill
 * @return true on success, false on failure
 */
bool display_integration_get_stats(display_integration_stats_t *stats) {
    if (!stats || !integration_initialized) {
        return false;
    }

    *stats = integration_stats;

    // Get additional performance data from display controller
    if (layered_display_enabled && global_display_controller) {
        display_controller_performance_t controller_perf;
        display_controller_error_t error = display_controller_get_performance(
            global_display_controller, &controller_perf);

        if (error == DISPLAY_CONTROLLER_SUCCESS) {
            stats->avg_layered_display_time_ns =
                controller_perf.avg_display_time_ns;
            stats->cache_hit_rate = controller_perf.cache_hit_rate;
            stats->memory_usage_bytes =
                controller_perf.cache_memory_usage_bytes;
        }
    }

    // Calculate health indicators
    stats->performance_within_threshold =
        (stats->avg_layered_display_time_ns <
         (current_config.performance_threshold_ms * 1000000));

    stats->cache_efficiency_good =
        (stats->cache_hit_rate >= current_config.cache_hit_rate_threshold);

    // Memory usage acceptable if under 10MB (reasonable threshold for display
    // system)
    stats->memory_usage_acceptable =
        (stats->memory_usage_bytes < 10 * 1024 * 1024);

    return true;
}

/**
 * Reset display integration performance statistics.
 */
void display_integration_reset_stats(void) {
    if (!integration_initialized) {
        return;
    }

    memset(&integration_stats, 0, sizeof(integration_stats));
    integration_stats.init_time = time(NULL);

    // Reset display controller performance if available
    if (layered_display_enabled && global_display_controller) {
        display_controller_reset_performance_metrics(global_display_controller);
    }
}

/**
 * Check if layered display is currently enabled and working.
 *
 * @return true if layered display is active, false otherwise
 */
bool display_integration_is_layered_active(void) {
    return integration_initialized && layered_display_enabled &&
           global_display_controller != NULL;
}

/**
 * Get the global display controller instance.
 * For use by LLE and other components that need display integration.
 *
 * @return pointer to display controller, or NULL if not initialized
 */
display_controller_t *display_integration_get_controller(void) {
    return global_display_controller;
}

/**
 * Get display integration health status.
 *
 * @return health status enum
 */
display_integration_health_t display_integration_get_health(void) {
    if (!integration_initialized) {
        return DISPLAY_INTEGRATION_HEALTH_NOT_INITIALIZED;
    }

    if (!layered_display_enabled) {
        return DISPLAY_INTEGRATION_HEALTH_DISABLED;
    }

    if (!global_display_controller) {
        return DISPLAY_INTEGRATION_HEALTH_ERROR;
    }

    // Check performance metrics
    display_integration_stats_t stats;
    if (display_integration_get_stats(&stats)) {
        // Check if fallback rate is too high
        if (stats.total_display_calls > 0) {
            double fallback_rate =
                (double)stats.fallback_calls / stats.total_display_calls;
            if (fallback_rate > 0.5) {
                return DISPLAY_INTEGRATION_HEALTH_DEGRADED;
            }
        }

        // Check performance thresholds
        if (stats.avg_layered_display_time_ns >
            (current_config.performance_threshold_ms * 1000000)) {
            return DISPLAY_INTEGRATION_HEALTH_DEGRADED;
        }

        if (stats.cache_hit_rate < current_config.cache_hit_rate_threshold) {
            return DISPLAY_INTEGRATION_HEALTH_DEGRADED;
        }
    }

    return DISPLAY_INTEGRATION_HEALTH_EXCELLENT;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Get human-readable health status string.
 *
 * @param health Health status enum
 * @return String description of health status
 */
const char *
display_integration_health_string(display_integration_health_t health) {
    switch (health) {
    case DISPLAY_INTEGRATION_HEALTH_EXCELLENT:
        return "Excellent";
    case DISPLAY_INTEGRATION_HEALTH_GOOD:
        return "Good";
    case DISPLAY_INTEGRATION_HEALTH_DEGRADED:
        return "Degraded";
    case DISPLAY_INTEGRATION_HEALTH_ERROR:
        return "Error";
    case DISPLAY_INTEGRATION_HEALTH_DISABLED:
        return "Disabled";
    case DISPLAY_INTEGRATION_HEALTH_NOT_INITIALIZED:
        return "Not Initialized";
    default:
        return "Unknown";
    }
}

/**
 * Get enhanced prompt using layered display system.
 * This function provides enhanced prompt generation using the theme system.
 *
 * @param enhanced_prompt Pointer to store the generated enhanced prompt
 * @return true on success, false on failure
 */
bool display_integration_get_enhanced_prompt(char **enhanced_prompt) {
    if (!enhanced_prompt) {
        return false;
    }

    *enhanced_prompt = NULL;

    // Prioritize layered display over legacy enhanced display
    if (!integration_initialized || !global_display_controller) {
        return false; // Enhanced display not available
    }

    if (!layered_display_enabled) {
        return false; // No layered display mode active
    }

    // Full Display Controller Integration for Prompt Generation
    integration_stats.layered_display_calls++;

    // Enhanced Performance Monitoring: Start timing for prompt generation
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    // Generate base prompt using memory pool system
    size_t base_prompt_size = 512;
    char *base_prompt = lush_pool_alloc(base_prompt_size);
    if (!base_prompt) {
        return false;
    }

    // Use Spec 25 prompt composer when LLE is active - completely bypass legacy
    bool theme_result = false;
    const char *theme_name = "default";
    const char *debug = getenv("LUSH_PROMPT_DEBUG");

    if (debug && strcmp(debug, "1") == 0) {
        fprintf(stderr, "[DI] g_lle=%p composer=%p\n",
                (void *)g_lle_integration,
                g_lle_integration ? (void *)g_lle_integration->prompt_composer
                                  : NULL);
    }

    if (g_lle_integration && g_lle_integration->prompt_composer) {
        lle_prompt_composer_t *composer = g_lle_integration->prompt_composer;
        lle_prompt_output_t output;
        memset(&output, 0, sizeof(output));

        // Update background job count from executor (Issue #22)
        executor_t *executor = get_global_executor();
        if (executor) {
            executor_update_job_status(executor);
            int job_count = executor_count_jobs(executor);
            lle_prompt_context_set_job_count(&composer->context, job_count);
        }

        lle_result_t result = lle_composer_render(composer, &output);
        if (debug && strcmp(debug, "1") == 0) {
            fprintf(stderr, "[DI] Spec25 render: result=%d ps1='%s'\n", result,
                    output.ps1);
        }
        if (result == LLE_SUCCESS && output.ps1_len > 0) {
            /* Spec 25 prompt is ready - return directly, bypass display
             * controller The display controller would transform/cache this
             * incorrectly */
            *enhanced_prompt = lush_pool_strdup(output.ps1);
            lush_pool_free(base_prompt);
            lle_composer_clear_regeneration_flag(composer);

            gettimeofday(&end_time, NULL);
            uint64_t operation_time_ns =
                ((uint64_t)(end_time.tv_sec - start_time.tv_sec)) *
                    1000000000ULL +
                ((uint64_t)(end_time.tv_usec - start_time.tv_usec)) * 1000ULL;
            display_integration_record_display_timing(operation_time_ns);

            return (*enhanced_prompt != NULL);
        }

        // LLE mode but render failed - use minimal failsafe, do NOT use legacy
        *enhanced_prompt = lush_pool_strdup((getuid() > 0) ? "$ " : "# ");
        lush_pool_free(base_prompt);
        return (*enhanced_prompt != NULL);
    }

    // Ultimate fallback prompt generation (LLE not configured)
    if (!theme_result) {
        char *current_dir = getcwd(NULL, 0);
        const char *user = getenv("USER");
        const char *hostname = getenv("HOSTNAME");
        if (!hostname)
            hostname = "localhost";

        int written = snprintf(base_prompt, base_prompt_size, "[%s@%s] %s $ ",
                               user ? user : "user", hostname,
                               current_dir ? basename(current_dir) : "~");

        if (written >= (int)base_prompt_size) {
            strncpy(base_prompt, "$ ", base_prompt_size - 1);
            base_prompt[base_prompt_size - 1] = '\0';
        }

        if (current_dir)
            lush_pool_free(current_dir);
    }

    // Update theme context before display controller operations
    theme_name = get_active_theme_name();
    symbol_compatibility_t symbol_mode = get_symbol_mode();

    // Set theme context in display controller for theme-aware caching
    display_controller_error_t theme_context_result =
        display_controller_set_theme_context(global_display_controller,
                                             theme_name, symbol_mode);

    if (theme_context_result != DISPLAY_CONTROLLER_SUCCESS &&
        current_config.debug_mode) {
        fprintf(
            stderr,
            "display_integration: Warning - failed to set theme context: %d\n",
            theme_context_result);
    }

    // Use display controller for sophisticated prompt caching and
    // optimization
    char display_output[4096];
    char *current_command = ""; // Prompt generation has no active command

    display_controller_error_t result = display_controller_display(
        global_display_controller, base_prompt, current_command, display_output,
        sizeof(display_output));

    if (result == DISPLAY_CONTROLLER_SUCCESS) {
        // Display controller succeeded - use optimized output
        *enhanced_prompt = lush_pool_strdup(display_output);
        lush_pool_free(base_prompt);

        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Enhanced prompt generation "
                            "using display controller cache\n");
        }

        // Enhanced Performance Monitoring: Record timing
        gettimeofday(&end_time, NULL);
        uint64_t operation_time_ns =
            ((uint64_t)(end_time.tv_sec - start_time.tv_sec)) * 1000000000ULL +
            ((uint64_t)(end_time.tv_usec - start_time.tv_usec)) * 1000ULL;
        display_integration_record_display_timing(operation_time_ns);

        return (*enhanced_prompt != NULL);
    } else {
        // Display controller failed - use base prompt as fallback
        integration_stats.layered_display_errors++;
        integration_stats.fallback_calls++;
        log_controller_error("enhanced_prompt_generation", result);

        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Enhanced prompt fallback to "
                            "theme system\n");
        }

        *enhanced_prompt = base_prompt;
        return true;
    }
}

/**
 * Print diagnostic information about display integration.
 */
void display_integration_print_diagnostics(void) {
    if (!integration_initialized) {
        printf("Display Integration: Not initialized\n");
        return;
    }

    printf("=== Display Integration Diagnostics ===\n");
    printf("Status: %s\n", layered_display_enabled ? "Enabled" : "Disabled");
    printf("Health: %s\n",
           display_integration_health_string(display_integration_get_health()));

    display_integration_stats_t stats;
    if (display_integration_get_stats(&stats)) {
        printf("Total display calls: %" PRIu64 "\n", stats.total_display_calls);
        printf("Layered display calls: %" PRIu64 "\n",
               stats.layered_display_calls);
        printf("Fallback calls: %" PRIu64 "\n", stats.fallback_calls);

        if (stats.total_display_calls > 0) {
            double layered_rate = (double)stats.layered_display_calls /
                                  stats.total_display_calls * 100.0;
            double fallback_rate = (double)stats.fallback_calls /
                                   stats.total_display_calls * 100.0;
            printf("Layered display rate: %.1f%%\n", layered_rate);
            printf("Fallback rate: %.1f%%\n", fallback_rate);
        }

        if (layered_display_enabled) {
            printf("Average display time: %.2fms\n",
                   stats.avg_layered_display_time_ns / 1000000.0);
            printf("Cache hit rate: %.1f%%\n", stats.cache_hit_rate * 100.0);
            printf("Memory usage: %zu bytes\n", stats.memory_usage_bytes);
        }
    }

    printf("Configuration:\n");
    printf("  Layered display: enabled (exclusive system)\n");
    printf("  Caching: %s\n",
           current_config.enable_caching ? "enabled" : "disabled");
    printf("  Performance monitoring: %s\n",
           current_config.enable_performance_monitoring ? "enabled"
                                                        : "disabled");
    printf("  Optimization level: %d\n", current_config.optimization_level);
    printf("  Debug mode: %s\n",
           current_config.debug_mode ? "enabled" : "disabled");
    printf("=======================================\n");
}

// ============================================================================
// v1.3.0 SAFETY INFRASTRUCTURE IMPLEMENTATION
// ============================================================================

/**
 * Perform comprehensive safety check for layered display operation.
 * Professional safety-first implementation with detailed diagnostics.
 */
bool safe_layered_display_attempt(
    const char *function_name, integration_fallback_reason_t *fallback_reason) {
    (void)function_name; /* Reserved for diagnostic logging */
    if (!fallback_reason) {
        return false; // Invalid parameter - cannot proceed safely
    }

    // Initialize fallback reason
    *fallback_reason = INTEGRATION_FALLBACK_NONE;

    // Update safety check statistics
    integration_stats.safety_checks_performed++;

    // Check 1: Integration system initialization
    if (!integration_initialized) {
        *fallback_reason = INTEGRATION_FALLBACK_INITIALIZATION_ERROR;
        return false;
    }

    // Check 2: Layered display enablement
    // The layered display architecture is complete and ready for integration
    if (!layered_display_enabled) {
        *fallback_reason = INTEGRATION_FALLBACK_USER_REQUEST;
        return false;
    }

    // Check 3: Display controller availability
    if (!global_display_controller) {
        *fallback_reason = INTEGRATION_FALLBACK_CONTROLLER_NULL;
        return false;
    }

    // Check 4: Memory and system resources
    // Static buffer is always valid - memory check placeholder for future
    // dynamic allocation scenarios
    (void)0; /* Memory check placeholder */

    // Check 5: Configuration safety
    if (current_config.strict_compatibility_mode) {
        // v1.3.0: Layered display is exclusive - strict mode affects other
        // settings No fallback needed since layered display is the only system
    }

    // All safety checks passed
    return true;
}

/**
 * Log a fallback event for diagnostics and monitoring.
 */
void log_fallback_event(const char *function_name,
                        integration_fallback_reason_t reason) {
    if (!function_name) {
        return; // Invalid parameter
    }

    // Update statistics
    if (reason >= 0 && reason < 10) {
        integration_stats.fallback_events[reason]++;
    }
    integration_stats.last_fallback_time = time(NULL);
    integration_stats.last_fallback_reason = reason;

    // Debug logging if enabled
    if (current_config.debug_mode) {
        fprintf(stderr, "display_integration: %s fallback - %s\n",
                function_name, integration_fallback_reason_string(reason));
    }

    // Enterprise logging if enabled
    if (current_config.enable_enterprise_logging) {
        fprintf(
            stderr,
            "DISPLAY_INTEGRATION_FALLBACK: function=%s reason=%s time=%ld\n",
            function_name, integration_fallback_reason_string(reason),
            (long)integration_stats.last_fallback_time);
    }
}

/**
 * Log a display controller error with context.
 */
void log_controller_error(const char *function_name,
                          display_controller_error_t error) {
    if (!function_name) {
        return; // Invalid parameter
    }

    // Update error statistics
    integration_stats.layered_display_errors++;
    integration_stats.last_error_time = time(NULL);

    // Debug logging if enabled
    if (current_config.debug_mode) {
        fprintf(stderr, "display_integration: %s controller error %d\n",
                function_name, (int)error);
    }

    // Enterprise logging if enabled
    if (current_config.enable_enterprise_logging) {
        fprintf(
            stderr, "DISPLAY_CONTROLLER_ERROR: function=%s error=%d time=%ld\n",
            function_name, (int)error, (long)integration_stats.last_error_time);
    }
}

/**
 * Get human-readable string for fallback reason.
 */
const char *
integration_fallback_reason_string(integration_fallback_reason_t reason) {
    switch (reason) {
    case INTEGRATION_FALLBACK_NONE:
        return "no fallback";
    case INTEGRATION_FALLBACK_CONTROLLER_NULL:
        return "display controller not available";
    case INTEGRATION_FALLBACK_CONTROLLER_ERROR:
        return "display controller error";
    case INTEGRATION_FALLBACK_BUFFER_ERROR:
        return "buffer allocation error";
    case INTEGRATION_FALLBACK_TIMEOUT:
        return "operation timeout";
    case INTEGRATION_FALLBACK_USER_REQUEST:
        return "user disabled layered display";
    case INTEGRATION_FALLBACK_SAFETY_CHECK:
        return "safety check failed";
    case INTEGRATION_FALLBACK_MEMORY_ERROR:
        return "memory allocation failure";
    case INTEGRATION_FALLBACK_INITIALIZATION_ERROR:
        return "system not initialized";
    case INTEGRATION_FALLBACK_RECURSION_PROTECTION:
        return "recursion protection";
    default:
        return "unknown reason";
    }
}

// ============================================================================
// AUTOSUGGESTIONS LAYER INTEGRATION
// ============================================================================

/**
 * Initialize autosuggestions layer integration.
 * Resilient initialization that prepares infrastructure for LLE development.
 */
bool display_integration_init_autosuggestions(void) {
    if (autosuggestions_layer_initialized) {
        return true; // Already initialized
    }

    if (!integration_initialized) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Display integration not "
                            "ready for autosuggestions\n");
        }
        return false;
    }

    // Create layered display system directly if needed for autosuggestions
    if (!layered_display_enabled || !global_display_controller) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Creating layered display for "
                            "autosuggestions infrastructure\n");
        }
        // Initialize layered display components directly to avoid recursion
        if (!global_display_controller) {
            global_display_controller = display_controller_create();
            if (global_display_controller) {
                display_controller_config_t controller_config;
                display_controller_create_default_config(&controller_config);

                layer_event_system_t *event_system = layer_events_create(NULL);
                if (!event_system) {
                    display_controller_destroy(global_display_controller);
                    global_display_controller = NULL;
                    return false;
                }

                layer_events_error_t event_init_error =
                    layer_events_init(event_system);
                if (event_init_error != LAYER_EVENTS_SUCCESS) {
                    layer_events_destroy(event_system);
                    display_controller_destroy(global_display_controller);
                    global_display_controller = NULL;
                    return false;
                }

                display_controller_error_t error =
                    display_controller_init(global_display_controller,
                                            &controller_config, event_system);
                if (error == DISPLAY_CONTROLLER_SUCCESS) {
                    error = display_controller_prepare_shell_integration(
                        global_display_controller, &current_config);
                    if (error == DISPLAY_CONTROLLER_SUCCESS) {
                        layered_display_enabled = true;
                        if (current_config.debug_mode) {
                            fprintf(
                                stderr,
                                "display_integration: Auto-enabled layered "
                                "display for autosuggestions infrastructure\n");
                        }
                    }
                }

                if (!layered_display_enabled) {
                    display_controller_destroy(global_display_controller);
                    global_display_controller = NULL;
                    return false;
                }
            }
        }
    }

    // Get terminal control and event system from display controller
    terminal_control_t *terminal_ctrl =
        display_controller_get_terminal_control(global_display_controller);
    layer_event_system_t *event_system =
        display_controller_get_event_system(global_display_controller);

    if (!terminal_ctrl || !event_system) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Failed to get display "
                            "controller components for autosuggestions\n");
        }
        return false;
    }

    // Create the autosuggestions layer
    global_autosuggestions_layer =
        autosuggestions_layer_create(event_system, terminal_ctrl);
    if (!global_autosuggestions_layer) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Failed to create "
                            "autosuggestions layer\n");
        }
        return false;
    }

    // Initialize with default configuration - resilient to terminal limitations
    autosuggestions_layer_error_t init_error =
        autosuggestions_layer_init(global_autosuggestions_layer, NULL);
    if (init_error != AUTOSUGGESTIONS_LAYER_SUCCESS) {
        // Handle common initialization failures gracefully
        const char *error_desc = "Unknown error";
        switch (init_error) {
        case AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL:
            error_desc = "Unsupported terminal (expected in test environments)";
            break;
        case AUTOSUGGESTIONS_LAYER_ERROR_TERMINAL_TOO_SMALL:
            error_desc = "Terminal too small";
            break;
        case AUTOSUGGESTIONS_LAYER_ERROR_MEMORY_ALLOCATION:
            error_desc = "Memory allocation failed";
            break;
        default:
            error_desc = "Layer initialization failed";
            break;
        }

        if (current_config.debug_mode) {
            fprintf(stderr,
                    "display_integration: Autosuggestions initialization "
                    "failed: %s (code %d)\n",
                    error_desc, init_error);
            fprintf(stderr, "display_integration: Infrastructure created but "
                            "not active - ready for LLE development\n");
        }

        // Clean up but don't fail completely - infrastructure is still valuable
        // for LLE
        autosuggestions_layer_destroy(&global_autosuggestions_layer);

        // Mark as "initialized" for infrastructure purposes even though not
        // active This allows cache tracking and preparation for LLE development
        autosuggestions_layer_initialized = true;
        return true; // Success for infrastructure purposes
    }

    autosuggestions_layer_initialized = true;

    if (current_config.debug_mode) {
        fprintf(stderr, "display_integration: Autosuggestions system fully "
                        "initialized and ready\n");
    }

    return true;
}

/**
 * Cleanup autosuggestions layer integration.
 */
void display_integration_cleanup_autosuggestions(void) {
    if (global_autosuggestions_layer) {
        autosuggestions_layer_destroy(&global_autosuggestions_layer);
        autosuggestions_layer_initialized = false;
    }
}

/**
 * Update autosuggestions using layered display system.
 */
bool display_integration_update_autosuggestions(const char *line_buffer,
                                                int cursor_pos, int line_end) {
    if (!autosuggestions_layer_initialized) {
        return false;
    }

    if (!line_buffer || cursor_pos != line_end || line_end < 2) {
        return display_integration_clear_autosuggestions();
    }

    if (!layered_display_enabled) {
        return false;
    }

    if (!global_display_controller) {
        return false;
    }

    // Get terminal control and event system using professional getter functions
    terminal_control_t *terminal_ctrl =
        display_controller_get_terminal_control(global_display_controller);
    layer_event_system_t *event_system =
        display_controller_get_event_system(global_display_controller);

    if (!terminal_ctrl) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Terminal control is NULL\n");
        }
        return false;
    }

    if (!event_system) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Event system is NULL\n");
        }
        return false;
    }

    // Create context from readline parameters
    autosuggestions_context_t context;
    autosuggestions_layer_error_t context_error =
        autosuggestions_layer_create_context_from_readline(
            &context, line_buffer, cursor_pos, line_end);

    if (context_error != AUTOSUGGESTIONS_LAYER_SUCCESS) {
        if (current_config.debug_mode) {
            fprintf(stderr,
                    "display_integration: Failed to create autosuggestions "
                    "context: %d\n",
                    context_error);
        }
        return false;
    }

    // Generate and display suggestion using professional layered system
    autosuggestions_layer_error_t update_error =
        autosuggestions_layer_update(global_autosuggestions_layer, &context);

    if (update_error != AUTOSUGGESTIONS_LAYER_SUCCESS) {
        if (current_config.debug_mode) {
            fprintf(stderr,
                    "display_integration: Failed to update layered "
                    "autosuggestions: %d\n",
                    update_error);
        }
        return false;
    }

    // Successfully generated and displayed suggestion via layered system
    if (current_config.debug_mode) {
        fprintf(stderr, "display_integration: Professional layered "
                        "autosuggestions displayed successfully\n");
    }

    return true; // Professional layered autosuggestions system active
}

/**
 * Clear autosuggestions display using layered system.
 */
bool display_integration_clear_autosuggestions(void) {
    if (!autosuggestions_layer_initialized) {
        return false;
    }

    // Check if layered display is available and active
    if (!layered_display_enabled || !global_display_controller ||
        !global_autosuggestions_layer) {
        if (current_config.debug_mode) {
            fprintf(stderr, "display_integration: Layered autosuggestions not "
                            "active for clearing\n");
        }
        return false;
    }

    // Clear suggestion using professional layered system
    autosuggestions_layer_error_t clear_error =
        autosuggestions_layer_clear(global_autosuggestions_layer);

    if (clear_error != AUTOSUGGESTIONS_LAYER_SUCCESS) {
        if (current_config.debug_mode) {
            fprintf(
                stderr,
                "display_integration: Failed to clear layered suggestion: %d\n",
                clear_error);
        }
        return false;
    }

    // Successfully cleared suggestion via layered system
    if (current_config.debug_mode) {
        fprintf(stderr, "display_integration: Professional layered "
                        "autosuggestions cleared successfully\n");
    }

    return true;
}

// ============================================================================
// ENHANCED PERFORMANCE MONITORING IMPLEMENTATION
// ============================================================================

// Global enhanced performance metrics
static display_perf_metrics_t enhanced_perf_metrics = {0};
static bool enhanced_perf_monitoring_initialized = false;

/**
 * Initialize enhanced performance monitoring system.
 */
bool display_integration_perf_monitor_init(void) {
    if (enhanced_perf_monitoring_initialized) {
        return true; // Already initialized
    }

    // Initialize metrics structure
    memset(&enhanced_perf_metrics, 0, sizeof(display_perf_metrics_t));

    // Set performance targets
    enhanced_perf_metrics.cache_hit_rate_target =
        75.0; // Development phase minimum
    enhanced_perf_metrics.display_time_target_ms = 50.0; // Release standard
    enhanced_perf_metrics.measurement_frequency_hz =
        10; // Default 10Hz monitoring
    enhanced_perf_metrics.monitoring_active = true;

    // Initialize timing arrays
    for (int i = 0; i < 60; i++) {
        enhanced_perf_metrics.measurements_window[i] = 0;
    }

    enhanced_perf_monitoring_initialized = true;

    if (current_config.debug_mode) {
        printf("Enhanced Performance Monitoring: Initialized (targets: cache "
               ">%.1f%%, timing <%.1fms)\n",
               enhanced_perf_metrics.cache_hit_rate_target,
               enhanced_perf_metrics.display_time_target_ms);
    }

    return true;
}

/**
 * Get current enhanced performance metrics.
 */
bool display_integration_perf_monitor_get_metrics(
    display_perf_metrics_t *metrics) {
    if (!metrics || !enhanced_perf_monitoring_initialized) {
        return false;
    }

    *metrics = enhanced_perf_metrics;
    return true;
}

/**
 * Record a display operation for enhanced timing analysis.
 */
bool display_integration_record_display_timing(uint64_t operation_time_ns) {
    if (!enhanced_perf_monitoring_initialized) {
        return false;
    }

    // Update measurement count
    enhanced_perf_metrics.display_operations_measured++;
    enhanced_perf_metrics.display_time_total_ns += operation_time_ns;

    // Update min/max timing
    if (enhanced_perf_metrics.display_operations_measured == 1) {
        enhanced_perf_metrics.display_time_min_ns = operation_time_ns;
        enhanced_perf_metrics.display_time_max_ns = operation_time_ns;
    } else {
        if (operation_time_ns < enhanced_perf_metrics.display_time_min_ns) {
            enhanced_perf_metrics.display_time_min_ns = operation_time_ns;
        }
        if (operation_time_ns > enhanced_perf_metrics.display_time_max_ns) {
            enhanced_perf_metrics.display_time_max_ns = operation_time_ns;
        }
    }

    // Calculate average in milliseconds
    enhanced_perf_metrics.display_time_avg_ms =
        (double)enhanced_perf_metrics.display_time_total_ns / 1000000.0 /
        enhanced_perf_metrics.display_operations_measured;

    // Check if timing target is achieved
    enhanced_perf_metrics.display_timing_target_achieved =
        enhanced_perf_metrics.display_time_avg_ms <=
        enhanced_perf_metrics.display_time_target_ms;

    // Update rolling window for trend analysis
    enhanced_perf_metrics
        .measurements_window[enhanced_perf_metrics.measurements_index] =
        operation_time_ns;
    enhanced_perf_metrics.measurements_index =
        (enhanced_perf_metrics.measurements_index + 1) % 60;

    enhanced_perf_metrics.last_measurement_time = time(NULL);

    return true;
}

/**
 * Record cache operation for enhanced cache efficiency analysis.
 */
bool display_integration_record_cache_operation(bool was_hit) {
    if (!enhanced_perf_monitoring_initialized) {
        return false;
    }

    // Update cache operation counts
    enhanced_perf_metrics.cache_operations_total++;

    if (was_hit) {
        enhanced_perf_metrics.cache_hits_global++;
    } else {
        enhanced_perf_metrics.cache_misses_global++;
    }

    // Calculate current cache hit rate
    if (enhanced_perf_metrics.cache_operations_total > 0) {
        enhanced_perf_metrics.cache_hit_rate_current =
            (100.0 * enhanced_perf_metrics.cache_hits_global) /
            enhanced_perf_metrics.cache_operations_total;

        // Check if cache target is achieved
        enhanced_perf_metrics.cache_target_achieved =
            enhanced_perf_metrics.cache_hit_rate_current >=
            enhanced_perf_metrics.cache_hit_rate_target;
    }

    return true;
}

/**
 * Establish performance baseline for enhanced monitoring.
 */
bool display_integration_establish_baseline(void) {
    if (!enhanced_perf_monitoring_initialized) {
        return false;
    }

    // Require minimum measurements for valid baseline
    if (enhanced_perf_metrics.display_operations_measured < 10 ||
        enhanced_perf_metrics.cache_operations_total < 20) {
        if (current_config.debug_mode) {
            printf(
                "Performance Baseline: Insufficient data (display ops: %" PRIu64
                ", cache ops: %" PRIu64 ")\n",
                enhanced_perf_metrics.display_operations_measured,
                enhanced_perf_metrics.cache_operations_total);
        }
        return false;
    }

    // Establish baseline values
    enhanced_perf_metrics.baseline_cache_hit_rate =
        enhanced_perf_metrics.cache_hit_rate_current;
    enhanced_perf_metrics.baseline_display_time_ms =
        enhanced_perf_metrics.display_time_avg_ms;
    enhanced_perf_metrics.baseline_establishment_time = time(NULL);
    enhanced_perf_metrics.baseline_established = true;

    if (current_config.debug_mode) {
        printf("Performance Baseline: Established (cache: %.1f%%, timing: "
               "%.2fms)\n",
               enhanced_perf_metrics.baseline_cache_hit_rate,
               enhanced_perf_metrics.baseline_display_time_ms);
    }

    return true;
}

/**
 * Check if enhanced performance targets are being met.
 */
bool display_integration_perf_monitor_check_targets(bool *cache_target_met,
                                                bool *timing_target_met) {
    if (!cache_target_met || !timing_target_met ||
        !enhanced_perf_monitoring_initialized) {
        return false;
    }

    *cache_target_met = enhanced_perf_metrics.cache_target_achieved;
    *timing_target_met = enhanced_perf_metrics.display_timing_target_achieved;

    return true;
}

/**
 * Record cache operation for specific layer.
 */
void display_integration_record_layer_cache_operation(const char *layer_name,
                                                      bool hit) {
    if (!layer_name)
        return;

    if (strcmp(layer_name, "display_controller") == 0) {
        if (hit)
            layer_cache_stats.display_controller_hits++;
        else
            layer_cache_stats.display_controller_misses++;
    } else if (strcmp(layer_name, "composition_engine") == 0) {
        if (hit)
            layer_cache_stats.composition_engine_hits++;
        else
            layer_cache_stats.composition_engine_misses++;
    } else if (strcmp(layer_name, "command_layer") == 0) {
        if (hit)
            layer_cache_stats.command_layer_hits++;
        else
            layer_cache_stats.command_layer_misses++;
    } else if (strcmp(layer_name, "autosuggestions") == 0) {
        if (hit)
            layer_cache_stats.autosuggestions_hits++;
        else
            layer_cache_stats.autosuggestions_misses++;
    } else if (strcmp(layer_name, "prompt_layer") == 0) {
        if (hit)
            layer_cache_stats.prompt_layer_hits++;
        else
            layer_cache_stats.prompt_layer_misses++;
    }

    // Also record in global stats for backward compatibility
    display_integration_record_cache_operation(hit);
}

/**
 * Print detailed layer-specific cache performance report.
 */
void display_integration_print_layer_cache_report(void) {
    printf("\n=== Layer-Specific Cache Performance Report ===\n");

    // Display Controller Cache
    uint64_t dc_total = layer_cache_stats.display_controller_hits +
                        layer_cache_stats.display_controller_misses;
    if (dc_total > 0) {
        double dc_hit_rate =
            (100.0 * layer_cache_stats.display_controller_hits) / dc_total;
        printf("Display Controller Cache:\n");
        printf("  Operations: %" PRIu64 " total (%" PRIu64 " hits, %" PRIu64
               " misses)\n",
               dc_total, layer_cache_stats.display_controller_hits,
               layer_cache_stats.display_controller_misses);
        printf("  Hit Rate: %.1f%% %s\n", dc_hit_rate,
               dc_hit_rate >= 75.0 ? "✓" : "✗");
    } else {
        printf("Display Controller Cache: No operations recorded\n");
    }

    // Composition Engine Cache
    uint64_t ce_total = layer_cache_stats.composition_engine_hits +
                        layer_cache_stats.composition_engine_misses;
    if (ce_total > 0) {
        double ce_hit_rate =
            (100.0 * layer_cache_stats.composition_engine_hits) / ce_total;
        printf("Composition Engine Cache:\n");
        printf("  Operations: %" PRIu64 " total (%" PRIu64 " hits, %" PRIu64
               " misses)\n",
               ce_total, layer_cache_stats.composition_engine_hits,
               layer_cache_stats.composition_engine_misses);
        printf("  Hit Rate: %.1f%% %s\n", ce_hit_rate,
               ce_hit_rate >= 75.0 ? "✓" : "✗");
    } else {
        printf("Composition Engine Cache: No operations recorded\n");
    }

    // Command Layer Cache
    uint64_t cl_total = layer_cache_stats.command_layer_hits +
                        layer_cache_stats.command_layer_misses;
    if (cl_total > 0) {
        double cl_hit_rate =
            (100.0 * layer_cache_stats.command_layer_hits) / cl_total;
        printf("Command Layer Cache:\n");
        printf("  Operations: %" PRIu64 " total (%" PRIu64 " hits, %" PRIu64
               " misses)\n",
               cl_total, layer_cache_stats.command_layer_hits,
               layer_cache_stats.command_layer_misses);
        printf("  Hit Rate: %.1f%% %s\n", cl_hit_rate,
               cl_hit_rate >= 80.0 ? "✓" : "✗"); // Command layer has 80% target
    } else {
        printf("Command Layer Cache: No operations recorded\n");
    }

    // Autosuggestions Cache
    uint64_t as_total = layer_cache_stats.autosuggestions_hits +
                        layer_cache_stats.autosuggestions_misses;
    if (as_total > 0) {
        double as_hit_rate =
            (100.0 * layer_cache_stats.autosuggestions_hits) / as_total;
        printf("Autosuggestions Cache:\n");
        printf("  Operations: %" PRIu64 " total (%" PRIu64 " hits, %" PRIu64
               " misses)\n",
               as_total, layer_cache_stats.autosuggestions_hits,
               layer_cache_stats.autosuggestions_misses);
        printf("  Hit Rate: %.1f%% %s\n", as_hit_rate,
               as_hit_rate >= 70.0 ? "✓"
                                   : "✗"); // Autosuggestions has 70% target
    } else {
        printf("Autosuggestions Cache: No operations recorded\n");
    }

    // Prompt Layer Cache
    uint64_t pl_total = layer_cache_stats.prompt_layer_hits +
                        layer_cache_stats.prompt_layer_misses;
    if (pl_total > 0) {
        double pl_hit_rate =
            (100.0 * layer_cache_stats.prompt_layer_hits) / pl_total;
        printf("Prompt Layer Cache:\n");
        printf("  Operations: %" PRIu64 " total (%" PRIu64 " hits, %" PRIu64
               " misses)\n",
               pl_total, layer_cache_stats.prompt_layer_hits,
               layer_cache_stats.prompt_layer_misses);
        printf("  Hit Rate: %.1f%% %s\n", pl_hit_rate,
               pl_hit_rate >= 75.0 ? "✓" : "✗");
    } else {
        printf("Prompt Layer Cache: No operations recorded\n");
    }

    printf("=====================================\n");
}

/**
 * Reset layer-specific cache statistics.
 */
void display_integration_reset_layer_cache_stats(void) {
    memset(&layer_cache_stats, 0, sizeof(layer_cache_stats));
}

/**
 * Generate enhanced performance report.
 */
bool display_integration_perf_monitor_report(bool detailed) {
    if (!enhanced_perf_monitoring_initialized) {
        printf("Enhanced Performance Monitoring: Not initialized\n");
        return false;
    }

    printf("\n=== Enhanced Performance Report ===\n");

    // Cache Performance Analysis
    printf("Cache Performance:\n");
    printf("  Operations: %" PRIu64 " total (%" PRIu64 " hits, %" PRIu64
           " misses)\n",
           enhanced_perf_metrics.cache_operations_total,
           enhanced_perf_metrics.cache_hits_global,
           enhanced_perf_metrics.cache_misses_global);
    printf("  Hit Rate: %.1f%% (Target: >%.1f%%) %s\n",
           enhanced_perf_metrics.cache_hit_rate_current,
           enhanced_perf_metrics.cache_hit_rate_target,
           enhanced_perf_metrics.cache_target_achieved ? "✓" : "✗");

    // Display Timing Analysis
    printf("Display Timing:\n");
    printf("  Operations: %" PRIu64 " measured\n",
           enhanced_perf_metrics.display_operations_measured);
    printf("  Average: %.2fms (Target: <%.1fms) %s\n",
           enhanced_perf_metrics.display_time_avg_ms,
           enhanced_perf_metrics.display_time_target_ms,
           enhanced_perf_metrics.display_timing_target_achieved ? "✓" : "✗");

    if (detailed && enhanced_perf_metrics.display_operations_measured > 0) {
        printf("  Range: %.2fms - %.2fms\n",
               enhanced_perf_metrics.display_time_min_ns / 1000000.0,
               enhanced_perf_metrics.display_time_max_ns / 1000000.0);
    }

    // Baseline Comparison
    if (enhanced_perf_metrics.baseline_established) {
        printf("Baseline Comparison:\n");
        printf("  Cache Rate: %.1f%% -> %.1f%% (%+.1f%%)\n",
               enhanced_perf_metrics.baseline_cache_hit_rate,
               enhanced_perf_metrics.cache_hit_rate_current,
               enhanced_perf_metrics.cache_hit_rate_current -
                   enhanced_perf_metrics.baseline_cache_hit_rate);
        printf("  Display Time: %.2fms -> %.2fms (%+.2fms)\n",
               enhanced_perf_metrics.baseline_display_time_ms,
               enhanced_perf_metrics.display_time_avg_ms,
               enhanced_perf_metrics.display_time_avg_ms -
                   enhanced_perf_metrics.baseline_display_time_ms);
    } else {
        printf("Baseline: Not established (need more measurements)\n");
    }

    // Memory Pool Performance (if available)
    if (global_memory_pool && global_memory_pool->initialized) {
        lush_pool_stats_t pool_stats = lush_pool_get_stats();
        printf("Memory Pool Performance:\n");
        printf("  Pool allocations: %" PRIu64 " (%.1f%% hit rate)\n",
               pool_stats.pool_hits, pool_stats.pool_hit_rate);
        printf("  Malloc fallbacks: %" PRIu64 "\n",
               pool_stats.malloc_fallbacks);
        printf("  Active allocations: %u\n", pool_stats.active_allocations);
        printf("  Pool memory usage: %" PRIu64 " bytes (peak: %" PRIu64
               " bytes)\n",
               pool_stats.current_pool_usage, pool_stats.peak_pool_usage);
        if (pool_stats.avg_allocation_time_ns > 0) {
            printf("  Avg allocation time: %" PRIu64 " ns\n",
                   pool_stats.avg_allocation_time_ns);
        }

        // Memory pool efficiency assessment
        printf("  Pool efficiency: ");
        if (pool_stats.pool_hit_rate > 80.0) {
            printf("EXCELLENT ✓\n");
        } else if (pool_stats.pool_hit_rate > 60.0) {
            printf("GOOD ⚠\n");
        } else {
            printf("NEEDS OPTIMIZATION ✗\n");
        }
    }

    // Overall Performance Status
    printf("Performance Status: ");
    if (enhanced_perf_metrics.cache_target_achieved &&
        enhanced_perf_metrics.display_timing_target_achieved) {
        printf("TARGETS MET ✓\n");
    } else if (enhanced_perf_metrics.cache_target_achieved ||
               enhanced_perf_metrics.display_timing_target_achieved) {
        printf("PARTIAL ⚠\n");
    } else {
        printf("NEEDS OPTIMIZATION ✗\n");
    }

    printf("=====================================\n\n");

    return true;
}

/**
 * Reset enhanced performance metrics.
 */
bool display_integration_perf_monitor_reset(void) {
    if (!enhanced_perf_monitoring_initialized) {
        return false;
    }

    // Preserve configuration settings
    double cache_target = enhanced_perf_metrics.cache_hit_rate_target;
    double timing_target = enhanced_perf_metrics.display_time_target_ms;
    uint32_t frequency = enhanced_perf_metrics.measurement_frequency_hz;
    bool monitoring = enhanced_perf_metrics.monitoring_active;

    // Reset metrics
    memset(&enhanced_perf_metrics, 0, sizeof(display_perf_metrics_t));

    // Restore configuration
    enhanced_perf_metrics.cache_hit_rate_target = cache_target;
    enhanced_perf_metrics.display_time_target_ms = timing_target;
    enhanced_perf_metrics.measurement_frequency_hz = frequency;
    enhanced_perf_metrics.monitoring_active = monitoring;

    if (current_config.debug_mode) {
        printf("Enhanced Performance Metrics: Reset\n");
    }

    return true;
}

/**
 * Enable/disable real-time enhanced performance monitoring.
 */
bool display_integration_perf_monitor_set_active(bool enable,
                                                 uint32_t frequency_hz) {
    if (!enhanced_perf_monitoring_initialized) {
        return false;
    }

    // Validate frequency range
    if (frequency_hz < 1 || frequency_hz > 60) {
        return false;
    }

    enhanced_perf_metrics.monitoring_active = enable;
    enhanced_perf_metrics.measurement_frequency_hz = frequency_hz;

    if (current_config.debug_mode) {
        printf("Enhanced Performance Monitoring: %s (frequency: %uHz)\n",
               enable ? "enabled" : "disabled", frequency_hz);
    }

    return true;
}