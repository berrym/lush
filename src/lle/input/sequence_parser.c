/**
 * @file sequence_parser.c
 * @brief Terminal Escape Sequence Parser
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Comprehensive state machine-based parser for terminal escape sequences.
 * Handles CSI, OSC, DCS, and all other terminal sequence types with
 * timeout-based ambiguity resolution and robust error recovery.
 *
 * Spec 06: Input Parsing - Phase 3
 */

#include "lle/error_handling.h"
#include "lle/input_parsing.h"
#include <string.h>
#include <time.h>

/** Check if character is a control character */
#define IS_CONTROL_CHAR(c) ((c) < 0x20 || (c) == 0x7F)
/** Check if character is a CSI parameter byte */
#define IS_CSI_PARAMETER(c) (((c) >= '0' && (c) <= '9') || (c) == ';')
/** Check if character is a CSI intermediate byte */
#define IS_CSI_INTERMEDIATE(c) ((c) >= 0x20 && (c) <= 0x2F)
/** Check if character is a CSI final byte */
#define IS_CSI_FINAL(c) ((c) >= 0x40 && (c) <= 0x7E)

/**
 * @brief Get current time in microseconds
 *
 * @return Current monotonic time in microseconds
 */
static uint64_t get_current_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/**
 * @brief Initialize a sequence parser
 *
 * @param parser Output pointer for created parser
 * @param terminal_caps Terminal capabilities for feature detection
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_sequence_parser_init(lle_sequence_parser_t **parser,
                         lle_terminal_capabilities_t *terminal_caps,
                         lle_memory_pool_t *memory_pool) {
    if (!parser || !terminal_caps || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate parser structure
    lle_sequence_parser_t *new_parser =
        lle_pool_alloc(sizeof(lle_sequence_parser_t));
    if (!new_parser) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize all fields
    memset(new_parser, 0, sizeof(lle_sequence_parser_t));

    new_parser->type = LLE_SEQ_TYPE_UNKNOWN;
    new_parser->state = LLE_PARSER_STATE_NORMAL;
    new_parser->buffer_pos = 0;
    new_parser->buffer_capacity = LLE_MAX_SEQUENCE_LENGTH;
    new_parser->sequence_start_time = 0;
    new_parser->terminal_caps = terminal_caps;
    new_parser->memory_pool = memory_pool;

    new_parser->parameter_count = 0;
    new_parser->intermediate_count = 0;
    new_parser->final_char = 0;

    new_parser->malformed_sequences = 0;
    new_parser->timeout_sequences = 0;
    new_parser->last_error_time = 0;

    *parser = new_parser;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a sequence parser
 *
 * @param parser Parser to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_sequence_parser_destroy(lle_sequence_parser_t *parser) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_pool_free(parser);
    return LLE_SUCCESS;
}

/**
 * @brief Reset parser state to normal
 *
 * @param parser Parser to reset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_sequence_parser_reset_state(lle_sequence_parser_t *parser) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    parser->type = LLE_SEQ_TYPE_UNKNOWN;
    parser->state = LLE_PARSER_STATE_NORMAL;
    parser->buffer_pos = 0;
    parser->sequence_start_time = 0;
    parser->parameter_count = 0;
    parser->intermediate_count = 0;
    parser->final_char = 0;

    memset(parser->buffer, 0, sizeof(parser->buffer));
    memset(parser->parameters, 0, sizeof(parser->parameters));
    memset(parser->intermediate_chars, 0, sizeof(parser->intermediate_chars));

    return LLE_SUCCESS;
}

/**
 * @brief Check if sequence has timed out
 *
 * @param parser Parser to check
 * @return true if sequence has timed out, false otherwise
 */
static bool has_sequence_timed_out(lle_sequence_parser_t *parser) {
    if (parser->sequence_start_time == 0) {
        return false;
    }

    uint64_t current_time = get_current_time_us();
    uint64_t elapsed = current_time - parser->sequence_start_time;

    return elapsed > LLE_MAX_SEQUENCE_TIMEOUT_US;
}

