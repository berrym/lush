/**
 * @file bin_return_value.c
 * @brief `return_value` builtin -- (lush extension) read previous return value
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"

/* Forward declaration -- defined in src/posix_opts.c */
bool is_posix_mode_enabled(void);

/**
 * @brief Set a string return value for the current function
 *
 * Lush extension (not POSIX) that allows functions to return
 * string values via command substitution. Not available in POSIX mode.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is the return value)
 * @return 0 on success, 1 on error or in POSIX mode
 */
int bin_return_value(int argc, char **argv) {
    // POSIX compliance: return_value is not available in strict POSIX mode
    if (is_posix_mode_enabled()) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_FEATURE_DISABLED, SHELL_SEVERITY_ERROR,
                               loc, "not available in POSIX mode");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "switch to bash/zsh/lush mode (e.g. `mode bash`) to "
                     "use return_value");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: return_value: not available in POSIX mode\n");
        }
        return 1;
    }

    if (argc < 2) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing value argument");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(err, "usage: return_value <value>");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: return_value: missing value argument\n");
        }
        return 1;
    }

    // Output the return value with a special marker that command substitution
    // can recognize
    printf("__LUSH_RETURN__:%s:__END__\n", argv[1]);
    fflush(stdout);

    // Return success
    return 0;
}
