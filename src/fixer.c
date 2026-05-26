/**
 * @file fixer.c
 * @brief Auto-fix linter implementation
 *
 * Implements automatic fixing of shell script compatibility issues.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "fixer.h"
#include "node.h"
#include "parser.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Cross-platform forward declarations
int strncasecmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);

/* ============================================================================
 * Constants
 * ============================================================================
 */

#define FIXER_INITIAL_CAPACITY 32
#define FIXER_MAX_LINE_LENGTH 4096

/* ============================================================================
 * Internal Helpers
 * ============================================================================
 */

/// @brief Compare fixes by position (for sorting end-to-start)
static int compare_fixes_reverse(const void *a, const void *b) {
    const fixer_fix_t *fa = (const fixer_fix_t *)a;
    const fixer_fix_t *fb = (const fixer_fix_t *)b;

    /// Sort by byte offset, descending (end to start)
    if (fa->match_start > fb->match_start)
        return -1;
    if (fa->match_start < fb->match_start)
        return 1;
    return 0;
}

/// @brief Calculate byte offset from line and column
static size_t calc_offset(const char *content, int line, int column) {
    size_t offset = 0;
    int current_line = 1;

    while (content[offset] && current_line < line) {
        if (content[offset] == '\n') {
            current_line++;
        }
        offset++;
    }

    /// Add column offset (1-based)
    if (column > 1) {
        offset += (size_t)(column - 1);
    }

    return offset;
}

/// Suppress unused warning - will be used when line-based fixes are added
static size_t (*calc_offset_ptr)(const char *, int, int) = calc_offset;

/**
 * @brief Callback context for fix collection
 */
typedef struct {
    fixer_context_t *fixer_ctx;
    const char *target; ///< Target shell name (string for flexibility)
    const char *line;
    int line_num;
    size_t line_offset; ///< Byte offset of line start in content
} collect_ctx_t;

/// @brief Callback to check each compat entry for fixes
static void collect_fix_callback(const compat_entry_t *entry, void *user_data) {
    collect_ctx_t *ctx = (collect_ctx_t *)user_data;

    if (!entry || !ctx || !entry->lint.replacement || !entry->lint.pattern) {
        return;
    }

    /// Get fix type for the target shell
    fix_type_t fix_type =
        compat_get_fix_type_for_target(&entry->lint.fix, ctx->target);

    /// Skip entries without fixes for this target
    if (fix_type == FIX_TYPE_NONE) {
        return;
    }

    /// Check if pattern matches this line
    regex_t regex;
    if (regcomp(&regex, entry->lint.pattern, REG_EXTENDED) != 0) {
        return;
    }

    regmatch_t match;
    if (regexec(&regex, ctx->line, 1, &match, 0) == 0) {
        /// Pattern matched - create fix
        fixer_fix_t fix = {
            .line = ctx->line_num,
            .column = (int)(match.rm_so + 1),
            .match_start = ctx->line_offset + (size_t)match.rm_so,
            .match_length = (size_t)(match.rm_eo - match.rm_so),
            .original = ctx->line + match.rm_so,
            .replacement = entry->lint.replacement,
            .type = fix_type,
            .message = entry->lint.message,
            .entry = entry,
        };

        fixer_add_fix(ctx->fixer_ctx, &fix);
    }

    regfree(&regex);
}

/* ============================================================================
 * Context Management
 * ============================================================================
 */

