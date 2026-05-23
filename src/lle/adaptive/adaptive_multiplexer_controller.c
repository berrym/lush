/**
 * @file adaptive_multiplexer_controller.c
 * @brief Terminal multiplexer controller for tmux/screen environments
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 26 Phase 2 - Multiplexer Controller
 *
 * Implements the multiplexer controller for terminal multiplexers
 * (tmux/screen). Provides special handling for multiplexer-specific escape
 * sequence passthrough and capability adaptation.
 *
 * Key Features:
 * - Multiplexer type detection (tmux, screen, other)
 * - Escape sequence passthrough for multiplexers
 * - Focus event support
 * - Base native controller with multiplexer adaptations
 * - Special handling for multiplexer quirks
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Forward declaration from native controller
extern lle_result_t
lle_initialize_native_controller(lle_adaptive_context_t *context,
                                 lush_memory_pool_t *memory_pool);
extern void lle_cleanup_native_controller(lle_native_controller_t *native);
extern lle_result_t lle_native_read_line(lle_native_controller_t *native,
                                         const char *prompt, char **line);

/* ============================================================================
 * MULTIPLEXER ADAPTER STRUCTURE
 * ============================================================================
 */

/**
 * Multiplexer adapter for special handling.
 * Wraps escape sequences and handles multiplexer-specific quirks.
 */
struct lle_multiplexer_adapter_t {
    lle_multiplexer_type_t type;

    // Passthrough configuration
    const char *passthrough_prefix;
    const char *passthrough_suffix;
    bool needs_escape_doubling;

    // Capabilities affected by multiplexer
    bool supports_focus_events;
    bool supports_true_mouse;
    bool supports_clipboard;

    // Statistics
    uint64_t sequences_wrapped;
    uint64_t passthrough_operations;
};

/**
 * Multiplexer controller structure.
 */
struct lle_multiplexer_controller_t {
    // Multiplexer type and configuration
    lle_multiplexer_type_t multiplexer_type;

    // Multiplexer-specific capabilities
    bool supports_passthrough;
    bool supports_focus_events;
    bool requires_escape_doubling;

    // Base native controller
    lle_native_controller_t *base_controller;

    // Multiplexer adapter
    lle_multiplexer_adapter_t *adapter;

    // Memory management
    lush_memory_pool_t *memory_pool;

    // Statistics
    uint64_t lines_read;
    uint64_t adapted_sequences;
};

/* ============================================================================
 * MULTIPLEXER DETECTION
 * ============================================================================
 */

/**
 * @brief Detect the terminal multiplexer type from environment variables.
 *
 * Checks TMUX, STY, and TERM environment variables to identify
 * if running inside tmux, screen, or another multiplexer.
 *
 * @return The detected multiplexer type.
 */
static lle_multiplexer_type_t lle_detect_multiplexer_type(void) {
    // Check for tmux
    const char *tmux_env = getenv("TMUX");
    if (tmux_env && strlen(tmux_env) > 0) {
        return LLE_MUX_TYPE_TMUX;
    }

    // Check for screen
    const char *sty_env = getenv("STY");
    if (sty_env && strlen(sty_env) > 0) {
        return LLE_MUX_TYPE_SCREEN;
    }

    // Check TERM variable patterns
    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "tmux") != NULL) {
            return LLE_MUX_TYPE_TMUX;
        }
        if (strstr(term, "screen") != NULL) {
            return LLE_MUX_TYPE_SCREEN;
        }
    }

    return LLE_MUX_TYPE_NONE;
}

/**
 * @brief Configure capabilities based on the detected multiplexer type.
 *
 * Sets up multiplexer-specific capability flags for passthrough,
 * focus events, and escape doubling requirements.
 *
 * @param mux The multiplexer controller to configure.
 * @param detection Terminal detection results (reserved for future use).
 */
