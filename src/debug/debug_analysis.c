/**
 * @file debug_analysis.c
 * @brief Script Analysis and Issue Detection
 *
 * Provides static analysis capabilities for shell scripts, detecting
 * syntax errors, style issues, security vulnerabilities, performance
 * anti-patterns, and portability concerns.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compat.h"
#include "debug.h"
#include "errors.h"
#include "fixer.h"
#include "lle/unicode_compare.h"
#include "node.h"
#include "parser.h"
#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Forward declarations for static functions
static node_t *debug_analyze_syntax(debug_context_t *ctx, const char *file,
                                    const char *content);
static void debug_analyze_style(debug_context_t *ctx, const char *file,
                                const char *content);
static void debug_analyze_performance(debug_context_t *ctx, const char *file,
                                      const char *content);
static void debug_analyze_security(debug_context_t *ctx, const char *file,
                                   const char *content);
static void debug_analyze_portability(debug_context_t *ctx, const char *file,
                                      const char *content, node_t *ast);
static void debug_analyze_types(debug_context_t *ctx, const char *file,
                                const char *content);

/**
 * @brief Analyze a script file for various issues
 * @param ctx Debug context for output
 * @param script_path Path to the script file to analyze
 */
void debug_analyze_script(debug_context_t *ctx, const char *script_path) {
    if (!ctx || !script_path) {
        return;
    }

    debug_printf(ctx, "Analyzing script: %s\n", script_path);

    // Check if file exists
    struct stat st;
    if (stat(script_path, &st) != 0) {
        debug_printf(ctx, "ERROR: Script file not found: %s\n", script_path);
        return;
    }

    // Read script file
    FILE *file = fopen(script_path, "r");
    if (!file) {
        debug_printf(ctx, "ERROR: Cannot open script file: %s\n", script_path);
        return;
    }

    // Read entire file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *script_content = malloc(file_size + 1);
    if (!script_content) {
        fclose(file);
        debug_printf(ctx, "ERROR: Memory allocation failed\n");
        return;
    }

    size_t bytes_read = fread(script_content, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        debug_printf(ctx, "ERROR: Short read on %s (expected %ld, got %zu)\n",
                     script_path, file_size, bytes_read);
        free(script_content);
        return;
    }
    script_content[file_size] = '\0';

    // Clear previous analysis results
    debug_clear_analysis_issues(ctx);

    // Perform various analysis checks
    // Syntax analysis returns the AST for use by other analyzers
    node_t *ast = debug_analyze_syntax(ctx, script_path, script_content);
    debug_analyze_style(ctx, script_path, script_content);
    debug_analyze_performance(ctx, script_path, script_content);
    debug_analyze_security(ctx, script_path, script_content);
    debug_analyze_portability(ctx, script_path, script_content, ast);
    debug_analyze_types(ctx, script_path, script_content);

    // Generate analysis report
    debug_show_analysis_report(ctx);

    // Cleanup
    if (ast) {
        free_node_tree(ast);
    }
    free(script_content);
}

/**
 * @brief Add an analysis issue to the context
 * @param ctx Debug context
 * @param file File path where issue was found
 * @param line Line number of the issue
 * @param severity Severity level (error, warning, info)
 * @param category Category of issue (syntax, style, security, etc.)
 * @param message Description of the issue
 * @param suggestion Suggested fix for the issue
 */
void debug_add_analysis_issue(debug_context_t *ctx, const char *file, int line,
                              const char *severity, const char *category,
                              const char *message, const char *suggestion) {
    if (!ctx || !file || !severity || !category || !message) {
        return;
    }

    analysis_issue_t *issue = malloc(sizeof(analysis_issue_t));
    if (!issue) {
        return;
    }

    issue->file_path = strdup(file);
    issue->line_number = line;
    issue->severity = strdup(severity);
    issue->category = strdup(category);
    issue->message = strdup(message);
    issue->suggestion = suggestion ? strdup(suggestion) : NULL;
    issue->next = ctx->analysis_issues;

    ctx->analysis_issues = issue;
    ctx->issue_count++;
}

/**
 * @brief Analyze script for syntax issues
 * @param ctx Debug context
 * @param file File path being analyzed
 * @param content Script content to analyze
 * @return Parsed AST on success, NULL on syntax error (caller must free)
 */
