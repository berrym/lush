/**
 * @file manual_input_test.c
 * @brief LLE Manual Integration Test - Raw Input Verification
 *
 * CRITICAL TEST: Verifies we can read and process terminal input at 11%
 * implementation.
 *
 * This version tests fundamental terminal input capabilities:
 * 1. Can we read raw terminal input?
 * 2. Can we detect key sequences?
 * 3. Can we process UTF-8?
 * 4. Is there lag or dropped input?
 *
 * If these basics don't work, the architecture is broken.
 *
 * Exit Codes:
 * - 0: Test completed successfully
 * - 1: Terminal setup failed
 *
 * User Controls:
 * - Type to test input
 * - Arrow keys to test sequences
 * - Unicode to test UTF-8
 * - Ctrl+C to exit
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

/* ============================================================================
 * Test State
 * ============================================================================
 */

typedef struct {
    bool running;
    struct termios original_termios;
    bool termios_saved;

    /// Statistics
    uint64_t byte_count;
    uint64_t sequence_count;
    uint64_t utf8_char_count;
    struct timeval start_time;
    struct timeval last_input_time;
} test_context_t;

static test_context_t g_ctx = {0};

/* ============================================================================
 * Signal Handling
 * ============================================================================
 */

static void handle_sigint(int sig) {
    (void)sig;
    g_ctx.running = false;
}

/* ============================================================================
 * Terminal Setup/Restore
 * ============================================================================
 */

static int setup_raw_terminal(test_context_t *ctx) {
    if (tcgetattr(STDIN_FILENO, &ctx->original_termios) < 0) {
        fprintf(stderr, "ERROR: Failed to get terminal attributes: %s\n",
                strerror(errno));
        return -1;
    }
    ctx->termios_saved = true;

    struct termios raw = ctx->original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /// DON'T disable OPOST - we need output processing for proper \n handling
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; /// 100ms timeout

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        fprintf(stderr, "ERROR: Failed to set raw mode: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void restore_terminal(test_context_t *ctx) {
    if (ctx->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &ctx->original_termios);
    }
}

/* ============================================================================
 * UTF-8 Detection
 * ============================================================================
 */

static int utf8_byte_count(uint8_t first_byte) {
    if ((first_byte & 0x80) == 0)
        return 1; /// 0xxxxxxx
    if ((first_byte & 0xE0) == 0xC0)
        return 2; /// 110xxxxx
    if ((first_byte & 0xF0) == 0xE0)
        return 3; /// 1110xxxx
    if ((first_byte & 0xF8) == 0xF0)
        return 4; /// 11110xxx
    return 1;     /// Invalid
}

/* ============================================================================
 * Escape Sequence Detection
 * ============================================================================
 */

static const char *detect_sequence(const uint8_t *buf, ssize_t len) {
    if (len < 2 || buf[0] != 27)
        return NULL;

    if (len >= 3 && buf[1] == '[') {
        switch (buf[2]) {
        case 'A':
            return "Up Arrow";
        case 'B':
            return "Down Arrow";
        case 'C':
            return "Right Arrow";
        case 'D':
            return "Left Arrow";
        case 'H':
            return "Home";
        case 'F':
            return "End";
        }

        if (len >= 4 && buf[2] >= '0' && buf[2] <= '9') {
            if (buf[3] == '~') {
                switch (buf[2]) {
                case '1':
                    return "Home";
                case '2':
                    return "Insert";
                case '3':
                    return "Delete";
                case '4':
                    return "End";
                case '5':
                    return "Page Up";
                case '6':
                    return "Page Down";
                }
            }
        }
    }

    return "Unknown Sequence";
}

/* ============================================================================
 * Display Helpers
 * ============================================================================
 */

static void print_header(void) {
    printf("\033[2J\033[H"); /// Clear screen and home
    printf("==================================================================="
           "=====\n");
    printf("         LLE Manual Integration Test - Raw Input Verification\n");
    printf("==================================================================="
           "=====\n");
    printf("Purpose: Verify terminal input works at 11%% implementation\n");
    printf("\n");
    printf("Test Scenarios:\n");
    printf("  1. Type ASCII - verify bytes received\n");
    printf("  2. Type Unicode (emoji, Chinese) - verify UTF-8 processing\n");
    printf("  3. Arrow keys - verify escape sequence detection\n");
    printf("  4. Type fast - verify no lag or dropped input\n");
    printf("\n");
    printf("Controls: Type to test | Ctrl+C to exit\n");
    printf("==================================================================="
           "=====\n\n");
}