static void lle_configure_multiplexer_capabilities(
    lle_multiplexer_controller_t *mux,
    const lle_terminal_detection_result_t *detection) {
    (void)detection; // Reserved for detection-based capability tuning

    switch (mux->multiplexer_type) {
    case LLE_MUX_TYPE_TMUX:
        // tmux supports DCS passthrough
        mux->supports_passthrough = true;
        mux->supports_focus_events = true;
        mux->requires_escape_doubling = false;
        break;

    case LLE_MUX_TYPE_SCREEN:
        // screen has limited passthrough
        mux->supports_passthrough = false;
        mux->supports_focus_events = false;
        mux->requires_escape_doubling = true;
        break;

    case LLE_MUX_TYPE_OTHER:
        // Unknown multiplexer - conservative settings
        mux->supports_passthrough = false;
        mux->supports_focus_events = false;
        mux->requires_escape_doubling = false;
        break;

    default:
        mux->supports_passthrough = false;
        mux->supports_focus_events = false;
        mux->requires_escape_doubling = false;
        break;
    }
}

/* ============================================================================
 * MULTIPLEXER ADAPTER IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a multiplexer adapter for escape sequence handling.
 *
 * Allocates and configures an adapter with multiplexer-specific
 * passthrough prefixes, suffixes, and capability flags.
 *
 * @param adapter Output pointer to receive the created adapter.
 * @param type The multiplexer type to configure for.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_multiplexer_adapter_create(lle_multiplexer_adapter_t **adapter,
                                            lle_multiplexer_type_t type) {

    lle_multiplexer_adapter_t *adapt =
        calloc(1, sizeof(lle_multiplexer_adapter_t));
    if (!adapt) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    adapt->type = type;

    // Configure passthrough based on multiplexer type
    switch (type) {
    case LLE_MUX_TYPE_TMUX:
        // tmux DCS passthrough: ESC P tmux ; ESC <sequence> ESC ESC
        adapt->passthrough_prefix = "\x1bPtmux;\x1b";
        adapt->passthrough_suffix = "\x1b\\";
        adapt->needs_escape_doubling = true;
        adapt->supports_focus_events = true;
        adapt->supports_true_mouse = true;
        adapt->supports_clipboard = true;
        break;

    case LLE_MUX_TYPE_SCREEN:
        // screen DCS passthrough: ESC P ESC <sequence> ESC ESC
        adapt->passthrough_prefix = "\x1bP\x1b";
        adapt->passthrough_suffix = "\x1b\\";
        adapt->needs_escape_doubling = true;
        adapt->supports_focus_events = false;
        adapt->supports_true_mouse = false;
        adapt->supports_clipboard = false;
        break;

    default:
        adapt->passthrough_prefix = NULL;
        adapt->passthrough_suffix = NULL;
        adapt->needs_escape_doubling = false;
        adapt->supports_focus_events = false;
        adapt->supports_true_mouse = false;
        adapt->supports_clipboard = false;
        break;
    }

    *adapter = adapt;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a multiplexer adapter and free its resources.
 *
 * @param adapter The adapter to destroy, or NULL for no-op.
 */
static void
lle_multiplexer_adapter_destroy(lle_multiplexer_adapter_t *adapter) {
    free(adapter);
}

/**
 * @brief Wrap an escape sequence for multiplexer passthrough.
 *
 * Wraps the given escape sequence with multiplexer-specific DCS
 * passthrough sequences. Handles escape doubling when required.
 *
 * @param adapter The multiplexer adapter.
 * @param sequence The escape sequence to wrap.
 * @param seq_len The length of the sequence in bytes.
 * @param wrapped Output pointer to receive the wrapped sequence (caller frees).
 * @param wrapped_len Output pointer to receive the wrapped length.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t
lle_multiplexer_adapter_wrap_sequence(lle_multiplexer_adapter_t *adapter,
                                      const char *sequence, size_t seq_len,
                                      char **wrapped, size_t *wrapped_len) {

    if (!adapter->passthrough_prefix || !adapter->passthrough_suffix) {
        // No wrapping needed
        *wrapped = strdup(sequence);
        if (!*wrapped) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        *wrapped_len = seq_len;
        return LLE_SUCCESS;
    }

    size_t prefix_len = strlen(adapter->passthrough_prefix);
    size_t suffix_len = strlen(adapter->passthrough_suffix);

    // Calculate wrapped size (account for escape doubling)
    size_t doubled_len = seq_len;
    if (adapter->needs_escape_doubling) {
        // Count escapes in sequence
        size_t escape_count = 0;
        for (size_t i = 0; i < seq_len; i++) {
            if (sequence[i] == '\x1b') {
                escape_count++;
            }
        }
        doubled_len += escape_count;
    }

    size_t total_len = prefix_len + doubled_len + suffix_len;
    char *result = malloc(total_len + 1);
    if (!result) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Build wrapped sequence
    size_t pos = 0;

    // Add prefix
    memcpy(result + pos, adapter->passthrough_prefix, prefix_len);
    pos += prefix_len;

    // Add sequence (with escape doubling if needed)
    if (adapter->needs_escape_doubling) {
        for (size_t i = 0; i < seq_len; i++) {
            result[pos++] = sequence[i];
            if (sequence[i] == '\x1b') {
                result[pos++] = '\x1b'; // Double the escape
            }
        }
    } else {
        memcpy(result + pos, sequence, seq_len);
        pos += seq_len;
    }

    // Add suffix
    memcpy(result + pos, adapter->passthrough_suffix, suffix_len);
    pos += suffix_len;

    result[pos] = '\0';

    *wrapped = result;
    *wrapped_len = pos;

    adapter->sequences_wrapped++;
    adapter->passthrough_operations++;

    return LLE_SUCCESS;
}

/**
 * @brief Check if an escape sequence requires multiplexer passthrough wrapping.
 *
 * Determines whether the sequence contains OSC or DCS sequences
 * that need to be wrapped for proper transmission through a multiplexer.
 *
 * @param adapter The multiplexer adapter.
 * @param sequence The escape sequence to check.
 * @return true if wrapping is needed, false otherwise.
 */
