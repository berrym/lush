/**
 * @file terminal_abstraction.h
 * @brief LLE Terminal State Abstraction Layer - Type Definitions
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 02 - Terminal Abstraction Complete Specification
 * Version: 1.0.0
 *
 * This header contains ALL type definitions and function declarations for the
 * LLE terminal abstraction system. NO implementations are included here.
 *
 * Critical Design Principles:
 * 1. NEVER query terminal state during operation - internal model is
 * authoritative
 * 2. NEVER send direct escape sequences - all output through Lush display
 * 3. NEVER assume terminal cursor position - calculate from buffer state
 * 4. NEVER track terminal state changes - generate complete display content
 * 5. Internal buffer state is authoritative - single source of truth
 *
 * Architecture: Research-validated design following proven patterns from
 * JLine, ZSH ZLE, Fish, and Rustyline.
 */

#ifndef LLE_TERMINAL_ABSTRACTION_H
#define LLE_TERMINAL_ABSTRACTION_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Include LLE dependencies */
#include "lle/arena.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

/* Prevent conflicts with forward declarations in other headers */
#ifdef lle_input_event_t
#undef lle_input_event_t
#endif
#ifdef lle_terminal_abstraction_t
#undef lle_terminal_abstraction_t
#endif

/* ============================================================================
 * FORWARD DECLARATIONS FOR LUSH INTEGRATION
 * ============================================================================
 */

/* Lush display system types (defined in Lush, used by LLE) */
typedef struct lush_display_context lush_display_context_t;
typedef struct lush_display_layer lush_display_layer_t;
typedef struct lush_layer_content lush_layer_content_t;
typedef struct lush_display_line lush_display_line_t;
typedef int lush_result_t;

/* Lush display layer priorities */
#define LUSH_LAYER_PRIORITY_EDITING 100

/* Lush result codes */
#define LUSH_SUCCESS 0

/* Forward declarations for input parsing components (from input_parsing.h) */
typedef struct lle_sequence_parser lle_sequence_parser_t;
typedef struct lle_key_detector lle_key_detector_t;

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/**
 * @brief Terminal type enumeration for optimization
 *
 * Spec Reference: Section 4 - Terminal Capability Detection
 */
typedef enum {
    LLE_TERMINAL_UNKNOWN = 0,
    LLE_TERMINAL_GENERIC,
    LLE_TERMINAL_XTERM,
    LLE_TERMINAL_RXVT,
    LLE_TERMINAL_KONSOLE,
    LLE_TERMINAL_GNOME_TERMINAL,
    LLE_TERMINAL_SCREEN,
    LLE_TERMINAL_TMUX,
    LLE_TERMINAL_LINUX_CONSOLE,
    LLE_TERMINAL_DARWIN_TERMINAL,
    LLE_TERMINAL_ITERM2,
    LLE_TERMINAL_ALACRITTY,
    LLE_TERMINAL_KITTY
} lle_terminal_type_t;

/**
 * @brief Input event types
 *
 * Spec Reference: Section 7 - Input Event Processing
 */
typedef enum {
    LLE_INPUT_TYPE_CHARACTER = 0,
    LLE_INPUT_TYPE_SPECIAL_KEY,
    LLE_INPUT_TYPE_WINDOW_RESIZE,
    LLE_INPUT_TYPE_SIGNAL,
    LLE_INPUT_TYPE_TIMEOUT,
    LLE_INPUT_TYPE_ERROR,
    LLE_INPUT_TYPE_EOF
} lle_input_type_t;

/**
 * @brief Special key codes
 */
typedef enum {
    LLE_KEY_UNKNOWN = 0,
    LLE_KEY_UP,
    LLE_KEY_DOWN,
    LLE_KEY_LEFT,
    LLE_KEY_RIGHT,
    LLE_KEY_HOME,
    LLE_KEY_END,
    LLE_KEY_PAGE_UP,
    LLE_KEY_PAGE_DOWN,
    LLE_KEY_INSERT,
    LLE_KEY_DELETE,
    LLE_KEY_BACKSPACE,
    LLE_KEY_TAB,
    LLE_KEY_ENTER,
    LLE_KEY_ESCAPE,
    LLE_KEY_F1,
    LLE_KEY_F2,
    LLE_KEY_F3,
    LLE_KEY_F4,
    LLE_KEY_F5,
    LLE_KEY_F6,
    LLE_KEY_F7,
    LLE_KEY_F8,
    LLE_KEY_F9,
    LLE_KEY_F10,
    LLE_KEY_F11,
    LLE_KEY_F12
} lle_special_key_t;

