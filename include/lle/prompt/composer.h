/**
 * @file composer.h
 * @brief LLE Prompt Composer - Template/Segment/Theme Integration
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 25 - Prompt Composer
 * Version: 1.1.0
 *
 * The prompt composer integrates the template engine, segment registry,
 * and theme registry to render complete prompts. It provides the callbacks
 * needed by the template engine to resolve segments, check visibility,
 * and apply theme colors.
 *
 * Shell Event Integration (Spec 26):
 * The composer registers handlers with the shell event hub to automatically
 * respond to directory changes, pre-command, and post-command events.
 * This enables event-driven cache invalidation instead of time-based polling.
 */

#ifndef LLE_PROMPT_COMPOSER_H
#define LLE_PROMPT_COMPOSER_H

#include "lle/error_handling.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/template.h"
#include "lle/prompt/theme.h"
#include "lle/prompt/transient.h"

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration for shell event hub */
struct lle_shell_event_hub;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/** @brief Maximum rendered prompt length */
#define LLE_PROMPT_OUTPUT_MAX 4096

/** @brief Maximum right prompt length */
#define LLE_RPROMPT_OUTPUT_MAX 1024

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief PS1/PS2 ownership — who last set the variable
 *
 * Used to implement "respect user PS1": when the user explicitly sets
 * PS1 (via assignment or shell script), the theme system stops
 * overwriting it on each prompt cycle. Theme switches always update PS1.
 */
typedef enum {
    PS1_OWNER_THEME, /**< Theme system set PS1 (can be overwritten by theme) */
    PS1_OWNER_USER   /**< User explicitly set PS1 (theme won't overwrite) */
} lle_ps1_owner_t;

/**
 * @brief Prompt composer configuration
 */
typedef struct lle_composer_config {
    bool enable_right_prompt;      /**< Render right prompt (RPROMPT) */
    bool enable_transient;         /**< Use transient prompt for history */
    bool newline_before_prompt;    /**< Print newline before prompt for visual
                                      separation */
    bool respect_user_ps1;         /**< Don't overwrite user PS1/PS2 */
    bool use_external_prompt;      /**< Defer to external prompt program */
    char external_prompt_cmd[256]; /**< External prompt command */
} lle_composer_config_t;

/**
 * @brief Prompt composer state
 *
 * Central coordinator that connects template engine, segment registry,
 * and theme registry for unified prompt rendering.
 *
 * Shell Event Integration:
 * The composer maintains a reference to the shell event hub and registers
 * handlers for directory changed, pre-command, and post-command events.
 * This enables automatic cache invalidation and context updates.
 */
typedef struct lle_prompt_composer {
    lle_segment_registry_t *segments; /**< Segment registry */
    lle_theme_registry_t *themes;     /**< Theme registry */
    lle_prompt_context_t context;     /**< Current prompt context */
    lle_composer_config_t config;     /**< Composer configuration */
    bool initialized;                 /**< Composer is initialized */

    /** @brief Shell event hub integration (Spec 26) */
    struct lle_shell_event_hub *shell_event_hub; /**< Shell event hub ref */
    bool events_registered;  /**< Event handlers registered */
    bool needs_regeneration; /**< Prompt needs to be re-rendered */

    /** @brief Transient prompt state (Spec 25 Section 12) */
    lle_transient_state_t transient; /**< Transient prompt tracking */
    const char *current_command;     /**< Command being executed */
    bool current_command_is_bg;      /**< Command is background */

    /** @brief Cached parsed templates */
    lle_parsed_template_t *cached_left_template;
    lle_parsed_template_t *cached_right_template;
    lle_parsed_template_t *cached_ps2_template;
    char cached_left_format[LLE_TEMPLATE_MAX];
    char cached_right_format[LLE_TEMPLATE_MAX];
    char cached_ps2_format[LLE_TEMPLATE_MAX];

    /** @brief PS1/PS2 ownership tracking (Spec 28 Phase 2) */
    lle_ps1_owner_t ps1_owner; /**< Who last set PS1 */
    lle_ps1_owner_t ps2_owner; /**< Who last set PS2 */

    /** @brief Statistics */
    uint64_t total_renders;
    uint64_t total_render_time_ns;
    uint64_t cache_hits;
    uint64_t event_triggered_refreshes; /**< Refreshes triggered by events */
} lle_prompt_composer_t;

/**
 * @brief Rendered prompt output
 */
typedef struct lle_prompt_output {
    char ps1[LLE_PROMPT_OUTPUT_MAX]; /**< Primary prompt (PS1) */
    size_t ps1_len;                  /**< PS1 length in bytes */
    size_t ps1_visual_width;         /**< PS1 visual width */

    char ps2[LLE_PROMPT_OUTPUT_MAX]; /**< Continuation prompt (PS2) */
    size_t ps2_len;                  /**< PS2 length in bytes */
    size_t ps2_visual_width;         /**< PS2 visual width */

    char rprompt[LLE_RPROMPT_OUTPUT_MAX]; /**< Right prompt */
    size_t rprompt_len;                   /**< RPROMPT length in bytes */
    size_t rprompt_visual_width;          /**< RPROMPT visual width */

    bool has_rprompt;  /**< Right prompt is set */
    bool is_multiline; /**< PS1 contains newlines */
} lle_prompt_output_t;

/* ============================================================================
 * COMPOSER LIFECYCLE API
 * ============================================================================
 */

