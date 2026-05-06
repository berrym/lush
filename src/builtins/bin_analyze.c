/**
 * @file bin_analyze.c
 * @brief `analyze` builtin -- full script analysis (info + warnings + errors)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "compat.h"
#include "debug.h"
#include "executor.h"
#include "fixer.h"
#include "lush.h"
#include "shell_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Analyze scripts for issues and portability (builtin command)
 *
 * Analyzes shell scripts for syntax errors, style issues, security
 * vulnerabilities, performance problems, and portability concerns.
 *
 * Usage: analyze [OPTIONS] <script>
 *        lint [OPTIONS] <script>
 *
 * Options:
 *   -t, --target=SHELL  Target shell for compatibility (posix, bash, zsh)
 *   -s, --strict        Treat warnings as errors
 *   -h, --help          Show help message
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 if no issues, 1 if warnings, 2 if errors
 */
int bin_analyze(int argc, char **argv) {
    bool strict_mode = false;
    const char *target_shell = NULL;
    const char *script_file = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS] <script>\n", argv[0]);
            printf("\nAnalyze shell scripts for issues and portability.\n");
            printf("\nOptions:\n");
            printf("  -t, --target=SHELL  Target shell (posix, bash, zsh)\n");
            printf("  -s, --strict        Treat warnings as errors\n");
            printf("  -h, --help          Show this help message\n");
            printf("\nCategories checked:\n");
            printf("  syntax       - Syntax errors and parsing issues\n");
            printf("  style        - Code style and formatting\n");
            printf("  performance  - Performance anti-patterns\n");
            printf("  security     - Security vulnerabilities\n");
            printf("  portability  - Shell compatibility issues\n");
            printf("\nExit codes:\n");
            printf("  0  No issues found\n");
            printf("  1  Warnings found\n");
            printf("  2  Errors found\n");
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                target_shell = argv[++i];
            } else {
                fprintf(stderr, "%s: -t requires an argument\n", argv[0]);
                return 1;
            }
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            target_shell = argv[i] + 9;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[i]);
            return 1;
        } else {
            script_file = argv[i];
        }
    }

    if (!script_file) {
        fprintf(stderr, "%s: missing script file argument\n", argv[0]);
        fprintf(stderr, "Usage: %s [OPTIONS] <script>\n", argv[0]);
        return 1;
    }

    /* Set target shell if specified (stored as string for flexibility) */
    if (target_shell) {
        compat_set_target(target_shell);
    }

    /* Set strict mode if requested */
    if (strict_mode) {
        compat_set_strict(true);
    }

    /* Initialize debug context for analysis */
    debug_context_t *ctx = debug_init();
    if (!ctx) {
        fprintf(stderr, "%s: failed to initialize analysis context\n", argv[0]);
        return 1;
    }

    /* Enable context so debug_printf works for output */
    debug_enable(ctx, true);

    /* Run analysis (includes report output) */
    debug_analyze_script(ctx, script_file);

    /* Determine exit code based on issues found */
    int exit_status = 0;
    if (ctx->issue_count > 0) {
        analysis_issue_t *issue = ctx->analysis_issues;
        while (issue) {
            if (strcmp(issue->severity, "error") == 0) {
                exit_status = 2;
                break;
            } else if (strcmp(issue->severity, "warning") == 0 &&
                       exit_status < 1) {
                exit_status = 1;
            }
            issue = issue->next;
        }

        /* In strict mode, warnings become errors */
        if (strict_mode && exit_status == 1) {
            exit_status = 2;
        }
    }

    /* Cleanup */
    debug_cleanup(ctx);

    /* Reset strict mode */
    if (strict_mode) {
        compat_set_strict(false);
    }

    return exit_status;
}
