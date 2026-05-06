/**
 * @file bin_hash.c
 * @brief `hash` builtin -- manage the command path hash table
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "ht.h"
#include "shell_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* command_hash is owned by src/builtins/builtins.c (registry-side). */
extern ht_strstr_t *command_hash;

/**
 * @brief Remember or report utility locations (POSIX hash)
 *
 * With no arguments, displays all remembered utility locations.
 * With utility names, finds and remembers their PATH locations.
 * With -r, forgets all remembered locations.
 * With -t name, prints the remembered pathname of name.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and utility names
 * @return 0 on success, 1 if utility not found, 2 on invalid option
 */
int bin_hash(int argc, char **argv) {
    init_command_hash();

    // Handle -r option (forget all remembered locations)
    if (argc == 2 && strcmp(argv[1], "-r") == 0) {
        if (command_hash) {
            ht_strstr_destroy(command_hash);
            command_hash = ht_strstr_create(HT_STR_CASECMP | HT_SEED_RANDOM);
        }
        return 0;
    }

    // Handle -t option (print remembered pathname)
    if (argc >= 2 && strcmp(argv[1], "-t") == 0) {
        if (argc < 3) {
            executor_error_report(current_executor, SHELL_ERR_MISSING_ARGUMENT,
                                  builtin_get_source_location(),
                                  "-t: option requires an argument");
            return 2;
        }
        int ret = 0;
        for (int i = 2; i < argc; i++) {
            const char *utility = argv[i];
            const char *path =
                command_hash ? ht_strstr_get(command_hash, utility) : NULL;
            if (path) {
                printf("%s\n", path);
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "%s: not found", utility);
                ret = 1;
            }
        }
        return ret;
    }

    // Handle invalid options
    if (argc >= 2 && argv[1][0] == '-' && strcmp(argv[1], "-r") != 0) {
        executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                              builtin_get_source_location(), "invalid option");
        return 2;
    }

    // No arguments - display all remembered locations
    if (argc == 1) {
        if (!command_hash) {
            return 0;
        }

        ht_enum_t *enumerator = ht_strstr_enum_create(command_hash);
        if (enumerator) {
            const char *key, *value;
            while (ht_strstr_enum_next(enumerator, &key, &value)) {
                if (key && value) {
                    printf("%s\t%s\n", key, value);
                }
            }
            ht_strstr_enum_destroy(enumerator);
        }
        return 0;
    }

    // Arguments provided - find and remember utility locations
    for (int i = 1; i < argc; i++) {
        const char *utility = argv[i];

        // Skip if it's a builtin (POSIX requirement)
        if (is_builtin(utility)) {
            continue;
        }

        // Find the utility in PATH
        char *path = find_command_in_path(utility);
        if (path) {
            // Remember this location
            ht_strstr_insert(command_hash, utility, path);
            free(path);
        } else {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "%s: not found", utility);
            return 1;
        }
    }

    return 0;
}
