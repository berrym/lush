/**
 * @file input_stream.c
 * @brief LLE Input Stream Management
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides raw terminal input stream buffering and flow control for the
 * input parsing system.
 *
 * Specification: docs/lle_specification/06_input_parsing_complete.md
 * Phase: 1 - Input Stream Management
 */

#include "lle/error_handling.h"
#include "lle/input_parsing.h"
#include "lle/memory_management.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/**
 * @brief Set terminal file descriptor to non-blocking mode
 *
 * @param fd File descriptor to modify
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Set terminal file descriptor to blocking mode
 *
 * @param fd File descriptor to modify
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Compact buffer by moving unconsumed data to beginning
 *
 * @param stream Input stream to compact
 */
static void compact_buffer(lle_input_stream_t *stream) {
    if (!stream || stream->buffer_pos == 0) {
        return;
    }

    size_t remaining = stream->buffer_used - stream->buffer_pos;
    if (remaining > 0) {
        memmove(stream->buffer, stream->buffer + stream->buffer_pos, remaining);
    }

    stream->buffer_used = remaining;
    stream->buffer_pos = 0;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================
 */

/**
 * @brief Initialize input stream
 *
 * @param stream Output pointer for created stream
 * @param terminal Terminal system reference
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_init(lle_input_stream_t **stream,
                                   lle_terminal_system_t *terminal,
                                   lle_memory_pool_t *memory_pool) {
    if (!stream || !terminal || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result = LLE_SUCCESS;
    lle_input_stream_t *new_stream = NULL;

    /// Allocate stream structure
    new_stream = lle_pool_alloc(sizeof(lle_input_stream_t));
    if (!new_stream) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memset(new_stream, 0, sizeof(lle_input_stream_t));

    /// Allocate input buffer
    new_stream->buffer = lle_pool_alloc(LLE_INPUT_BUFFER_SIZE);
    if (!new_stream->buffer) {
        lle_pool_free(new_stream);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize stream fields
    new_stream->buffer_size = LLE_INPUT_BUFFER_SIZE;
    new_stream->buffer_used = 0;
    new_stream->buffer_pos = 0;
    new_stream->memory_pool = memory_pool;

    /// Get terminal file descriptor - assuming STDIN for now
    new_stream->terminal_fd = STDIN_FILENO;

    /// Set non-blocking mode by default for responsive input
    new_stream->blocking_mode = false;
    result = set_nonblocking(new_stream->terminal_fd);
    if (result != LLE_SUCCESS) {
        lle_pool_free(new_stream->buffer);
        lle_pool_free(new_stream);
        return result;
    }

    /// Initialize statistics
    new_stream->bytes_read = 0;
    new_stream->read_operations = 0;
    new_stream->buffer_overflows = 0;

    /// Flow control disabled by default
    new_stream->flow_control_enabled = false;

    /* Note: terminal_caps would be set from terminal system in full integration
     */
    new_stream->terminal_caps = NULL;

    *stream = new_stream;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy input stream
 *
 * @param stream Stream to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_destroy(lle_input_stream_t *stream) {
    if (!stream) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Restore blocking mode on terminal before cleanup
    if (!stream->blocking_mode) {
        set_blocking(stream->terminal_fd);
    }

    /// Free buffer
    if (stream->buffer) {
        lle_pool_free(stream->buffer);
    }

    /// Free stream structure
    lle_pool_free(stream);

    return LLE_SUCCESS;
}

/**
 * @brief Read data from terminal into internal buffer
 *
 * @param stream Input stream instance
 * @param buffer Output buffer for read data
 * @param buffer_size Size of output buffer
 * @param bytes_read Output for number of bytes read
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_read(lle_input_stream_t *stream, char *buffer,
                                   size_t buffer_size, size_t *bytes_read) {
    if (!stream || !buffer || !bytes_read) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *bytes_read = 0;

    /// Compact buffer if needed to make room
    if (stream->buffer_used >= stream->buffer_size) {
        compact_buffer(stream);

        /// Check if buffer is still full after compacting
        if (stream->buffer_used >= stream->buffer_size) {
            stream->buffer_overflows++;
            return LLE_ERROR_BUFFER_OVERFLOW;
        }
    }

    /// Read from terminal
    size_t space_available = stream->buffer_size - stream->buffer_used;
    ssize_t n = read(stream->terminal_fd, stream->buffer + stream->buffer_used,
                     space_available);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /// No data available in non-blocking mode - not an error
            return LLE_SUCCESS;
        }
        return LLE_ERROR_IO_ERROR;
    }

    if (n == 0) {
        /// EOF - terminal closed
        return LLE_ERROR_IO_ERROR;
    }

    /// Update buffer state
    stream->buffer_used += (size_t)n;
    stream->bytes_read += (size_t)n;
    stream->read_operations++;

    /// Copy to output buffer if requested
    size_t copy_size = (size_t)n < buffer_size ? (size_t)n : buffer_size;
    memcpy(buffer, stream->buffer + stream->buffer_used - (size_t)n, copy_size);
    *bytes_read = copy_size;

    return LLE_SUCCESS;
}

/**
 * @brief Buffer incoming data (for testing or piped input)
 *
 * @param stream Input stream instance
 * @param data Data to buffer
 * @param data_len Length of data
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_buffer_data(lle_input_stream_t *stream,
                                          const char *data, size_t data_len) {
    if (!stream || !data) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (data_len == 0) {
        return LLE_SUCCESS;
    }

    /// Compact buffer if needed
    if (stream->buffer_used + data_len > stream->buffer_size) {
        compact_buffer(stream);

        /// Check if there's still not enough space
        if (stream->buffer_used + data_len > stream->buffer_size) {
            stream->buffer_overflows++;
            return LLE_ERROR_BUFFER_OVERFLOW;
        }
    }

    /// Copy data to buffer
    memcpy(stream->buffer + stream->buffer_used, data, data_len);
    stream->buffer_used += data_len;
    stream->bytes_read += data_len;

    return LLE_SUCCESS;
}

/**
 * @brief Get buffered data without consuming
 *
 * @param stream Input stream instance
 * @param data Output pointer to buffered data
 * @param data_len Output for length of buffered data
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_get_buffered(lle_input_stream_t *stream,
                                           const char **data,
                                           size_t *data_len) {
    if (!stream || !data || !data_len) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t available = stream->buffer_used - stream->buffer_pos;

    if (available == 0) {
        *data = NULL;
        *data_len = 0;
        return LLE_SUCCESS;
    }

    *data = stream->buffer + stream->buffer_pos;
    *data_len = available;

    return LLE_SUCCESS;
}

/**
 * @brief Consume processed data from buffer
 *
 * @param stream Input stream instance
 * @param bytes Number of bytes to consume
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_consume(lle_input_stream_t *stream,
                                      size_t bytes) {
    if (!stream) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t available = stream->buffer_used - stream->buffer_pos;

    if (bytes > available) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    stream->buffer_pos += bytes;

    /// Compact buffer if we've consumed a significant portion
    if (stream->buffer_pos > stream->buffer_size / 2) {
        compact_buffer(stream);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Peek at single byte at offset without consuming
 *
 * @param stream Input stream instance
 * @param offset Offset from current position
 * @param byte Output for peeked byte
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_peek(lle_input_stream_t *stream, size_t offset,
                                   char *byte) {
    if (!stream || !byte) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t available = stream->buffer_used - stream->buffer_pos;

    if (offset >= available) {
        return LLE_ERROR_BUFFER_UNDERFLOW;
    }

    *byte = stream->buffer[stream->buffer_pos + offset];

    return LLE_SUCCESS;
}

/**
 * @brief Set blocking mode
 *
 * @param stream Input stream instance
 * @param blocking Whether to enable blocking mode
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_set_blocking(lle_input_stream_t *stream,
                                           bool blocking) {
    if (!stream) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (stream->blocking_mode == blocking) {
        return LLE_SUCCESS; /// Already in desired mode
    }

    lle_result_t result;
    if (blocking) {
        result = set_blocking(stream->terminal_fd);
    } else {
        result = set_nonblocking(stream->terminal_fd);
    }

    if (result == LLE_SUCCESS) {
        stream->blocking_mode = blocking;
    }

    return result;
}

/**
 * @brief Enable/disable flow control
 *
 * @param stream Input stream instance
 * @param enabled Whether to enable flow control
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_set_flow_control(lle_input_stream_t *stream,
                                               bool enabled) {
    if (!stream) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    stream->flow_control_enabled = enabled;
    return LLE_SUCCESS;
}

/**
 * @brief Get stream statistics
 *
 * @param stream Input stream instance
 * @param bytes_read Output for total bytes read
 * @param read_operations Output for total read operations
 * @param buffer_overflows Output for buffer overflow count
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_get_statistics(lle_input_stream_t *stream,
                                             uint64_t *bytes_read,
                                             uint64_t *read_operations,
                                             uint64_t *buffer_overflows) {
    if (!stream) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (bytes_read) {
        *bytes_read = stream->bytes_read;
    }

    if (read_operations) {
        *read_operations = stream->read_operations;
    }

    if (buffer_overflows) {
        *buffer_overflows = stream->buffer_overflows;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Reset stream state
 *
 * @param stream Input stream to reset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_reset(lle_input_stream_t *stream) {
    if (!stream) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear buffer
    stream->buffer_used = 0;
    stream->buffer_pos = 0;

    /// Reset statistics
    stream->bytes_read = 0;
    stream->read_operations = 0;
    stream->buffer_overflows = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Get available bytes in buffer
 *
 * @param stream Input stream instance
 * @param available Output for number of available bytes
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_stream_get_available(lle_input_stream_t *stream,
                                            size_t *available) {
    if (!stream || !available) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *available = stream->buffer_used - stream->buffer_pos;

    return LLE_SUCCESS;
}