static node_t *debug_analyze_syntax(debug_context_t *ctx, const char *file,
                                    const char *content) {
    if (!ctx || !file || !content) {
        return NULL;
    }

    // Try to parse the script — content is the entire script body, so
    // its first character is line 1 of the source file.
    parser_t *parser = parser_new_with_source(content, file, 1);
    if (!parser) {
        debug_add_analysis_issue(ctx, file, 1, "error", "syntax",
                                 "Failed to create parser",
                                 "Check script syntax");
        return NULL;
    }

    // Parse and check for errors
    node_t *ast = parser_parse(parser);
    if (!ast) {
        debug_add_analysis_issue(
            ctx, file, 1, "error", "syntax", "Syntax error in script",
            "Check parentheses, quotes, and command structure");
    } else {
        // Basic syntax validation passed
        debug_printf(ctx, "Syntax validation: PASSED\n");
    }

    parser_free(parser);
    return ast; // Caller is responsible for freeing
}

/**
 * @brief Analyze script for style issues
 * @param ctx Debug context
 * @param file File path being analyzed
 * @param content Script content to analyze
 */
static void debug_analyze_style(debug_context_t *ctx, const char *file,
                                const char *content) {
    if (!ctx || !file || !content) {
        return;
    }

    int line_number = 1;
    const char *line_start = content;
    const char *pos = content;

    while (*pos) {
        if (*pos == '\n') {
            // Check line length
            int line_length = pos - line_start;
            if (line_length > 120) {
                debug_add_analysis_issue(ctx, file, line_number, "warning",
                                         "style", "Line too long",
                                         "Consider breaking long lines");
            }

            // Check for trailing whitespace
            if (pos > line_start && (*(pos - 1) == ' ' || *(pos - 1) == '\t')) {
                debug_add_analysis_issue(ctx, file, line_number, "info",
                                         "style", "Trailing whitespace",
                                         "Remove trailing spaces/tabs");
            }

            line_number++;
            line_start = pos + 1;
        }
        pos++;
    }

    // Check for shebang
    if (strncmp(content, "#!", 2) != 0) {
        debug_add_analysis_issue(ctx, file, 1, "warning", "style",
                                 "Missing shebang",
                                 "Add #!/bin/sh or #!/usr/bin/env lush");
    }

    // Check for consistent indentation
    bool uses_tabs = false;
    bool uses_spaces = false;
    pos = content;
    line_number = 1;

    while (*pos) {
        if (*pos == '\n') {
            line_number++;
            pos++;
            // Check indentation at start of line
            while (*pos == ' ' || *pos == '\t') {
                if (*pos == '\t') {
                    uses_tabs = true;
                } else {
                    uses_spaces = true;
                }
                pos++;
            }
        } else {
            pos++;
        }
    }

    if (uses_tabs && uses_spaces) {
        debug_add_analysis_issue(ctx, file, 1, "warning", "style",
                                 "Mixed tabs and spaces",
                                 "Use consistent indentation");
    }
}

/**
 * @brief Analyze script for performance issues
 * @param ctx Debug context
 * @param file File path being analyzed
 * @param content Script content to analyze
 */
static void debug_analyze_performance(debug_context_t *ctx, const char *file,
                                      const char *content) {
    if (!ctx || !file || !content) {
        return;
    }

    int line_number = 1;
    const char *pos = content;

    // Look for performance anti-patterns
    while (*pos) {
        if (*pos == '\n') {
            line_number++;
        }

        // Check for inefficient patterns
        if (strncmp(pos, "cat ", 4) == 0 && strstr(pos, " | ") != NULL) {
            debug_add_analysis_issue(ctx, file, line_number, "info",
                                     "performance", "Useless use of cat",
                                     "Use input redirection instead");
        }

        if (strncmp(pos, "$(ls ", 5) == 0 || strncmp(pos, "`ls ", 4) == 0) {
            debug_add_analysis_issue(ctx, file, line_number, "warning",
                                     "performance", "Parsing ls output",
                                     "Use shell globbing instead");
        }

        if (strncmp(pos, "for ", 4) == 0 && strstr(pos, "$(seq ") != NULL) {
            debug_add_analysis_issue(ctx, file, line_number, "info",
                                     "performance", "Inefficient loop",
                                     "Use arithmetic expansion instead");
        }

        pos++;
    }
}

