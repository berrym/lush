/**
 * @file theme.c
 * @brief LLE Theme Registry System Implementation
 *
 * Specification: Spec 25 Section 4 - Theme Registry System
 * Version: 1.0.0
 *
 * Implements theme registry, theme creation, inheritance resolution,
 * and built-in themes.
 */

#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Theme Registry Implementation
 * ============================================================================
 */

/**
 * @brief Initialize the theme registry
 *
 * Sets up a theme registry for storing and managing themes. The registry
 * starts with no themes and "minimal" as the default theme name.
 *
 * @param registry Pointer to registry structure to initialize
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if registry is NULL
 */
lle_result_t lle_theme_registry_init(lle_theme_registry_t *registry) {
    if (!registry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(registry, 0, sizeof(*registry));
    snprintf(registry->default_theme_name, sizeof(registry->default_theme_name),
             "minimal");
    registry->initialized = true;

    return LLE_SUCCESS;
}

/**
 * @brief Cleanup the theme registry
 *
 * Frees all registered themes and resets the registry to uninitialized state.
 *
 * @param registry Pointer to registry to cleanup (ignored if NULL or not initialized)
 */
void lle_theme_registry_cleanup(lle_theme_registry_t *registry) {
    if (!registry || !registry->initialized) {
        return;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (registry->themes[i]) {
            lle_theme_free(registry->themes[i]);
            registry->themes[i] = NULL;
        }
    }

    registry->count = 0;
    registry->active_theme = NULL;
    registry->initialized = false;
}

/**
 * @brief Register a theme with the registry
 *
 * Adds a theme to the registry. If the theme specifies inheritance,
 * resolves the parent theme and applies inherited properties.
 *
 * @param registry Pointer to initialized registry
 * @param theme    Pointer to theme to register (ownership transferred to registry)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if invalid,
 *         LLE_ERROR_BUFFER_OVERFLOW if registry full, LLE_ERROR_INVALID_STATE if duplicate name
 */
lle_result_t lle_theme_registry_register(lle_theme_registry_t *registry,
                                         lle_theme_t *theme) {
    if (!registry || !theme || !registry->initialized) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (strlen(theme->name) == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (registry->count >= LLE_THEME_REGISTRY_MAX) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /* Check for duplicate name */
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->themes[i]->name, theme->name) == 0) {
            return LLE_ERROR_INVALID_STATE; /* Duplicate */
        }
    }

    /* Resolve inheritance if specified */
    if (strlen(theme->inherits_from) > 0) {
        lle_result_t result = lle_theme_resolve_inheritance(registry, theme);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    /* Register theme */
    registry->themes[registry->count] = theme;
    registry->count++;

    /* Update statistics */
    if (theme->source == LLE_THEME_SOURCE_BUILTIN) {
        registry->builtin_count++;
    } else {
        registry->user_count++;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Find a theme by name
 *
 * Searches the registry for a theme with the given name.
 *
 * @param registry Pointer to initialized registry
 * @param name     Theme name to search for
 * @return Pointer to theme if found, NULL if not found or invalid parameters
 */
lle_theme_t *lle_theme_registry_find(const lle_theme_registry_t *registry,
                                     const char *name) {
    if (!registry || !name || !registry->initialized) {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->themes[i]->name, name) == 0) {
            return registry->themes[i];
        }
    }

    return NULL;
}

/**
 * @brief Set the active theme
 *
 * Activates a theme by name, deactivating the previously active theme.
 * Increments the theme switch counter for statistics.
 *
 * @param registry Pointer to initialized registry
 * @param name     Name of theme to activate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if invalid,
 *         LLE_ERROR_NOT_FOUND if theme not found
 */
lle_result_t lle_theme_registry_set_active(lle_theme_registry_t *registry,
                                           const char *name) {
    if (!registry || !name || !registry->initialized) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_theme_t *theme = lle_theme_registry_find(registry, name);
    if (!theme) {
        return LLE_ERROR_NOT_FOUND;
    }

    /* Deactivate current theme */
    if (registry->active_theme) {
        registry->active_theme->is_active = false;
    }

    /* Activate new theme */
    registry->active_theme = theme;
    theme->is_active = true;
    snprintf(registry->active_theme_name, sizeof(registry->active_theme_name),
             "%s", name);
    registry->total_switches++;

    return LLE_SUCCESS;
}

/**
 * @brief Get the currently active theme
 *
 * Returns the currently active theme in the registry.
 *
 * @param registry Pointer to initialized registry
 * @return Pointer to active theme, or NULL if no theme active or invalid registry
 */
lle_theme_t *
lle_theme_registry_get_active(const lle_theme_registry_t *registry) {
    if (!registry || !registry->initialized) {
        return NULL;
    }
    return registry->active_theme;
}

/**
 * @brief Get all registered theme names
 *
 * Retrieves the names of all registered themes. If names array is provided,
 * fills it with pointers to theme name strings (up to max_names).
 *
 * @param registry  Pointer to initialized registry
 * @param names     Optional output array for theme name pointers (may be NULL)
 * @param max_names Maximum number of names to store in array
 * @return Total count of registered themes
 */
size_t lle_theme_registry_list(const lle_theme_registry_t *registry,
                               const char **names, size_t max_names) {
    if (!registry || !registry->initialized) {
        return 0;
    }

    if (names && max_names > 0) {
        size_t count =
            (registry->count < max_names) ? registry->count : max_names;
        for (size_t i = 0; i < count; i++) {
            names[i] = registry->themes[i]->name;
        }
    }

    return registry->count;
}

/* ============================================================================
 * Theme Creation and Management
 * ============================================================================
 */

/**
 * @brief Create a new theme
 *
 * Allocates and initializes a new theme with the given name, description,
 * and category. The theme is initialized with Unicode symbols by default.
 *
 * @param name        Theme name (required, must not be empty)
 * @param description Optional description (may be NULL)
 * @param category    Theme category classification
 * @return Pointer to newly allocated theme, or NULL on failure
 */