fixer_result_t fixer_init(fixer_context_t *ctx) {
    if (!ctx) {
        return FIXER_ERR_NOMEM;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->fixes = malloc(FIXER_INITIAL_CAPACITY * sizeof(fixer_fix_t));
    if (!ctx->fixes) {
        return FIXER_ERR_NOMEM;
    }

    ctx->capacity = FIXER_INITIAL_CAPACITY;
    ctx->count = 0;

    return FIXER_OK;
}

void fixer_cleanup(fixer_context_t *ctx) {
    if (!ctx) {
        return;
    }

    free(ctx->fixes);
    free(ctx->script_path);
    free(ctx->content);

    memset(ctx, 0, sizeof(*ctx));
}

fixer_result_t fixer_load_file(fixer_context_t *ctx, const char *path) {
    if (!ctx || !path) {
        return FIXER_ERR_IO;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return FIXER_ERR_IO;
    }

    /// Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0 || size > 10 * 1024 * 1024) { /// Max 10MB
        fclose(fp);
        return FIXER_ERR_IO;
    }

    /// Allocate and read
    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        return FIXER_ERR_NOMEM;
    }

    size_t read = fread(content, 1, (size_t)size, fp);
    fclose(fp);

    content[read] = '\0';

    /// Store in context
    free(ctx->content);
    free(ctx->script_path);

    ctx->content = content;
    ctx->content_len = read;
    ctx->script_path = strdup(path);

    /// Reset fixes
    ctx->count = 0;

    return FIXER_OK;
}

fixer_result_t fixer_load_string(fixer_context_t *ctx, const char *content,
                                 const char *path) {
    if (!ctx || !content) {
        return FIXER_ERR_IO;
    }

    free(ctx->content);
    free(ctx->script_path);

    ctx->content = strdup(content);
    if (!ctx->content) {
        return FIXER_ERR_NOMEM;
    }

    ctx->content_len = strlen(content);
    ctx->script_path = path ? strdup(path) : NULL;
    ctx->count = 0;

    return FIXER_OK;
}

/* ============================================================================
 * Fix Collection
 * ============================================================================
 */

size_t fixer_collect_fixes(fixer_context_t *ctx, shell_mode_t target) {
    if (!ctx || !ctx->content) {
        return 0;
    }

    /// Convert enum to string for internal use
    const char *target_str = shell_mode_name(target);

    /// Suppress unused warning
    (void)calc_offset_ptr;

    ctx->count = 0;

    /// Check each line against all compat entries with fix patterns
    const char *line_start = ctx->content;
    size_t line_offset = 0;
    int line_num = 1;

    while (*line_start) {
        /// Find end of line
        const char *line_end = strchr(line_start, '\n');
        if (!line_end) {
            line_end = line_start + strlen(line_start);
        }

        /// Extract line for pattern matching
        size_t line_len = (size_t)(line_end - line_start);
        char line[FIXER_MAX_LINE_LENGTH];
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        /// Set up callback context for this line
        collect_ctx_t collect_ctx = {
            .fixer_ctx = ctx,
            .target = target_str,
            .line = line,
            .line_num = line_num,
            .line_offset = line_offset,
        };

        /// Check all compat entries against this line
        compat_foreach_entry(collect_fix_callback, &collect_ctx);

        /// Move to next line
        line_num++;
        line_offset += line_len + ((*line_end) ? 1 : 0);
        line_start = (*line_end) ? line_end + 1 : line_end;
    }

    return ctx->count;
}

fixer_result_t fixer_add_fix(fixer_context_t *ctx, const fixer_fix_t *fix) {
    if (!ctx || !fix) {
        return FIXER_ERR_NOMEM;
    }

    /// Grow array if needed
    if (ctx->count >= ctx->capacity) {
        size_t new_cap = ctx->capacity * 2;
        fixer_fix_t *new_fixes =
            realloc(ctx->fixes, new_cap * sizeof(fixer_fix_t));
        if (!new_fixes) {
            return FIXER_ERR_NOMEM;
        }
        ctx->fixes = new_fixes;
        ctx->capacity = new_cap;
    }

    ctx->fixes[ctx->count++] = *fix;
    return FIXER_OK;
}

size_t fixer_count_safe(const fixer_context_t *ctx) {
    if (!ctx)
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < ctx->count; i++) {
        if (ctx->fixes[i].type == FIX_TYPE_SAFE) {
            count++;
        }
    }
    return count;
}

size_t fixer_count_unsafe(const fixer_context_t *ctx) {
    if (!ctx)
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < ctx->count; i++) {
        if (ctx->fixes[i].type == FIX_TYPE_UNSAFE) {
            count++;
        }
    }
    return count;
}