/**
 * @brief Key modifier flags
 */
typedef enum {
    LLE_MOD_NONE = 0,
    LLE_MOD_SHIFT = (1 << 0),
    LLE_MOD_ALT = (1 << 1),
    LLE_MOD_CTRL = (1 << 2),
    LLE_MOD_META = (1 << 3)
} lle_key_modifier_t;

/**
 * @brief Terminal optimization flags
 */
typedef enum {
    LLE_OPT_NONE = 0,
    LLE_OPT_FAST_CURSOR = (1 << 0),
    LLE_OPT_BATCH_UPDATES = (1 << 1),
    LLE_OPT_INCREMENTAL_DRAW = (1 << 2),
    LLE_OPT_UNICODE_AWARE = (1 << 3)
} lle_optimization_flags_t;

/* ============================================================================
 * CORE STRUCTURES
 * ============================================================================
 */

/**
 * @brief Command buffer structure - authoritative text storage
 *
 * Spec Reference: Section 3.1 - Internal State Authority Model
 */
typedef struct lle_command_buffer {
    char *data;            /* Buffer content (UTF-8) */
    size_t length;         /* Current content length */
    size_t capacity;       /* Allocated buffer size */
    size_t allocated_size; /* Actual allocation size */

    /* Buffer change tracking for optimization */
    size_t last_change_offset; /* Last modification offset */
    size_t last_change_length; /* Last modification length */
    bool needs_full_refresh;   /* Requires complete display update */
} lle_command_buffer_t;

/**
 * @brief Line attributes for display styling
 */
typedef struct lle_line_attributes {
    uint32_t fg_color;   /* Foreground color (RGB or palette) */
    uint32_t bg_color;   /* Background color (RGB or palette) */
    uint16_t attributes; /* Bold, italic, underline, etc. */
    bool use_truecolor;  /* Use 24-bit color vs palette */
} lle_line_attributes_t;

/**
 * @brief Display line structure - terminal display content
 *
 * Spec Reference: Section 3.1 - Internal State Authority Model
 */
typedef struct lle_display_line {
    char *content;   /* Line content (UTF-8) */
    size_t length;   /* Content length */
    size_t capacity; /* Allocated capacity */

    /* Visual attributes for this line */
    lle_line_attributes_t attributes; /* Colors, styles, etc. */

    /* Cursor information if cursor is on this line */
    bool contains_cursor; /* True if cursor on this line */
    size_t cursor_column; /* Visual cursor column (if present) */
} lle_display_line_t;

/**
 * @brief Internal state structure - AUTHORITATIVE MODEL
 *
 * This is the single source of truth for all editing state.
 * NEVER query terminal - calculate everything from this state.
 *
 * Spec Reference: Section 3 - Internal State Authority Model
 */
typedef struct lle_internal_state {
    /* Command Buffer State - AUTHORITATIVE */
    lle_command_buffer_t *command_buffer; /* Command being edited */
    size_t cursor_position; /* Cursor position in buffer (logical) */
    size_t selection_start; /* Selection start (if any) */
    size_t selection_end;   /* Selection end (if any) */
    bool has_selection;     /* Selection active flag */

    /* Display State Model - What we believe terminal contains */
    lle_display_line_t *display_lines; /* Current display content */
    size_t display_line_count;         /* Number of display lines */
    size_t display_capacity;           /* Allocated display line capacity */

    /* Display Geometry State */
    size_t terminal_width;  /* Terminal columns */
    size_t terminal_height; /* Terminal rows */
    size_t prompt_width;    /* Prompt width in columns */
    size_t display_offset;  /* Horizontal scroll offset */
    size_t vertical_offset; /* Vertical scroll offset */

    /* Edit State Tracking */
    bool buffer_modified;        /* Buffer changed since last display */
    uint64_t modification_count; /* Number of modifications */
    uint64_t last_update_time;   /* Last update timestamp */

    /* CRITICAL: NO terminal cursor position tracking */
    /* Cursor position always calculated from buffer state + display geometry */
} lle_internal_state_t;