lle_theme_t *lle_theme_create(const char *name, const char *description,
                              lle_theme_category_t category) {
    if (!name || strlen(name) == 0) {
        return NULL;
    }

    lle_theme_t *theme = calloc(1, sizeof(*theme));
    if (!theme) {
        return NULL;
    }

    snprintf(theme->name, sizeof(theme->name), "%s", name);
    if (description) {
        snprintf(theme->description, sizeof(theme->description), "%s",
                 description);
    }
    theme->category = category;
    theme->source = LLE_THEME_SOURCE_RUNTIME;

    /* Initialize with Unicode symbols by default */
    lle_symbol_set_init_unicode(&theme->symbols);

    return theme;
}

/**
 * @brief Free a theme
 *
 * Releases memory allocated for a theme structure.
 *
 * @param theme Pointer to theme to free (ignored if NULL)
 */
void lle_theme_free(lle_theme_t *theme) { free(theme); }

/**
 * @brief Resolve theme inheritance
 *
 * Looks up the parent theme and copies unset properties from parent to child.
 * Handles colors, symbols, layout, and capabilities. Detects inheritance
 * cycles up to depth 10.
 *
 * @param registry Pointer to registry containing parent theme
 * @param theme    Pointer to theme with inherits_from set
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if invalid,
 *         LLE_ERROR_NOT_FOUND if parent not found, LLE_ERROR_INVALID_STATE on cycle
 */
lle_result_t lle_theme_resolve_inheritance(lle_theme_registry_t *registry,
                                           lle_theme_t *theme) {
    if (!registry || !theme) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (strlen(theme->inherits_from) == 0) {
        return LLE_SUCCESS; /* No inheritance */
    }

    /* Find parent theme */
    lle_theme_t *parent =
        lle_theme_registry_find(registry, theme->inherits_from);
    if (!parent) {
        return LLE_ERROR_NOT_FOUND;
    }

    /* Check for inheritance cycle (max depth 10) */
    lle_theme_t *ancestor = parent;
    for (int depth = 0; depth < 10 && ancestor; depth++) {
        if (strcmp(ancestor->name, theme->name) == 0) {
            return LLE_ERROR_INVALID_STATE; /* Cycle detected */
        }
        ancestor = ancestor->parent;
    }

    /* Link parent */
    theme->parent = parent;

    /* Inherit colors (only if child's is unset) */
    lle_color_scheme_t *child = &theme->colors;
    const lle_color_scheme_t *par = &parent->colors;

#define INHERIT_COLOR(field)                                                   \
    if (child->field.mode == LLE_COLOR_MODE_NONE) {                            \
        child->field = par->field;                                             \
    }

    INHERIT_COLOR(primary);
    INHERIT_COLOR(secondary);
    INHERIT_COLOR(success);
    INHERIT_COLOR(warning);
    INHERIT_COLOR(error);
    INHERIT_COLOR(info);
    INHERIT_COLOR(text);
    INHERIT_COLOR(text_dim);
    INHERIT_COLOR(text_bright);
    INHERIT_COLOR(border);
    INHERIT_COLOR(background);
    INHERIT_COLOR(highlight);
    INHERIT_COLOR(git_clean);
    INHERIT_COLOR(git_dirty);
    INHERIT_COLOR(git_staged);
    INHERIT_COLOR(git_untracked);
    INHERIT_COLOR(git_branch);
    INHERIT_COLOR(git_ahead);
    INHERIT_COLOR(git_behind);
    INHERIT_COLOR(path_home);
    INHERIT_COLOR(path_root);
    INHERIT_COLOR(path_normal);
    INHERIT_COLOR(path_separator);
    INHERIT_COLOR(status_ok);
    INHERIT_COLOR(status_error);
    INHERIT_COLOR(status_running);

#undef INHERIT_COLOR

    /* Inherit symbols if empty */
#define INHERIT_SYMBOL(field)                                                  \
    if (strlen(theme->symbols.field) == 0) {                                   \
        memcpy(theme->symbols.field, parent->symbols.field,                    \
               sizeof(theme->symbols.field));                                  \
    }

    INHERIT_SYMBOL(prompt);
    INHERIT_SYMBOL(prompt_root);
    INHERIT_SYMBOL(continuation);
    INHERIT_SYMBOL(separator_left);
    INHERIT_SYMBOL(separator_right);
    INHERIT_SYMBOL(branch);
    INHERIT_SYMBOL(staged);
    INHERIT_SYMBOL(unstaged);
    INHERIT_SYMBOL(untracked);
    INHERIT_SYMBOL(ahead);
    INHERIT_SYMBOL(behind);
    INHERIT_SYMBOL(stash);
    INHERIT_SYMBOL(conflict);
    INHERIT_SYMBOL(directory);
    INHERIT_SYMBOL(home);
    INHERIT_SYMBOL(error);
    INHERIT_SYMBOL(success);
    INHERIT_SYMBOL(time);
    INHERIT_SYMBOL(jobs);

#undef INHERIT_SYMBOL

    /* Inherit layout if not set */
    if (strlen(theme->layout.ps1_format) == 0) {
        memcpy(theme->layout.ps1_format, parent->layout.ps1_format,
               sizeof(theme->layout.ps1_format));
    }
    if (strlen(theme->layout.ps2_format) == 0) {
        memcpy(theme->layout.ps2_format, parent->layout.ps2_format,
               sizeof(theme->layout.ps2_format));
    }
    if (strlen(theme->layout.rps1_format) == 0) {
        memcpy(theme->layout.rps1_format, parent->layout.rps1_format,
               sizeof(theme->layout.rps1_format));
    }
    if (strlen(theme->layout.transient_format) == 0) {
        memcpy(theme->layout.transient_format, parent->layout.transient_format,
               sizeof(theme->layout.transient_format));
    }

    /* Inherit per-segment configs from parent if not present in child */
    for (size_t pi = 0; pi < parent->segment_config_count; pi++) {
        const lle_segment_config_t *parent_cfg = &parent->segment_configs[pi];
        if (!parent_cfg->configured) {
            continue;
        }
        /* Check if child already has config for this segment */
        bool found = false;
        for (size_t ci = 0; ci < theme->segment_config_count; ci++) {
            if (strcmp(theme->segment_configs[ci].name, parent_cfg->name) == 0) {
                found = true;
                break;
            }
        }
        if (!found && theme->segment_config_count < LLE_THEME_MAX_SEGMENT_CONFIGS) {
            theme->segment_configs[theme->segment_config_count] = *parent_cfg;
            theme->segment_config_count++;
        }
    }

    /* Inherit capabilities (additive for inheritable flags) */
    theme->capabilities |= (parent->capabilities & LLE_THEME_CAP_INHERITABLE);

    return LLE_SUCCESS;
}