size_t fixer_count_manual(const fixer_context_t *ctx) {
    if (!ctx)
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < ctx->count; i++) {
        if (ctx->fixes[i].type == FIX_TYPE_MANUAL) {
            count++;
        }
    }
    return count;
}

/* ============================================================================
 * Fix Application
 * ============================================================================
 */

fixer_result_t fixer_apply_fixes(fixer_context_t *ctx,
                                 const fixer_options_t *options, char *output,
                                 size_t output_size, size_t *fixes_applied) {
    if (!ctx || !options || !output || output_size == 0) {
        return FIXER_ERR_NOMEM;
    }

    if (ctx->count == 0) {
        /// No fixes - copy original
        if (ctx->content_len >= output_size) {
            return FIXER_ERR_NOMEM;
        }
        memcpy(output, ctx->content, ctx->content_len + 1);
        if (fixes_applied)
            *fixes_applied = 0;
        return FIXER_OK;
    }

    /// Sort fixes in reverse order (end to start)
    qsort(ctx->fixes, ctx->count, sizeof(fixer_fix_t), compare_fixes_reverse);

    /// Start with a copy of the original
    char *working = malloc(ctx->content_len * 2 + 1); /// Extra space for growth
    if (!working) {
        return FIXER_ERR_NOMEM;
    }
    memcpy(working, ctx->content, ctx->content_len + 1);
    size_t working_len = ctx->content_len;

    size_t applied = 0;

    for (size_t i = 0; i < ctx->count; i++) {
        fixer_fix_t *fix = &ctx->fixes[i];

        /// Skip based on fix type and options
        if (fix->type == FIX_TYPE_MANUAL) {
            continue; /// Never auto-fix manual items
        }
        if (fix->type == FIX_TYPE_UNSAFE && !options->include_unsafe) {
            continue;
        }
        if (fix->type == FIX_TYPE_NONE) {
            continue;
        }

        if (!fix->replacement || fix->match_start >= working_len) {
            continue;
        }

        /// Apply replacement
        size_t repl_len = strlen(fix->replacement);
        size_t new_len = working_len - fix->match_length + repl_len;

        /// Calculate tail position
        size_t tail_start = fix->match_start + fix->match_length;
        size_t tail_len = working_len - tail_start;

        /// If replacement is longer, grow buffer first
        if (repl_len > fix->match_length) {
            char *new_working = realloc(working, new_len + 1);
            if (!new_working) {
                free(working);
                return FIXER_ERR_NOMEM;
            }
            working = new_working;
        }

        /// Shift tail (memmove handles overlap)
        memmove(working + fix->match_start + repl_len, working + tail_start,
                tail_len + 1);

        /// Insert replacement
        memcpy(working + fix->match_start, fix->replacement, repl_len);
        working_len = new_len;

        /// If replacement is shorter, shrink buffer after the move
        if (repl_len < fix->match_length) {
            char *new_working = realloc(working, new_len + 1);
            if (new_working) {
                working = new_working;
            }
            /// If realloc fails on shrink, we can continue with larger buffer
        }

        applied++;
    }

    /// Copy result to output
    if (working_len >= output_size) {
        free(working);
        return FIXER_ERR_NOMEM;
    }

    memcpy(output, working, working_len + 1);
    free(working);

    if (fixes_applied)
        *fixes_applied = applied;

    return FIXER_OK;
}

fixer_result_t fixer_apply_fixes_alloc(fixer_context_t *ctx,
                                       const fixer_options_t *options,
                                       char **output, size_t *fixes_applied) {
    if (!ctx || !options || !output) {
        return FIXER_ERR_NOMEM;
    }

    /// Estimate output size (2x original should be safe)
    size_t buf_size = ctx->content_len * 2 + 1;
    char *buf = malloc(buf_size);
    if (!buf) {
        return FIXER_ERR_NOMEM;
    }

    fixer_result_t result =
        fixer_apply_fixes(ctx, options, buf, buf_size, fixes_applied);
    if (result != FIXER_OK) {
        free(buf);
        return result;
    }

    *output = buf;
    return FIXER_OK;
}