/**
 * @brief Initialize the prompt composer
 *
 * @param composer  Composer to initialize
 * @param segments  Segment registry to use
 * @param themes    Theme registry to use
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_init(lle_prompt_composer_t *composer,
                               lle_segment_registry_t *segments,
                               lle_theme_registry_t *themes);

/**
 * @brief Cleanup the prompt composer
 *
 * @param composer  Composer to cleanup
 */
void lle_composer_cleanup(lle_prompt_composer_t *composer);

/**
 * @brief Configure the prompt composer
 *
 * @param composer  Composer to configure
 * @param config    Configuration to apply
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_configure(lle_prompt_composer_t *composer,
                                    const lle_composer_config_t *config);

/* ============================================================================
 * PROMPT RENDERING API
 * ============================================================================
 */

/**
 * @brief Render the complete prompt
 *
 * Renders PS1, PS2, and optionally RPROMPT using the active theme's
 * templates and the segment registry.
 *
 * @param composer  Prompt composer
 * @param output    Output structure to fill
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_render(lle_prompt_composer_t *composer,
                                 lle_prompt_output_t *output);

/**
 * @brief Render a single template string
 *
 * Lower-level function for rendering arbitrary templates.
 *
 * @param composer     Prompt composer
 * @param template_str Template string to render
 * @param output       Output buffer
 * @param output_size  Output buffer size
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_render_template(lle_prompt_composer_t *composer,
                                          const char *template_str,
                                          char *output, size_t output_size);

/**
 * @brief Update the prompt context
 *
 * Call after command execution to update exit code, duration, etc.
 *
 * @param composer    Prompt composer
 * @param exit_code   Exit code of last command
 * @param duration_ms Duration of last command in milliseconds
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_update_context(lle_prompt_composer_t *composer,
                                         int exit_code, uint64_t duration_ms);

/**
 * @brief Refresh directory information in context
 *
 * Call after directory change to update CWD, git repo status, etc.
 *
 * @param composer  Prompt composer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_refresh_directory(lle_prompt_composer_t *composer);

/**
 * @brief Invalidate all segment caches
 *
 * Call on events that may affect segment output.
 *
 * @param composer  Prompt composer
 */
void lle_composer_invalidate_caches(lle_prompt_composer_t *composer);

/* ============================================================================
 * THEME INTEGRATION API
 * ============================================================================
 */

/**
 * @brief Set the active theme by name
 *
 * @param composer    Prompt composer
 * @param theme_name  Name of theme to activate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_composer_set_theme(lle_prompt_composer_t *composer,
                                    const char *theme_name);

/**
 * @brief Get the current active theme
 *
 * @param composer  Prompt composer
 * @return Pointer to active theme or NULL
 */
const lle_theme_t *
lle_composer_get_theme(const lle_prompt_composer_t *composer);

/* ============================================================================
 * PS1/PS2 OWNERSHIP API (Spec 28 Phase 2)
 * ============================================================================
 */

/**
 * @brief Notify that the user explicitly set PS1 or PROMPT
 *
 * Called from the executor's variable assignment path when PS1 or PROMPT
 * is assigned by user code. Sets ps1_owner to PS1_OWNER_USER so the
 * theme system won't overwrite it on subsequent prompt renders.
 *
 * @param composer  Prompt composer (may be NULL — safe to call before init)
 */
void lle_prompt_notify_ps1_changed(lle_prompt_composer_t *composer);

/**
 * @brief Notify that the user explicitly set PS2
 *
 * @param composer  Prompt composer (may be NULL)
 */
void lle_prompt_notify_ps2_changed(lle_prompt_composer_t *composer);

/* ============================================================================
 * SHELL EVENT INTEGRATION API (Spec 26)
 * ============================================================================
 */

/**
 * @brief Register prompt composer with shell event hub
 *
 * Registers event handlers for directory changed, pre-command, and
 * post-command events. This enables automatic cache invalidation and
 * context updates based on shell activity.
 *
 * This is the key integration point with Spec 26 (LLE Initialization System).
 * Call this after composer init and when shell event hub is available.
 *
 * @param composer   Prompt composer
 * @param event_hub  Shell event hub from Spec 26
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_composer_register_shell_events(lle_prompt_composer_t *composer,
                                   struct lle_shell_event_hub *event_hub);

/**
 * @brief Unregister prompt composer from shell event hub
 *
 * Removes event handlers. Call before destroying the composer if
 * the event hub will outlive it.
 *
 * @param composer  Prompt composer
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_composer_unregister_shell_events(lle_prompt_composer_t *composer);

/**
 * @brief Check if prompt needs regeneration
 *
 * Returns true if an event has triggered a need to re-render the prompt.
 * Call this before rendering to determine if cached output is stale.
 *
 * @param composer  Prompt composer
 * @return true if regeneration needed
 */
bool lle_composer_needs_regeneration(const lle_prompt_composer_t *composer);

/**
 * @brief Clear regeneration flag
 *
 * Call after successfully rendering the prompt to clear the flag.
 *
 * @param composer  Prompt composer
 */
void lle_composer_clear_regeneration_flag(lle_prompt_composer_t *composer);

/* ============================================================================
 * TEMPLATE CALLBACK HELPERS (INTERNAL)
 * ============================================================================
 */

/**
 * @brief Create render context for template engine
 *
 * @param composer  Prompt composer
 * @return Render context with callbacks configured
 */
lle_template_render_ctx_t
lle_composer_create_render_ctx(lle_prompt_composer_t *composer);

#ifdef __cplusplus
}
#endif

#endif /* LLE_PROMPT_COMPOSER_H */