/* ============================================================================
 * Color Helpers
 * ============================================================================
 */

/**
 * @brief Create a basic ANSI color
 *
 * Creates a color using the basic 8-color ANSI palette.
 *
 * @param color Basic color value (0-7)
 * @return Initialized lle_color_t structure
 */
lle_color_t lle_color_basic(lle_basic_color_t color) {
    lle_color_t c = {0};
    c.mode = LLE_COLOR_MODE_BASIC;
    c.value.basic = (uint8_t)color;
    return c;
}

/**
 * @brief Create a 256-palette color
 *
 * Creates a color using the 256-color extended palette.
 *
 * @param index Palette index (0-255)
 * @return Initialized lle_color_t structure
 */
lle_color_t lle_color_256(uint8_t index) {
    lle_color_t c = {0};
    c.mode = LLE_COLOR_MODE_256;
    c.value.palette = index;
    return c;
}

/**
 * @brief Create a true color (RGB)
 *
 * Creates a 24-bit true color from RGB components.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Initialized lle_color_t structure
 */
lle_color_t lle_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    lle_color_t c = {0};
    c.mode = LLE_COLOR_MODE_TRUE;
    c.value.rgb.r = r;
    c.value.rgb.g = g;
    c.value.rgb.b = b;
    return c;
}

/**
 * @brief Generate ANSI escape sequence for a color
 *
 * Converts a color structure to ANSI escape sequence string. Handles
 * basic, 256-color, and true color modes. For foreground colors, also
 * appends attribute escape codes (bold, dim, italic, underline).
 *
 * @param color       Pointer to color to convert
 * @param foreground  true for foreground color, false for background
 * @param output      Output buffer for escape sequence
 * @param output_size Size of output buffer
 * @return Number of bytes written (excluding null terminator), or 0 on error
 */
size_t lle_color_to_ansi(const lle_color_t *color, bool foreground,
                         char *output, size_t output_size) {
    if (!color || !output || output_size == 0) {
        return 0;
    }

    int base = foreground ? 30 : 40;
    int len = 0;

    switch (color->mode) {
    case LLE_COLOR_MODE_NONE:
        output[0] = '\0';
        return 0;

    case LLE_COLOR_MODE_BASIC:
        len = snprintf(output, output_size, "\033[%dm",
                       base + color->value.basic);
        break;

    case LLE_COLOR_MODE_256:
        len = snprintf(output, output_size, "\033[%d;5;%dm",
                       foreground ? 38 : 48, color->value.palette);
        break;

    case LLE_COLOR_MODE_TRUE:
        len = snprintf(output, output_size, "\033[%d;2;%d;%d;%dm",
                       foreground ? 38 : 48, color->value.rgb.r,
                       color->value.rgb.g, color->value.rgb.b);
        break;
    }

    /* Add attributes if foreground */
    if (foreground && len > 0 && (size_t)len < output_size - 20) {
        char attrs[32] = "";
        if (color->bold) {
            strcat(attrs, "\033[1m");
        }
        if (color->dim) {
            strcat(attrs, "\033[2m");
        }
        if (color->italic) {
            strcat(attrs, "\033[3m");
        }
        if (color->underline) {
            strcat(attrs, "\033[4m");
        }
        if (strlen(attrs) > 0) {
            size_t remaining = output_size - (size_t)len;
            size_t attrs_len = strlen(attrs);
            if (attrs_len < remaining) {
                memcpy(output + len, attrs, attrs_len + 1);
                len += (int)attrs_len;
            }
        }
    }

    return (size_t)len;
}

/**
 * @brief Downgrade color to match terminal capabilities
 *
 * Converts truecolor to 256-color or basic ANSI as needed based on
 * terminal capabilities. Uses color cube approximation for 256-color
 * and threshold-based mapping for basic colors.
 *
 * @param color         Pointer to color to downgrade (may be NULL)
 * @param has_truecolor true if terminal supports 24-bit color
 * @param has_256color  true if terminal supports 256-color palette
 * @return Downgraded color suitable for terminal capabilities
 */