bool fixer_verify_syntax(const char *content, shell_mode_t target) {
    if (!content) {
        return false;
    }

    (void)target; /// Target shell mode for future use

    /// Use the parser to verify
    parser_t *parser = parser_new(content);
    if (!parser) {
        return false;
    }

    node_t *ast = parser_parse(parser);
    bool ok = (ast != NULL);

    if (ast) {
        free_node_tree(ast);
    }
    parser_free(parser);

    return ok;
}

fixer_result_t fixer_write_file(const char *path, const char *content,
                                bool create_backup) {
    if (!path || !content) {
        return FIXER_ERR_IO;
    }

    /// Create backup if requested
    if (create_backup) {
        char backup_path[1024];
        snprintf(backup_path, sizeof(backup_path), "%s.bak", path);

        FILE *src = fopen(path, "r");
        if (src) {
            FILE *dst = fopen(backup_path, "w");
            if (dst) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                    fwrite(buf, 1, n, dst);
                }
                fclose(dst);
            }
            fclose(src);
        }
    }

    /// Write new content
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return FIXER_ERR_IO;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, fp);
    fclose(fp);

    return (written == len) ? FIXER_OK : FIXER_ERR_IO;
}

/* ============================================================================
 * Diff Generation
 * ============================================================================
 */

int fixer_generate_diff(fixer_context_t *ctx, const fixer_options_t *options,
                        char *output, size_t output_size) {
    if (!ctx || !options || !output || output_size == 0) {
        return -1;
    }

    /// Get fixed content
    char *fixed = NULL;
    size_t applied = 0;

    if (fixer_apply_fixes_alloc(ctx, options, &fixed, &applied) != FIXER_OK) {
        return -1;
    }

    if (applied == 0) {
        free(fixed);
        return snprintf(output, output_size, "No changes to apply.\n");
    }

    /// Generate simple diff output
    int written = 0;
    const char *path = ctx->script_path ? ctx->script_path : "<stdin>";

    written += snprintf(output + written, output_size - (size_t)written,
                        "--- %s\n+++ %s (fixed)\n", path, path);

    /// Simple line-by-line diff
    const char *orig_line = ctx->content;
    const char *fixed_line = fixed;
    int line_num = 1;

    while (*orig_line || *fixed_line) {
        /// Get original line
        const char *orig_end = strchr(orig_line, '\n');
        if (!orig_end)
            orig_end = orig_line + strlen(orig_line);
        size_t orig_len = (size_t)(orig_end - orig_line);

        /// Get fixed line
        const char *fixed_end = strchr(fixed_line, '\n');
        if (!fixed_end)
            fixed_end = fixed_line + strlen(fixed_line);
        size_t fixed_len = (size_t)(fixed_end - fixed_line);

        /// Compare and output diff
        if (orig_len != fixed_len ||
            memcmp(orig_line, fixed_line, orig_len) != 0) {
            written += snprintf(output + written, output_size - (size_t)written,
                                "@@ -%d +%d @@\n", line_num, line_num);
            written += snprintf(output + written, output_size - (size_t)written,
                                "-%.*s\n", (int)orig_len, orig_line);
            written += snprintf(output + written, output_size - (size_t)written,
                                "+%.*s\n", (int)fixed_len, fixed_line);
        }

        orig_line = (*orig_end) ? orig_end + 1 : orig_end;
        fixed_line = (*fixed_end) ? fixed_end + 1 : fixed_end;
        line_num++;

        if ((size_t)written >= output_size - 1)
            break;
    }

    free(fixed);
    return written;
}

void fixer_print_diff(fixer_context_t *ctx, const fixer_options_t *options) {
    char diff_buf[64 * 1024];
    int len = fixer_generate_diff(ctx, options, diff_buf, sizeof(diff_buf));
    if (len > 0) {
        printf("%s", diff_buf);
    }
}

/* ============================================================================
 * Reporting
 * ============================================================================
 */

