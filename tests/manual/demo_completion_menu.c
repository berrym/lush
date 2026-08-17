/**
 * @file demo_completion_menu.c
 * @brief Manual completion menu demo
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * Interactive Completion Menu Demo
 *
 * Manual test program to demonstrate and test the interactive completion menu.
 *
 * Usage: ./demo_completion_menu
 *
 * Controls:
 *   Arrow Up/Down: Navigate items
 *   Arrow Left/Right: Navigate categories
 *   Enter: Select item
 *   Escape/Ctrl-C: Exit
 */

#include "../../include/completion_menu.h"
#include "../../include/completion_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/// ============================================================================
/// Mock implementations to avoid linking with full lush
/// ============================================================================

typedef struct builtin_s {
    const char *name;
    const char *doc;
    void *func;
} builtin;

/// Mock builtin list
builtin builtins[] = {
    {  "cd",        "Change directory", NULL},
    {"echo",          "Echo arguments", NULL},
    { "pwd", "Print working directory", NULL},
    {"exit",              "Exit shell", NULL},
    {"help",               "Show help", NULL},
};

const size_t builtins_count = sizeof(builtins) / sizeof(builtins[0]);

/// Mock alias lookup
char *lookup_alias(const char *name) {
    (void)name;
    return NULL;
}

/// Mock readline integration (need to include readline_integration.h for
/// typedef)
void lush_add_completion(lush_completions_t *lc, const char *completion) {
    (void)lc;
    (void)completion;
}

/// Terminal mode management
static struct termios original_termios;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &original_termios);
    atexit(disable_raw_mode);

    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/// Read a key with escape sequence handling
static int read_key(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return c;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return c;

        if (seq[0] == '[') {
            switch (seq[1]) {
            case 'A':
                return 1000; /// Up
            case 'B':
                return 1001; /// Down
            case 'C':
                return 1002; /// Right
            case 'D':
                return 1003; /// Left
            }
        }
    }

    return c;
}

/// Create sample completion result
static completion_result_t *create_sample_completions(void) {
    completion_result_t *result = completion_result_create(32);

    /// Add builtins
    completion_result_add(result, "cd", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "echo", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "pwd", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "exit", " ", COMPLETION_TYPE_BUILTIN, 900);

    /// Add aliases
    completion_result_add(result, "ll", " ", COMPLETION_TYPE_ALIAS, 950);
    completion_result_add(result, "la", " ", COMPLETION_TYPE_ALIAS, 950);

    /// Add commands
    completion_result_add(result, "ls", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "grep", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "cat", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "vim", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "git", " ", COMPLETION_TYPE_COMMAND, 800);

    /// Add files
    completion_result_add(result, "file1.txt", " ", COMPLETION_TYPE_FILE, 600);
    completion_result_add(result, "file2.c", " ", COMPLETION_TYPE_FILE, 600);
    completion_result_add(result, "readme.md", " ", COMPLETION_TYPE_FILE, 600);

    /// Add directories
    completion_result_add(result, "src/", "/", COMPLETION_TYPE_DIRECTORY, 700);
    completion_result_add(result, "include/", "/", COMPLETION_TYPE_DIRECTORY,
                          700);
    completion_result_add(result, "tests/", "/", COMPLETION_TYPE_DIRECTORY,
                          700);

    /// Add variables
    completion_result_add(result, "$HOME", "", COMPLETION_TYPE_VARIABLE, 500);
    completion_result_add(result, "$PATH", "", COMPLETION_TYPE_VARIABLE, 500);
    completion_result_add(result, "$USER", "", COMPLETION_TYPE_VARIABLE, 500);

    /// Sort by type and relevance
    completion_result_sort(result);

    return result;
}

int main(void) {
    printf("\n");
    printf(
        "\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97\n");
    printf("\xe2\x95\x91  Interactive Completion Menu - Demo Program           "
           "        \xe2\x95\x91\n");
    printf(
        "\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95"
        "\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2"
        "\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\n");
    printf("\n");
    printf("Controls:\n");
    printf("  Arrow Up/Down  - Navigate items\n");
    printf("  Arrow Left/Right - Navigate categories\n");
    printf("  Enter          - Select item\n");
    printf("  Escape/Ctrl-C  - Exit\n");
    printf("\n");
    printf("Creating sample completions...\n");

    /// Create sample completion result
    completion_result_t *result = create_sample_completions();
    if (!result) {
        fprintf(stderr, "Failed to create completions\n");
        return 1;
    }

    size_t category_count =
        (result->builtin_count > 0) + (result->command_count > 0) +
        (result->file_count > 0) + (result->directory_count > 0) +
        (result->variable_count > 0) + (result->alias_count > 0);
    printf("Created %zu completions in %zu categories\n", result->count,
           category_count);

    /// Create menu
    completion_menu_t *menu = completion_menu_create(result, NULL);
    if (!menu) {
        fprintf(stderr, "Failed to create menu\n");
        completion_result_free(result);
        return 1;
    }

    printf("\nPress any key to show menu...\n");
    getchar();

    /// Display menu
    printf("\nCompletion Menu:\n");
    completion_menu_display(menu);

    /// Enable raw mode for key reading
    enable_raw_mode();

    printf("\nNavigate with arrow keys. Press Enter to select, Escape to "
           "exit.\n\n");

    /// Event loop
    bool running = true;
    while (running) {
        int key = read_key();
        bool changed = false;

        switch (key) {
        case 1000: /// Up
            changed = completion_menu_navigate(menu, MENU_NAV_UP);
            break;

        case 1001: /// Down
            changed = completion_menu_navigate(menu, MENU_NAV_DOWN);
            break;

        case 1002: /// Right
            changed = completion_menu_navigate(menu, MENU_NAV_RIGHT);
            break;

        case 1003: /// Left
            changed = completion_menu_navigate(menu, MENU_NAV_LEFT);
            break;

        case '\r': /// Enter
        case '\n': {
            const char *selected = completion_menu_get_selected_text(menu);
            disable_raw_mode();
            completion_menu_clear(menu);
            printf("\nSelected: %s\n", selected ? selected : "(none)");
            running = false;
        } break;

        case '\x1b': /// Escape
        case 3:      /// Ctrl-C
            disable_raw_mode();
            completion_menu_clear(menu);
            printf("\nCancelled.\n");
            running = false;
            break;
        }

        /// Refresh display if navigation changed
        if (changed && running) {
            completion_menu_refresh(menu);
        }
    }

    /// Cleanup
    completion_menu_free(menu);
    completion_result_free(result);

    printf("\nDemo complete!\n\n");

    return 0;
}