lle_color_t lle_color_downgrade(const lle_color_t *color, bool has_truecolor,
                                bool has_256color) {
    if (!color) {
        lle_color_t none = {0};
        return none;
    }

    /* If terminal supports the color mode, return as-is */
    if (color->mode == LLE_COLOR_MODE_NONE) {
        return *color;
    }
    if (color->mode == LLE_COLOR_MODE_BASIC) {
        return *color; /* Basic colors always supported */
    }
    if (color->mode == LLE_COLOR_MODE_256 && has_256color) {
        return *color;
    }
    if (color->mode == LLE_COLOR_MODE_TRUE && has_truecolor) {
        return *color;
    }

    /* Need to downgrade */
    lle_color_t result = *color;

    if (color->mode == LLE_COLOR_MODE_TRUE) {
        if (has_256color) {
            /* Downgrade truecolor to 256-color approximation */
            /* Use 6x6x6 color cube (colors 16-231) */
            uint8_t r = color->value.rgb.r;
            uint8_t g = color->value.rgb.g;
            uint8_t b = color->value.rgb.b;

            /* Map 0-255 to 0-5 */
            uint8_t r6 = (r * 6) / 256;
            uint8_t g6 = (g * 6) / 256;
            uint8_t b6 = (b * 6) / 256;

            result.mode = LLE_COLOR_MODE_256;
            result.value.palette = 16 + (36 * r6) + (6 * g6) + b6;
        } else {
            /* Downgrade to basic 8-color */
            uint8_t r = color->value.rgb.r;
            uint8_t g = color->value.rgb.g;
            uint8_t b = color->value.rgb.b;

            /* Simple threshold-based mapping */
            int bright = (r > 127 || g > 127 || b > 127) ? 1 : 0;
            int basic = 0;

            if (r > 127)
                basic |= 1; /* Red bit */
            if (g > 127)
                basic |= 2; /* Green bit */
            if (b > 127)
                basic |= 4; /* Blue bit */

            result.mode = LLE_COLOR_MODE_BASIC;
            result.value.basic = (uint8_t)basic;
            result.bold = bright ? true : result.bold;
        }
    } else if (color->mode == LLE_COLOR_MODE_256 && !has_256color) {
        /* Downgrade 256-color to basic 8-color */
        uint8_t idx = color->value.palette;

        if (idx < 8) {
            /* Standard colors 0-7 map directly */
            result.mode = LLE_COLOR_MODE_BASIC;
            result.value.basic = idx;
        } else if (idx < 16) {
            /* Bright colors 8-15 map to standard + bold */
            result.mode = LLE_COLOR_MODE_BASIC;
            result.value.basic = idx - 8;
            result.bold = true;
        } else if (idx < 232) {
            /* 6x6x6 color cube (16-231) */
            int cube_idx = idx - 16;
            int r = cube_idx / 36;
            int g = (cube_idx / 6) % 6;
            int b = cube_idx % 6;

            int basic = 0;
            if (r >= 3)
                basic |= 1;
            if (g >= 3)
                basic |= 2;
            if (b >= 3)
                basic |= 4;

            result.mode = LLE_COLOR_MODE_BASIC;
            result.value.basic = (uint8_t)basic;
            result.bold = (r >= 4 || g >= 4 || b >= 4);
        } else {
            /* Grayscale 232-255 */
            int gray = idx - 232; /* 0-23 */
            if (gray < 8) {
                result.mode = LLE_COLOR_MODE_BASIC;
                result.value.basic = LLE_COLOR_BLACK;
            } else if (gray < 16) {
                result.mode = LLE_COLOR_MODE_BASIC;
                result.value.basic = LLE_COLOR_WHITE;
                result.dim = true;
            } else {
                result.mode = LLE_COLOR_MODE_BASIC;
                result.value.basic = LLE_COLOR_WHITE;
            }
        }
    }

    return result;
}

/* ============================================================================
 * Symbol Set Helpers
 * ============================================================================
 */

/**
 * @brief Initialize symbol set with Unicode defaults
 *
 * Populates a symbol set with Unicode characters for modern terminal
 * display. Includes arrows, check marks, and other Unicode glyphs.
 *
 * @param symbols Pointer to symbol set to initialize (ignored if NULL)
 */
void lle_symbol_set_init_unicode(lle_symbol_set_t *symbols) {
    if (!symbols) {
        return;
    }

    memset(symbols, 0, sizeof(*symbols));

    snprintf(symbols->prompt, sizeof(symbols->prompt), "%s", "$");
    snprintf(symbols->prompt_root, sizeof(symbols->prompt_root), "%s", "#");
    snprintf(symbols->continuation, sizeof(symbols->continuation), "%s", ">");
    snprintf(symbols->branch, sizeof(symbols->branch), "%s", "");
    snprintf(symbols->staged, sizeof(symbols->staged), "%s", "+");
    snprintf(symbols->unstaged, sizeof(symbols->unstaged), "%s", "!");
    snprintf(symbols->untracked, sizeof(symbols->untracked), "%s", "?");
    snprintf(symbols->ahead, sizeof(symbols->ahead), "%s", "↑");
    snprintf(symbols->behind, sizeof(symbols->behind), "%s", "↓");
    snprintf(symbols->stash, sizeof(symbols->stash), "%s", "≡");
    snprintf(symbols->conflict, sizeof(symbols->conflict), "%s", "!");
    snprintf(symbols->home, sizeof(symbols->home), "%s", "~");
    snprintf(symbols->error, sizeof(symbols->error), "%s", "✗");
    snprintf(symbols->success, sizeof(symbols->success), "%s", "✓");
    snprintf(symbols->jobs, sizeof(symbols->jobs), "%s", "⚙");
}

/**
 * @brief Initialize symbol set with ASCII fallbacks
 *
 * Populates a symbol set with ASCII-only characters for compatibility
 * with terminals that do not support Unicode.
 *
 * @param symbols Pointer to symbol set to initialize (ignored if NULL)
 */
void lle_symbol_set_init_ascii(lle_symbol_set_t *symbols) {
    if (!symbols) {
        return;
    }

    memset(symbols, 0, sizeof(*symbols));

    snprintf(symbols->prompt, sizeof(symbols->prompt), "%s", "$");
    snprintf(symbols->prompt_root, sizeof(symbols->prompt_root), "%s", "#");
    snprintf(symbols->continuation, sizeof(symbols->continuation), "%s", ">");
    snprintf(symbols->branch, sizeof(symbols->branch), "%s", "");
    snprintf(symbols->staged, sizeof(symbols->staged), "%s", "+");
    snprintf(symbols->unstaged, sizeof(symbols->unstaged), "%s", "!");
    snprintf(symbols->untracked, sizeof(symbols->untracked), "%s", "?");
    snprintf(symbols->ahead, sizeof(symbols->ahead), "%s", "^");
    snprintf(symbols->behind, sizeof(symbols->behind), "%s", "v");
    snprintf(symbols->stash, sizeof(symbols->stash), "%s", "=");
    snprintf(symbols->conflict, sizeof(symbols->conflict), "%s", "!");
    snprintf(symbols->home, sizeof(symbols->home), "%s", "~");
    snprintf(symbols->error, sizeof(symbols->error), "%s", "x");
    snprintf(symbols->success, sizeof(symbols->success), "%s", "o");
    snprintf(symbols->jobs, sizeof(symbols->jobs), "%s", "*");
}