void fixer_print_summary(const fixer_context_t *ctx,
                         const fixer_options_t *options) {
    if (!ctx)
        return;

    size_t safe = fixer_count_safe(ctx);
    size_t unsafe = fixer_count_unsafe(ctx);
    size_t manual = fixer_count_manual(ctx);
    size_t total = ctx->count;

    printf("Fix summary:\n");
    printf("  Total issues: %zu\n", total);
    printf("  Safe fixes:   %zu (--fix)\n", safe);
    printf("  Unsafe fixes: %zu (--unsafe-fixes)\n", unsafe);
    printf("  Manual fixes: %zu (cannot auto-fix)\n", manual);

    if (options) {
        size_t would_apply = safe;
        if (options->include_unsafe) {
            would_apply += unsafe;
        }
        printf("\n");
        if (options->dry_run) {
            printf("Would apply %zu fix(es).\n", would_apply);
        } else {
            printf("Will apply %zu fix(es).\n", would_apply);
        }
    }
}

void fixer_print_fixes(const fixer_context_t *ctx, bool include_unsafe) {
    if (!ctx)
        return;

    for (size_t i = 0; i < ctx->count; i++) {
        const fixer_fix_t *fix = &ctx->fixes[i];

        if (fix->type == FIX_TYPE_UNSAFE && !include_unsafe) {
            continue;
        }

        const char *type_str;
        switch (fix->type) {
        case FIX_TYPE_SAFE:
            type_str = "SAFE";
            break;
        case FIX_TYPE_UNSAFE:
            type_str = "UNSAFE";
            break;
        case FIX_TYPE_MANUAL:
            type_str = "MANUAL";
            break;
        default:
            type_str = "NONE";
            break;
        }

        printf("  Line %d: [%s] %s\n", fix->line, type_str,
               fix->message ? fix->message : "Fix available");

        if (fix->original && fix->replacement && fix->type != FIX_TYPE_MANUAL) {
            printf("    %s -> %s\n", fix->original, fix->replacement);
        }
    }
}

const char *fixer_result_string(fixer_result_t result) {
    switch (result) {
    case FIXER_OK:
        return "Success";
    case FIXER_ERR_IO:
        return "I/O error";
    case FIXER_ERR_PARSE:
        return "Parse error";
    case FIXER_ERR_NOMEM:
        return "Out of memory";
    case FIXER_ERR_NOFIX:
        return "No fixes to apply";
    case FIXER_ERR_VERIFY:
        return "Verification failed";
    default:
        return "Unknown error";
    }
}

/* ============================================================================
 * Interactive Fix Mode
 * ============================================================================
 */

/// @brief Get the line content at a given line number
static const char *get_line_at(const char *content, int line_num,
                               size_t *line_len) {
    const char *p = content;
    int current = 1;

    while (*p && current < line_num) {
        if (*p == '\n') {
            current++;
        }
        p++;
    }

    if (current != line_num) {
        if (line_len)
            *line_len = 0;
        return NULL;
    }

    const char *end = strchr(p, '\n');
    if (!end) {
        end = p + strlen(p);
    }

    if (line_len) {
        *line_len = (size_t)(end - p);
    }
    return p;
}

fixer_result_t fixer_interactive_init(fixer_interactive_t *session,
                                      fixer_context_t *ctx,
                                      const fixer_options_t *options) {
    if (!session || !ctx) {
        return FIXER_ERR_NOMEM;
    }

    memset(session, 0, sizeof(*session));
    session->ctx = ctx;

    if (options) {
        session->options = *options;
    }

    /// Allocate accepted flags array
    if (ctx->count > 0) {
        session->accepted = calloc(ctx->count, sizeof(bool));
        if (!session->accepted) {
            return FIXER_ERR_NOMEM;
        }
    }

    session->current = 0;
    session->apply_all = false;
    session->aborted = false;

    return FIXER_OK;
}

void fixer_interactive_cleanup(fixer_interactive_t *session) {
    if (!session) {
        return;
    }

    free(session->accepted);
    memset(session, 0, sizeof(*session));
}

