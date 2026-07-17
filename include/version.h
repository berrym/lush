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
#define LUSH_VERSION_MAJOR 0
#define LUSH_VERSION_MINOR 3
#define LUSH_VERSION_PATCH 0
/// Pre-release: the -dev suffix marks development in the 0.3 line, not a
/// released 0.3.0. Per SemVer a pre-release identifier sorts before the
/// release. The 0.x series stays pre-1.0 until lush is empirically proven
/// ready for permanent daily use; 1.0.0 is a deliberate, gated milestone.
#define LUSH_VERSION_STRING "0.3.0-dev"
#define LUSH_TAGLINE                                                           \
    "A modern, polyglot shell with its own ideas about what a shell should be"
#define LUSH_DESCRIPTION                                                       \
    "Lush is a polyglot Unix shell written from scratch in C11: POSIX, bash, " \
    "and zsh syntax are different ways to reach one underlying engine, and "   \
    "lush is unafraid to diverge from the older shells to fix long-standing "  \
    "problems rather than reproduce them. It ships an integrated script "      \
    "debugger, a schema-driven configuration system, a first-class "           \
    "Scalar/List/Map value model, and the native Lush Line Editor -- syntax "  \
    "highlighting and context-aware completion with no dependency on GNU "     \
    "Readline. Early-stage and under active development: capable and fun to "  \
    "use today, not yet a production daily driver."

#endif /// VERSION_H
