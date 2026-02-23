/**
 * @file theme.h
 * @brief LLE Theme Registry System - Type Definitions and Function Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 25 Section 4 - Theme Registry System
 * Version: 1.0.0
 *
 * The theme registry provides unified storage for all themes (built-in and
 * user). Both types use identical registration, ensuring first-class
 * citizenship for all.
 *
 * Key Features:
 * - Theme registration and lookup
 * - Theme inheritance with cycle detection
 * - Color schemes with semantic colors
 * - Symbol sets with Unicode/ASCII fallbacks
 * - Prompt layout templates
 */

#ifndef LLE_PROMPT_THEME_H
#define LLE_PROMPT_THEME_H

#include "lle/error_handling.h"
#include "lle/syntax_highlighting.h"

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/** @brief Maximum theme name length */
#define LLE_THEME_NAME_MAX 64

/** @brief Maximum theme description length */
#define LLE_THEME_DESC_MAX 256

/** @brief Maximum template string length */
#define LLE_TEMPLATE_MAX 1024

/** @brief Maximum color code length */
#define LLE_COLOR_CODE_MAX 32

/** @brief Maximum prompt output length */
#define LLE_PROMPT_OUTPUT_MAX 4096

/** @brief Maximum number of themes in registry */
#define LLE_THEME_REGISTRY_MAX 64

/** @brief Maximum symbol length (UTF-8) */
#define LLE_SYMBOL_MAX 16

/** @brief Maximum enabled segments per theme */
#define LLE_THEME_MAX_SEGMENTS 32

/* ============================================================================
 * COLOR TYPES
 * ============================================================================
 */

/**
 * @brief Color representation modes
 */
typedef enum lle_color_mode {
    LLE_COLOR_MODE_NONE,  /**< No color */
    LLE_COLOR_MODE_BASIC, /**< 8 basic ANSI colors (0-7) */
    LLE_COLOR_MODE_256,   /**< 256-color palette */
    LLE_COLOR_MODE_TRUE   /**< 24-bit true color (RGB) */
} lle_color_mode_t;

/**
 * @brief Single color value (supports all modes)
 */
typedef struct lle_color {
    lle_color_mode_t mode; /**< Color mode */
    union {
        uint8_t basic;   /**< 0-7 for basic colors */
        uint8_t palette; /**< 0-255 for 256-color */
        struct {
            uint8_t r, g, b; /**< RGB for true color */
        } rgb;
    } value;
    bool bold;      /**< Bold attribute */
    bool italic;    /**< Italic attribute */
    bool underline; /**< Underline attribute */
    bool dim;       /**< Dim attribute */
} lle_color_t;

/**
 * @brief Basic ANSI color values
 */
typedef enum lle_basic_color {
    LLE_COLOR_BLACK = 0,
    LLE_COLOR_RED = 1,
    LLE_COLOR_GREEN = 2,
    LLE_COLOR_YELLOW = 3,
    LLE_COLOR_BLUE = 4,
    LLE_COLOR_MAGENTA = 5,
    LLE_COLOR_CYAN = 6,
    LLE_COLOR_WHITE = 7
} lle_basic_color_t;

/**
 * @brief Semantic color scheme for themes
 */
typedef struct lle_color_scheme {
    /* Core semantic colors */
    lle_color_t primary;   /**< Primary accent color */
    lle_color_t secondary; /**< Secondary accent color */
    lle_color_t success;   /**< Success indicator */
    lle_color_t warning;   /**< Warning indicator */
    lle_color_t error;     /**< Error indicator */
    lle_color_t info;      /**< Information color */

    /* Text colors */
    lle_color_t text;        /**< Normal text */
    lle_color_t text_dim;    /**< Dimmed text */
    lle_color_t text_bright; /**< Bright/highlighted text */

    /* Structural colors */
    lle_color_t border;     /**< Border/separator color */
    lle_color_t background; /**< Background color */
    lle_color_t highlight;  /**< Highlight color */

    /* Git-specific colors */
    lle_color_t git_clean;     /**< Clean repository */
    lle_color_t git_dirty;     /**< Dirty repository */
    lle_color_t git_staged;    /**< Staged changes */
    lle_color_t git_untracked; /**< Untracked files */
    lle_color_t git_branch;    /**< Branch name */
    lle_color_t git_ahead;     /**< Commits ahead */
    lle_color_t git_behind;    /**< Commits behind */

    /* Path colors */
    lle_color_t path_home;      /**< Home directory */
    lle_color_t path_root;      /**< Root directory */
    lle_color_t path_normal;    /**< Normal path */
    lle_color_t path_separator; /**< Path separator */

    /* Status colors */
    lle_color_t status_ok;      /**< Success status */
    lle_color_t status_error;   /**< Error status */
    lle_color_t status_running; /**< Running status */
} lle_color_scheme_t;