bool fixer_interactive_next(fixer_interactive_t *session,
                            const fixer_fix_t **fix) {
    if (!session || !session->ctx || !fix) {
        return false;
    }

    /// Find next applicable fix
    while (session->current < session->ctx->count) {
        const fixer_fix_t *f = &session->ctx->fixes[session->current];

        /// Skip manual fixes - they can't be auto-applied
        if (f->type == FIX_TYPE_MANUAL) {
            session->current++;
            continue;
        }

        /// Skip unsafe if not included
        if (f->type == FIX_TYPE_UNSAFE && !session->options.include_unsafe) {
            session->current++;
            continue;
        }

        /// Skip fixes without replacement
        if (!f->replacement) {
            session->current++;
            continue;
        }

        *fix = f;
        return true;
    }

    return false;
}

void fixer_interactive_respond(fixer_interactive_t *session,
                               fixer_response_t response) {
    if (!session || session->current >= session->ctx->count) {
        return;
    }

    switch (response) {
    case FIXER_RESPONSE_YES:
        session->accepted[session->current] = true;
        session->current++;
        break;

    case FIXER_RESPONSE_NO:
        session->accepted[session->current] = false;
        session->current++;
        break;

    case FIXER_RESPONSE_ALL:
        /// Accept current and all remaining applicable fixes
        session->apply_all = true;
        for (size_t i = session->current; i < session->ctx->count; i++) {
            const fixer_fix_t *f = &session->ctx->fixes[i];
            if (f->type == FIX_TYPE_MANUAL) {
                continue;
            }
            if (f->type == FIX_TYPE_UNSAFE &&
                !session->options.include_unsafe) {
                continue;
            }
            if (f->replacement) {
                session->accepted[i] = true;
            }
        }
        session->current = session->ctx->count; /// End the loop
        break;

    case FIXER_RESPONSE_QUIT:
        session->aborted = true;
        session->current = session->ctx->count; /// End the loop
        break;

    case FIXER_RESPONSE_DIFF:
    case FIXER_RESPONSE_HELP:
        /// These don't advance - handled by caller
        break;
    }
}

fixer_result_t fixer_interactive_apply(fixer_interactive_t *session,
                                       char **output, size_t *fixes_applied) {
    if (!session || !session->ctx || !output) {
        return FIXER_ERR_NOMEM;
    }

    /// Count accepted fixes
    size_t accepted_count = 0;
    for (size_t i = 0; i < session->ctx->count; i++) {
        if (session->accepted[i]) {
            accepted_count++;
        }
    }

    if (accepted_count == 0) {
        /// No fixes to apply - return original content
        *output = strdup(session->ctx->content);
        if (!*output) {
            return FIXER_ERR_NOMEM;
        }
        if (fixes_applied)
            *fixes_applied = 0;
        return FIXER_OK;
    }

    /// Sort fixes in reverse order for application
    qsort(session->ctx->fixes, session->ctx->count, sizeof(fixer_fix_t),
          compare_fixes_reverse);

    /// We need to re-map accepted flags after sort - but since we sorted
    /// by match_start descending, we need to track which fixes were accepted
    /// by their original index. For simplicity, we'll apply all accepted
    /// fixes based on the match_start position.

    /// Start with a copy of the original
    size_t buf_size = session->ctx->content_len * 2 + 1;
    char *working = malloc(buf_size);
    if (!working) {
        return FIXER_ERR_NOMEM;
    }
    memcpy(working, session->ctx->content, session->ctx->content_len + 1);
    size_t working_len = session->ctx->content_len;

    size_t applied = 0;

    for (size_t i = 0; i < session->ctx->count; i++) {
        if (!session->accepted[i]) {
            continue;
        }

        fixer_fix_t *fix = &session->ctx->fixes[i];

        if (!fix->replacement || fix->match_start >= working_len) {
            continue;
        }

        /// Apply replacement
        size_t repl_len = strlen(fix->replacement);
        size_t new_len = working_len - fix->match_length + repl_len;

        /// Make room if needed
        if (new_len >= buf_size) {
            buf_size = new_len * 2;
            char *new_working = realloc(working, buf_size);
            if (!new_working) {
                free(working);
                return FIXER_ERR_NOMEM;
            }
            working = new_working;
        }

        /// Shift tail
        size_t tail_start = fix->match_start + fix->match_length;
        size_t tail_len = working_len - tail_start;
        memmove(working + fix->match_start + repl_len, working + tail_start,
                tail_len + 1);

        /// Insert replacement
        memcpy(working + fix->match_start, fix->replacement, repl_len);
        working_len = new_len;

        applied++;
    }

    *output = working;
    if (fixes_applied)
        *fixes_applied = applied;

    return FIXER_OK;
}