/**
 * @brief Parse CSI parameters from buffer
 *
 * CSI format: ESC [ [parameters] [intermediate bytes] final_byte
 * Parameters are semicolon-separated decimal numbers.
 *
 * @param parser Parser instance
 * @param params_start Start of parameter string
 * @param params_len Length of parameter string
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t parse_csi_parameters(lle_sequence_parser_t *parser,
                                         const char *params_start,
                                         size_t params_len) {
    if (!parser || !params_start || params_len == 0) {
        return LLE_SUCCESS; /// No parameters is valid
    }

    parser->parameter_count = 0;
    uint32_t current_param = 0;
    bool has_current = false;

    for (size_t i = 0;
         i < params_len && parser->parameter_count < LLE_MAX_CSI_PARAMETERS;
         i++) {
        char c = params_start[i];

        if (c >= '0' && c <= '9') {
            current_param = current_param * 10 + (c - '0');
            has_current = true;
        } else if (c == ';') {
            /// Semicolon separates parameters
            parser->parameters[parser->parameter_count++] =
                has_current ? current_param : 0;
            current_param = 0;
            has_current = false;
        } else {
            /// Invalid character in parameters
            break;
        }
    }

    /// Add final parameter
    if (has_current && parser->parameter_count < LLE_MAX_CSI_PARAMETERS) {
        parser->parameters[parser->parameter_count++] = current_param;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Process a complete CSI sequence
 *
 * CSI sequences have the format: ESC [ [params] [intermediates] final
 * Examples:
 *   ESC[2J - Clear screen
 *   ESC[1;5H - Move cursor to row 1, col 5
 *   ESC[31m - Set foreground color to red
 *
 * @param parser Parser instance
 * @param parsed_input Output pointer for parsed result
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t process_csi_sequence(lle_sequence_parser_t *parser,
                                         lle_parsed_input_t **parsed_input) {
    if (!parser || !parsed_input) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// CSI sequence is complete - parse it
    /// Buffer contains: ESC [ [parameters] [intermediates] final

    /// Find where parameters end and intermediates begin
    size_t param_start = 2; /// Skip ESC [
    size_t param_end = param_start;
    size_t intermediate_start = param_start;

    /// Scan for parameters (digits and semicolons)
    while (param_end < parser->buffer_pos) {
        char c = parser->buffer[param_end];
        if (IS_CSI_PARAMETER(c)) {
            param_end++;
        } else {
            break;
        }
    }

    /// Parse parameters
    if (param_end > param_start) {
        parse_csi_parameters(parser, parser->buffer + param_start,
                             param_end - param_start);
    }

    /// Scan for intermediate bytes
    intermediate_start = param_end;
    while (intermediate_start < parser->buffer_pos - 1) { /// -1 for final byte
        char c = parser->buffer[intermediate_start];
        if (IS_CSI_INTERMEDIATE(c)) {
            if (parser->intermediate_count <
                sizeof(parser->intermediate_chars)) {
                parser->intermediate_chars[parser->intermediate_count++] = c;
            }
            intermediate_start++;
        } else {
            break;
        }
    }

    /// Final character
    parser->final_char = parser->buffer[parser->buffer_pos - 1];

    /// Create parsed input result
    lle_parsed_input_t *result = lle_pool_alloc(sizeof(lle_parsed_input_t));
    if (!result) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(result, 0, sizeof(lle_parsed_input_t));
    result->type = LLE_PARSED_INPUT_TYPE_SEQUENCE;
    result->handled = false;

    *parsed_input = result;
    return LLE_SUCCESS;
}

/**
 * @brief Process a control character
 *
 * @param parser Parser instance
 * @param c Control character to process
 * @param parsed_input Output pointer for parsed result
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t process_control_char(lle_sequence_parser_t *parser, char c,
                                         lle_parsed_input_t **parsed_input) {
    if (!parser || !parsed_input) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Store control character
    parser->buffer[0] = c;
    parser->buffer_pos = 1;
    parser->type = LLE_SEQ_TYPE_CONTROL_CHAR;

    /// Create parsed input result
    lle_parsed_input_t *result = lle_pool_alloc(sizeof(lle_parsed_input_t));
    if (!result) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(result, 0, sizeof(lle_parsed_input_t));
    result->type = LLE_PARSED_INPUT_TYPE_KEY;
    result->handled = false;

    *parsed_input = result;
    return LLE_SUCCESS;
}

/**
 * @brief Process data through the sequence parser
 *
 * This is the main parsing loop that implements the state machine.
 *
 * @param parser Parser instance
 * @param data Input data to process
 * @param data_len Length of input data
 * @param parsed_input Output pointer for parsed result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_sequence_parser_process_data(lle_sequence_parser_t *parser,
                                 const char *data, size_t data_len,
                                 lle_parsed_input_t **parsed_input) {
    if (!parser || !data || !parsed_input) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *parsed_input = NULL;

    for (size_t i = 0; i < data_len; i++) {
        unsigned char c = data[i];

        /// Check for sequence timeout
        if (parser->state != LLE_PARSER_STATE_NORMAL &&
            has_sequence_timed_out(parser)) {
            parser->timeout_sequences++;
            parser->last_error_time = get_current_time_us();
            lle_sequence_parser_reset_state(parser);
        }

        switch (parser->state) {
        case LLE_PARSER_STATE_NORMAL:
            if (c == 0x1B) { /// ESC
                parser->state = LLE_PARSER_STATE_ESCAPE;
                parser->buffer[0] = c;
                parser->buffer_pos = 1;
                parser->sequence_start_time = get_current_time_us();
            } else if (IS_CONTROL_CHAR(c)) {
                /// Control character in normal state
                return process_control_char(parser, c, parsed_input);
            } else {
                /// Regular character - not our responsibility (UTF-8 processor
                /// handles this)
                continue;
            }
            break;

        case LLE_PARSER_STATE_ESCAPE:
            /// Store character
            if (parser->buffer_pos >= parser->buffer_capacity) {
                /// Buffer overflow - treat as malformed
                parser->malformed_sequences++;
                parser->last_error_time = get_current_time_us();
                lle_sequence_parser_reset_state(parser);
                break;
            }

            parser->buffer[parser->buffer_pos++] = c;

            /// Determine sequence type based on second character
            if (c == '[') {
                /// CSI sequence
                parser->state = LLE_PARSER_STATE_CSI;
                parser->type = LLE_SEQ_TYPE_CSI;
            } else if (c == ']') {
                /// OSC sequence
                parser->state = LLE_PARSER_STATE_OSC;
                parser->type = LLE_SEQ_TYPE_OSC;
            } else if (c == 'P') {
                /// DCS sequence
                parser->state = LLE_PARSER_STATE_DCS;
                parser->type = LLE_SEQ_TYPE_DCS;
            } else if (c == 'O') {
                /// SS3 sequence (function keys)
                parser->type = LLE_SEQ_TYPE_SS3;
                /// SS3 sequences are typically followed by one more character
                parser->state = LLE_PARSER_STATE_KEY_SEQUENCE;
            } else if (c == 'N') {
                /// SS2 sequence
                parser->type = LLE_SEQ_TYPE_SS2;
                parser->state = LLE_PARSER_STATE_KEY_SEQUENCE;
            } else if ((c >= 0x20 && c < 0x7F) || c == 0x7F) {
                /// ESC + printable ASCII or DEL = Meta/Alt + character
                /// This is how macOS Terminal sends Alt+key when Option is
                /// Meta, or when user physically presses ESC then a letter
                /// (e.g., ESC f for M-f). 0x7F (DEL/Backspace) is included for
                /// Alt+Backspace.
                lle_parsed_input_t *result =
                    lle_pool_alloc(sizeof(lle_parsed_input_t));
                if (!result) {
                    lle_sequence_parser_reset_state(parser);
                    return LLE_ERROR_OUT_OF_MEMORY;
                }

                memset(result, 0, sizeof(lle_parsed_input_t));
                result->type = LLE_PARSED_INPUT_TYPE_KEY;
                /// 0x7F (DEL/Backspace) needs SPECIAL type to be recognized
                result->data.key_info.type =
                    (c == 0x7F) ? LLE_KEY_TYPE_SPECIAL : LLE_KEY_TYPE_REGULAR;
                result->data.key_info.keycode = c;
                result->data.key_info.modifiers = LLE_KEY_MOD_ALT;
                result->handled = false;

                *parsed_input = result;
                lle_sequence_parser_reset_state(parser);
                return LLE_SUCCESS;
            } else {
                /// Other two-character escape sequence
                lle_result_t result =
                    process_control_char(parser, c, parsed_input);
                lle_sequence_parser_reset_state(parser);
                return result;
            }
            break;

        case LLE_PARSER_STATE_CSI:
            /// Store character
            if (parser->buffer_pos >= parser->buffer_capacity) {
                parser->malformed_sequences++;
                parser->last_error_time = get_current_time_us();
                lle_sequence_parser_reset_state(parser);
                break;
            }

            parser->buffer[parser->buffer_pos++] = c;

            /// Check if this is the final character
            if (IS_CSI_FINAL(c)) {
                /// Sequence is complete
                lle_result_t result =
                    process_csi_sequence(parser, parsed_input);
                lle_sequence_parser_reset_state(parser);
                return result;
            }
            break;

        case LLE_PARSER_STATE_OSC:
        case LLE_PARSER_STATE_DCS:
            /// OSC and DCS sequences are terminated by ST (ESC \) or BEL (0x07)
            if (parser->buffer_pos >= parser->buffer_capacity) {
                parser->malformed_sequences++;
                parser->last_error_time = get_current_time_us();
                lle_sequence_parser_reset_state(parser);
                break;
            }

            parser->buffer[parser->buffer_pos++] = c;

            /// Check for terminator
            if (c == 0x07) { /// BEL
                /// Sequence complete
                lle_parsed_input_t *result =
                    lle_pool_alloc(sizeof(lle_parsed_input_t));
                if (result) {
                    memset(result, 0, sizeof(lle_parsed_input_t));
                    result->type = LLE_PARSED_INPUT_TYPE_SEQUENCE;
                    *parsed_input = result;
                }
                lle_sequence_parser_reset_state(parser);
                return result ? LLE_SUCCESS : LLE_ERROR_OUT_OF_MEMORY;
            } else if (c == '\\' && parser->buffer_pos >= 2 &&
                       parser->buffer[parser->buffer_pos - 2] == 0x1B) {
                /// ESC \ (ST - String Terminator)
                lle_parsed_input_t *result =
                    lle_pool_alloc(sizeof(lle_parsed_input_t));
                if (result) {
                    memset(result, 0, sizeof(lle_parsed_input_t));
                    result->type = LLE_PARSED_INPUT_TYPE_SEQUENCE;
                    *parsed_input = result;
                }
                lle_sequence_parser_reset_state(parser);
                return result ? LLE_SUCCESS : LLE_ERROR_OUT_OF_MEMORY;
            }
            break;

        case LLE_PARSER_STATE_KEY_SEQUENCE:
            /// SS2/SS3 sequences followed by one character
            if (parser->buffer_pos >= parser->buffer_capacity) {
                parser->malformed_sequences++;
                parser->last_error_time = get_current_time_us();
                lle_sequence_parser_reset_state(parser);
                break;
            }

            parser->buffer[parser->buffer_pos++] = c;

            /// Key sequence is complete
            lle_parsed_input_t *result =
                lle_pool_alloc(sizeof(lle_parsed_input_t));
            if (result) {
                memset(result, 0, sizeof(lle_parsed_input_t));
                result->type = LLE_PARSED_INPUT_TYPE_KEY;
                *parsed_input = result;
            }
            lle_sequence_parser_reset_state(parser);
            return result ? LLE_SUCCESS : LLE_ERROR_OUT_OF_MEMORY;

        case LLE_PARSER_STATE_ERROR_RECOVERY:
            /// In error recovery, skip characters until we see ESC or normal
            /// text
            if (c == 0x1B) {
                parser->state = LLE_PARSER_STATE_ESCAPE;
                parser->buffer[0] = c;
                parser->buffer_pos = 1;
                parser->sequence_start_time = get_current_time_us();
            } else if (!IS_CONTROL_CHAR(c)) {
                lle_sequence_parser_reset_state(parser);
            }
            break;

        default:
            /// Unknown state - reset
            lle_sequence_parser_reset_state(parser);
            break;
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Get current parser state
 *
 * @param parser Parser to query
 * @return Current parser state
 */