static void print_stats(test_context_t *ctx) {
    struct timeval now;
    gettimeofday(&now, NULL);

    double elapsed = (now.tv_sec - ctx->start_time.tv_sec) +
                     (now.tv_usec - ctx->start_time.tv_usec) / 1e6;

    double input_lag = (now.tv_sec - ctx->last_input_time.tv_sec) +
                       (now.tv_usec - ctx->last_input_time.tv_usec) / 1e6;

    printf("\r[Stats: %.1fs | %" PRIu64 " bytes | %" PRIu64
           " sequences | %" PRIu64 " UTF-8 chars | Lag: %.0fms]",
           elapsed, ctx->byte_count, ctx->sequence_count, ctx->utf8_char_count,
           input_lag * 1000);
    fflush(stdout);
}

/* ============================================================================
 * Main Test Loop
 * ============================================================================
 */

static int run_test(test_context_t *ctx) {
    print_header();
    printf("Ready to test. Start typing...\n\n");

    gettimeofday(&ctx->start_time, NULL);
    ctx->last_input_time = ctx->start_time;
    ctx->running = true;

    uint8_t buf[256];
    uint8_t utf8_buf[4];
    int utf8_pending = 0;

    while (ctx->running) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

        if (n > 0) {
            gettimeofday(&ctx->last_input_time, NULL);
            ctx->byte_count += n;

            printf("\n[Input: %zd bytes] ", n);

            /// Check for escape sequence
            if (buf[0] == 27 && n > 1) {
                const char *seq = detect_sequence(buf, n);
                if (seq) {
                    printf("\n  \xe2\x86\x92 Sequence detected: %s\n", seq);
                    ctx->sequence_count++;
                } else {
                    printf("\n  \xe2\x86\x92 Escape pressed\n");
                }

                /// Show raw bytes
                printf("  Raw: ");
                for (ssize_t i = 0; i < n; i++) {
                    printf("%02X ", buf[i]);
                }
                printf("\n");
            }
            /// Check for control characters
            else if (buf[0] < 32) {
                if (buf[0] == 3) { /// Ctrl+C
                    ctx->running = false;
                    break;
                } else if (buf[0] == 13) { /// Enter
                    printf("\n  \xe2\x86\x92 Enter pressed\n");
                } else if (buf[0] == 127 || buf[0] == 8) { /// Backspace
                    printf("\n  \xe2\x86\x92 Backspace pressed\n");
                } else {
                    printf("\n  \xe2\x86\x92 Control char: ^%c (0x%02X)\n",
                           buf[0] + 64, buf[0]);
                }
            }
            /// Regular character
            else {
                /// UTF-8 processing
                for (ssize_t i = 0; i < n; i++) {
                    if (utf8_pending == 0) {
                        int bytes = utf8_byte_count(buf[i]);
                        if (bytes == 1) {
                            printf("%c", buf[i]);
                        } else {
                            utf8_buf[0] = buf[i];
                            utf8_pending = bytes - 1;
                        }
                    } else {
                        utf8_buf[utf8_byte_count(utf8_buf[0]) - utf8_pending] =
                            buf[i];
                        utf8_pending--;

                        if (utf8_pending == 0) {
                            printf(" ");
                            for (int j = 0; j < utf8_byte_count(utf8_buf[0]);
                                 j++) {
                                printf("%c", utf8_buf[j]);
                            }
                            printf(" [UTF-8: %d bytes]",
                                   utf8_byte_count(utf8_buf[0]));
                            ctx->utf8_char_count++;
                        }
                    }
                }
                printf("\n");
            }

            print_stats(ctx);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "\nERROR: Read failed: %s\n", strerror(errno));
            return -1;
        }

        /// Update stats periodically
        static int counter = 0;
        if (++counter % 100 == 0) {
            print_stats(ctx);
        }
    }

    printf("\n\n");
    return 0;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================
 */

int main(void) {
    test_context_t *ctx = &g_ctx;

    signal(SIGINT, handle_sigint);

    if (setup_raw_terminal(ctx) < 0) {
        return 1;
    }

    int result = run_test(ctx);

    restore_terminal(ctx);

    /// Print final results
    printf("\n");
    printf("==================================================================="
           "=====\n");
    printf("                        Test Completed\n");
    printf("==================================================================="
           "=====\n");
    printf("Total Bytes:       %" PRIu64 "\n", ctx->byte_count);
    printf("Sequences:         %" PRIu64 "\n", ctx->sequence_count);
    printf("UTF-8 Characters:  %" PRIu64 "\n", ctx->utf8_char_count);
    printf("\n");
    printf("Manual Verification Questions:\n");
    printf("  - Did all typed characters appear?\n");
    printf("  - Were arrow keys detected correctly?\n");
    printf("  - Did Unicode characters display properly?\n");
    printf("  - Was there any lag or dropped input?\n");
    printf("\n");
    printf("If YES to all: Basic input system is functional\n");
    printf("If NO to any: Document issues - may need architectural changes\n");
    printf("\n");
    printf("Next Step: If basic input works, test buffer integration\n");
    printf("==================================================================="
           "=====\n");

    return result == 0 ? 0 : 1;
}
