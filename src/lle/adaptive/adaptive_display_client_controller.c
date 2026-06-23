/**
 * @file adaptive_display_client_controller.c
 * @brief Enhanced display client controller for non-TTY stdin environments
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 26 - Display Client Controller
 *
 * Implements the display client controller for Enhanced mode terminals.
 * This mode is used for editor terminals (Zed, VS Code) and AI assistants
 * where stdin may not be a TTY but stdout has display capabilities.
 *
 * Key Features:
 * - Non-TTY stdin support with cooked mode input
 * - Color and formatting output despite non-TTY stdin
 * - Content generation for display layer integration
 * - Line-oriented rendering without raw terminal control
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * DISPLAY CLIENT CONTROLLER STRUCTURE
 * ============================================================================
 */

/**
 * Display content rendering pipeline.
 * Generates formatted output for display layer consumption.
 */
typedef struct {
    // Rendering capabilities
    bool color_enabled;
    bool cursor_control_enabled;
    bool clear_enabled;
    int color_depth; /**< 0=none, 1=8, 2=256, 3=truecolor */

    // Content buffers
    char *prompt_buffer;
    size_t prompt_buffer_size;
    char *content_buffer;
    size_t content_buffer_size;
    size_t content_length;

    // Formatting state
    bool bold_active;
    bool italic_active;
    bool underline_active;
    int current_fg_color;
    int current_bg_color;

    // Performance
    uint64_t renders_completed;
    uint64_t total_render_time_us;
} lle_render_pipeline_t;

/**
 * Enhanced input processor for non-raw mode input.
 * Processes cooked mode input without terminal control sequences.
 */
struct lle_enhanced_input_processor_t {
    // Input buffer
    char *input_buffer;
    size_t buffer_size;
    size_t buffer_used;

    // Input state
    bool echo_enabled;
    bool line_buffered;
    bool utf8_mode;

    // Special key handling
    bool handle_ctrl_sequences;
    bool handle_escape_sequences;

    // Performance
    uint64_t bytes_processed;
    uint64_t lines_processed;
};

/**
 * Display content generator.
 * Creates formatted content for display output.
 */
struct lle_display_content_generator_t {
    // Generation capabilities
    bool supports_colors;
    bool supports_cursor;
    bool supports_unicode;

    // Content generation state
    char *generated_content;
    size_t content_capacity;
    size_t content_length;

    // Formatting functions
    char *(*format_prompt)(struct lle_display_content_generator_t *gen,
                           const char *prompt);
    char *(*format_line)(struct lle_display_content_generator_t *gen,
                         const char *line);
    char *(*format_completion)(struct lle_display_content_generator_t *gen,
                               const char *completion);

    // Performance
    uint64_t generations_completed;
};

/**
 * Display client controller - Enhanced mode implementation.
 */
struct lle_display_client_controller_t {
    // Display capabilities
    bool supports_color_output;
    bool supports_cursor_positioning;
    bool supports_clear_operations;
    int terminal_width;
    int terminal_height;

    // Core components
    lle_enhanced_input_processor_t *input_processor;
    lle_display_content_generator_t *content_generator;
    lle_render_pipeline_t *render_pipeline;

    // Display state
    char *current_prompt;
    char *current_line;
    size_t cursor_position;

    // Memory management
    lush_memory_pool_t *memory_pool;

    // Statistics
    uint64_t lines_read;
    uint64_t displays_updated;
    uint64_t errors_encountered;
};

