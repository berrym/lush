/**
 * @file simple_input_test.c
 * @brief Minimal terminal input test - no formatting, just raw I/O
 *
 * Tests absolute basics:
 * - Can we read from stdin?
 * - Can we detect sequences?
 * - Is there display corruption?
 */

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static bool running = true;

static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void handle_sigint(int sig) {
    (void)sig;
    running = false;
}

int main(void) {
    // Get original terminal settings
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        fprintf(stderr, "Failed to get terminal attributes\n");
        return 1;
    }

    // Set raw mode for INPUT only - keep output processing for display
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // DON'T disable OPOST - we need output processing for proper \n handling
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        fprintf(stderr, "Failed to set raw mode\n");
        return 1;
    }

    signal(SIGINT, handle_sigint);

    printf("Simple input test - Type to see bytes, Ctrl+C to exit\n");

    uint8_t buf[256];
    uint64_t total = 0;

    while (running) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

        if (n > 0) {
            total += n;

            // Check for Ctrl+C
            if (buf[0] == 3) {
                running = false;
                break;
            }

            // Show what we got
            printf("\nRead %zd bytes (total: %" PRIu64 "): ", n, total);
            for (ssize_t i = 0; i < n; i++) {
                printf("%02X ", buf[i]);
            }

            // Check for escape sequence
            if (buf[0] == 27 && n > 1) {
                printf(" <ESC sequence>");
            }

            fflush(stdout);
        }
    }

    printf("\n\nTest complete. Total bytes: %" PRIu64 "\n", total);

    restore_terminal();
    return 0;
}