/* ============================================================================
 * SYMBOL TYPES
 * ============================================================================
 */

/**
 * @brief Symbol compatibility mode
 */
typedef enum lle_symbol_mode {
    LLE_SYMBOL_MODE_UNICODE, /**< Full Unicode symbols */
    LLE_SYMBOL_MODE_ASCII,   /**< ASCII-only fallbacks */
    LLE_SYMBOL_MODE_AUTO     /**< Auto-detect terminal capability */
} lle_symbol_mode_t;

/**
 * @brief Symbol set for a theme
 */
typedef struct lle_symbol_set {
    char prompt[LLE_SYMBOL_MAX];          /**< Main prompt symbol */
    char prompt_root[LLE_SYMBOL_MAX];     /**< Root user prompt */
    char continuation[LLE_SYMBOL_MAX];    /**< PS2 continuation */
    char separator_left[LLE_SYMBOL_MAX];  /**< Powerline left separator */
    char separator_right[LLE_SYMBOL_MAX]; /**< Powerline right separator */
    char branch[LLE_SYMBOL_MAX];          /**< Git branch symbol */
    char staged[LLE_SYMBOL_MAX];          /**< Staged changes */
    char unstaged[LLE_SYMBOL_MAX];        /**< Unstaged changes */
    char untracked[LLE_SYMBOL_MAX];       /**< Untracked files */
    char ahead[LLE_SYMBOL_MAX];           /**< Commits ahead */
    char behind[LLE_SYMBOL_MAX];          /**< Commits behind */
    char stash[LLE_SYMBOL_MAX];           /**< Git stash */
    char conflict[LLE_SYMBOL_MAX];        /**< Merge conflict */
    char directory[LLE_SYMBOL_MAX];       /**< Directory icon */
    char home[LLE_SYMBOL_MAX];            /**< Home directory */
    char error[LLE_SYMBOL_MAX];           /**< Error indicator */
    char success[LLE_SYMBOL_MAX];         /**< Success indicator */
    char time[LLE_SYMBOL_MAX];            /**< Time display */
    char jobs[LLE_SYMBOL_MAX];            /**< Background jobs */
} lle_symbol_set_t;

/* ============================================================================
 * PER-SEGMENT CONFIGURATION
 * ============================================================================
 */

/** @brief Maximum per-segment configs per theme */
#define LLE_THEME_MAX_SEGMENT_CONFIGS 16

/**
 * @brief Per-segment configuration from [segments.<name>] TOML sections
 *
 * Each field has a companion *_set flag indicating whether the value was
 * explicitly configured. Unset fields inherit defaults from the segment
 * renderer. This allows themes to selectively override specific properties.
 */
typedef struct lle_segment_config {
    char name[32];   /**< Segment name (e.g., "directory", "git") */
    bool configured; /**< True if any field was set */

    /* Visibility */
    bool show;     /**< Whether segment is visible */
    bool show_set; /**< True if show was explicitly configured */

    /* Truncation (meaning is segment-specific: path components, branch chars) */
    int truncation_length;     /**< Truncation length */
    bool truncation_length_set; /**< True if truncation_length was configured */

    /* Style variant ("full", "short", "basename") */
    char style[32]; /**< Style variant name */
    bool style_set; /**< True if style was configured */

    /* Format string (strftime for time, etc.) */
    char format[64]; /**< Format string */
    bool format_set; /**< True if format was configured */

    /* Git-specific booleans */
    bool show_branch;            /**< Show git branch name */
    bool show_branch_set;        /**< True if show_branch was configured */
    bool show_status;            /**< Show git dirty/clean status */
    bool show_status_set;        /**< True if show_status was configured */
    bool show_ahead_behind;      /**< Show ahead/behind counts */
    bool show_ahead_behind_set;  /**< True if show_ahead_behind was configured */
    bool show_stash;             /**< Show stash indicator */
    bool show_stash_set;         /**< True if show_stash was configured */

    /* Directory-specific */
    char home_symbol[LLE_SYMBOL_MAX]; /**< Home directory replacement symbol */
    bool home_symbol_set; /**< True if home_symbol was configured */
} lle_segment_config_t;