/**
 * @brief Analyze script for security issues
 * @param ctx Debug context
 * @param file File path being analyzed
 * @param content Script content to analyze
 */
static void debug_analyze_security(debug_context_t *ctx, const char *file,
                                   const char *content) {
    if (!ctx || !file || !content) {
        return;
    }

    int line_number = 1;
    const char *pos = content;

    // Look for security issues
    while (*pos) {
        if (*pos == '\n') {
            line_number++;
        }

        // Check for unquoted variables
        if (*pos == '$' && *(pos + 1) != '(' && *(pos + 1) != '{') {
            const char *var_start = pos + 1;
            const char *var_end = var_start;

            // Find end of variable name
            while (*var_end && (isalnum(*var_end) || *var_end == '_')) {
                var_end++;
            }

            if (var_end > var_start) {
                debug_add_analysis_issue(
                    ctx, file, line_number, "warning", "security",
                    "Unquoted variable",
                    "Quote variables to prevent word splitting");
            }
        }

        // Check for eval usage
        if (strncmp(pos, "eval ", 5) == 0) {
            debug_add_analysis_issue(ctx, file, line_number, "error",
                                     "security", "Use of eval",
                                     "Avoid eval for security reasons");
        }

        // Check for dangerous commands
        if (strncmp(pos, "rm -rf ", 7) == 0) {
            debug_add_analysis_issue(ctx, file, line_number, "warning",
                                     "security", "Dangerous rm command",
                                     "Be careful with recursive deletion");
        }

        if (strncmp(pos, "chmod 777 ", 10) == 0) {
            debug_add_analysis_issue(ctx, file, line_number, "warning",
                                     "security", "Overly permissive chmod",
                                     "Use minimal necessary permissions");
        }

        pos++;
    }
}

/**
 * @brief Analyze script for portability issues
 * @param ctx Debug context
 * @param file File path being analyzed
 * @param content Script content to analyze
 * @param ast Parsed AST (may be NULL if parsing failed)
 *
 * This function performs three levels of portability analysis:
 * 1. AST-based checks (most accurate, no false positives from strings/comments)
 * 2. Pattern-based TOML database checks (catches things AST might miss)
 * 3. Legacy pattern-based checks for common issues
 */