/* ============================================================================
 * RENDER PIPELINE IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a new render pipeline for display output.
 *
 * Allocates and initializes a render pipeline with the specified
 * color and cursor capabilities.
 *
 * @param pipeline Output pointer to receive the created pipeline.
 * @param color_enabled Whether color output is supported.
 * @param cursor_enabled Whether cursor control is supported.
 * @param color_depth Color depth (0=none, 1=8, 2=256, 3=truecolor).
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_render_pipeline_create(lle_render_pipeline_t **pipeline,
                                               bool color_enabled,
                                               bool cursor_enabled,
                                               int color_depth) {

    lle_render_pipeline_t *pipe = calloc(1, sizeof(lle_render_pipeline_t));
    if (!pipe) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    pipe->color_enabled = color_enabled;
    pipe->cursor_control_enabled = cursor_enabled;
    pipe->clear_enabled = cursor_enabled; // Clear requires cursor control
    pipe->color_depth = color_depth;

    // Allocate rendering buffers
    pipe->prompt_buffer_size = 1024;
    pipe->prompt_buffer = malloc(pipe->prompt_buffer_size);
    if (!pipe->prompt_buffer) {
        free(pipe);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    pipe->content_buffer_size = 4096;
    pipe->content_buffer = malloc(pipe->content_buffer_size);
    if (!pipe->content_buffer) {
        free(pipe->prompt_buffer);
        free(pipe);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    pipe->current_fg_color = -1; // No color
    pipe->current_bg_color = -1;

    *pipeline = pipe;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a render pipeline and free its resources.
 *
 * @param pipeline The render pipeline to destroy, or NULL for no-op.
 */
static void lle_render_pipeline_destroy(lle_render_pipeline_t *pipeline) {
    if (!pipeline) {
        return;
    }

    free(pipeline->prompt_buffer);
    free(pipeline->content_buffer);
    free(pipeline);
}

/**
 * @brief Generate and append ANSI color escape sequence to buffer.
 *
 * Generates appropriate color sequences based on the pipeline's
 * color depth. Optimizes by skipping if colors haven't changed.
 *
 * @param pipe The render pipeline.
 * @param fg_color Foreground color index, or -1 for no change.
 * @param bg_color Background color index, or -1 for no change.
 */
static void lle_render_pipeline_append_color(lle_render_pipeline_t *pipe,
                                             int fg_color, int bg_color) {

    if (!pipe->color_enabled || pipe->color_depth == 0) {
        return;
    }

    // Optimize: only emit if changed
    if (fg_color == pipe->current_fg_color &&
        bg_color == pipe->current_bg_color) {
        return;
    }

    // Ensure buffer capacity
    size_t needed = pipe->content_length + 32; // Max ANSI sequence length
    if (needed >= pipe->content_buffer_size) {
        size_t new_size = pipe->content_buffer_size * 2;
        char *new_buffer = realloc(pipe->content_buffer, new_size);
        if (!new_buffer) {
            return; // Continue without color
        }
        pipe->content_buffer = new_buffer;
        pipe->content_buffer_size = new_size;
    }

    // Generate appropriate color sequence based on depth
    if (pipe->color_depth >= 2 && fg_color >= 0) {
        // 256 color mode
        int written = snprintf(pipe->content_buffer + pipe->content_length,
                               pipe->content_buffer_size - pipe->content_length,
                               "\x1b[38;5;%dm", fg_color);
        if (written > 0) {
            pipe->content_length += written;
        }
    } else if (pipe->color_depth == 1 && fg_color >= 0 && fg_color < 8) {
        // Basic 8 color mode
        int written = snprintf(pipe->content_buffer + pipe->content_length,
                               pipe->content_buffer_size - pipe->content_length,
                               "\x1b[%dm", 30 + fg_color);
        if (written > 0) {
            pipe->content_length += written;
        }
    }

    pipe->current_fg_color = fg_color;
    pipe->current_bg_color = bg_color;
}

/**
 * @brief Append text to the render pipeline buffer.
 *
 * Appends raw text to the content buffer, growing the buffer
 * as needed to accommodate the new content.
 *
 * @param pipe The render pipeline.
 * @param text The text to append.
 * @param length The length of the text in bytes.
 * @return LLE_SUCCESS on success, LLE_ERROR_OUT_OF_MEMORY on allocation
 * failure.
 */
