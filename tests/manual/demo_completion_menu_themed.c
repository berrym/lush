/*
 * Interactive Completion Menu - Themed Demo
 *
 * Demonstrates theme integration with completion menu.
 * Shows how menu adapts to different theme configurations.
 */

#include "../../include/completion_menu.h"
#include "../../include/completion_menu_theme.h"
#include "../../include/completion_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Mock implementations
// ============================================================================

typedef struct builtin_s {
    const char *name;
    const char *doc;
    void *func;
} builtin;

builtin builtins[] = {
    {  "cd",        "Change directory", NULL},
    {"echo",          "Echo arguments", NULL},
    { "pwd", "Print working directory", NULL},
    {"exit",              "Exit shell", NULL},
    {"help",               "Show help", NULL},
};

const size_t builtins_count = sizeof(builtins) / sizeof(builtins[0]);

char *lookup_alias(const char *name) {
    (void)name;
    return NULL;
}

void lush_add_completion(lush_completions_t *lc, const char *completion) {
    (void)lc;
    (void)completion;
}

// Mock theme functions (theme system not available in minimal build)
theme_definition_t *theme_get_active(void) { return NULL; }

int theme_detect_color_support(void) {
    return 256; // Simulate 256-color support
}

symbol_compatibility_t symbol_get_compatibility_mode(void) {
    return SYMBOL_MODE_UNICODE; // Use Unicode symbols
}

// ============================================================================
// Demo
// ============================================================================

static completion_result_t *create_demo_completions(void) {
    completion_result_t *result = completion_result_create(32);

    // Add builtins
    completion_result_add(result, "cd", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "echo", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "pwd", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "exit", " ", COMPLETION_TYPE_BUILTIN, 900);
    completion_result_add(result, "help", " ", COMPLETION_TYPE_BUILTIN, 900);

    // Add aliases
    completion_result_add(result, "ll", " ", COMPLETION_TYPE_ALIAS, 950);
    completion_result_add(result, "la", " ", COMPLETION_TYPE_ALIAS, 950);
    completion_result_add(result, "gs", " ", COMPLETION_TYPE_ALIAS, 950);

    // Add commands
    completion_result_add(result, "ls", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "grep", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "cat", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "vim", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "git", " ", COMPLETION_TYPE_COMMAND, 800);
    completion_result_add(result, "make", " ", COMPLETION_TYPE_COMMAND, 800);

    // Add files
    completion_result_add(result, "file1.txt", " ", COMPLETION_TYPE_FILE, 600);
    completion_result_add(result, "file2.c", " ", COMPLETION_TYPE_FILE, 600);
    completion_result_add(result, "readme.md", " ", COMPLETION_TYPE_FILE, 600);
    completion_result_add(result, "config.json", " ", COMPLETION_TYPE_FILE,
                          600);

    // Add directories
    completion_result_add(result, "src/", "/", COMPLETION_TYPE_DIRECTORY, 700);
    completion_result_add(result, "include/", "/", COMPLETION_TYPE_DIRECTORY,
                          700);
    completion_result_add(result, "tests/", "/", COMPLETION_TYPE_DIRECTORY,
                          700);
    completion_result_add(result, "docs/", "/", COMPLETION_TYPE_DIRECTORY, 700);

    // Add variables
    completion_result_add(result, "$HOME", "", COMPLETION_TYPE_VARIABLE, 500);
    completion_result_add(result, "$PATH", "", COMPLETION_TYPE_VARIABLE, 500);
    completion_result_add(result, "$USER", "", COMPLETION_TYPE_VARIABLE, 500);
    completion_result_add(result, "$SHELL", "", COMPLETION_TYPE_VARIABLE, 500);

    // Add history
    completion_result_add(result, "git commit -m", " ", COMPLETION_TYPE_HISTORY,
                          400);
    completion_result_add(result, "make clean", " ", COMPLETION_TYPE_HISTORY,
                          400);

    completion_result_sort(result);
    return result;
}

int main(void) {
    printf("\n");
    printf(
        "╔════════════════════════════════════════════════════════════════╗\n");
    printf(
        "║  Completion Menu - Theme Integration Demo                     ║\n");
    printf(
        "╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Initialize theme system
    printf("Initializing theme system...\n");
    if (!completion_menu_theme_init()) {
        fprintf(stderr, "Failed to initialize theme system\n");
        return 1;
    }

    // Get theme configuration
    const completion_menu_theme_config_t *theme_config =
        completion_menu_theme_get_config();

    printf("Theme Configuration:\n");
    printf("  Colors: %s\n", theme_config->use_colors ? "enabled" : "disabled");
    printf("  Unicode: %s\n",
           theme_config->use_unicode ? "enabled" : "disabled");
    printf("  Bold: %s\n", theme_config->use_bold ? "enabled" : "disabled");
    printf("\n");

    // Create completions
    printf("Creating sample completions...\n");
    completion_result_t *result = create_demo_completions();
    if (!result) {
        fprintf(stderr, "Failed to create completions\n");
        return 1;
    }

    printf("Created %zu completions\n\n", result->count);

    // Create menu
    completion_menu_t *menu = completion_menu_create(result, NULL);
    if (!menu) {
        fprintf(stderr, "Failed to create menu\n");
        completion_result_free(result);
        return 1;
    }

    // Display with theme integration
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Themed Completion Menu Display:\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    completion_menu_display_themed(menu);

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");

    // Test individual formatting functions
    printf("Testing themed formatting functions:\n\n");

    // Test header formatting
    char header[256];
    completion_menu_format_header_themed(COMPLETION_TYPE_BUILTIN, 5, header,
                                         sizeof(header));
    printf("Header example: %s\n\n", header);

    // Test item formatting
    completion_item_t test_item = {.text = "example_command",
                                   .suffix = " ",
                                   .type = COMPLETION_TYPE_COMMAND,
                                   .relevance_score = 800,
                                   .description = NULL,
                                   .owns_text = false,
                                   .owns_description = false};

    char item_line[512];
    completion_menu_format_item_themed(&test_item, false, item_line,
                                       sizeof(item_line));
    printf("Item (normal):   %s\n", item_line);

    completion_menu_format_item_themed(&test_item, true, item_line,
                                       sizeof(item_line));
    printf("Item (selected): %s\n\n", item_line);

    // Test scroll indicators
    char scroll[128];
    completion_menu_format_scroll_indicator_themed(1, scroll, sizeof(scroll));
    printf("Scroll down: %s\n", scroll);

    completion_menu_format_scroll_indicator_themed(-1, scroll, sizeof(scroll));
    printf("Scroll up:   %s\n\n", scroll);

    // Cleanup
    completion_menu_free(menu);
    completion_result_free(result);
    completion_menu_theme_cleanup();

    printf("Demo complete!\n\n");

    return 0;
}