static void debug_analyze_portability(debug_context_t *ctx, const char *file,
                                      const char *content, node_t *ast) {
    if (!ctx || !file || !content) {
        return;
    }

    // Initialize compat system if not already done. compat_init() already
    // preserves any pre-set target across the reset, so no caller-side
    // save/restore is needed — and attempting one (passing the buffer
    // pointer returned by compat_get_target() back into compat_set_target())
    // produces a strncpy src/dst overlap.
    if (compat_get_entry_count() == 0) {
        compat_init(NULL);
    }

    const char *target_str = compat_get_target();

    // Convert string target to enum for API functions that still use
    // shell_mode_t
    shell_mode_t target = SHELL_MODE_POSIX;
    if (target_str) {
        shell_mode_parse(target_str, &target);
    }

    // === Level 1: AST-based checking (most accurate) ===
    // Process AST findings one at a time to avoid static buffer issues
    if (ast) {
        compat_ast_issue_t ast_issues[64];
        size_t ast_found = compat_check_ast_issues(ast, target, ast_issues, 64);

        for (size_t i = 0; i < ast_found; i++) {
            debug_add_analysis_issue(
                ctx, file, ast_issues[i].line, ast_issues[i].severity,
                "portability", ast_issues[i].message, ast_issues[i].suggestion);
        }
    }

    // === Level 2: Pattern-based TOML database checks ===
    // These catch constructs that may not have dedicated AST node types
    compat_result_t results[64];
    size_t found = compat_check_script(content, target, results, 64);

    for (size_t i = 0; i < found; i++) {
        const compat_entry_t *entry = results[i].entry;
        if (!entry)
            continue;

        // Skip entries that are covered by AST-based checking to avoid
        // duplicates AST covers: extended_test, arithmetic_command,
        // arithmetic_for, process_substitution, arrays, here_string,
        // redirect_both, redirect_append_both, redirect_fd, coproc,
        // select_loop, time_keyword, anonymous_function
        if (entry->feature) {
            static const char *ast_covered_features[] = {"extended_test",
                                                         "arithmetic_command",
                                                         "arithmetic_for",
                                                         "process_substitution",
                                                         "arrays",
                                                         "here_string",
                                                         "redirect_both",
                                                         "redirect_append_both",
                                                         "redirect_fd",
                                                         "coproc",
                                                         "select_loop",
                                                         "time_keyword",
                                                         "anonymous_function",
                                                         NULL};

            bool skip = false;
            for (const char **feat = ast_covered_features; *feat; feat++) {
                if (lle_unicode_strings_equal(entry->feature, *feat,
                                              &LLE_UNICODE_COMPARE_DEFAULT)) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                continue; // Already handled by AST analysis
            }
        }

        const char *severity =
            compat_severity_name(compat_effective_severity(entry));

        debug_add_analysis_issue(
            ctx, file, results[i].line, severity, "portability",
            entry->lint.message ? entry->lint.message : entry->description,
            entry->lint.suggestion);
    }

    // === Level 3: Legacy pattern-based checks ===
    // These are simple checks not yet in the TOML database
    // Only report if targeting POSIX (these features work in bash/zsh/lush)
    if (target == SHELL_MODE_POSIX) {
        int line_number = 1;
        const char *pos = content;

        while (*pos) {
            if (*pos == '\n') {
                line_number++;
            }

            // Check for bash-specific function syntax (not yet in AST)
            if (strncmp(pos, "function ", 9) == 0) {
                debug_add_analysis_issue(ctx, file, line_number, "info",
                                         "portability",
                                         "Bash-specific function syntax",
                                         "Use POSIX function syntax");
            }

            // Check for non-portable commands
            if (strncmp(pos, "echo -e ", 8) == 0) {
                debug_add_analysis_issue(
                    ctx, file, line_number, "warning", "portability",
                    "Non-portable echo option", "Use printf instead");
            }

            if (strncmp(pos, "source ", 7) == 0) {
                debug_add_analysis_issue(ctx, file, line_number, "info",
                                         "portability", "Bash-specific source",
                                         "Use . instead for POSIX compliance");
            }

            pos++;
        }
    }
}

/* ============================================================================
 * Type-mismatch analysis (predictive, per SEMANTICS.md section 3.9)
 * ============================================================================
 *
 * SEMANTICS section 3.9 makes a list value in a scalar-requiring slot a
 * runtime type error -- enforced at runtime by executor.c (commit
 * 1c5e587f). This pass catches the same condition statically, so the
 * user can see the issue without running the script.
 *
 * Pattern-based, not full type inference. It detects ${...[@]...}
 * (and the parameter-flag (k)/(v)/(kv) vector operators) appearing
 * immediately after an "=" assignment marker, with or without an
 * intervening double quote. This covers the most common bug shape:
 *
 *     x=${arr[@]}              -- assignment RHS
 *     x="${arr[@]}"            -- quoted assignment RHS (quotes are a
 *                                 whitespace anchor, not a type op)
 *
 * Bare ${arr} references (where the type depends on runtime value)
 * and cases inside command substitution or arithmetic are out of
 * scope for the pattern pass -- they need real type inference.
 *
 * False positives are possible (e.g. a literal "=" inside a comment
 * followed by "${...[@]}" syntax illustrated in documentation). The
 * pass skips lines whose first non-whitespace character is "#".
 */

/**
 * @brief Scan a ${...} expansion for a vector-yielding subscript
 *
 * Finds [@] subscripts and the parameter-flag (k)/(v)/(kv) vector
 * operators inside the brace expansion that begins at @p start. The
 * (i)/(I) flags yield indices (scalars) and are excluded.
 *
 * @param start Pointer to the leading '$' of "${...".
 * @param avail Bytes available from @p start to the end of the line.
 * @param out_has_vector Set to true if a vector trigger was found.
 * @param out_end Position immediately after the closing '}'.
 * @return true if a complete "${...}" was scanned (closing brace
 *         found), false if the expansion was unterminated.
 */