static lle_result_t lle_render_pipeline_append_text(lle_render_pipeline_t *pipe,
                                                    const char *text,
                                                    size_t length) {

    if (!text || length == 0) {
        return LLE_SUCCESS;
    }

    // Ensure buffer capacity
    size_t needed = pipe->content_length + length + 1;
    if (needed >= pipe->content_buffer_size) {
        size_t new_size = pipe->content_buffer_size;
        while (new_size < needed) {
            new_size *= 2;
        }
        char *new_buffer = realloc(pipe->content_buffer, new_size);
        if (!new_buffer) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                             "adaptive display controller allocation failed");
        }
        pipe->content_buffer = new_buffer;
        pipe->content_buffer_size = new_size;
    }

    memcpy(pipe->content_buffer + pipe->content_length, text, length);
    pipe->content_length += length;
    pipe->content_buffer[pipe->content_length] = '\0';

    return LLE_SUCCESS;
}

/**
 * @brief Reset all formatting attributes to terminal defaults.
 *
 * Emits the ANSI reset sequence and clears the pipeline's
 * internal formatting state.
 *
 * @param pipe The render pipeline.
 */
static void lle_render_pipeline_reset_formatting(lle_render_pipeline_t *pipe) {
    if (!pipe->color_enabled) {
        return;
    }

    const char *reset_seq = "\x1b[0m";
    lle_render_pipeline_append_text(pipe, reset_seq, strlen(reset_seq));
    pipe->current_fg_color = -1;
    pipe->current_bg_color = -1;
    pipe->bold_active = false;
    pipe->italic_active = false;
    pipe->underline_active = false;
}

/**
 * @brief Render a prompt string with color formatting.
 *
 * Clears the content buffer and renders the prompt with
 * green foreground color if colors are enabled.
 *
 * @param pipe The render pipeline.
 * @param prompt The prompt string to render.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t
lle_render_pipeline_render_prompt(lle_render_pipeline_t *pipe,
                                  const char *prompt) {

    pipe->content_length = 0;

    // Apply prompt color (green) if colors enabled
    if (pipe->color_enabled && pipe->color_depth > 0) {
        lle_render_pipeline_append_color(pipe, 2, -1); // Green foreground
    }

    lle_result_t result =
        lle_render_pipeline_append_text(pipe, prompt, strlen(prompt));
    if (result != LLE_SUCCESS) {
        return result;
    }

    lle_render_pipeline_reset_formatting(pipe);

    return LLE_SUCCESS;
}

/* ============================================================================
 * ENHANCED INPUT PROCESSOR IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create an enhanced input processor for non-TTY input handling.
 *
 * Allocates and configures an input processor based on terminal
 * detection results. Configures echo, line buffering, and UTF-8
 * support appropriately.
 *
 * @param processor Output pointer to receive the created processor.
 * @param detection Terminal detection results for configuration.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_enhanced_input_processor_create(
    lle_enhanced_input_processor_t **processor,
    const lle_terminal_detection_result_t *detection) {

    lle_enhanced_input_processor_t *proc =
        calloc(1, sizeof(lle_enhanced_input_processor_t));
    if (!proc) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    // Initialize input buffer
    proc->buffer_size = 4096;
    proc->input_buffer = malloc(proc->buffer_size);
    if (!proc->input_buffer) {
        free(proc);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    // Configure based on detection
    proc->echo_enabled = detection->stdout_is_tty;
    proc->line_buffered = !detection->stdin_is_tty; // Cooked mode for non-TTY
    proc->utf8_mode = detection->supports_unicode;
    proc->handle_ctrl_sequences = true;
    proc->handle_escape_sequences = detection->stdin_is_tty;

    *processor = proc;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy an enhanced input processor and free its resources.
 *
 * @param processor The input processor to destroy, or NULL for no-op.
 */
static void lle_enhanced_input_processor_destroy(
    lle_enhanced_input_processor_t *processor) {

    if (!processor) {
        return;
    }

    free(processor->input_buffer);
    free(processor);
}