/**
 * @brief Terminal capabilities detected from environment/terminfo
 *
 * ONE-TIME DETECTION at startup - NO runtime terminal queries
 *
 * Spec Reference: Section 4 - Terminal Capability Detection
 */
typedef struct lle_terminal_capabilities {
    /* Basic terminal information */
    bool is_tty;            /* Running in TTY */
    char *terminal_type;    /* TERM environment variable */
    char *terminal_program; /* Terminal program name */

    /* Display capabilities (from terminfo/environment) */
    bool supports_ansi_colors;    /* Basic 8/16 color support */
    bool supports_256_colors;     /* 256 color support */
    bool supports_truecolor;      /* 24-bit color support */
    uint8_t detected_color_depth; /* Color depth (4, 8, or 24) */

    /* Text attributes (from terminfo) */
    bool supports_bold;
    bool supports_italic;
    bool supports_underline;
    bool supports_strikethrough;
    bool supports_reverse;
    bool supports_dim;

    /* Advanced features (from environment/terminfo) */
    bool supports_mouse_reporting;
    bool supports_bracketed_paste;
    bool supports_focus_events;
    bool supports_synchronized_output;
    bool supports_unicode;

    /* Terminal geometry */
    size_t terminal_width;  /* Columns */
    size_t terminal_height; /* Rows */

    /* Performance characteristics */
    uint32_t estimated_latency_ms; /* Estimated terminal latency */
    bool supports_fast_updates;    /* Can handle rapid updates */

    /* Terminal-specific optimizations */
    lle_terminal_type_t terminal_type_enum;
    lle_optimization_flags_t optimizations;
} lle_terminal_capabilities_t;

/**
 * @brief Display content structure - what gets sent to Lush
 *
 * Spec Reference: Section 5 - Display Content Generation
 */
typedef struct lle_display_content {
    /* Complete display lines */
    lle_display_line_t *lines;
    size_t line_count;
    size_t line_capacity; /* Total allocated lines for proper cleanup */

    /* Cursor position information */
    size_t cursor_line;
    size_t cursor_column;
    bool cursor_visible;

    /* Content metadata */
    uint64_t generation_time;
    bool is_complete_refresh;
    uint32_t content_version;
} lle_display_content_t;

/**
 * @brief Display generation parameters
 */
typedef struct lle_generation_params {
    bool force_full_refresh;
    bool optimize_for_speed;
    size_t max_display_lines;
} lle_generation_params_t;

/**
 * @brief Display generator - converts internal state to display content
 *
 * Spec Reference: Section 5 - Display Content Generation
 */
typedef struct lle_display_generator {
    lle_terminal_capabilities_t *capabilities;
    lle_internal_state_t *internal_state;

    /* Content generation state */
    lle_display_content_t *current_content;
    lle_display_content_t *previous_content;

    /* Generation parameters */
    lle_generation_params_t params;
} lle_display_generator_t;

/**
 * @brief LLE layer configuration for Lush
 */
typedef struct lle_layer_config {
    const char *layer_name;
    int layer_priority;
    bool supports_transparency;
    bool requires_full_refresh;
    uint8_t color_capabilities;
} lle_layer_config_t;

/**
 * @brief Lush display client - LLE integration with Lush display
 *
 * LLE NEVER directly controls terminal - always through Lush
 *
 * Spec Reference: Section 6 - Lush Display Layer Integration
 */
typedef struct lle_lush_display_client {
    /* Lush display system integration */
    lush_display_context_t *display_context;
    lush_display_layer_t *lle_display_layer;

    /* LLE-specific layer configuration */
    lle_layer_config_t layer_config;

    /* Terminal capabilities for display optimization */
    lle_terminal_capabilities_t *capabilities;

    /* Display submission tracking */
    uint64_t last_submission_time;
    uint64_t submission_count;
} lle_lush_display_client_t;

/**
 * @brief Input event structure
 *
 * Spec Reference: Section 7 - Input Event Processing
 *
 * Note: Struct name matches forward declaration in memory_management.h
 */
