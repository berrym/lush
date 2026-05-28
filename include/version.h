/**
 * @file version.h
 * @brief Shell version information and identification
 *
 * Defines version numbers, name, and description for Lush shell.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef VERSION_H
#define VERSION_H

#define LUSH_NAME "lush"
#define LUSH_VERSION_MAJOR 1
#define LUSH_VERSION_MINOR 5
#define LUSH_VERSION_PATCH 0
#define LUSH_VERSION_STRING "1.5.0"
#define LUSH_TAGLINE "A Unix shell with developer-first design"
#define LUSH_DESCRIPTION                                                       \
    "Lush is a Unix shell built entirely from scratch. It combines POSIX "     \
    "compliance with carefully chosen extensions from Bash and Zsh, plus "     \
    "capabilities found nowhere else - most notably an integrated debugger "   \
    "for shell scripts. The native line editor (LLE) provides syntax "         \
    "highlighting and context-aware completions without relying on GNU "       \
    "Readline or any external library."

#endif /// VERSION_H