/**
 * @brief Read a line of input using the enhanced input processor.
 *
 * Reads a line from stdin using cooked mode (fgets). Removes
 * trailing newlines and updates processing statistics.
 *
 * @param processor The input processor.
 * @param line Output pointer to receive the input buffer (not duplicated).
 * @param length Output pointer to receive the line length.
 * @return LLE_SUCCESS on success, or an error code on failure/EOF.
 */
static lle_result_t lle_enhanced_input_processor_read_line(
    lle_enhanced_input_processor_t *processor, char **line, size_t *length) {

    processor->buffer_used = 0;

    // Read line using standard fgets (cooked mode)
    if (!fgets(processor->input_buffer, processor->buffer_size, stdin)) {
        if (feof(stdin)) {
            return LLE_FAULT(
                LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                "adaptive display controller allocation failed"); // EOF
        }
        return LLE_ERROR_INPUT_PARSING;
    }

    processor->buffer_used = strlen(processor->input_buffer);

    // Remove trailing newline
    if (processor->buffer_used > 0 &&
        processor->input_buffer[processor->buffer_used - 1] == '\n') {
        processor->input_buffer[processor->buffer_used - 1] = '\0';
        processor->buffer_used--;
    }

    processor->lines_processed++;
    processor->bytes_processed += processor->buffer_used;

    *line = processor->input_buffer;
    *length = processor->buffer_used;

    return LLE_SUCCESS;
}

/* ============================================================================
 * DISPLAY CONTENT GENERATOR IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Format a prompt string for display output.
 *
 * Applies color formatting to the prompt if colors are supported.
 * Uses green foreground color for prompts.
 *
 * @param gen The content generator.
 * @param prompt The raw prompt string.
 * @return Formatted prompt string, or NULL on memory failure.
 */
static char *
lle_content_generator_format_prompt(lle_display_content_generator_t *gen,
                                    const char *prompt) {

    size_t prompt_len = strlen(prompt);
    size_t needed = prompt_len + 64; // Extra for formatting

    if (needed > gen->content_capacity) {
        char *new_content = realloc(gen->generated_content, needed);
        if (!new_content) {
            return NULL;
        }
        gen->generated_content = new_content;
        gen->content_capacity = needed;
    }

    if (gen->supports_colors) {
        snprintf(gen->generated_content, gen->content_capacity,
                 "\x1b[32m%s\x1b[0m", prompt);
    } else {
        strncpy(gen->generated_content, prompt, gen->content_capacity - 1);
        gen->generated_content[gen->content_capacity - 1] = '\0';
    }

    gen->generations_completed++;
    return gen->generated_content;
}

/**
 * @brief Format a content line for display output.
 *
 * Copies the line to the generator's internal buffer for output.
 * Does not apply additional formatting to regular content lines.
 *
 * @param gen The content generator.
 * @param line The raw line content.
 * @return Formatted line string, or NULL on memory failure.
 */
static char *
lle_content_generator_format_line(lle_display_content_generator_t *gen,
                                  const char *line) {

    size_t line_len = strlen(line);
    size_t needed = line_len + 1;

    if (needed > gen->content_capacity) {
        char *new_content = realloc(gen->generated_content, needed);
        if (!new_content) {
            return NULL;
        }
        gen->generated_content = new_content;
        gen->content_capacity = needed;
    }

    strncpy(gen->generated_content, line, gen->content_capacity - 1);
    gen->generated_content[gen->content_capacity - 1] = '\0';

    return gen->generated_content;
}

/**
 * @brief Format a completion suggestion for display output.
 *
 * Applies gray color formatting to completion suggestions if
 * colors are supported, making them visually distinct.
 *
 * @param gen The content generator.
 * @param completion The completion suggestion text.
 * @return Formatted completion string, or NULL on memory failure.
 */