typedef struct lle_input_event_t {
    lle_input_type_t type;
    uint64_t timestamp;
    uint32_t sequence_number;

    union {
        /* Character input */
        struct {
            uint32_t codepoint; /* Unicode codepoint */
            char utf8_bytes[8]; /* UTF-8 representation */
            uint8_t byte_count; /* Number of UTF-8 bytes */
        } character;

        /* Special key input */
        struct {
            lle_special_key_t key; /* Special key enum (arrows, F-keys, etc.) */
            lle_key_modifier_t
                modifiers;    /* Modifier flags (Ctrl, Alt, Shift) */
            uint32_t keycode; /* Raw keycode for letters (e.g., 'A'=65 with Ctrl
                                 modifier) */
        } special_key;

        /* Window resize event */
        struct {
            size_t new_width;
            size_t new_height;
        } resize;

        /* Signal event */
        struct {
            int signal_number;
        } signal;

        /* Error event */
        struct {
            lle_result_t error_code;
            char error_message[256];
        } error;
    } data;
} lle_input_event_t;

/**
 * @brief Input processor structure
 *
 * Spec Reference: Section 7 - Input Event Processing
 */
typedef struct lle_input_processor {
    lle_terminal_capabilities_t *capabilities;
    struct lle_unix_interface *unix_interface;

    /* Input processing state */
    uint64_t events_processed;
    uint32_t next_sequence_number;

    /* Performance tracking */
    uint64_t total_processing_time_us;

    /* Event arena for per-event allocations (fixes memory leak).
     * Reset after each event is consumed to reclaim memory. */
    lle_arena_t *event_arena;
} lle_input_processor_t;

/**
 * @brief Unix terminal interface - minimal abstraction
 *
 * Spec Reference: Section 8 - Unix Terminal Interface
 */
typedef struct lle_unix_interface {
    int terminal_fd;                 /* Terminal file descriptor */
    struct termios original_termios; /* Original terminal settings */
    struct termios raw_termios;      /* Raw mode settings */
    bool raw_mode_active;            /* Currently in raw mode */

    /* Window size tracking */
    size_t current_width;
    size_t current_height;
    bool size_changed;

    /* Signal handling integration */
    bool sigwinch_received;

    /* Escape sequence parsing (Spec 06 integration) */
    lle_sequence_parser_t *sequence_parser; /* Comprehensive sequence parser */
    lle_key_detector_t *key_detector;       /* Key sequence detector */
    lle_terminal_capabilities_t
        *capabilities;              /* Terminal capabilities for parser */
    lle_memory_pool_t *memory_pool; /* Memory pool for parser */

    /* Error state */
    lle_result_t last_error;
} lle_unix_interface_t;

/**
 * @brief Main terminal abstraction structure
 *
 * Spec Reference: Section 2 - Architecture Overview
 *
 * Note: Struct name matches forward declaration in performance.h
 */
typedef struct lle_terminal_abstraction_t {
    /* Internal State Authority Model - CORE COMPONENT */
    lle_internal_state_t *internal_state;

    /* Display Content Generation System */
    lle_display_generator_t *display_generator;

    /* Lush Display Layer Integration */
    lle_lush_display_client_t *display_client;

    /* Terminal Capability Model (detected once at startup) */
    lle_terminal_capabilities_t *capabilities;

    /* Input Processing System */
    lle_input_processor_t *input_processor;

    /* Unix Terminal Interface (minimal, abstracted) */
    lle_unix_interface_t *unix_interface;

    /* Error handling context */
    lle_error_context_t *error_ctx;

    /* Performance monitoring */
    lle_performance_monitor_t *perf_monitor;
} lle_terminal_abstraction_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ============================================================================
 */

/* Main Terminal Abstraction Lifecycle */
lle_result_t
lle_terminal_abstraction_init(lle_terminal_abstraction_t **abstraction,
                              lush_display_context_t *lush_display);
void lle_terminal_abstraction_destroy(lle_terminal_abstraction_t *abstraction);

/* Internal State Operations */
lle_result_t lle_internal_state_init(lle_internal_state_t **state,
                                     lle_terminal_capabilities_t *caps);