/* ============================================================================
 * THEME TYPES
 * ============================================================================
 */

/**
 * @brief Theme capability flags
 */
typedef enum lle_theme_capability {
    LLE_THEME_CAP_NONE = 0,
    LLE_THEME_CAP_256_COLOR = (1 << 0),      /**< Uses 256-color palette */
    LLE_THEME_CAP_TRUE_COLOR = (1 << 1),     /**< Uses 24-bit true color */
    LLE_THEME_CAP_POWERLINE = (1 << 2),      /**< Requires powerline fonts */
    LLE_THEME_CAP_NERD_FONT = (1 << 3),      /**< Requires nerd fonts */
    LLE_THEME_CAP_UNICODE = (1 << 4),        /**< Uses Unicode symbols */
    LLE_THEME_CAP_ASCII_FALLBACK = (1 << 5), /**< Has ASCII fallbacks */
    LLE_THEME_CAP_MULTILINE = (1 << 6),      /**< Multi-line prompt */
    LLE_THEME_CAP_RIGHT_PROMPT = (1 << 7),   /**< Uses right prompt */
    LLE_THEME_CAP_TRANSIENT = (1 << 8),      /**< Supports transient prompt */
    LLE_THEME_CAP_ASYNC_SEGMENTS = (1 << 9), /**< Has async segments */
    LLE_THEME_CAP_INHERITABLE = (1 << 10)    /**< Can be inherited from */
} lle_theme_capability_t;

/**
 * @brief Theme category for organization
 */
typedef enum lle_theme_category {
    LLE_THEME_CATEGORY_MINIMAL,      /**< Ultra-simple themes */
    LLE_THEME_CATEGORY_CLASSIC,      /**< Traditional shell style */
    LLE_THEME_CATEGORY_MODERN,       /**< Modern with Unicode */
    LLE_THEME_CATEGORY_POWERLINE,    /**< Powerline-style */
    LLE_THEME_CATEGORY_PROFESSIONAL, /**< Business/corporate */
    LLE_THEME_CATEGORY_CREATIVE,     /**< Colorful/artistic */
    LLE_THEME_CATEGORY_CUSTOM        /**< User-defined */
} lle_theme_category_t;

/**
 * @brief Theme source type
 */
typedef enum lle_theme_source {
    LLE_THEME_SOURCE_BUILTIN, /**< Compiled into binary */
    LLE_THEME_SOURCE_SYSTEM,  /**< /etc/lush/themes/ */
    LLE_THEME_SOURCE_USER,    /**< ~/.config/lush/themes/ */
    LLE_THEME_SOURCE_RUNTIME  /**< Registered at runtime */
} lle_theme_source_t;

/**
 * @brief Prompt layout configuration
 */
typedef struct lle_prompt_layout {
    char ps1_format[LLE_TEMPLATE_MAX];       /**< Left prompt format */
    char ps2_format[LLE_TEMPLATE_MAX];       /**< Continuation format */
    char rps1_format[LLE_TEMPLATE_MAX];      /**< Right prompt format */
    char transient_format[LLE_TEMPLATE_MAX]; /**< Transient prompt format */

    bool enable_right_prompt; /**< Show RPROMPT */
    bool enable_transient;    /**< Simplify old prompts */
    bool enable_multiline;    /**< Multi-line primary prompt */
    bool compact_mode;        /**< Reduce spacing */

    uint8_t newline_before; /**< Newlines before prompt */
    uint8_t newline_after;  /**< Newlines after prompt */
} lle_prompt_layout_t;

/**
 * @brief Complete theme definition
 */