static bool scan_vector_expansion(const char *start, size_t avail,
                                  bool *out_has_vector, size_t *out_end) {
    if (avail < 3 || start[0] != '$' || start[1] != '{') {
        return false;
    }
    size_t i = 2;
    int depth = 1;
    bool has_vector = false;
    while (i < avail && depth > 0) {
        char c = start[i];
        if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                i++;
                break;
            }
        } else if (c == '[' && i + 2 < avail && start[i + 1] == '@' &&
                   start[i + 2] == ']') {
            /* The [@] subscript is the canonical vector trigger. */
            has_vector = true;
        } else if (c == '(' && i + 2 < avail && depth == 1) {
            /* Parameter-flag operators (k) / (v) / (kv) yield vectors
             * from a map. (i)/(I) yield indices (scalars) and are not
             * vector-yielding -- exclude them. */
            char f1 = start[i + 1];
            char f2 = start[i + 2];
            if ((f1 == 'k' || f1 == 'v') &&
                (f2 == ')' || f2 == 'k' || f2 == 'v')) {
                has_vector = true;
            }
        }
        i++;
    }
    if (depth != 0) {
        return false; /* no closing brace -- bail */
    }
    *out_has_vector = has_vector;
    *out_end = i;
    return true;
}

/**
 * @brief Static type-mismatch warning pass (SEMANTICS.md section 3.9)
 *
 * Walks the script content line by line and flags ${...[@]...} (plus
 * the parameter-flag (k)/(v)/(kv) vector operators) appearing as the
 * RHS of an "=" assignment, with or without an intervening double
 * quote. Adds entries under the "type" category to ctx's analysis
 * issue list. Comment-only lines are skipped; "==" string-equality
 * is excluded.
 *
 * @param ctx Debug context to receive analysis issues.
 * @param file Script path (for issue line citations).
 * @param content Full script content as a single NUL-terminated string.
 */
static void debug_analyze_types(debug_context_t *ctx, const char *file,
                                const char *content) {
    if (!ctx || !file || !content) {
        return;
    }

    int line_num = 1;
    const char *line_start = content;
    size_t content_len = strlen(content);

    for (size_t i = 0; i <= content_len; i++) {
        if (i == content_len || content[i] == '\n') {
            size_t line_len = (size_t)(content + i - line_start);

            /* Skip blank lines and shell comments outright. */
            const char *trim = line_start;
            const char *end = line_start + line_len;
            while (trim < end && (*trim == ' ' || *trim == '\t')) {
                trim++;
            }
            if (trim >= end || *trim == '#') {
                goto next_line;
            }

            /* Walk the line scanning for "=" followed (with optional
             * double quote) by a ${...} expansion that contains [@]. */
            for (size_t k = 0; k + 2 < line_len; k++) {
                if (line_start[k] != '=') {
                    continue;
                }
                /* Skip "==" (string equality), not assignment. */
                if (k + 1 < line_len && line_start[k + 1] == '=') {
                    continue;
                }
                size_t e = k + 1;
                if (e < line_len && line_start[e] == '"') {
                    e++;
                }
                if (e + 1 >= line_len) {
                    continue;
                }
                if (line_start[e] != '$' || line_start[e + 1] != '{') {
                    continue;
                }
                bool has_vector = false;
                size_t end_pos = 0;
                if (!scan_vector_expansion(line_start + e, line_len - e,
                                           &has_vector, &end_pos)) {
                    continue;
                }
                if (has_vector) {
                    debug_add_analysis_issue(
                        ctx, file, line_num, "warning", "type",
                        "list value (`${...[@]}`) reaches a scalar "
                        "assignment RHS -- runtime type error per "
                        "SEMANTICS section 3.9",
                        "use ${name[*]} for a space-joined scalar, "
                        "or use the array directly in a vector-"
                        "accepting position (argv, for-in list)");
                    k = e + end_pos; /* don't double-flag */
                }
            }

        next_line:
            line_start = content + i + 1;
            line_num++;
        }
    }
}

/**
 * @brief Display the analysis report
 * @param ctx Debug context containing analysis results
 */