lle_parser_state_t
lle_sequence_parser_get_state(const lle_sequence_parser_t *parser) {
    if (!parser) {
        return LLE_PARSER_STATE_NORMAL;
    }

    return parser->state;
}

/**
 * @brief Get current sequence type
 *
 * @param parser Parser to query
 * @return Current sequence type
 */
lle_sequence_type_t
lle_sequence_parser_get_type(const lle_sequence_parser_t *parser) {
    if (!parser) {
        return LLE_SEQ_TYPE_UNKNOWN;
    }

    return parser->type;
}

/**
 * @brief Get buffered sequence data
 *
 * @param parser Parser to query
 * @param buffer Output pointer to buffer data
 * @param buffer_len Output for buffer length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_sequence_parser_get_buffer(const lle_sequence_parser_t *parser,
                                            const char **buffer,
                                            size_t *buffer_len) {
    if (!parser || !buffer || !buffer_len) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *buffer = parser->buffer;
    *buffer_len = parser->buffer_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Get CSI parameters
 *
 * @param parser Parser to query
 * @param params Output pointer to parameter array
 * @param param_count Output for parameter count
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_sequence_parser_get_csi_params(const lle_sequence_parser_t *parser,
                                   const uint32_t **params,
                                   uint8_t *param_count) {
    if (!parser || !params || !param_count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *params = parser->parameters;
    *param_count = parser->parameter_count;

    return LLE_SUCCESS;
}

/**
 * @brief Get error statistics
 *
 * @param parser Parser to query
 * @param malformed Output for malformed sequence count
 * @param timeout Output for timeout count
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_sequence_parser_get_stats(const lle_sequence_parser_t *parser,
                                           uint32_t *malformed,
                                           uint32_t *timeout) {
    if (!parser || !malformed || !timeout) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *malformed = parser->malformed_sequences;
    *timeout = parser->timeout_sequences;

    return LLE_SUCCESS;
}

/**
 * @brief Check if parser has timed out waiting for sequence completion
 *
 * If in ESCAPE state and timeout exceeded, returns the ESC as a standalone key.
 *
 * @param parser The sequence parser
 * @param timeout_us Timeout in microseconds (typically 50000-100000 for ESC)
 * @param parsed_input Output: If timeout occurred, contains ESC key event
 * @return LLE_SUCCESS if timeout handled, LLE_ERROR_NOT_FOUND if no timeout
 */