static char *
lle_content_generator_format_completion(lle_display_content_generator_t *gen,
                                        const char *completion) {

    size_t completion_len = strlen(completion);
    size_t needed = completion_len + 64;

    if (needed > gen->content_capacity) {
        char *new_content = realloc(gen->generated_content, needed);
        if (!new_content) {
            return NULL;
        }
        gen->generated_content = new_content;
        gen->content_capacity = needed;
    }

    if (gen->supports_colors) {
        // Gray color for completion suggestion
        snprintf(gen->generated_content, gen->content_capacity,
                 "\x1b[90m%s\x1b[0m", completion);
    } else {
        strncpy(gen->generated_content, completion, gen->content_capacity - 1);
        gen->generated_content[gen->content_capacity - 1] = '\0';
    }

    return gen->generated_content;
}

/**
 * @brief Create a display content generator.
 *
 * Allocates and initializes a content generator with the specified
 * capabilities. Configures formatting functions for prompts, lines,
 * and completions.
 *
 * @param generator Output pointer to receive the created generator.
 * @param supports_colors Whether color output is supported.
 * @param supports_cursor Whether cursor positioning is supported.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_display_content_generator_create(
    lle_display_content_generator_t **generator, bool supports_colors,
    bool supports_cursor) {

    lle_display_content_generator_t *gen =
        calloc(1, sizeof(lle_display_content_generator_t));
    if (!gen) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    gen->supports_colors = supports_colors;
    gen->supports_cursor = supports_cursor;
    gen->supports_unicode = true; // Assume UTF-8 for enhanced mode

    // Allocate initial content buffer
    gen->content_capacity = 4096;
    gen->generated_content = malloc(gen->content_capacity);
    if (!gen->generated_content) {
        free(gen);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    // Assign formatting functions
    gen->format_prompt = lle_content_generator_format_prompt;
    gen->format_line = lle_content_generator_format_line;
    gen->format_completion = lle_content_generator_format_completion;

    *generator = gen;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a display content generator and free its resources.
 *
 * @param generator The content generator to destroy, or NULL for no-op.
 */
void lle_display_content_generator_destroy(
    lle_display_content_generator_t *generator) {

    if (!generator) {
        return;
    }

    free(generator->generated_content);
    free(generator);
}

/* ============================================================================
 * DISPLAY CLIENT CONTROLLER API
 * ============================================================================
 */

/**
 * @brief Initialize the display client controller for Enhanced mode.
 *
 * Creates and configures a display client controller for use in
 * environments where stdin is not a TTY but stdout has display
 * capabilities (e.g., editor terminals, AI assistants).
 *
 * @param context The adaptive context to initialize the controller in.
 * @param memory_pool Memory pool for allocations.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_initialize_display_client_controller(lle_adaptive_context_t *context,
                                         lush_memory_pool_t *memory_pool) {

    lle_display_client_controller_t *client =
        calloc(1, sizeof(lle_display_client_controller_t));
    if (!client) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    // Configure capabilities based on detection results
    client->supports_color_output = context->detection_result->supports_colors;
    client->supports_cursor_positioning =
        context->detection_result->supports_cursor_positioning;
    client->supports_clear_operations =
        context->detection_result->stdout_is_tty;

    // Get terminal dimensions (default to 80x24 if unavailable)
    client->terminal_width = 80;
    client->terminal_height = 24;

    // Determine color depth
    int color_depth = 0;
    if (context->detection_result->supports_truecolor) {
        color_depth = 3;
    } else if (context->detection_result->supports_256_colors) {
        color_depth = 2;
    } else if (context->detection_result->supports_colors) {
        color_depth = 1;
    }

    // Initialize content generation pipeline
    lle_result_t result = lle_display_content_generator_create(
        &client->content_generator, client->supports_color_output,
        client->supports_cursor_positioning);
    if (result != LLE_SUCCESS) {
        free(client);
        return result;
    }

    // Initialize enhanced input processing
    result = lle_enhanced_input_processor_create(&client->input_processor,
                                                 context->detection_result);
    if (result != LLE_SUCCESS) {
        lle_display_content_generator_destroy(client->content_generator);
        free(client);
        return result;
    }

    // Initialize render pipeline
    result = lle_render_pipeline_create(
        &client->render_pipeline, client->supports_color_output,
        client->supports_cursor_positioning, color_depth);
    if (result != LLE_SUCCESS) {
        lle_enhanced_input_processor_destroy(client->input_processor);
        lle_display_content_generator_destroy(client->content_generator);
        free(client);
        return result;
    }

    client->memory_pool = memory_pool;
    context->controller.display_client = client;

    return LLE_SUCCESS;
}

/**
 * @brief Clean up and destroy a display client controller.
 *
 * Releases all resources associated with the display client controller
 * including render pipeline, input processor, and content generator.
 *
 * @param client The display client controller to destroy, or NULL for no-op.
 */