void debug_show_analysis_report(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    debug_print_header(ctx, "Script Analysis Report");

    if (ctx->issue_count == 0) {
        debug_printf(ctx, "No issues found - script looks good!\n");
        return;
    }

    // Count issues by severity
    int error_count = 0, warning_count = 0, info_count = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->severity, "error") == 0) {
            error_count++;
        } else if (strcmp(issue->severity, "warning") == 0) {
            warning_count++;
        } else if (strcmp(issue->severity, "info") == 0) {
            info_count++;
        }
        issue = issue->next;
    }

    debug_printf(ctx,
                 "Issues found: %d total (%d errors, %d warnings, %d info)\n\n",
                 ctx->issue_count, error_count, warning_count, info_count);

    // Show issues by category
    const char *categories[] = {"syntax", "security",    "performance",
                                "style",  "portability", "type"};
    const char *category_names[] = {"Syntax", "Security",    "Performance",
                                    "Style",  "Portability", "Type"};

    for (int i = 0; i < 6; i++) {
        bool has_issues = false;
        issue = ctx->analysis_issues;

        // Check if we have issues in this category
        while (issue) {
            if (strcmp(issue->category, categories[i]) == 0) {
                has_issues = true;
                break;
            }
            issue = issue->next;
        }

        if (!has_issues) {
            continue;
        }

        debug_printf(ctx, "%s Issues:\n", category_names[i]);
        debug_printf(ctx, "%-8s %-4s %-60s\n", "Severity", "Line", "Message");
        debug_printf(ctx, "%-8s %-4s %-60s\n", "--------", "----", "-------");

        issue = ctx->analysis_issues;
        while (issue) {
            if (strcmp(issue->category, categories[i]) == 0) {
                debug_printf(ctx, "%-8s %-4d %s\n", issue->severity,
                             issue->line_number, issue->message);
                if (issue->suggestion) {
                    debug_printf(ctx, "         %-4s Suggestion: %s\n", "",
                                 issue->suggestion);
                }
            }
            issue = issue->next;
        }
        debug_printf(ctx, "\n");
    }

    // Summary and recommendations
    debug_printf(ctx, "Summary:\n");
    if (error_count > 0) {
        debug_printf(
            ctx, "  WARNING: %d syntax or critical errors need to be fixed\n",
            error_count);
    }
    if (warning_count > 0) {
        debug_printf(ctx, "  WARNING: %d warnings should be addressed\n",
                     warning_count);
    }
    if (info_count > 0) {
        debug_printf(ctx, "  INFO: %d informational items for improvement\n",
                     info_count);
    }

    debug_printf(ctx, "\nRecommendations:\n");
    debug_printf(ctx, "  - Fix all syntax errors before running the script\n");
    debug_printf(ctx,
                 "  - Address security warnings to prevent vulnerabilities\n");
    debug_printf(
        ctx, "  - Consider performance suggestions for better efficiency\n");
    debug_printf(ctx, "  - Follow style guidelines for maintainability\n");
    debug_printf(
        ctx,
        "  - Address portability issues for cross-platform compatibility\n");
}

/**
 * @brief Display analysis report with mode filtering
 *
 * In LINT mode, only shows warnings and errors (skips info items).
 * This makes the output actionable rather than informational.
 *
 * @param ctx Debug context containing analysis results
 * @param mode Analysis mode (FULL or LINT)
 */
