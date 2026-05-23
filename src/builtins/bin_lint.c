/**
 * @file bin_lint.c
 * @brief `lint` builtin -- actionable script linting with optional auto-fix
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "compat.h"
#include "debug.h"
#include "fixer.h"
#include "lush.h"

/**
 * @brief Lint builtin - actionable script linting with optional auto-fix
 *
 * Unlike analyze, lint only shows warnings and errors (not info items)
 * and can optionally apply automatic fixes.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 if no issues, 1 if unfixed warnings remain, 2 if errors remain
 */
int bin_lint(int argc, char **argv) {
    bool strict_mode = false;
    bool fix_mode = false;
    bool fix_interactive = false;
    bool unsafe_fixes = false;
    bool dry_run = false;
    bool show_diff = false;
    bool create_backup = true;
    const char *target_shell = NULL;
    const char *script_file = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS] <script>\n", argv[0]);
            printf("\nLint shell scripts for actionable issues.\n");
            printf("\nOptions:\n");
            printf("  -t, --target=SHELL  Target shell (posix, bash, zsh)\n");
            printf("  -s, --strict        Treat warnings as errors\n");
            printf("  --fix               Apply safe automatic fixes\n");
            printf("  --fix-interactive   Interactively approve each fix\n");
            printf("  --unsafe-fixes      Also apply unsafe fixes (implies "
                   "--fix)\n");
            printf("  --dry-run           Preview fixes without applying\n");
            printf("  --diff              Show unified diff of changes\n");
            printf(
                "  --no-backup         Don't create .bak backup when fixing\n");
            printf("  -h, --help          Show this help message\n");
            printf("\nFix safety levels:\n");
            printf("  safe   - Applied with --fix (e.g., source -> .)\n");
            printf("  unsafe - Requires --unsafe-fixes (e.g., [[ ]] -> [ ])\n");
            printf("  manual - Cannot be auto-fixed, shown as suggestions\n");
            printf("\nExit codes:\n");
            printf("  0  No issues or all fixed\n");
            printf("  1  Unfixed warnings remain\n");
            printf("  2  Unfixed errors remain\n");
            printf("  3  Fix application failed\n");
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
        } else if (strcmp(argv[i], "--fix") == 0) {
            fix_mode = true;
        } else if (strcmp(argv[i], "--fix-interactive") == 0) {
            fix_interactive = true;
        } else if (strcmp(argv[i], "--unsafe-fixes") == 0) {
            fix_mode = true;
            unsafe_fixes = true;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--diff") == 0) {
            show_diff = true;
        } else if (strcmp(argv[i], "--no-backup") == 0) {
            create_backup = false;
        } else if (strcmp(argv[i], "--backup") == 0) {
            create_backup = true;
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

    // Set target shell if specified (stored as string for flexibility)
    if (target_shell) {
        compat_set_target(target_shell);
    }

    // Set strict mode if requested
    if (strict_mode) {
        compat_set_strict(true);
    }

    // Initialize debug context for analysis
    debug_context_t *ctx = debug_init();
    if (!ctx) {
        fprintf(stderr, "%s: failed to initialize lint context\n", argv[0]);
        return 1;
    }

    // Enable context so debug_printf works for output
    debug_enable(ctx, true);

    // Suppress unused variable warnings
    (void)show_diff;

    int remaining;

    if (fix_interactive) {
        // Interactive fix mode - run analysis first, then interactive fixer
        remaining = debug_lint_script(ctx, script_file, false, false, false);

        if (remaining > 0) {
            // Load script for interactive fixing
            fixer_context_t fixer_ctx;
            if (fixer_init(&fixer_ctx) == FIXER_OK) {
                if (fixer_load_file(&fixer_ctx, script_file) == FIXER_OK) {
                    // Get target shell
                    shell_mode_t target = SHELL_MODE_POSIX;
                    const char *target_str = compat_get_target();
                    if (target_str) {
                        shell_mode_parse(target_str, &target);
                    }

                    // Collect fixes
                    size_t fixes_found =
                        fixer_collect_fixes(&fixer_ctx, target);

                    if (fixes_found > 0) {
                        fixer_options_t opts = {
                            .include_unsafe = unsafe_fixes,
                            .dry_run = dry_run,
                            .create_backup = create_backup,
                            .verify_syntax = true,
                            .target = target,
                        };

                        int applied = fixer_run_interactive(&fixer_ctx, &opts,
                                                            script_file);
                        if (applied > 0) {
                            remaining -= applied;
                            if (remaining < 0)
                                remaining = 0;
                        }
                    } else {
                        printf("No automatic fixes available.\n");
                    }
                }
                fixer_cleanup(&fixer_ctx);
            }
        }
    } else {
        // Standard fix mode (automatic or none)
        remaining = debug_lint_script(ctx, script_file, fix_mode, unsafe_fixes,
                                      dry_run);
    }

    // Determine exit code
    int exit_status = 0;
    if (remaining < 0) {
        exit_status = 3; // Fix application error
    } else if (remaining > 0) {
        // Check if we have errors or just warnings
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

        // In strict mode, warnings become errors
        if (strict_mode && exit_status == 1) {
            exit_status = 2;
        }
    }

    // Cleanup
    debug_cleanup(ctx);

    // Reset strict mode
    if (strict_mode) {
        compat_set_strict(false);
    }

    return exit_status;
}