void lle_internal_state_destroy(lle_internal_state_t *state);
lle_result_t lle_internal_state_insert_text(lle_internal_state_t *state,
                                            size_t position, const char *text,
                                            size_t text_length);
lle_result_t lle_internal_state_delete_text(lle_internal_state_t *state,
                                            size_t position, size_t length);
lle_result_t lle_internal_state_calculate_cursor_display_position(
    lle_internal_state_t *state, size_t *display_line, size_t *display_column);
lle_result_t lle_internal_state_update_geometry(lle_internal_state_t *state,
                                                size_t width, size_t height);

/* Command Buffer Operations */
lle_result_t lle_command_buffer_init(lle_command_buffer_t **buffer,
                                     size_t initial_capacity);
void lle_command_buffer_destroy(lle_command_buffer_t *buffer);
lle_result_t lle_command_buffer_insert(lle_command_buffer_t *buffer,
                                       size_t position, const char *text,
                                       size_t length);
lle_result_t lle_command_buffer_delete(lle_command_buffer_t *buffer,
                                       size_t position, size_t length);
void lle_command_buffer_clear(lle_command_buffer_t *buffer);

/* Terminal Capability Detection */
lle_result_t
lle_capabilities_detect_environment(lle_terminal_capabilities_t **caps,
                                    lle_unix_interface_t *unix_iface);
void lle_capabilities_destroy(lle_terminal_capabilities_t *caps);
lle_result_t lle_capabilities_update_geometry(lle_terminal_capabilities_t *caps,
                                              size_t width, size_t height);

/* Display Content Generation */
lle_result_t lle_display_generator_init(lle_display_generator_t **generator,
                                        lle_terminal_capabilities_t *caps,
                                        lle_internal_state_t *state);
void lle_display_generator_destroy(lle_display_generator_t *generator);
lle_result_t
lle_display_generator_generate_content(lle_display_generator_t *generator,
                                       lle_display_content_t **content);

/* Display Content Operations */
lle_result_t lle_display_content_create(lle_display_content_t **content,
                                        size_t line_capacity);
void lle_display_content_destroy(lle_display_content_t *content);

/* Lush Display Client Operations */
lle_result_t
lle_lush_display_client_init(lle_lush_display_client_t **client,
                             lush_display_context_t *display_context,
                             lle_terminal_capabilities_t *capabilities);
void lle_lush_display_client_destroy(lle_lush_display_client_t *client);
lle_result_t
lle_lush_display_client_submit_content(lle_lush_display_client_t *client,
                                       lle_display_content_t *content);

/* Input Event Processing */
lle_result_t lle_input_processor_init(lle_input_processor_t **processor,
                                      lle_terminal_capabilities_t *caps,
                                      lle_unix_interface_t *unix_iface);
void lle_input_processor_destroy(lle_input_processor_t *processor);
lle_result_t lle_input_processor_process_event(lle_input_processor_t *processor,
                                               lle_input_event_t *event);
lle_result_t
lle_input_processor_read_next_event(lle_input_processor_t *processor,
                                    lle_input_event_t **event,
                                    uint32_t timeout_ms);

/* Unix Terminal Interface */
lle_result_t lle_unix_interface_init(lle_unix_interface_t **interface);
lle_result_t lle_unix_interface_init_sequence_parser(
    lle_unix_interface_t *interface, lle_terminal_capabilities_t *capabilities,
    lle_memory_pool_t *memory_pool);
void lle_unix_interface_destroy(lle_unix_interface_t *interface);
lle_result_t lle_unix_interface_enter_raw_mode(lle_unix_interface_t *interface);
lle_result_t lle_unix_interface_exit_raw_mode(lle_unix_interface_t *interface);
lle_result_t lle_unix_interface_read_event(lle_unix_interface_t *interface,
                                           lle_input_event_t *event,
                                           uint32_t timeout_ms);
lle_result_t lle_unix_interface_get_window_size(lle_unix_interface_t *interface,
                                                size_t *width, size_t *height);

/* Utility Functions */
uint64_t lle_get_current_time_microseconds(void);
lle_result_t lle_convert_lush_error(lush_result_t lush_error);

#endif /* LLE_TERMINAL_ABSTRACTION_H */