static bool
lle_multiplexer_adapter_needs_wrapping(lle_multiplexer_adapter_t *adapter,
                                       const char *sequence) {

    if (!adapter->passthrough_prefix) {
        return false;
    }

    // Check for sequences that need passthrough
    // OSC sequences (clipboard, window title, etc.)
    if (strstr(sequence, "\x1b]") != NULL) {
        return true;
    }

    // Device control strings
    if (strstr(sequence, "\x1bP") != NULL) {
        return true;
    }

    return false;
}

/* ============================================================================
 * MULTIPLEXER CONTROLLER API
 * ============================================================================
 */

/**
 * @brief Initialize the multiplexer controller for tmux/screen environments.
 *
 * Creates and configures a multiplexer controller that wraps the native
 * controller with multiplexer-specific escape sequence handling.
 *
 * @param context The adaptive context to initialize the controller in.
 * @param memory_pool Memory pool for allocations.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_initialize_multiplexer_controller(lle_adaptive_context_t *context,
                                      lush_memory_pool_t *memory_pool) {

    lle_multiplexer_controller_t *mux =
        calloc(1, sizeof(lle_multiplexer_controller_t));
    if (!mux) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Detect multiplexer type
    mux->multiplexer_type = lle_detect_multiplexer_type();

    // Configure multiplexer-specific capabilities
    lle_configure_multiplexer_capabilities(mux, context->detection_result);

    // Initialize base native controller
    lle_result_t result =
        lle_initialize_native_controller(context, memory_pool);
    if (result != LLE_SUCCESS) {
        free(mux);
        return result;
    }

    // Save base controller reference
    mux->base_controller = context->controller.native;

    // Create multiplexer adapter
    result =
        lle_multiplexer_adapter_create(&mux->adapter, mux->multiplexer_type);
    if (result != LLE_SUCCESS) {
        lle_cleanup_native_controller(mux->base_controller);
        free(mux);
        return result;
    }

    mux->memory_pool = memory_pool;

    // Update context to use multiplexer controller
    context->controller.mux = mux;

    return LLE_SUCCESS;
}

/**
 * @brief Clean up and destroy a multiplexer controller.
 *
 * Releases all resources including the multiplexer adapter and
 * the underlying native controller.
 *
 * @param mux The multiplexer controller to destroy, or NULL for no-op.
 */
void lle_cleanup_multiplexer_controller(lle_multiplexer_controller_t *mux) {
    if (!mux) {
        return;
    }

    lle_multiplexer_adapter_destroy(mux->adapter);
    lle_cleanup_native_controller(mux->base_controller);
    free(mux);
}