/**
 * @brief Initialize symbol set with Nerd Font icons
 *
 * Uses Nerd Font icons from the Nerd Fonts project for a rich visual
 * experience. Requires a Nerd Font patched terminal font.
 *
 * Nerd Font codepoints used:
 *   U+E0A0  - Git branch (Powerline)
 *   U+E0B0  - Right arrow separator (Powerline)
 *   U+E0B2  - Left arrow separator (Powerline)
 *   U+F067  - Plus (FontAwesome)
 *   U+F040  - Pencil (FontAwesome)
 *   U+F128  - Question (FontAwesome)
 *   U+F062  - Arrow up (FontAwesome)
 *   U+F063  - Arrow down (FontAwesome)
 *   U+F01C  - Inbox/stash (FontAwesome)
 *   U+F0E7  - Bolt/conflict (FontAwesome)
 *   U+F07B  - Folder (FontAwesome)
 *   U+F015  - Home (FontAwesome)
 *   U+F057  - Times circle/error (FontAwesome)
 *   U+F058  - Check circle/success (FontAwesome)
 *   U+F085  - Cogs/jobs (FontAwesome)
 *   U+F017  - Clock (FontAwesome)
 *
 * @param symbols Pointer to symbol set to initialize (ignored if NULL)
 */
void lle_symbol_set_init_nerd_font(lle_symbol_set_t *symbols) {
    if (!symbols) {
        return;
    }

    memset(symbols, 0, sizeof(*symbols));

    /* Prompt symbols */
    snprintf(symbols->prompt, sizeof(symbols->prompt), "%s", "❯");
    snprintf(symbols->prompt_root, sizeof(symbols->prompt_root), "%s", "❯");
    snprintf(symbols->continuation, sizeof(symbols->continuation), "%s", "…");

    /* Powerline separators (U+E0B0, U+E0B2) */
    snprintf(symbols->separator_left, sizeof(symbols->separator_left),
             "\xee\x82\xb0");  /* U+E0B0  */
    snprintf(symbols->separator_right, sizeof(symbols->separator_right),
             "\xee\x82\xb2"); /* U+E0B2  */

    /* Git symbols */
    snprintf(symbols->branch, sizeof(symbols->branch),
             "\xee\x82\xa0 "); /* U+E0A0  + space */
    snprintf(symbols->staged, sizeof(symbols->staged),
             "\xef\x81\xa7");  /* U+F067  */
    snprintf(symbols->unstaged, sizeof(symbols->unstaged),
             "\xef\x81\x80"); /* U+F040  */
    snprintf(symbols->untracked, sizeof(symbols->untracked),
             "\xef\x84\xa8"); /* U+F128  */
    snprintf(symbols->ahead, sizeof(symbols->ahead),
             "\xef\x81\xa2");  /* U+F062  */
    snprintf(symbols->behind, sizeof(symbols->behind),
             "\xef\x81\xa3"); /* U+F063  */
    snprintf(symbols->stash, sizeof(symbols->stash),
             "\xef\x80\x9c");  /* U+F01C  */
    snprintf(symbols->conflict, sizeof(symbols->conflict),
             "\xef\x83\xa7"); /* U+F0E7  */

    /* Directory symbols */
    snprintf(symbols->directory, sizeof(symbols->directory),
             "\xef\x81\xbb"); /* U+F07B  */
    snprintf(symbols->home, sizeof(symbols->home),
             "\xef\x80\x95");  /* U+F015  */

    /* Status symbols */
    snprintf(symbols->error, sizeof(symbols->error),
             "\xef\x81\x97");   /* U+F057  */
    snprintf(symbols->success, sizeof(symbols->success),
             "\xef\x81\x98"); /* U+F058  */

    /* Other symbols */
    snprintf(symbols->jobs, sizeof(symbols->jobs),
             "\xef\x82\x85"); /* U+F085  */
    snprintf(symbols->time, sizeof(symbols->time),
             "\xef\x80\x97"); /* U+F017  */
}

/* ============================================================================
 * Built-in Themes
 * ============================================================================
 */

