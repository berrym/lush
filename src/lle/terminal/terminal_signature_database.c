/**
 * @file terminal_signature_database.c
 * @brief Terminal Signature Database
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Comprehensive database of known terminal emulators with their
 * characteristics and preferred handling modes.
 *
 * Specification:
 * docs/lle_specification/critical_gaps/26_adaptive_terminal_integration_complete.md
 */

#include "lle/adaptive_terminal_integration.h"
#include <stddef.h>

/**
 * @brief Terminal signature database
 *
 * Comprehensive database of known terminal emulators with their
 * characteristics and preferred handling modes. Patterns support
 * wildcards (*) for flexible matching. Ordered by priority with
 * more specific patterns first.
 */
static const lle_terminal_signature_t lle_known_terminals[] = {
    /* ========================================================================
     * MODERN EDITOR TERMINALS (Enhanced Mode)
     * ========================================================================
     */

    /// Zed Editor
    {           .name = "zed",
     .term_program_pattern = "zed",
     .term_pattern = "xterm-256color",
     .env_var_check = "COLORTERM",
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_ENHANCED,
     .force_interactive = true,
     .requires_special_handling = false},

    /// Visual Studio Code
    {        .name = "vscode",
     .term_program_pattern = "vscode",
     .term_pattern = "xterm-256color",
     .env_var_check = "COLORTERM",
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_ENHANCED,
     .force_interactive = true,
     .requires_special_handling = false},

    /// Cursor (VS Code fork)
    {        .name = "cursor",
     .term_program_pattern = "cursor",
     .term_pattern = "xterm-256color",
     .env_var_check = "COLORTERM",
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_ENHANCED,
     .force_interactive = true,
     .requires_special_handling = false},

    /* ========================================================================
     * AI ASSISTANT AND PROGRAMMATIC ENVIRONMENTS
     * ========================================================================
     */

    /// AI Assistant interfaces
    {  .name = "ai_assistant",
     .term_program_pattern = "*assistant*",
     .term_pattern = "*",
     .env_var_check = "AI_ENVIRONMENT",
     .capability_level = LLE_CAPABILITY_BASIC,
     .preferred_mode = LLE_ADAPTIVE_MODE_ENHANCED,
     .force_interactive = true,
     .requires_special_handling = true },

    /* ========================================================================
     * TRADITIONAL NATIVE TERMINALS (Native Mode)
     * ========================================================================
     */

    /// iTerm2 (macOS)
    {        .name = "iterm2",
     .term_program_pattern = "iTerm",
     .term_pattern = "*",
     .env_var_check = "ITERM_SESSION_ID",
     .capability_level = LLE_CAPABILITY_PREMIUM,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// GNOME Terminal
    {.name = "gnome-terminal",
     .term_program_pattern = "gnome-terminal",
     .term_pattern = "gnome*",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// Konsole (KDE)
    {       .name = "konsole",
     .term_program_pattern = "konsole",
     .term_pattern = "konsole*",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// Kitty
    {         .name = "kitty",
     .term_program_pattern = NULL,
     .term_pattern = "*kitty*",
     .env_var_check = "KITTY_WINDOW_ID",
     .capability_level = LLE_CAPABILITY_PREMIUM,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// Alacritty
    {     .name = "alacritty",
     .term_program_pattern = NULL,
     .term_pattern = "alacritty",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// WezTerm
    {       .name = "wezterm",
     .term_program_pattern = "WezTerm",
     .term_pattern = "wezterm",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_PREMIUM,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// Ghostty
    {       .name = "ghostty",
     .term_program_pattern = "ghostty",
     .term_pattern = "xterm-ghostty",
     .env_var_check = "COLORTERM",
     .capability_level = LLE_CAPABILITY_PREMIUM,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// Terminal.app (macOS)
    {  .name = "terminal_app",
     .term_program_pattern = "Apple_Terminal",
     .term_pattern = "xterm-256color",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// xterm
    {         .name = "xterm",
     .term_program_pattern = NULL,
     .term_pattern = "xterm*",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_STANDARD,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// rxvt/urxvt
    {          .name = "rxvt",
     .term_program_pattern = NULL,
     .term_pattern = "rxvt*",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_STANDARD,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /// Linux console
    { .name = "linux_console",
     .term_program_pattern = NULL,
     .term_pattern = "linux",
     .env_var_check = NULL,
     .capability_level = LLE_CAPABILITY_BASIC,
     .preferred_mode = LLE_ADAPTIVE_MODE_NATIVE,
     .force_interactive = false,
     .requires_special_handling = false},

    /* ========================================================================
     * TERMINAL MULTIPLEXERS (Multiplexed Mode)
     * ========================================================================
     */

    /// tmux
    {          .name = "tmux",
     .term_program_pattern = NULL,
     .term_pattern = "tmux*",
     .env_var_check = "TMUX",
     .capability_level = LLE_CAPABILITY_FULL,
     .preferred_mode = LLE_ADAPTIVE_MODE_MULTIPLEXED,
     .force_interactive = false,
     .requires_special_handling = true },

    /// GNU Screen
    {        .name = "screen",
     .term_program_pattern = NULL,
     .term_pattern = "screen*",
     .env_var_check = "STY",
     .capability_level = LLE_CAPABILITY_STANDARD,
     .preferred_mode = LLE_ADAPTIVE_MODE_MULTIPLEXED,
     .force_interactive = false,
     .requires_special_handling = true }
};

#define LLE_NUM_KNOWN_TERMINALS                                                \
    (sizeof(lle_known_terminals) / sizeof(lle_known_terminals[0]))

/**
 * @brief Get terminal signature database
 *
 * Returns a pointer to the static terminal signature database and
 * optionally provides the count of entries.
 *
 * @param count Output for number of signatures in database (may be NULL)
 * @return Pointer to the terminal signature database array
 */
const lle_terminal_signature_t *
lle_get_terminal_signature_database(size_t *count) {
    if (count) {
        *count = LLE_NUM_KNOWN_TERMINALS;
    }
    return lle_known_terminals;
}