void lle_cleanup_display_client_controller(
    lle_display_client_controller_t *client) {

    if (!client) {
        return;
    }

    lle_render_pipeline_destroy(client->render_pipeline);
    lle_enhanced_input_processor_destroy(client->input_processor);
    lle_display_content_generator_destroy(client->content_generator);
    free(client->current_prompt);
    free(client->current_line);
    free(client);
}

/**
 * @brief Read a line of input using the display client controller.
 *
 * Renders the prompt with formatting, writes it to stdout, and reads
 * a line of input from stdin using cooked mode.
 *
 * @param client The display client controller.
 * @param prompt The prompt string to display.
 * @param line Output pointer to receive the allocated input line.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_display_client_read_line(lle_display_client_controller_t *client,
                             const char *prompt, char **line) {

    if (!client || !prompt || !line) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Render and display prompt
    lle_result_t result =
        lle_render_pipeline_render_prompt(client->render_pipeline, prompt);
    if (result != LLE_SUCCESS) {
        client->errors_encountered++;
        return result;
    }

    // Write prompt to stdout
    fwrite(client->render_pipeline->content_buffer, 1,
           client->render_pipeline->content_length, stdout);
    fflush(stdout);

    // Read line from input
    size_t length = 0;
    char *input_line = NULL;
    result = lle_enhanced_input_processor_read_line(client->input_processor,
                                                    &input_line, &length);
    if (result != LLE_SUCCESS) {
        client->errors_encountered++;
        return result;
    }

    // Duplicate line for caller
    *line = strdup(input_line);
    if (!*line) {
        client->errors_encountered++;
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive display controller allocation failed");
    }

    client->lines_read++;
    return LLE_SUCCESS;
}

/**
 * @brief Update the display client's visual output.
 *
 * For display client mode, updates are minimal as content is
 * rendered on-demand during read_line operations.
 *
 * @param client The display client controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_display_client_update_display(lle_display_client_controller_t *client) {

    if (!client) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // For display client mode, display updates are minimal
    // Content is rendered on-demand during read_line operations

    client->displays_updated++;
    return LLE_SUCCESS;
}

/**
 * @brief Handle terminal resize events in display client mode.
 *
 * Updates the stored terminal dimensions when the terminal is resized.
 *
 * @param client The display client controller.
 * @param new_width The new terminal width in columns.
 * @param new_height The new terminal height in rows.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_display_client_handle_resize(lle_display_client_controller_t *client,
                                 int new_width, int new_height) {

    if (!client) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    client->terminal_width = new_width;
    client->terminal_height = new_height;

    return LLE_SUCCESS;
}

/**
 * @brief Get display client controller statistics.
 *
 * Retrieves performance and usage statistics from the display client
 * controller.
 *
 * @param client The display client controller.
 * @param lines_read Output pointer for lines read count, or NULL to skip.
 * @param displays_updated Output pointer for display update count, or NULL to
 * skip.
 * @param errors Output pointer for error count, or NULL to skip.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t
lle_display_client_get_stats(const lle_display_client_controller_t *client,
                             uint64_t *lines_read, uint64_t *displays_updated,
                             uint64_t *errors) {

    if (!client) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (lines_read) {
        *lines_read = client->lines_read;
    }
    if (displays_updated) {
        *displays_updated = client->displays_updated;
    }
    if (errors) {
        *errors = client->errors_encountered;
    }

    return LLE_SUCCESS;
}