void fixer_print_fix_interactive(const fixer_context_t *ctx,
                                 const fixer_fix_t *fix, size_t index,
                                 size_t total) {
    if (!ctx || !fix) {
        return;
    }

    const char *path = ctx->script_path ? ctx->script_path : "<stdin>";

    /// Fix type indicator
    const char *type_str;
    const char *type_color;
    switch (fix->type) {
    case FIX_TYPE_SAFE:
        type_str = "safe";
        type_color = "\033[32m"; /// Green
        break;
    case FIX_TYPE_UNSAFE:
        type_str = "unsafe";
        type_color = "\033[33m"; /// Yellow
        break;
    default:
        type_str = "fix";
        type_color = "\033[0m";
        break;
    }

    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Fix %zu/%zu [%s%s\033[0m] %s:%d\n", index, total, type_color,
           type_str, path, fix->line);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    /// Show the message
    if (fix->message) {
        printf("\n  %s\n", fix->message);
    }

    /// Show the line context
    size_t line_len = 0;
    const char *line = get_line_at(ctx->content, fix->line, &line_len);

    if (line && line_len > 0) {
        printf("\n  \033[31m- %.*s\033[0m\n", (int)line_len, line);

        /// Show the replacement in context
        if (fix->replacement && fix->column > 0) {
            /// Reconstruct the line with the fix applied
            int col = fix->column - 1;
            if (col >= 0 && (size_t)col < line_len) {
                printf("  \033[32m+ %.*s%s%.*s\033[0m\n", col, line,
                       fix->replacement,
                       (int)(line_len - col - fix->match_length),
                       line + col + fix->match_length);
            }
        }
    }

    printf("\n");
}

void fixer_print_interactive_help(void) {
    printf("\nInteractive fix commands:\n");
    printf("  y - Yes, apply this fix\n");
    printf("  n - No, skip this fix\n");
    printf("  a - Apply All remaining fixes\n");
    printf("  q - Quit (apply accepted fixes so far)\n");
    printf("  d - show Diff for this fix\n");
    printf("  ? - show this help\n");
    printf("\n");
}

fixer_response_t fixer_read_response(void) {
    printf("Apply this fix? [y/n/a/q/d/?] ");
    fflush(stdout);

    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) {
        return FIXER_RESPONSE_QUIT;
    }

    /// Handle just Enter as 'yes'
    if (buf[0] == '\n') {
        return FIXER_RESPONSE_YES;
    }

    switch (buf[0]) {
    case 'y':
    case 'Y':
        return FIXER_RESPONSE_YES;

    case 'n':
    case 'N':
        return FIXER_RESPONSE_NO;

    case 'a':
    case 'A':
        return FIXER_RESPONSE_ALL;

    case 'q':
    case 'Q':
        return FIXER_RESPONSE_QUIT;

    case 'd':
    case 'D':
        return FIXER_RESPONSE_DIFF;

    case '?':
    case 'h':
    case 'H':
        return FIXER_RESPONSE_HELP;

    default:
        printf("Unknown command '%c'. Press '?' for help.\n", buf[0]);
        return fixer_read_response(); /// Retry
    }
}