void debug_show_analysis_report_filtered(debug_context_t *ctx,
                                         analysis_mode_t mode) {
    if (!ctx) {
        return;
    }

    const char *header =
        (mode == ANALYSIS_MODE_LINT) ? "Lint Report" : "Script Analysis Report";
    debug_print_header(ctx, header);

    // Count issues by severity (respecting mode filter)
    int error_count = 0, warning_count = 0, info_count = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->severity, "error") == 0) {
            error_count++;
        } else if (strcmp(issue->severity, "warning") == 0) {
            warning_count++;
        } else if (strcmp(issue->severity, "info") == 0) {
            info_count++;
        }
        issue = issue->next;
    }

    // In lint mode, only count actionable items
    int actionable_count = error_count + warning_count;
    if (mode == ANALYSIS_MODE_LINT) {
        if (actionable_count == 0) {
            debug_printf(ctx, "No actionable issues found.\n");
            return;
        }
        debug_printf(ctx, "Issues found: %d (%d errors, %d warnings)\n\n",
                     actionable_count, error_count, warning_count);
    } else {
        if (ctx->issue_count == 0) {
            debug_printf(ctx, "No issues found - script looks good!\n");
            return;
        }
        debug_printf(
            ctx, "Issues found: %d total (%d errors, %d warnings, %d info)\n\n",
            ctx->issue_count, error_count, warning_count, info_count);
    }

    // Show issues by category
    const char *categories[] = {"syntax", "security",    "performance",
                                "style",  "portability", "type"};
    const char *category_names[] = {"Syntax", "Security",    "Performance",
                                    "Style",  "Portability", "Type"};

    for (int i = 0; i < 6; i++) {
        bool has_issues = false;
        issue = ctx->analysis_issues;

        // Check if we have issues in this category (respecting mode filter)
        while (issue) {
            if (strcmp(issue->category, categories[i]) == 0) {
                // In lint mode, skip info items
                if (mode == ANALYSIS_MODE_LINT &&
                    strcmp(issue->severity, "info") == 0) {
                    issue = issue->next;
                    continue;
                }
                has_issues = true;
                break;
            }
            issue = issue->next;
        }

        if (!has_issues) {
            continue;
        }

        debug_printf(ctx, "%s Issues:\n", category_names[i]);
        debug_printf(ctx, "%-8s %-4s %-60s\n", "Severity", "Line", "Message");
        debug_printf(ctx, "%-8s %-4s %-60s\n", "--------", "----", "-------");

        issue = ctx->analysis_issues;
        while (issue) {
            if (strcmp(issue->category, categories[i]) == 0) {
                // In lint mode, skip info items
                if (mode == ANALYSIS_MODE_LINT &&
                    strcmp(issue->severity, "info") == 0) {
                    issue = issue->next;
                    continue;
                }
                debug_printf(ctx, "%-8s %-4d %s\n", issue->severity,
                             issue->line_number, issue->message);
                if (issue->suggestion) {
                    debug_printf(ctx, "         %-4s Suggestion: %s\n", "",
                                 issue->suggestion);
                }
            }
            issue = issue->next;
        }
        debug_printf(ctx, "\n");
    }

    // Summary (different for lint vs analyze)
    if (mode == ANALYSIS_MODE_LINT) {
        debug_printf(ctx, "Summary:\n");
        if (error_count > 0) {
            debug_printf(ctx, "  %d error(s) must be fixed\n", error_count);
        }
        if (warning_count > 0) {
            debug_printf(ctx, "  %d warning(s) should be addressed\n",
                         warning_count);
        }
    } else {
        debug_printf(ctx, "Summary:\n");
        if (error_count > 0) {
            debug_printf(
                ctx,
                "  WARNING: %d syntax or critical errors need to be fixed\n",
                error_count);
        }
        if (warning_count > 0) {
            debug_printf(ctx, "  WARNING: %d warnings should be addressed\n",
                         warning_count);
        }
        if (info_count > 0) {
            debug_printf(ctx,
                         "  INFO: %d informational items for improvement\n",
                         info_count);
        }

        debug_printf(ctx, "\nRecommendations:\n");
        debug_printf(ctx,
                     "  - Fix all syntax errors before running the script\n");
        debug_printf(
            ctx, "  - Address security warnings to prevent vulnerabilities\n");
        debug_printf(
            ctx,
            "  - Consider performance suggestions for better efficiency\n");
        debug_printf(ctx, "  - Follow style guidelines for maintainability\n");
        debug_printf(ctx, "  - Address portability issues for cross-platform "
                          "compatibility\n");
    }
}

/**
 * @brief Lint a script for actionable issues with optional auto-fix
 *
 * Unlike debug_analyze_script(), this function:
 * - Only shows warnings and errors (not info items)
 * - Can optionally apply automatic fixes
 * - Returns the number of remaining unfixed issues
 *
 * @param ctx Debug context for output
 * @param script_path Path to the script file to lint
 * @param fix Apply safe fixes
 * @param unsafe_fixes Also apply unsafe fixes
 * @param dry_run Preview fixes without applying
 * @return Number of unfixed issues remaining (0 = success)
 */