/**
 * @brief Create the minimal theme
 *
 * Creates the "minimal" built-in theme with a simple directory-only prompt.
 * Supports ASCII fallback and transient prompts.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_minimal(void) {
    lle_theme_t *theme =
        lle_theme_create("minimal", "Minimal prompt with directory only",
                         LLE_THEME_CATEGORY_MINIMAL);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities =
        LLE_THEME_CAP_ASCII_FALLBACK | LLE_THEME_CAP_INHERITABLE;

    /* Colors */
    theme->colors.path_normal = lle_color_basic(LLE_COLOR_BLUE);

    /* Layout */
    snprintf(
        theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
        "${directory}${?jobs: [${jobs}]}${?status: [${status}]} ${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format), "> ");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the default theme
 *
 * Creates the "default" built-in theme with user, host, directory, and
 * git status. Uses Unicode symbols and basic ANSI colors.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_default(void) {
    lle_theme_t *theme = lle_theme_create("default", "Default lush theme",
                                          LLE_THEME_CATEGORY_MODERN);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_INHERITABLE;

    /* Colors */
    theme->colors.primary = lle_color_basic(LLE_COLOR_GREEN);
    theme->colors.path_normal = lle_color_basic(LLE_COLOR_BLUE);
    theme->colors.git_branch = lle_color_basic(LLE_COLOR_MAGENTA);
    theme->colors.git_dirty = lle_color_basic(LLE_COLOR_RED);
    theme->colors.git_clean = lle_color_basic(LLE_COLOR_GREEN);
    theme->colors.error = lle_color_basic(LLE_COLOR_RED);

    /* Layout */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "${user}@${host}:${directory}${?git: (${git})} ${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format), "> ");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the classic theme (bash-like)
 *
 * Creates the "classic" built-in theme with traditional bash-style
 * [user@host dir]$ format. Uses ASCII-only symbols.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_classic(void) {
    lle_theme_t *theme = lle_theme_create(
        "classic", "Classic bash-style prompt", LLE_THEME_CATEGORY_CLASSIC);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities =
        LLE_THEME_CAP_ASCII_FALLBACK | LLE_THEME_CAP_INHERITABLE;

    /* ASCII-only symbols */
    lle_symbol_set_init_ascii(&theme->symbols);

    /* Layout */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "[${user}@${host} ${directory}]${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format), "> ");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the powerline theme
 *
 * Powerline-inspired prompt with separator glyphs.
 * Requires a powerline-patched font (Nerd Fonts, Powerline fonts, etc.)
 *
 * Note: True powerline with colored background segments would require
 * additional composer support for background colors. This theme provides
 * a powerline-style look with foreground colors and separator symbols.
 *
 * Powerline characters used:
 *   U+E0B0  - Left-pointing solid arrow
 *   U+E0B1  - Left-pointing thin arrow
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_powerline(void) {
    lle_theme_t *theme = lle_theme_create(
        "powerline", "Powerline-style prompt (requires patched font)",
        LLE_THEME_CATEGORY_POWERLINE);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_POWERLINE |
                          LLE_THEME_CAP_TRANSIENT |
                          LLE_THEME_CAP_ASYNC_SEGMENTS;

    /* Colors - using 256-color for better powerline appearance */
    theme->colors.primary = lle_color_256(39);     /* Bright blue */
    theme->colors.secondary = lle_color_256(245);  /* Gray for separators */
    theme->colors.path_normal = lle_color_256(33); /* Blue */
    theme->colors.git_branch = lle_color_256(135); /* Purple */
    theme->colors.git_dirty = lle_color_256(214);  /* Orange/Yellow */
    theme->colors.git_clean = lle_color_256(82);   /* Green */
    theme->colors.error = lle_color_256(196);      /* Red */
    theme->colors.text = lle_color_256(255);       /* White text */
    theme->colors.text_dim = lle_color_256(250);   /* Light gray */

    /* Powerline symbols - Private Use Area characters from powerline fonts */
    snprintf(theme->symbols.separator_left,
             sizeof(theme->symbols.separator_left),
             "\xee\x82\xb0"); /* U+E0B0  */
    snprintf(theme->symbols.separator_right,
             sizeof(theme->symbols.separator_right),
             "\xee\x82\xb2"); /* U+E0B2  */

    /* Use chevron for prompt symbol */
    snprintf(theme->symbols.prompt, sizeof(theme->symbols.prompt), "❯");

    /* Powerline-style layout - separators between colored segments
     * Without background colors, we use the arrows as visual dividers */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "${user} \xee\x82\xb1 ${directory}${?git: \xee\x82\xb1 ${git}} "
             "${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format),
             "\xee\x82\xb1 "); /* U+E0B1 thin arrow for continuation */
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the informative theme
 *
 * Creates the "informative" built-in theme with detailed information
 * including git status on a separate line. Supports multiline, right
 * prompt, and async segments.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_informative(void) {
    lle_theme_t *theme =
        lle_theme_create("informative", "Detailed information prompt",
                         LLE_THEME_CATEGORY_MODERN);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_MULTILINE |
                          LLE_THEME_CAP_RIGHT_PROMPT |
                          LLE_THEME_CAP_ASYNC_SEGMENTS;

    /* Colors */
    theme->colors.primary = lle_color_basic(LLE_COLOR_CYAN);
    theme->colors.path_normal = lle_color_basic(LLE_COLOR_BLUE);
    theme->colors.git_branch = lle_color_basic(LLE_COLOR_MAGENTA);
    theme->colors.error = lle_color_basic(LLE_COLOR_RED);

    /* Multi-line layout */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "${user}@${host}:${directory}${?git:\\n  git:(${git})}\n"
             "${?status:[${status}] }${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format),
             "... ");
    snprintf(theme->layout.rps1_format, sizeof(theme->layout.rps1_format),
             "${time}${?jobs: [${jobs}]}");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_right_prompt = true;
    theme->layout.enable_multiline = true;
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the two-line theme
 *
 * Creates the "two-line" built-in theme with box drawing characters
 * for a visual frame around prompt components.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_two_line(void) {
    lle_theme_t *theme =
        lle_theme_create("two-line", "Two-line prompt with box drawing",
                         LLE_THEME_CATEGORY_MODERN);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_MULTILINE;

    /* Colors */
    theme->colors.primary = lle_color_basic(LLE_COLOR_GREEN);
    theme->colors.path_normal = lle_color_basic(LLE_COLOR_BLUE);
    theme->colors.git_branch = lle_color_basic(LLE_COLOR_YELLOW);

    /* Two-line layout */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "┌─[${user}@${host}]─[${directory}]${?git:─[${git}]}\n"
             "└─${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format),
             "   > ");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_multiline = true;
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the nerd theme (Nerd Font icons)
 *
 * Full Nerd Font icon support with git branch icon, folder icons,
 * and other visual enhancements. Requires a Nerd Font patched font
 * (e.g., FiraCode Nerd Font, JetBrains Mono Nerd Font, Hack Nerd Font).
 *
 * Features:
 *   - Git branch icon before branch name
 *   - Folder icon in directory display
 *   - FontAwesome icons for status indicators
 *   - Powerline arrow separators
 *   - 256-color support for vibrant appearance
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_nerd(void) {
    lle_theme_t *theme = lle_theme_create(
        "nerd", "Nerd Font icons theme (requires Nerd Font)",
        LLE_THEME_CATEGORY_POWERLINE);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_NERD_FONT |
                          LLE_THEME_CAP_POWERLINE | LLE_THEME_CAP_TRANSIENT |
                          LLE_THEME_CAP_ASYNC_SEGMENTS;

    /* Initialize with Nerd Font symbols */
    lle_symbol_set_init_nerd_font(&theme->symbols);

    /* Colors - vibrant 256-color palette */
    theme->colors.primary = lle_color_256(39);       /* Bright blue */
    theme->colors.secondary = lle_color_256(245);    /* Gray */
    theme->colors.path_normal = lle_color_256(33);   /* Blue */
    theme->colors.git_branch = lle_color_256(141);   /* Light purple */
    theme->colors.git_staged = lle_color_256(83);    /* Green */
    theme->colors.git_dirty = lle_color_256(214);    /* Orange */
    theme->colors.git_untracked = lle_color_256(245);/* Gray */
    theme->colors.git_ahead = lle_color_256(39);     /* Blue */
    theme->colors.git_behind = lle_color_256(202);   /* Orange-red */
    theme->colors.git_clean = lle_color_256(82);     /* Green */
    theme->colors.error = lle_color_256(196);        /* Red */
    theme->colors.success = lle_color_256(82);       /* Green */
    theme->colors.text = lle_color_256(255);         /* White */
    theme->colors.text_dim = lle_color_256(245);     /* Gray */

    /* Layout with Nerd Font folder icon (U+F07B) */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "${user}@${host} \xef\x81\xbb ${directory}${?git: (${git})} ${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format),
             "... ");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the corporate theme (ported from legacy)
 *
 * Professional theme for business environments with 256-color support.
 * Uses subdued blues and grays for a professional appearance.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_corporate(void) {
    lle_theme_t *theme = lle_theme_create(
        "corporate", "Professional theme for business environments",
        LLE_THEME_CATEGORY_CLASSIC);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities =
        LLE_THEME_CAP_ASCII_FALLBACK | LLE_THEME_CAP_INHERITABLE;

    /* Professional blues and grays using 256-color palette */
    theme->colors.primary = lle_color_256(24);      /* Dark blue */
    theme->colors.secondary = lle_color_256(67);    /* Steel blue */
    theme->colors.success = lle_color_256(28);      /* Dark green */
    theme->colors.warning = lle_color_256(172);     /* Orange */
    theme->colors.error = lle_color_256(124);       /* Dark red */
    theme->colors.info = lle_color_256(31);         /* Cyan */
    theme->colors.text = lle_color_256(250);        /* Light gray */
    theme->colors.text_dim = lle_color_256(242);    /* Dim gray */
    theme->colors.highlight = lle_color_256(117);   /* Light blue */
    theme->colors.git_clean = lle_color_256(28);    /* Dark green */
    theme->colors.git_dirty = lle_color_256(172);   /* Orange */
    theme->colors.git_staged = lle_color_256(34);   /* Bright green */
    theme->colors.git_branch = lle_color_256(67);   /* Steel blue */
    theme->colors.path_home = lle_color_256(117);   /* Light blue */
    theme->colors.path_root = lle_color_256(124);   /* Dark red */
    theme->colors.path_normal = lle_color_256(250); /* Light gray */

    /* Professional layout */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "[${user}@${host}] ${directory}${?git: (${git})} ${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format), "> ");
    snprintf(theme->layout.rps1_format, sizeof(theme->layout.rps1_format),
             "${time}");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_right_prompt = true;
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the dark theme (ported from legacy)
 *
 * Modern dark theme with bright accent colors and two-line layout.
 * Designed for dark terminal backgrounds with vibrant highlights.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_dark(void) {
    lle_theme_t *theme =
        lle_theme_create("dark", "Modern dark theme with bright accents",
                         LLE_THEME_CATEGORY_MODERN);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_MULTILINE |
                          LLE_THEME_CAP_INHERITABLE;

    /* Bright colors on dark background using 256-color palette */
    theme->colors.primary = lle_color_256(39);      /* Bright blue */
    theme->colors.secondary = lle_color_256(141);   /* Purple */
    theme->colors.success = lle_color_256(46);      /* Bright green */
    theme->colors.warning = lle_color_256(226);     /* Bright yellow */
    theme->colors.error = lle_color_256(196);       /* Bright red */
    theme->colors.info = lle_color_256(51);         /* Bright cyan */
    theme->colors.text = lle_color_256(255);        /* White */
    theme->colors.text_dim = lle_color_256(244);    /* Gray */
    theme->colors.highlight = lle_color_256(51);    /* Bright cyan */
    theme->colors.git_clean = lle_color_256(46);    /* Bright green */
    theme->colors.git_dirty = lle_color_256(226);   /* Yellow */
    theme->colors.git_staged = lle_color_256(82);   /* Lime green */
    theme->colors.git_branch = lle_color_256(141);  /* Purple */
    theme->colors.path_home = lle_color_256(39);    /* Bright blue */
    theme->colors.path_root = lle_color_256(196);   /* Bright red */
    theme->colors.path_normal = lle_color_256(255); /* White */

    /* Two-line layout with box drawing */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "┌─[${user}@${host}]─[${directory}]${?git:─[${git}]}\n"
             "└─${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format),
             "└─> ");
    snprintf(theme->layout.rps1_format, sizeof(theme->layout.rps1_format),
             "${time}");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_right_prompt = true;
    theme->layout.enable_multiline = true;
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the light theme (ported from legacy)
 *
 * Clean light theme with dark colors for excellent readability.
 * Designed for light terminal backgrounds with dark text.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_light(void) {
    lle_theme_t *theme = lle_theme_create(
        "light", "Clean light theme with excellent readability",
        LLE_THEME_CATEGORY_MINIMAL);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities =
        LLE_THEME_CAP_ASCII_FALLBACK | LLE_THEME_CAP_INHERITABLE;

    /* Dark colors on light background using 256-color palette */
    theme->colors.primary = lle_color_256(21);      /* Dark blue */
    theme->colors.secondary = lle_color_256(90);    /* Dark purple */
    theme->colors.success = lle_color_256(22);      /* Dark green */
    theme->colors.warning = lle_color_256(130);     /* Dark orange */
    theme->colors.error = lle_color_256(88);        /* Dark red */
    theme->colors.info = lle_color_256(23);         /* Dark cyan */
    theme->colors.text = lle_color_256(232);        /* Very dark gray */
    theme->colors.text_dim = lle_color_256(243);    /* Medium gray */
    theme->colors.highlight = lle_color_256(27);    /* Blue */
    theme->colors.git_clean = lle_color_256(22);    /* Dark green */
    theme->colors.git_dirty = lle_color_256(130);   /* Orange */
    theme->colors.git_staged = lle_color_256(28);   /* Green */
    theme->colors.git_branch = lle_color_256(90);   /* Purple */
    theme->colors.path_home = lle_color_256(21);    /* Blue */
    theme->colors.path_root = lle_color_256(88);    /* Dark red */
    theme->colors.path_normal = lle_color_256(232); /* Dark gray */

    /* Clean simple layout */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "${user}@${host}:${directory}${?git: (${git})} ${symbol} ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format), "> ");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "${symbol} ");
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Create the colorful theme (ported from legacy)
 *
 * Vibrant colorful theme for creative workflows. Uses rainbow colors
 * and decorative symbols for an expressive appearance.
 *
 * @return Pointer to newly allocated theme, or NULL on allocation failure
 */
lle_theme_t *lle_theme_create_colorful(void) {
    lle_theme_t *theme =
        lle_theme_create("colorful", "Vibrant theme for creative workflows",
                         LLE_THEME_CATEGORY_MODERN);
    if (!theme) {
        return NULL;
    }

    theme->source = LLE_THEME_SOURCE_BUILTIN;
    theme->capabilities = LLE_THEME_CAP_UNICODE | LLE_THEME_CAP_RIGHT_PROMPT |
                          LLE_THEME_CAP_INHERITABLE;

    /* Vibrant rainbow colors using 256-color palette */
    theme->colors.primary = lle_color_256(201);    /* Magenta */
    theme->colors.secondary = lle_color_256(45);   /* Cyan */
    theme->colors.success = lle_color_256(118);    /* Lime */
    theme->colors.warning = lle_color_256(220);    /* Gold */
    theme->colors.error = lle_color_256(196);      /* Red */
    theme->colors.info = lle_color_256(75);        /* Sky blue */
    theme->colors.text = lle_color_256(255);       /* White */
    theme->colors.text_dim = lle_color_256(245);   /* Gray */
    theme->colors.highlight = lle_color_256(226);  /* Yellow */
    theme->colors.git_clean = lle_color_256(118);  /* Lime */
    theme->colors.git_dirty = lle_color_256(220);  /* Gold */
    theme->colors.git_staged = lle_color_256(46);  /* Green */
    theme->colors.git_branch = lle_color_256(201); /* Magenta */
    theme->colors.path_home = lle_color_256(45);   /* Cyan */
    theme->colors.path_root = lle_color_256(196);  /* Red */
    theme->colors.path_normal = lle_color_256(75); /* Sky blue */

    /* Colorful symbols: ● user@host directory (git) ➜ */
    snprintf(theme->symbols.prompt, sizeof(theme->symbols.prompt), "➜");
    snprintf(theme->symbols.directory, sizeof(theme->symbols.directory), "●");

    /* Creative layout with symbols */
    snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
             "● ${user}@${host} ${directory}${?git: (${git})} ➜ ");
    snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format), "▶ ");
    snprintf(theme->layout.rps1_format, sizeof(theme->layout.rps1_format),
             "⏰ ${time}");
    snprintf(theme->layout.transient_format,
             sizeof(theme->layout.transient_format), "➜ ");
    theme->layout.enable_right_prompt = true;
    theme->layout.enable_transient = true;

    return theme;
}

/**
 * @brief Register all built-in themes
 *
 * Creates and registers all built-in themes (minimal, default, classic,
 * powerline, informative, two-line, corporate, dark, light, colorful).
 * Sets "minimal" as the active theme after registration.
 *
 * @param registry Pointer to initialized registry
 * @return Number of themes successfully registered
 */
size_t lle_theme_register_builtins(lle_theme_registry_t *registry) {
    if (!registry || !registry->initialized) {
        return 0;
    }

    size_t count = 0;
    lle_theme_t *theme;

    /* Minimal theme */
    theme = lle_theme_create_minimal();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Default theme */
    theme = lle_theme_create_default();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Classic theme */
    theme = lle_theme_create_classic();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Powerline theme */
    theme = lle_theme_create_powerline();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Nerd Font theme */
    theme = lle_theme_create_nerd();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Informative theme */
    theme = lle_theme_create_informative();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Two-line theme */
    theme = lle_theme_create_two_line();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Corporate theme (ported from legacy) */
    theme = lle_theme_create_corporate();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Dark theme (ported from legacy) */
    theme = lle_theme_create_dark();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Light theme (ported from legacy) */
    theme = lle_theme_create_light();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Colorful theme (ported from legacy) */
    theme = lle_theme_create_colorful();
    if (theme && lle_theme_registry_register(registry, theme) == LLE_SUCCESS) {
        count++;
    }

    /* Set default active theme */
    if (count > 0) {
        lle_theme_registry_set_active(registry, "minimal");
    }

    return count;
}