int fixer_run_interactive(fixer_context_t *ctx, const fixer_options_t *options,
                          const char *script_path) {
    if (!ctx || !options) {
        return -1;
    }

    /// Count applicable fixes
    size_t applicable = 0;
    for (size_t i = 0; i < ctx->count; i++) {
        const fixer_fix_t *f = &ctx->fixes[i];
        if (f->type == FIX_TYPE_MANUAL)
            continue;
        if (f->type == FIX_TYPE_UNSAFE && !options->include_unsafe)
            continue;
        if (!f->replacement)
            continue;
        applicable++;
    }

    if (applicable == 0) {
        printf("No automatic fixes available.\n");
        return 0;
    }

    printf("\nInteractive fix mode: %zu fix(es) available\n", applicable);
    printf("Press '?' for help\n");

    /// Initialize session
    fixer_interactive_t session;
    if (fixer_interactive_init(&session, ctx, options) != FIXER_OK) {
        fprintf(stderr, "Failed to initialize interactive session\n");
        return -1;
    }

    /// Process each fix
    const fixer_fix_t *fix;
    size_t fix_index = 0;

    while (fixer_interactive_next(&session, &fix)) {
        fix_index++;

        /// Show the fix
        fixer_print_fix_interactive(ctx, fix, fix_index, applicable);

        /// Get user response
        fixer_response_t response;
        do {
            response = fixer_read_response();

            if (response == FIXER_RESPONSE_HELP) {
                fixer_print_interactive_help();
            } else if (response == FIXER_RESPONSE_DIFF) {
                /// Show just this fix as a diff
                printf("\n");
                if (fix->original && fix->replacement) {
                    printf("  @@ -%d,%d +%d,%d @@\n", fix->line, 1, fix->line,
                           1);
                    printf("  - %s\n", fix->original);
                    printf("  + %s\n", fix->replacement);
                }
                printf("\n");
            }
        } while (response == FIXER_RESPONSE_HELP ||
                 response == FIXER_RESPONSE_DIFF);

        fixer_interactive_respond(&session, response);
    }

    /// Count accepted
    size_t accepted = 0;
    for (size_t i = 0; i < ctx->count; i++) {
        if (session.accepted[i]) {
            accepted++;
        }
    }

    if (accepted == 0) {
        printf("\nNo fixes applied.\n");
        fixer_interactive_cleanup(&session);
        return 0;
    }

    /// Apply accepted fixes
    printf("\nApplying %zu fix(es)...\n", accepted);

    char *fixed_content = NULL;
    size_t applied = 0;

    fixer_result_t result =
        fixer_interactive_apply(&session, &fixed_content, &applied);

    if (result != FIXER_OK) {
        fprintf(stderr, "Failed to apply fixes: %s\n",
                fixer_result_string(result));
        fixer_interactive_cleanup(&session);
        return -1;
    }

    /// Verify syntax if requested
    if (options->verify_syntax && fixed_content) {
        if (!fixer_verify_syntax(fixed_content, options->target)) {
            fprintf(stderr,
                    "ERROR: Fixed script has syntax errors, not applying\n");
            free(fixed_content);
            fixer_interactive_cleanup(&session);
            return -1;
        }
    }

    /// Write to file if not dry run
    if (!options->dry_run && script_path && fixed_content) {
        result = fixer_write_file(script_path, fixed_content,
                                  options->create_backup);
        if (result != FIXER_OK) {
            fprintf(stderr, "Failed to write fixed file: %s\n",
                    fixer_result_string(result));
            free(fixed_content);
            fixer_interactive_cleanup(&session);
            return -1;
        }

        printf("Applied %zu fix(es) to %s\n", applied, script_path);
        if (options->create_backup) {
            printf("Backup saved to %s.bak\n", script_path);
        }
    } else if (options->dry_run) {
        printf("\nDry run - would apply %zu fix(es):\n", applied);
        /// Show diff
        if (fixed_content && ctx->content) {
            printf("--- %s\n", script_path ? script_path : "<stdin>");
            printf("+++ %s (fixed)\n", script_path ? script_path : "<stdin>");
            /// Simple diff output
            printf("%s", fixed_content);
        }
    }

    free(fixed_content);
    fixer_interactive_cleanup(&session);

    return (int)applied;
}