lle_result_t
lle_sequence_parser_check_timeout(lle_sequence_parser_t *parser,
                                  uint64_t timeout_us,
                                  lle_parsed_input_t **parsed_input) {
    if (!parser || !parsed_input) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *parsed_input = NULL;

    /// Only check timeout if parser is accumulating a sequence
    if (parser->state == LLE_PARSER_STATE_NORMAL) {
        return LLE_ERROR_NOT_FOUND; /// Not accumulating, no timeout to check
    }

    /// Check if we have a sequence start time
    if (parser->sequence_start_time == 0) {
        return LLE_ERROR_NOT_FOUND;
    }

    uint64_t current_time = get_current_time_us();
    uint64_t elapsed = current_time - parser->sequence_start_time;

    if (elapsed < timeout_us) {
        return LLE_ERROR_NOT_FOUND; /// Timeout not yet exceeded
    }

    /// Timeout exceeded - if in ESCAPE state with just ESC buffered, return ESC
    /// key
    if (parser->state == LLE_PARSER_STATE_ESCAPE && parser->buffer_pos == 1 &&
        parser->buffer[0] == 0x1B) {

        /// Create ESC key event
        lle_parsed_input_t *result = lle_pool_alloc(sizeof(lle_parsed_input_t));
        if (!result) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        memset(result, 0, sizeof(lle_parsed_input_t));
        result->type = LLE_PARSED_INPUT_TYPE_KEY;
        result->data.key_info.type = LLE_KEY_TYPE_SPECIAL;
        result->data.key_info.keycode = 27; /// ESC
        result->data.key_info.modifiers = 0;
        result->data.key_info.timestamp = current_time;
        result->handled = false;
        result->parse_time_us = 0;

        /// Reset parser state
        parser->timeout_sequences++;
        lle_sequence_parser_reset_state(parser);

        *parsed_input = result;
        return LLE_SUCCESS;
    }

    /// Timeout in other state - reset parser and discard partial sequence
    parser->timeout_sequences++;
    lle_sequence_parser_reset_state(parser);

    return LLE_ERROR_NOT_FOUND;
}