int debug_lint_script(debug_context_t *ctx, const char *script_path, bool fix,
                      bool unsafe_fixes, bool dry_run) {
    if (!ctx || !script_path) {
        return -1;
    }

    debug_printf(ctx, "Linting script: %s\n", script_path);

    // Check if file exists
    struct stat st;
    if (stat(script_path, &st) != 0) {
        debug_printf(ctx, "ERROR: Script file not found: %s\n", script_path);
        return -1;
    }

    // Read script file
    FILE *file = fopen(script_path, "r");
    if (!file) {
        debug_printf(ctx, "ERROR: Cannot open script file: %s\n", script_path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *script_content = malloc(file_size + 1);
    if (!script_content) {
        fclose(file);
        debug_printf(ctx, "ERROR: Memory allocation failed\n");
        return -1;
    }

    size_t bytes_read = fread(script_content, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        debug_printf(ctx, "ERROR: Short read on %s (expected %ld, got %zu)\n",
                     script_path, file_size, bytes_read);
        free(script_content);
        return -1;
    }
    script_content[file_size] = '\0';

    // Clear previous analysis results
    debug_clear_analysis_issues(ctx);

    // Perform analysis (same as analyze, we'll filter in the report)
    node_t *ast = debug_analyze_syntax(ctx, script_path, script_content);
    debug_analyze_style(ctx, script_path, script_content);
    debug_analyze_performance(ctx, script_path, script_content);
    debug_analyze_security(ctx, script_path, script_content);
    debug_analyze_portability(ctx, script_path, script_content, ast);

    // Count actionable issues (errors + warnings only)
    int error_count = 0, warning_count = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->severity, "error") == 0) {
            error_count++;
        } else if (strcmp(issue->severity, "warning") == 0) {
            warning_count++;
        }
        issue = issue->next;
    }

    int actionable_count = error_count + warning_count;

    // Handle fix mode
    if (fix && actionable_count > 0) {
        fixer_context_t fixer_ctx;
        if (fixer_init(&fixer_ctx) == FIXER_OK) {
            if (fixer_load_string(&fixer_ctx, script_content, script_path) ==
                FIXER_OK) {
                // Convert string target to enum for fixer API
                shell_mode_t target = SHELL_MODE_POSIX;
                const char *target_str = compat_get_target();
                if (target_str) {
                    shell_mode_parse(target_str, &target);
                }
                size_t fixes_found = fixer_collect_fixes(&fixer_ctx, target);

                if (fixes_found > 0) {
                    fixer_options_t opts = {
                        .include_unsafe = unsafe_fixes,
                        .dry_run = dry_run,
                        .create_backup = true,
                        .verify_syntax = true,
                        .target = target,
                    };

                    if (dry_run) {
                        debug_printf(ctx, "\nDry run - would apply fixes:\n");
                        fixer_print_diff(&fixer_ctx, &opts);
                    } else {
                        char *fixed_content = NULL;
                        size_t applied = 0;

                        if (fixer_apply_fixes_alloc(&fixer_ctx, &opts,
                                                    &fixed_content,
                                                    &applied) == FIXER_OK) {
                            if (applied > 0) {
                                // Verify syntax before writing
                                if (fixer_verify_syntax(fixed_content,
                                                        target)) {
                                    if (fixer_write_file(script_path,
                                                         fixed_content,
                                                         true) == FIXER_OK) {
                                        debug_printf(
                                            ctx,
                                            "\nApplied %zu fix(es) to %s\n",
                                            applied, script_path);
                                        debug_printf(ctx,
                                                     "Backup saved to %s.bak\n",
                                                     script_path);
                                        actionable_count -= (int)applied;
                                    } else {
                                        debug_printf(ctx, "ERROR: Failed to "
                                                          "write fixed file\n");
                                    }
                                } else {
                                    debug_printf(ctx, "ERROR: Fixed script has "
                                                      "syntax errors, "
                                                      "not applying\n");
                                }
                            }
                            free(fixed_content);
                        }
                    }

                    fixer_print_summary(&fixer_ctx, &opts);
                } else {
                    debug_printf(ctx, "\nNo automatic fixes available.\n");
                }
            }
            fixer_cleanup(&fixer_ctx);
        }
    }

    // Show filtered report (lint mode - no info items)
    debug_show_analysis_report_filtered(ctx, ANALYSIS_MODE_LINT);

    // Cleanup
    if (ast) {
        free_node_tree(ast);
    }
    free(script_content);

    // Return remaining unfixed issues
    return (actionable_count > 0) ? actionable_count : 0;
}