typedef struct lle_theme {
    /* Identity */
    char name[LLE_THEME_NAME_MAX];        /**< Theme name */
    char description[LLE_THEME_DESC_MAX]; /**< Theme description */
    char author[LLE_THEME_NAME_MAX];      /**< Theme author */
    char version[16];                     /**< Theme version */

    /* Classification */
    lle_theme_category_t category; /**< Theme category */
    lle_theme_source_t source;     /**< Theme source */
    uint32_t capabilities;         /**< Capability flags */

    /* Inheritance */
    char inherits_from[LLE_THEME_NAME_MAX]; /**< Parent theme name */

    /* Visual configuration */
    lle_color_scheme_t colors;  /**< Color scheme */
    lle_symbol_set_t symbols;   /**< Symbol set */
    lle_prompt_layout_t layout; /**< Prompt layout */

    /* Syntax highlighting colors (optional - overrides defaults) */
    lle_syntax_colors_t syntax_colors; /**< Syntax highlighting colors */
    bool has_syntax_colors; /**< True if syntax colors defined in theme */

    /* Segment configuration */
    char enabled_segments[LLE_THEME_MAX_SEGMENTS][32];
    size_t enabled_segment_count;

    /* Per-segment configuration from [segments.<name>] TOML sections */
    lle_segment_config_t segment_configs[LLE_THEME_MAX_SEGMENT_CONFIGS];
    size_t segment_config_count;

    /* Runtime state (not persisted) */
    struct lle_theme *parent; /**< Resolved parent pointer */
    bool is_active;           /**< Currently active */
    uint64_t load_time_ns;    /**< Time to load */
} lle_theme_t;

/**
 * @brief Theme registry structure
 */
typedef struct lle_theme_registry {
    lle_theme_t *themes[LLE_THEME_REGISTRY_MAX]; /**< Theme storage */
    size_t count;                                /**< Number of themes */

    /* Active theme tracking */
    lle_theme_t *active_theme; /**< Currently active theme */
    char active_theme_name[LLE_THEME_NAME_MAX];

    /* Default theme */
    char default_theme_name[LLE_THEME_NAME_MAX];

    /* State */
    bool initialized; /**< Registry is initialized */

    /* Statistics */
    size_t builtin_count;    /**< Built-in theme count */
    size_t user_count;       /**< User theme count */
    uint64_t total_switches; /**< Total theme switches */
} lle_theme_registry_t;

/* ============================================================================
 * THEME REGISTRY API
 * ============================================================================
 */

/**
 * @brief Initialize the theme registry
 *
 * @param registry  Registry to initialize
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_registry_init(lle_theme_registry_t *registry);

/**
 * @brief Cleanup the theme registry and all registered themes
 *
 * @param registry  Registry to cleanup
 */
void lle_theme_registry_cleanup(lle_theme_registry_t *registry);

/**
 * @brief Register a theme with the registry
 *
 * Works identically for built-in and user themes (first-class citizenship).
 *
 * @param registry  Target registry
 * @param theme     Theme to register (ownership transferred)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_registry_register(lle_theme_registry_t *registry,
                                         lle_theme_t *theme);

/**
 * @brief Find a theme by name
 *
 * @param registry  Registry to search
 * @param name      Theme name
 * @return Pointer to theme or NULL if not found
 */
lle_theme_t *lle_theme_registry_find(const lle_theme_registry_t *registry,
                                     const char *name);

/**
 * @brief Set the active theme
 *
 * @param registry  Registry containing themes
 * @param name      Name of theme to activate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_registry_set_active(lle_theme_registry_t *registry,
                                           const char *name);

/**
 * @brief Get the currently active theme
 *
 * @param registry  Registry to query
 * @return Pointer to active theme or NULL
 */
lle_theme_t *
lle_theme_registry_get_active(const lle_theme_registry_t *registry);

/**
 * @brief Get all registered theme names
 *
 * @param registry   Registry to query
 * @param names      Output array for theme names
 * @param max_names  Maximum number of names
 * @return Number of themes in registry
 */
size_t lle_theme_registry_list(const lle_theme_registry_t *registry,
                               const char **names, size_t max_names);

/* ============================================================================
 * THEME CREATION AND MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create a new theme with the given name
 *
 * @param name         Theme name
 * @param description  Theme description
 * @param category     Theme category
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create(const char *name, const char *description,
                              lle_theme_category_t category);

/**
 * @brief Free a theme and its resources
 *
 * @param theme  Theme to free
 */
void lle_theme_free(lle_theme_t *theme);

/**
 * @brief Resolve theme inheritance
 *
 * Copies unset values from parent theme(s). Detects inheritance cycles.
 *
 * @param registry  Registry containing themes
 * @param theme     Theme to resolve
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_resolve_inheritance(lle_theme_registry_t *registry,
                                           lle_theme_t *theme);

/* ============================================================================
 * COLOR HELPERS
 * ============================================================================
 */

/**
 * @brief Create a basic ANSI color
 *
 * @param color  Basic color value (0-7)
 * @return Color structure
 */