/**
 * @brief Read a line of input using the multiplexer controller.
 *
 * Delegates to the underlying native controller for actual input reading.
 *
 * @param mux The multiplexer controller.
 * @param prompt The prompt string to display.
 * @param line Output pointer to receive the allocated input line.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_multiplexer_read_line(lle_multiplexer_controller_t *mux,
                                       const char *prompt, char **line) {

    if (!mux || !prompt || !line) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Use base native controller for reading
    lle_result_t result =
        lle_native_read_line(mux->base_controller, prompt, line);
    if (result == LLE_SUCCESS) {
        mux->lines_read++;
    }

    return result;
}

/**
 * @brief Send an escape sequence through the multiplexer with adaptation.
 *
 * Wraps the sequence in DCS passthrough if needed, then writes it
 * to stdout. Handles multiplexer-specific quirks automatically.
 *
 * @param mux The multiplexer controller.
 * @param sequence The escape sequence to send.
 * @param length The length of the sequence in bytes.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_multiplexer_send_sequence(lle_multiplexer_controller_t *mux,
                                           const char *sequence,
                                           size_t length) {

    if (!mux || !sequence) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Check if sequence needs wrapping
    if (!lle_multiplexer_adapter_needs_wrapping(mux->adapter, sequence)) {
        // Send directly
        ssize_t written = write(STDOUT_FILENO, sequence, length);
        if (written < 0 || (size_t)written != length) {
            return LLE_ERROR_TERMINAL_ABSTRACTION;
        }
        return LLE_SUCCESS;
    }

    // Wrap sequence for multiplexer passthrough
    char *wrapped = NULL;
    size_t wrapped_len = 0;
    lle_result_t result = lle_multiplexer_adapter_wrap_sequence(
        mux->adapter, sequence, length, &wrapped, &wrapped_len);

    if (result != LLE_SUCCESS) {
        return result;
    }

    // Send wrapped sequence
    ssize_t written = write(STDOUT_FILENO, wrapped, wrapped_len);
    free(wrapped);

    if (written < 0 || (size_t)written != wrapped_len) {
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    mux->adapted_sequences++;
    return LLE_SUCCESS;
}

/**
 * @brief Get the human-readable name of a multiplexer type.
 *
 * @param type The multiplexer type.
 * @return A static string with the multiplexer name.
 */
const char *lle_multiplexer_type_name(lle_multiplexer_type_t type) {
    switch (type) {
    case LLE_MUX_TYPE_TMUX:
        return "tmux";
    case LLE_MUX_TYPE_SCREEN:
        return "screen";
    case LLE_MUX_TYPE_OTHER:
        return "other";
    case LLE_MUX_TYPE_NONE:
    default:
        return "none";
    }
}

/**
 * @brief Get multiplexer controller statistics.
 *
 * Retrieves performance and usage statistics from the multiplexer
 * controller.
 *
 * @param mux The multiplexer controller.
 * @param lines_read Output pointer for lines read count, or NULL to skip.
 * @param adapted_sequences Output pointer for adapted sequence count, or NULL
 * to skip.
 * @param passthrough_ops Output pointer for passthrough operation count, or
 * NULL to skip.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_multiplexer_get_stats(const lle_multiplexer_controller_t *mux,
                                       uint64_t *lines_read,
                                       uint64_t *adapted_sequences,
                                       uint64_t *passthrough_ops) {

    if (!mux) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (lines_read) {
        *lines_read = mux->lines_read;
    }
    if (adapted_sequences) {
        *adapted_sequences = mux->adapted_sequences;
    }
    if (passthrough_ops && mux->adapter) {
        *passthrough_ops = mux->adapter->passthrough_operations;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Check if the multiplexer supports a specific feature.
 *
 * Queries the multiplexer's capability for features like passthrough,
 * focus_events, mouse, and clipboard support.
 *
 * @param mux The multiplexer controller.
 * @param feature The feature name to check.
 * @return true if the feature is supported, false otherwise.
 */
bool lle_multiplexer_supports_feature(const lle_multiplexer_controller_t *mux,
                                      const char *feature) {

    if (!mux || !feature) {
        return false;
    }

    if (strcmp(feature, "passthrough") == 0) {
        return mux->supports_passthrough;
    }
    if (strcmp(feature, "focus_events") == 0) {
        return mux->supports_focus_events;
    }
    if (strcmp(feature, "mouse") == 0 && mux->adapter) {
        return mux->adapter->supports_true_mouse;
    }
    if (strcmp(feature, "clipboard") == 0 && mux->adapter) {
        return mux->adapter->supports_clipboard;
    }

    return false;
}
