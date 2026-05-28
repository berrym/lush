/**
 * @file bin_getopts.c
 * @brief `getopts` builtin -- parse positional parameters
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"
#include "symtable.h"

/**
 * @brief Parse positional parameters for shell scripts
 *
 * POSIX-compliant option parser for shell scripts. Uses OPTIND and
 * OPTARG variables to track parsing state. Supports silent mode
 * (optstring starts with ':') for custom error handling.
 *
 * @param argc Argument count
 * @param argv Argument vector (optstring, varname, [args...])
 * @return 0 if option found, 1 if no more options
 */
int bin_getopts(int argc, char **argv) {
    if (argc < 3) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing required arguments");
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
                for (size_t k = 0; k < current_executor->context_depth &&
                                   k < SHELL_ERROR_CONTEXT_MAX;
                     k++) {
                    if (current_executor->context_stack[k]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[k]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "usage: getopts optstring name [args...]");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: getopts: usage: getopts optstring name [args...]\n");
        }
        return 1;
    }

    char *optstring = argv[1];
    char *varname = argv[2];

    /// Get current OPTIND value from environment
    char *optind_str = symtable_get_global("OPTIND");
    int current_optind = optind_str ? atoi(optind_str) : 1;

    /// Determine arguments to parse
    char **parse_args;
    int parse_argc;

    if (argc > 3) {
        /// Use provided arguments
        parse_args = &argv[3];
        parse_argc = argc - 3;
    } else {
        /// Use positional parameters - get from shell environment
        /// For now, use a simple implementation
        parse_args = NULL;
        parse_argc = 0;

        /// Try to get positional parameters from shell variables
        char *argc_str = symtable_get_global("#");
        if (argc_str) {
            parse_argc = atoi(argc_str);
            if (parse_argc > 0) {
                parse_args = malloc((parse_argc + 1) * sizeof(char *));
                for (int i = 0; i < parse_argc; i++) {
                    char param_name[16];
                    snprintf(param_name, sizeof(param_name), "%d", i + 1);
                    char *param_val = symtable_get_global(param_name);
                    parse_args[i] = param_val ? strdup(param_val) : strdup("");
                }
                parse_args[parse_argc] = NULL;
            }
        }
    }

    /// Check if we have arguments to parse
    if (parse_argc == 0 || current_optind > parse_argc) {
        /// No more arguments - OPTIND should point to first non-option argument
        /// Don't reset OPTIND to 1, leave it pointing to where non-options
        /// start
        if (argc <= 3 && parse_args) {
            for (int i = 0; i < parse_argc; i++) {
                free(parse_args[i]);
            }
            free(parse_args);
        }
        return 1;
    }

    /// Get current argument to parse
    char *current_arg = parse_args[current_optind - 1];

    /// Static variables to maintain state between calls
    static char *current_option_arg = NULL;
    static int option_pos = 0;

    /// Check if we're continuing with a combined option (like -abc)
    if (option_pos == 0) {
        /// Starting new argument
        if (!current_arg || current_arg[0] != '-' ||
            strcmp(current_arg, "-") == 0) {
            /// Not an option or single dash - OPTIND should point to this
            /// non-option Don't reset OPTIND to 1, it should point to current
            /// position
            if (argc <= 3 && parse_args) {
                for (int i = 0; i < parse_argc; i++) {
                    free(parse_args[i]);
                }
                free(parse_args);
            }
            return 1;
        }

        if (strcmp(current_arg, "--") == 0) {
            /// End of options marker
            char next_optind[16];
            snprintf(next_optind, sizeof(next_optind), "%d",
                     current_optind + 1);
            symtable_set_global("OPTIND", next_optind);
            if (argc <= 3 && parse_args) {
                for (int i = 0; i < parse_argc; i++) {
                    free(parse_args[i]);
                }
                free(parse_args);
            }
            return 1;
        }

        current_option_arg = current_arg;
        option_pos = 1; /// Skip the initial '-'
    }

    /// Get current option character
    char opt_char = current_option_arg[option_pos];
    if (opt_char == '\0') {
        /// Move to next argument
        current_optind++;
        option_pos = 0;
        char next_optind[16];
        snprintf(next_optind, sizeof(next_optind), "%d", current_optind);
        symtable_set_global("OPTIND", next_optind);
        if (argc <= 3 && parse_args) {
            for (int i = 0; i < parse_argc; i++) {
                free(parse_args[i]);
            }
            free(parse_args);
        }
        return bin_getopts(argc,
                           argv); /// Recursive call to process next argument
    }

    /// Check if option is valid
    bool silent_mode = (optstring[0] == ':');
    char *opt_pos = strchr(silent_mode ? optstring + 1 : optstring, opt_char);

    if (!opt_pos) {
        /// Invalid option
        if (silent_mode) {
            symtable_set_global(varname, "?");
            char optarg_val[2] = {opt_char, '\0'};
            symtable_set_global("OPTARG", optarg_val);
        } else {
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "illegal option -- %c", opt_char);
            symtable_set_global(varname, "?");
            symtable_set_global("OPTARG", "");
        }
        option_pos++;

        /// If we've finished processing this argument, move to next
        if (current_option_arg[option_pos] == '\0') {
            current_optind++;
            option_pos = 0;
        }

        char next_optind[16];
        snprintf(next_optind, sizeof(next_optind), "%d", current_optind);
        symtable_set_global("OPTIND", next_optind);
        if (argc <= 3 && parse_args) {
            for (int i = 0; i < parse_argc; i++) {
                free(parse_args[i]);
            }
            free(parse_args);
        }
        return 0;
    }

    /// Check if option requires an argument
    bool needs_arg = (opt_pos[1] == ':');

    if (needs_arg) {
        char *arg_value = NULL;

        if (current_option_arg[option_pos + 1] != '\0') {
            /// Argument is attached (like -fvalue)
            arg_value = &current_option_arg[option_pos + 1];
            option_pos = 0; /// Move to next argument
            current_optind++;
        } else {
            /// Argument should be in next parameter
            if (current_optind < parse_argc) {
                arg_value = parse_args[current_optind];
                current_optind++; /// Move past the option argument
                option_pos = 0;
            } else {
                /// Missing argument
                if (silent_mode) {
                    symtable_set_global(varname, ":");
                    char optarg_val[2] = {opt_char, '\0'};
                    symtable_set_global("OPTARG", optarg_val);
                } else {
                    executor_error_report(
                        current_executor, SHELL_ERR_MISSING_ARGUMENT,
                        builtin_get_source_location(),
                        "option requires an argument -- %c", opt_char);
                    symtable_set_global(varname, "?");
                    symtable_set_global("OPTARG", "");
                }
                char next_optind[16];
                snprintf(next_optind, sizeof(next_optind), "%d",
                         current_optind);
                symtable_set_global("OPTIND", next_optind);
                if (argc <= 3 && parse_args) {
                    for (int i = 0; i < parse_argc; i++) {
                        free(parse_args[i]);
                    }
                    free(parse_args);
                }
                return 0;
            }
        }

        /// Set option and argument
        char opt_val[2] = {opt_char, '\0'};
        symtable_set_global(varname, opt_val);
        symtable_set_global("OPTARG", arg_value ? arg_value : "");

        /// For options with arguments, we need to increment current_optind
        /// again since we consumed both the option and its argument
        current_optind++;
    } else {
        /// Option doesn't take an argument
        char opt_val[2] = {opt_char, '\0'};
        symtable_set_global(varname, opt_val);
        symtable_set_global("OPTARG", "");
        option_pos++;

        /// If we've finished processing this argument, move to next
        if (current_option_arg[option_pos] == '\0') {
            current_optind++;
            option_pos = 0;
        }
    }

    /// Update OPTIND
    char next_optind[16];
    snprintf(next_optind, sizeof(next_optind), "%d", current_optind);
    symtable_set_global("OPTIND", next_optind);

    /// Clean up if we allocated parse_args
    if (argc <= 3 && parse_args) {
        for (int i = 0; i < parse_argc; i++) {
            free(parse_args[i]);
        }
        free(parse_args);
    }

    return 0; /// Found an option
}