lle_color_t lle_color_basic(lle_basic_color_t color);

/**
 * @brief Create a 256-palette color
 *
 * @param index  Palette index (0-255)
 * @return Color structure
 */
lle_color_t lle_color_256(uint8_t index);

/**
 * @brief Create a true color (RGB)
 *
 * @param r  Red component (0-255)
 * @param g  Green component (0-255)
 * @param b  Blue component (0-255)
 * @return Color structure
 */
lle_color_t lle_color_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Generate ANSI escape sequence for a color
 *
 * @param color       Color to convert
 * @param foreground  true for foreground, false for background
 * @param output      Output buffer
 * @param output_size Size of output buffer
 * @return Length of generated sequence
 */
size_t lle_color_to_ansi(const lle_color_t *color, bool foreground,
                         char *output, size_t output_size);

/**
 * @brief Downgrade a color to match terminal capabilities
 *
 * Converts colors to a mode supported by the terminal. For example,
 * if the terminal only supports 256 colors, a true color will be
 * converted to the closest 256-color palette entry.
 *
 * @param color          Color to downgrade
 * @param has_truecolor  Terminal supports 24-bit true color
 * @param has_256color   Terminal supports 256 colors
 * @return Downgraded color that matches terminal capabilities
 */
lle_color_t lle_color_downgrade(const lle_color_t *color, bool has_truecolor,
                                bool has_256color);

/* ============================================================================
 * BUILT-IN THEMES
 * ============================================================================
 */

/**
 * @brief Register all built-in themes
 *
 * @param registry  Target registry
 * @return Number of themes registered
 */
size_t lle_theme_register_builtins(lle_theme_registry_t *registry);

/**
 * @brief Create the minimal theme
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_minimal(void);

/**
 * @brief Create the default theme
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_default(void);

/**
 * @brief Create the classic theme (bash-like)
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_classic(void);

/**
 * @brief Create the powerline theme
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_powerline(void);

/**
 * @brief Create the nerd theme (Nerd Font icons)
 *
 * Full Nerd Font icon support with git branch icon, folder icons,
 * and other visual enhancements. Requires a Nerd Font patched font.
 *
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_nerd(void);

/**
 * @brief Create the informative theme
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_informative(void);

/**
 * @brief Create the two-line theme
 * @return New theme or NULL on error
 */
lle_theme_t *lle_theme_create_two_line(void);

/* ============================================================================
 * SYMBOL SET HELPERS
 * ============================================================================
 */

/**
 * @brief Initialize symbol set with Unicode defaults
 *
 * @param symbols  Symbol set to initialize
 */
void lle_symbol_set_init_unicode(lle_symbol_set_t *symbols);

/**
 * @brief Initialize symbol set with ASCII fallbacks
 *
 * @param symbols  Symbol set to initialize
 */
void lle_symbol_set_init_ascii(lle_symbol_set_t *symbols);

/**
 * @brief Initialize symbol set with Nerd Font icons
 *
 * Uses Nerd Font icons for a rich visual experience. Requires a
 * Nerd Font patched terminal font (e.g., FiraCode Nerd Font,
 * JetBrains Mono Nerd Font, Hack Nerd Font, etc.)
 *
 * Icons used:
 *   - Branch:     (nf-dev-git_branch U+E0A0)
 *   - Staged:     (nf-fa-plus U+F067)
 *   - Unstaged:   (nf-fa-pencil U+F040)
 *   - Untracked:  (nf-fa-question U+F128)
 *   - Ahead:     ⇡ or  (nf-md-arrow_up U+F55C)
 *   - Behind:    ⇣ or  (nf-md-arrow_down U+F544)
 *   - Stash:      (nf-fa-inbox U+F01C)
 *   - Conflict:   (nf-fa-bolt U+F0E7)
 *   - Directory:  (nf-fa-folder U+F07B)
 *   - Home:       (nf-fa-home U+F015)
 *   - Error:      (nf-fa-times_circle U+F057)
 *   - Success:    (nf-fa-check_circle U+F058)
 *   - Jobs:       (nf-fa-cogs U+F085)
 *   - Time:       (nf-fa-clock_o U+F017)
 *
 * @param symbols  Symbol set to initialize
 */
void lle_symbol_set_init_nerd_font(lle_symbol_set_t *symbols);

#ifdef __cplusplus
}
#endif

#endif /* LLE_PROMPT_THEME_H */
