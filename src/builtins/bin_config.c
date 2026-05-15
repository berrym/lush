/**
 * @file bin_config.c
 * @brief `config` builtin -- central registry interface
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "config.h"

/**
 * @brief Manage shell configuration
 *
 * Interface to the shell configuration system. Supports subcommands:
 * show, set, get, reload, save for managing configuration options.
 *
 * @param argc Argument count
 * @param argv Argument vector with config subcommand and options
 * @return Always returns 0
 */
int bin_config(int argc, char **argv) {
    builtin_config(argc, argv);
    return 0;
}
